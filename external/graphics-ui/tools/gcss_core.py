"""Parse the registered Core GCSS selectors and typed values."""

from dataclasses import dataclass, field
import re

import gcss_syntax
from gxml_schema import CLASS, NAME, NODE_ID, Reference

TYPES = {
    "Window": "window", "Panel": "panel", "StackPanel": "stack_panel", "Grid": "grid", "FlexPanel": "flex_panel",
    "TextBlock": "text_block", "TextBox": "text_box", "Button": "button",
}
STATES = {"disabled": "disabled", "focus": "focus", "hover": "hover", "pressed": "pressed", "active": "pressed"}
PROPERTIES = {"width": "length", "height": "length", "gap": "length", "font-size": "length", "padding": "padding",
              "color": "color", "background-color": "color", "font-id": "font"}
HEADER = re.compile(
    rf"(?:{gcss_syntax.COMMENT}|[^{{}};/]|/(?!\*))+"
)
PART = re.compile(r"[^\s>]+")
MODIFIER = re.compile(rf"(#(?:{NODE_ID.pattern}))|(\.(?:{CLASS.pattern}))|(:[a-z-]+)")
VARIABLE = re.compile(r"var\((--[a-z][a-z0-9-]{0,61})\)")
TOKEN = re.compile(r"--[a-z][a-z0-9-]{0,61}")
RESOURCE = re.compile(rf"resource\(({NODE_ID.pattern})\)")


@dataclass
class SelectorPart:
    kind: str | None = None
    id: str = ""
    classes: list[str] = field(default_factory=list)
    state: str = "none"
    root: bool = False
    relation: str = "none"


@dataclass
class CoreRule:
    selector: list[SelectorPart]
    style: dict
    span: tuple[int, int]


def parse_selector(value, origin):
    text = re.sub(gcss_syntax.COMMENT, " ", value).strip()
    cursor, relation, result = 0, "none", []
    while cursor < len(text):
        match = PART.match(text, cursor)
        if not match or len(result) == 8:
            origin.error("Expected a selector part (limit eight parts)", "E_GCSS")
        simple = match[0]
        part = SelectorPart(relation=relation)
        type_match = re.match(NAME, simple)
        offset = 0
        if type_match:
            part.kind = TYPES.get(type_match[0])
            if not part.kind:
                origin.error(f"Unknown Core selector type {type_match[0]!r}", "E_GCSS")
            offset = type_match.end()
        while offset < len(simple):
            marker = MODIFIER.match(simple, offset)
            if not marker:
                origin.error(f"Unsupported selector {simple!r}", "E_GCSS")
            token = marker[0][1:]
            if marker[1] and not part.id:
                part.id = token
            elif marker[2] and len(part.classes) < 8:
                part.classes.append(token)
            elif marker[3] and token == "root" and not part.root:
                part.root = True
            elif marker[3] and token in STATES and part.state == "none":
                part.state = STATES[token]
            else:
                origin.error(f"Unsupported or repeated selector modifier {marker[0]!r}", "E_GCSS")
            offset = marker.end()
        result.append(part)
        cursor = match.end()
        while cursor < len(text) and text[cursor].isspace():
            cursor += 1
        relation = "descendant"
        if text[cursor:cursor + 1] == ">":
            relation = "child"
            cursor += 1
            while cursor < len(text) and text[cursor].isspace():
                cursor += 1
            if cursor == len(text):
                origin.error("Expected a selector after '>'", "E_GCSS")
    if not result:
        origin.error("Expected a Core selector", "E_GCSS")
    return result


def length(value, origin, minimum):
    if value == "0" and not minimum:
        return 0
    if re.fullmatch(r"[0-9]{1,4}px", value) and minimum <= int(value[:-2]) <= 4096:
        return int(value[:-2])
    origin.error(f"Expected a length between {minimum}px and 4096px", "E_GCSS")


def style_value(name, value, origin, resources):
    kind = PROPERTIES.get(name)
    if ref := RESOURCE.fullmatch(value):
        known = resources.get(ref[1])
        if kind in ("color", "font") and known and known[0] == kind:
            return Reference("resource", ref[1])
        origin.error(f"Unknown or mismatched resource {ref[1]!r} for {name!r}", "E_GCSS")
    if kind == "color" and re.fullmatch(r"#[0-9a-fA-F]{6}", value):
        return value.lower()
    if kind == "length":
        return length(value, origin, 0 if name == "gap" else 1)
    if kind == "font" and re.fullmatch(r"[0-9]{1,20}", value) and 0 < int(value) < (1 << 64):
        return int(value)
    if kind == "padding":
        edges = [length(edge, origin, 0) for edge in value.split()]
        match edges:
            case [all_edges]: return [all_edges] * 4
            case [vertical, horizontal]: return [horizontal, vertical, horizontal, vertical]
            case [top, horizontal, bottom]: return [horizontal, top, horizontal, bottom]
            case [top, right, bottom, left]: return [left, top, right, bottom]
    origin.error(f"Unsupported GCSS property or value for {name!r}", "E_GCSS")


def parse_core_style(file, source, resources):
    staged, tokens = [], {}
    byte_offsets = [0]
    for character in source:
        byte_offsets.append(byte_offsets[-1] + len(character.encode("utf-8")))
    for selector, origin, declarations, (start, end) in gcss_syntax.rules(file, source, HEADER, detailed=True):
        parts = parse_selector(selector, origin)
        for name, value, at in declarations:
            if name.startswith("--"):
                if not TOKEN.fullmatch(name) or parts != [SelectorPart(root=True)]:
                    at.error("Theme tokens must be defined on :root", "E_GCSS")
                tokens[name] = (value, at)
        staged.append((parts, declarations, (byte_offsets[start], byte_offsets[end])))
    if len(tokens) > 128:
        next(iter(tokens.values()))[1].error("Theme token limit is 128", "E_LIMIT")

    def substitute(value, at):
        path = []
        while ref := VARIABLE.fullmatch(value):
            name = ref[1]
            if name in path:
                at.error("Theme token cycle: " + " -> ".join(path[path.index(name):] + [name]), "E_CYCLE")
            if name not in tokens:
                at.error(f"Unknown theme token {name!r}", "E_GCSS")
            path.append(name)
            value = tokens[name][0]
        return value

    for value, at in tokens.values():
        substitute(value, at)
    result = []
    for parts, declarations, span in staged:
        styles = {}
        for name, value, at in declarations:
            if not name.startswith("--"):
                styles[name] = style_value(name, substitute(value, at), at, resources)
        if styles:
            result.append(CoreRule(parts, styles, span))
    return result
