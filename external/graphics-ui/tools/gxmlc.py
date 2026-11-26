"""Validate registered GXML documents or compile the Settings GXML/GCSS subset."""
import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import gcss_syntax
import gxml_schema
from gxml_schema import BINDING, CLASS, Diagnostic, MAX_BYTES, NAME, NODE_ID, Origin
from gxml_schema import LEGACY_MAX_NODES as MAX_NODES

SELECTOR = re.compile(rf"#(?:{NODE_ID.pattern})|\.(?:{CLASS.pattern})|{NAME}")


@dataclass(frozen=True)
class Rule:
    selector: str
    values: dict[str, tuple[str, Origin]]


def load(path):
    if not path.is_file():
        raise Diagnostic(f"{path.name}:1: expected a regular input file")
    with path.open("rb") as stream:
        raw = stream.read(MAX_BYTES + 1)
    if len(raw) > MAX_BYTES:
        raise Diagnostic(f"{path.name}:1: source exceeds 64 KiB")
    try:
        return raw, raw.decode("utf-8-sig")
    except UnicodeError as error:
        raise Diagnostic(f"{path.name}:1: expected UTF-8") from error


def parse_gxml(file, source):
    return gxml_schema.validate_legacy(file, source)


def dimension(value, at, minimum):
    if not re.fullmatch(r"[0-9]{1,4}", value) or not minimum <= int(value) <= 4096:
        at.error(f"Expected an integer between {minimum} and 4096")
    return value.lstrip("0") or "0"


def parse_gcss(file, source):
    rules = []
    for name, _, declarations in gcss_syntax.rules(file, source, SELECTOR):
        values = {}
        for key, value, origin in declarations:
            if key == "color" and re.fullmatch(r"#[0-9a-fA-F]{6}", value):
                value = value.lower()
            elif key in ("width", "height") and value.endswith("px"):
                value = dimension(value[:-2], origin, 1)
            else:
                origin.error(f"Unsupported GCSS property or value for {key}")
            values[key] = (value, origin)
        rules.append(Rule(name, values))
    return rules


def cascade(node, rules):
    selectors = {node.tag: 0, "#" + node.attributes["id"]: 2}
    selectors.update({"." + name: 1 for name in node.attributes.get("class", "").split()})
    result = {}
    for rule in rules:
        if rule.selector in selectors:
            priority = selectors[rule.selector]
            for key, (value, origin) in rule.values.items():
                if key not in result or priority >= result[key][0]:
                    result[key] = (priority, value, origin)
    return {key: (value, origin) for key, (_, value, origin) in result.items()}


def binding(value, at, mode):
    match = BINDING.fullmatch(value)
    if match is None or (match[2] or "OneWay") != mode:
        at.error(f"Expected {mode} binding")
    return match[1]


def command(value, at):
    if not re.fullmatch(NAME, value):
        at.error("Expected a registered C++ command identifier")
    return json.dumps(value)


def lower(nodes, rules):
    lines, origins = [], []

    def emit(text, origin):
        lines.append(text)
        origins.append({"source": origin.file, "line": origin.line})

    emit("view settings {", nodes[0].origin)
    for node in nodes:
        attrs, at = node.attributes, node.origin
        styles = cascade(node, rules)
        emit(f"  {gxml_schema.TYPES[node.tag].legacy_role} {json.dumps(attrs['id'])} {{", at)
        if node.tag == "Window":
            if styles:
                next(iter(styles.values()))[1].error("Window style is outside the prototype subset")
            emit("    layout: fill();", at)
        else:
            x = dimension(attrs.get("x", ""), at, 0)
            y = dimension(attrs.get("y", ""), at, 0)
            for key in ("width", "height"):
                if key in attrs:
                    dimension(attrs[key], at, 1)
            width, width_origin = styles["width"] if "width" in styles else (dimension(attrs.get("width", ""), at, 1), at)
            height, height_origin = styles["height"] if "height" in styles else (dimension(attrs.get("height", ""), at, 1), at)
            emit("    layout: fixed(", at)
            emit(f"      {x}, {y},", at)
            emit(f"      {width},", width_origin)
            emit(f"      {height});", height_origin)
            text = attrs.get("text", "")
            if node.tag == "TextBox":
                field = binding(text, at, "TwoWay")
                action = attrs.get("onChange", "")
                if action != "set_" + field:
                    at.error("TwoWay binding requires the Settings setter for the same field")
                emit(f"    value: {field};", at)
                emit(f"    text: {field};", at)
                emit(f"    on: {command(action, at)};", at)
            else:
                if text.startswith("{"):
                    text = binding(text, at, "OneWay")
                else:
                    if any(ord(char) < 32 and char not in "\n\t" for char in text):
                        at.error("Unsupported control character in text")
                    text = json.dumps(text, ensure_ascii=False)
                emit(f"    text: {text};", at)
                if "onClick" in attrs:
                    emit(f"    on: {command(attrs['onClick'], at)};", at)
                if "enabled" in attrs:
                    emit(f"    enabled: {binding(attrs['enabled'], at, 'OneWay')};", at)
            if "color" in styles:
                color, color_origin = styles["color"]
                emit(f"    text_color: {json.dumps(color)};", color_origin)
        emit("  }", at)
    emit("}", nodes[0].origin)
    return "\n".join(lines) + "\n", origins


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def compile_ui(markup, style, codegen, output):
    if markup.suffix != ".gxml" or style.suffix != ".gcss":
        raise Diagnostic("Expected .gxml and .gcss inputs")
    markup_bytes, xml = load(markup)
    style_bytes, css = load(style)
    ir, origins = lower(parse_gxml(markup.name, xml), parse_gcss(style.name, css))
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output) as temp:
        stage = Path(temp)
        intermediate = stage / "layout.gui"
        header = stage / "layout.generated.hpp"
        intermediate.write_text(ir, encoding="utf-8", newline="\n")
        run = subprocess.run([str(codegen.resolve(strict=True)), "--output", str(header), str(intermediate)],
                             capture_output=True, encoding="utf-8", errors="replace", timeout=45)
        if run.returncode:
            diagnostic = run.stderr.strip()
            match = re.match(r"layout\.gui:(\d+):\d+: (.*)", diagnostic)
            if match and 1 <= int(match[1]) <= len(origins):
                source = origins[int(match[1]) - 1]
                Origin(source["source"], source["line"]).error(match[2])
            raise Diagnostic(diagnostic or f"Native backend exited with code {run.returncode}")
        native = header.read_bytes()
        header_origins = []
        for index, line in enumerate(native.decode("utf-8").splitlines(), 1):
            if line.startswith("// gui-map "):
                marker = json.loads(line.removeprefix("// gui-map "))
                source = origins[marker["source_line"] - 1]
                header_origins.append({**source, "generated_line": index + 2, "context": marker["context"]})
        source_map = {
            "format": 1,
            "sources": {markup.name: sha(markup_bytes), style.name: sha(style_bytes)},
            "artifacts": {intermediate.name: sha(intermediate.read_bytes()), header.name: sha(native)},
            "intermediate_lines": origins,
            "native_lines": header_origins,
        }
        map_file = stage / "layout.map.json"
        map_file.write_text(json.dumps(source_map, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
        for file in (intermediate, header, map_file):
            target = output / file.name
            if not target.exists() or target.read_bytes() != file.read_bytes():
                file.replace(target)


def publish_file(output, data):
    if output.is_file() and output.stat().st_size == len(data) and output.read_bytes() == data:
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent) as temp:
        staged = Path(temp) / output.name
        staged.write_bytes(data)
        staged.replace(output)


def cache_path(directory, mode, sources):
    tools = Path(__file__).resolve().parent
    compiler_inputs = ("gxmlc.py", "gxml_schema.py", "gxml_core.py", "gcss_syntax.py", "gcss_core.py", "gxml_ir.py")
    identity = [mode, list(sys.version_info[:3]), [(name, sha((tools / name).read_bytes())) for name in compiler_inputs],
                [(role, name, sha(raw)) for role, name, raw in sources]]
    return directory / (sha(json.dumps(identity, ensure_ascii=False).encode("utf-8")) + ".cache")


def compile_core(markup, style, model_path, output, mode, cache):
    import gxml_core
    import gxml_ir

    suffix = {"native": ".hpp", "binary": ".guir", "bytecode": ".guib", "hybrid": ".guih", "topology": ".hpp"}[mode]
    if markup.suffix != ".gxml" or (style and style.suffix != ".gcss") or output.suffix != suffix:
        raise Diagnostic(f"Expected .gxml and optional .gcss inputs with a {suffix} output")
    markup_bytes, markup_source = load(markup)
    style_bytes, style_source = load(style) if style else (b"", "")
    model_bytes, model_source = load(model_path) if model_path else (b"", "")
    raw_sources = [(0, markup.name, markup_bytes)]
    if style:
        raw_sources.append((1, style.name, style_bytes))
    if model_path:
        raw_sources.append((2, model_path.name, model_bytes))
    entry = cache_path(cache, mode, raw_sources) if cache else None
    cache_limit = 64 * 1024 * 1024
    if entry and entry.is_file():
        with entry.open("rb") as stream:
            cached = stream.read(cache_limit + 33)
        if 32 < len(cached) <= cache_limit + 32 and cached[:32] == hashlib.sha256(cached[32:]).digest():
            publish_file(output, cached[32:])
            print("Core cache hit")
            return
    model = gxml_schema.parse_model(model_path.name, model_source) if model_path else gxml_schema.Model()
    document = gxml_core.compile_document(markup.name, markup_source, style.name if style else markup.name,
                                          style_source, model)
    if mode == "native":
        data = gxml_core.emit_header(document.nodes, document.rules, document.resources).encode("utf-8")
    elif mode == "topology":
        data = gxml_core.emit_topology_header(document.nodes, gxml_ir.topology_hash(document.nodes)).encode("utf-8")
    else:
        data = gxml_ir.encode(document.nodes, document.rules, model, document.resources, document.resource_spans,
                              raw_sources, mode=mode)
    if entry and len(data) <= cache_limit:
        publish_file(entry, hashlib.sha256(data).digest() + data)
        print("Core cache miss")
    publish_file(output, data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--describe-schema", action="store_true", help="Print the registered types and properties as JSON")
    mode.add_argument("--validate", action="store_true", help="Check the 0.2 schema without generating native code")
    for flag, value, description in (
        ("--compile-core", "native", "Generate the Core C++ document header"),
        ("--compile-ir", "binary", "Generate a versioned Core .guir file"),
        ("--compile-bytecode", "bytecode", "Generate a Core .guib construction program"),
        ("--compile-hybrid-header", "topology", "Generate a C++ header with the Hybrid topology"),
        ("--compile-hybrid-data", "hybrid", "Generate the paired Hybrid .guih data"),
    ):
        mode.add_argument(flag, dest="deployment", action="store_const", const=value, help=description)
    for option in ("markup", "style", "codegen", "output", "model"):
        parser.add_argument(f"--{option}", type=Path)
    parser.add_argument("--cache-dir", type=Path, help="Reuse compiled results from a trusted local build cache")
    args = parser.parse_args()
    native = args.style, args.codegen, args.output
    try:
        if args.cache_dir and not args.deployment:
            parser.error("--cache-dir requires a Core compilation mode")
        if args.describe_schema:
            if args.markup or args.model or any(native):
                parser.error("--describe-schema takes no input or output paths")
            print(json.dumps(gxml_schema.describe_schema(), indent=2))
        elif args.validate:
            if not args.markup or args.markup.suffix != ".gxml" or any(native):
                parser.error("--validate requires --markup <file.gxml> and accepts an optional --model <file.json>")
            model = gxml_schema.parse_model(args.model.name, load(args.model)[1]) if args.model else None
            nodes = gxml_schema.validate_document(args.markup.name, load(args.markup)[1], model)
            print(json.dumps({"schema": gxml_schema.VERSION, "nodes": len(nodes),
                              "components": sum(node.element.tag in ("Component", "Template") for node in nodes),
                              "resources": sum(node.element.tag == "Resource" for node in nodes)}))
        elif args.deployment:
            if not args.markup or not args.output or args.codegen:
                parser.error("Core compilation requires --markup and --output, with optional --style and --model")
            compile_core(args.markup, args.style, args.model, args.output, args.deployment, args.cache_dir)
        else:
            if not args.markup or not all(native) or args.model:
                parser.error("Native compilation requires --markup, --style, --codegen and --output")
            compile_ui(args.markup, args.style, args.codegen, args.output)
    except (OSError, Diagnostic, subprocess.TimeoutExpired) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
