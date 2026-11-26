"""Lower validated GXML and GCSS into a typed, in-memory Core Document."""

from dataclasses import dataclass
import json

import gcss_core
from gxml_schema import MAX_DEPTH, MAX_NODES, Reference, validate_document


def resolved_resources(nodes):
    raw = {node.element.attributes["id"]: node.properties for node in nodes if node.element.tag == "Resource"}
    values = {}
    for name, resource in raw.items():
        value = resource["value"]
        while isinstance(value, Reference):
            value = raw[value.name]["value"]
        values[name] = (resource["type"], value)
    return values


@dataclass
class CoreNode:
    id: str
    parent: int | None
    kind: str
    classes: tuple[str, ...]
    aliases: tuple[str, ...]
    horizontal: bool
    columns: int
    style: dict
    text: str | Reference
    enabled: bool | Reference
    command: str
    span: tuple[int, int]
    use_spans: tuple[tuple[int, int], ...]


@dataclass
class CoreSourceDocument:
    nodes: list[CoreNode]
    rules: list[gcss_core.CoreRule]
    resources: dict
    resource_spans: dict


def compile_document(markup_file, markup, style_file, style, model):
    registered = validate_document(markup_file, markup, model)
    resources = resolved_resources(registered)
    nodes = expand(registered)
    rules = gcss_core.parse_core_style(style_file, style, resources)
    spans = {node.element.attributes["id"]: node.element.span for node in registered if node.element.tag == "Resource"}
    return CoreSourceDocument(nodes, rules, resources, spans)


def expand(nodes):
    children = [[] for _ in nodes]
    for index, node in enumerate(nodes):
        parent = node.element.parent
        if parent is not None:
            children[parent].append(index)
    templates = {node.element.attributes["id"]: children[index][0] for index, node in enumerate(nodes)
                 if node.element.tag in ("Component", "Template")}
    expanded = []

    def append(index, parent, prefix, depth, instance=None, overrides=None, classes=(), aliases=(), use_spans=()):
        node = nodes[index]
        element = node.element
        at, name = element.origin, element.attributes["id"]
        if depth > MAX_DEPTH or len(expanded) == MAX_NODES:
            at.error(f"Expanded document exceeds {MAX_NODES} nodes or depth {MAX_DEPTH}", "E_LIMIT")
        properties = {**node.properties, **(overrides or {})}
        if "x" in properties or "y" in properties:
            at.error("Core documents use flow layout for child positions", "E_CORE")
        joined = f"{prefix}/{name}" if prefix else name
        identity = instance or joined
        passed_classes = tuple(dict.fromkeys((*element.attributes.get("class", "").split(), *classes)))
        if instance and name != instance.rsplit("/", 1)[-1]:
            aliases = (*aliases, name)
        if element.tag == "Use":
            target = templates[node.properties["component"]]
            return append(target, parent, prefix, depth, identity,
                          {key: properties[key] for key in ("width", "height") if key in properties},
                          passed_classes, aliases, (*use_spans, element.span))
        kind = gcss_core.TYPES.get(element.tag)
        if not kind:
            at.error(f"No Core document runtime for {element.tag!r}", "E_CORE")

        style = {key: properties[key] for key in ("width", "height", "color", "font") if key in properties}
        if "font" in style:
            style["font-id"] = style.pop("font")
        text, enabled = properties.get("text", ""), properties.get("enabled", True)
        command = properties.get("onChange") or properties.get("onClick")
        emitted = len(expanded)
        expanded.append(CoreNode(identity, parent, kind, passed_classes, aliases, properties.get("orientation") == "horizontal",
                                 properties.get("columns", 1), style, text, enabled, command.name if command else "",
                                 element.span, use_spans))
        for child in children[index]:
            if nodes[child].slot == "content":
                append(child, emitted, identity if instance else prefix, depth + 1, use_spans=use_spans)

    append(0, None, "", 1)
    return expanded


def string(value):
    return json.dumps(value, ensure_ascii=False)


def strings(values):
    return "{" + ", ".join(map(string, values)) + "}"


def resolve(value, resources):
    return resources[value.name][1] if isinstance(value, Reference) and value.category == "resource" else value


def style_initializer(properties, resources):
    values = []
    for name in ("width", "height", "gap", "font-size", "padding", "color", "background-color", "font-id"):
        value = resolve(properties.get(name), resources)
        if value is None:
            values.append("{}")
        elif name == "padding":
            values.append("LayoutInsets{" + ", ".join(map(str, value)) + "}")
        elif name in ("color", "background-color"):
            values.append("RenderColor{" + ", ".join(str(int(value[i:i + 2], 16)) for i in (1, 3, 5)) + "}")
        elif name == "font-id":
            values.append(str(value) + "ull")
        else:
            values.append(str(value))
    return "{" + ", ".join(values) + "}"


def shape_initializer(node):
    return [string(node.id), str(node.parent) if node.parent is not None else "{}", "DocumentKind::" + node.kind,
            strings(node.classes), strings(node.aliases), str(node.horizontal).lower(), str(node.columns)]


def node_initializer(node, resources):
    text, enabled = resolve(node.text, resources), node.enabled
    text_field = text.name if isinstance(text, Reference) else ""
    enabled_field = enabled.name if isinstance(enabled, Reference) else ""
    fields = [*shape_initializer(node), style_initializer(node.style, resources), string("" if text_field else text),
              string(text_field), string(enabled_field), string(node.command), str(bool(enabled)).lower()]
    return "            {" + ", ".join(fields) + "},"


def rule_initializer(rule, resources):
    selector = []
    for part in rule.selector:
        fields = ["DocumentKind::" + part.kind if part.kind else "{}", string(part.id), strings(part.classes),
                  "DocumentState::" + part.state, str(part.root).lower(), "DocumentRelation::" + part.relation]
        selector.append("{" + ", ".join(fields) + "}")
    return "            {{" + ", ".join(selector) + "}, " + style_initializer(rule.style, resources) + "},"


def emit_header(expanded, rules, resources):
    symbol = "document_" + expanded[0].id.replace("-", "_")
    return "\n".join([
        "// Generated from validated GXML/GCSS by gxmlc --compile-core.",
        "#pragma once", "#include <graphics-ui/core/document.hpp>", "",
        "namespace graphics_ui::generated {", f"inline core::Document {symbol}() {{", "    using namespace core;",
        "    return {", "        {", *(node_initializer(node, resources) for node in expanded), "        },",
        "        {", *(rule_initializer(rule, resources) for rule in rules), "        }", "    };", "}", "}", "",
    ])


def emit_topology_header(expanded, fingerprint):
    symbol = "topology_" + expanded[0].id.replace("-", "_")
    digest = ", ".join(f"std::byte{{0x{byte:02x}}}" for byte in fingerprint)
    return "\n".join([
        "// Generated from validated GXML/GCSS by gxmlc --compile-hybrid-header.",
        "#pragma once", "#include <graphics-ui/core/document_ir.hpp>", "",
        "namespace graphics_ui::generated {", f"inline core::DocumentTopology {symbol}() {{", "    using namespace core;",
        "    return {", "        {" + digest + "},", "        {",
        *("            {" + ", ".join(shape_initializer(node)) + "}," for node in expanded),
        "        }", "    };", "}", "}", "",
    ])
