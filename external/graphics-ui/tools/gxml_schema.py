"""Shared GXML type registration and semantic validation."""

from dataclasses import asdict, dataclass, field
import json
import re
from xml.parsers import expat

NAMESPACE = "urn:graphics-ui:gxml"
VERSION = "0.2-schema-preview"
MAX_BYTES = 65536
MAX_NODES = 512
MAX_DEPTH = 32
MAX_ATTRIBUTES = 16
LEGACY_MAX_NODES = 129
NAME = r"[A-Za-z_][A-Za-z_0-9]*"
NODE_ID = re.compile(r"[a-z][a-z0-9-]{0,63}")
CLASS = re.compile(r"[A-Za-z_][A-Za-z_0-9]{0,63}")
BINDING = re.compile(rf"\{{Binding ({NAME})(?:, Mode=(OneWay|TwoWay))?\}}")
RESOURCE = re.compile(rf"\{{Resource ({NODE_ID.pattern})\}}")
MODEL_NAME = re.compile(r"[A-Za-z_][A-Za-z_0-9]{0,63}")
OPEN_TAG = re.compile(rb'''<(?:[^"'<>]|"[^"]*"|'[^']*')*>''')
VALUE_TYPES = frozenset(("text", "bool", "integer", "number"))
RESOURCE_TYPES = frozenset(("text", "color", "font"))


class Diagnostic(ValueError):
    def __init__(self, message, *, origin=None, code=None, detail=None):
        super().__init__(message)
        self.origin = origin
        self.code = code
        self.detail = message if detail is None else detail


@dataclass(frozen=True)
class Origin:
    file: str
    line: int
    column: int | None = None

    def error(self, message, code=None):
        location = f"{self.file}:{self.line}"
        if self.column is not None:
            location += f":{self.column}"
        text = message if code is None else f"{code}: {message}"
        raise Diagnostic(f"{location}: {text}", origin=self, code=code, detail=message)


@dataclass(frozen=True)
class Property:
    kind: str
    required: bool = False
    default: str | None = None
    binding: str | None = None
    resource: bool = False
    minimum: int | None = None
    maximum: int | None = None
    argument: str | None = None
    writes_binding: str | None = None


@dataclass(frozen=True)
class Slot:
    category: str
    minimum: int = 0
    maximum: int = 128


@dataclass(frozen=True)
class ComponentType:
    properties: dict[str, Property]
    slots: dict[str, Slot] = field(default_factory=dict)
    category: str = "visual"
    legacy_role: str | None = None
    legacy_properties: tuple[str, ...] = ()


IDENTITY = {"id": Property("id", required=True), "class": Property("classes"), "slot": Property("slot")}
GEOMETRY = {axis: Property("integer", minimum=0 if axis in ("x", "y") else 1, maximum=4096)
            for axis in ("x", "y", "width", "height")}
TEXT = {"text": Property("text", default="", binding="OneWay", resource=True),
        "font": Property("font", resource=True), "color": Property("color", resource=True)}
ORIENTATION = Property("axis", default="vertical")
EVENT = Property("command")
CONTENT = {"content": Slot("visual")}
TYPES = {
    "Window": ComponentType({**IDENTITY, "width": GEOMETRY["width"], "height": GEOMETRY["height"]},
                            {**CONTENT, "definitions": Slot("definition")}, "root", "canvas", ("id", "class")),
    "Panel": ComponentType({**IDENTITY, **GEOMETRY}, CONTENT),
    "StackPanel": ComponentType({**IDENTITY, **GEOMETRY, "orientation": ORIENTATION}, CONTENT),
    "Grid": ComponentType({**IDENTITY, **GEOMETRY,
                           "columns": Property("integer", default="1", minimum=1, maximum=64)}, CONTENT),
    "FlexPanel": ComponentType({**IDENTITY, **GEOMETRY, "orientation": ORIENTATION}, CONTENT),
    "ScrollView": ComponentType({**IDENTITY, **GEOMETRY}, {"content": Slot("visual", 1, 1)}),
    "TextBlock": ComponentType({**IDENTITY, **GEOMETRY, **TEXT}, legacy_role="label", legacy_properties=("text",)),
    "TextBox": ComponentType({**IDENTITY, **GEOMETRY, **TEXT, "text": Property("text", required=True, binding="TwoWay"),
                             "onChange": Property("command", required=True, argument="text", writes_binding="text")},
                            legacy_role="input", legacy_properties=("text", "onChange")),
    "Button": ComponentType({**IDENTITY, **GEOMETRY, **TEXT, "enabled": Property("bool", default="true", binding="OneWay"),
                            "onClick": EVENT}, legacy_role="button", legacy_properties=("text", "enabled", "onClick")),
    "Component": ComponentType({"id": IDENTITY["id"], "slot": IDENTITY["slot"]},
                               {"content": Slot("visual", 1, 1)}, "definition"),
    "Template": ComponentType({"id": IDENTITY["id"], "slot": IDENTITY["slot"]},
                              {"content": Slot("visual", 1, 1)}, "definition"),
    "Use": ComponentType({**IDENTITY, **GEOMETRY, "component": Property("id", required=True)}),
    "Resource": ComponentType({"id": IDENTITY["id"], "slot": IDENTITY["slot"], "type": Property("resource_type", required=True),
                              "value": Property("resource_value", required=True)}, category="definition"),
}


def describe_schema():
    return {"version": VERSION, "namespace": NAMESPACE, "root": "Window",
            "limits": {"source_bytes": MAX_BYTES, "nodes": MAX_NODES, "depth": MAX_DEPTH, "attributes": MAX_ATTRIBUTES},
            "model_types": sorted(VALUE_TYPES), "resource_types": sorted(RESOURCE_TYPES),
            "types": {name: asdict(spec) for name, spec in TYPES.items()}}


@dataclass(frozen=True)
class Element:
    namespace: str
    tag: str
    attributes: dict[str, str]
    origin: Origin
    parent: int | None
    span: tuple[int, int] | None


def parse_elements(file, source, *, legacy=False):
    initial = Origin(file, 1, None if legacy else 1)
    try:
        encoded = source.encode("utf-8")
    except UnicodeError:
        initial.error("Expected UTF-8", None if legacy else "E_XML")
    if len(encoded) > MAX_BYTES:
        initial.error("GXML source exceeds 64 KiB", None if legacy else "E_LIMIT")
    parser = expat.ParserCreate(encoding="UTF-8", namespace_separator="|")
    parser.SetParamEntityParsing(expat.XML_PARAM_ENTITY_PARSING_NEVER)
    elements, parents = [], []

    def origin():
        return Origin(file, parser.CurrentLineNumber, None if legacy else parser.CurrentColumnNumber + 1)

    def start(qname, attrs):
        at = origin()
        uri, _, tag = qname.rpartition("|")
        if legacy:
            if len(parents) == 2 or len(elements) == LEGACY_MAX_NODES:
                at.error("Expected one Window with at most 128 leaf children")
        elif len(parents) == MAX_DEPTH or len(elements) == MAX_NODES or len(attrs) > MAX_ATTRIBUTES:
            at.error(f"Element limits are {MAX_NODES} nodes, depth {MAX_DEPTH} and {MAX_ATTRIBUTES} attributes", "E_LIMIT")
        parent = parents[-1] if parents else None
        parents.append(len(elements))
        span = None
        if not legacy:
            start_byte = parser.CurrentByteIndex
            span = (start_byte, OPEN_TAG.match(encoded, start_byte).end())
        elements.append(Element(uri, tag, attrs, at, parent, span))

    def characters(value):
        if value.strip():
            origin().error("Set visible content with the text attribute", None if legacy else "E_XML")

    def disallowed(*_):
        origin().error("DTD, entities and processing instructions are prohibited", None if legacy else "E_XML")

    def declaration(version, encoding, _):
        if version != "1.0" or (encoding and encoding.lower() != "utf-8"):
            origin().error("Expected XML 1.0 with UTF-8 encoding", None if legacy else "E_XML")

    parser.StartElementHandler = start
    parser.EndElementHandler = lambda _: parents.pop()
    parser.CharacterDataHandler = characters
    parser.StartDoctypeDeclHandler = parser.EntityDeclHandler = disallowed
    parser.ExternalEntityRefHandler = parser.ProcessingInstructionHandler = disallowed
    parser.XmlDeclHandler = declaration
    try:
        parser.Parse(source, True)
    except expat.ExpatError as error:
        Origin(file, error.lineno, None if legacy else error.offset + 1).error(str(error), None if legacy else "E_XML")
    return elements


def validate_identity(node, identifiers, *, legacy=False):
    at, attrs = node.origin, node.attributes
    identifier = attrs.get("id", "")
    if not NODE_ID.fullmatch(identifier) or identifier in identifiers:
        at.error(f"Missing, invalid or duplicate id {identifier!r}", None if legacy else "E_ID")
    classes = attrs.get("class", "").split()
    if len(classes) > 8 or any(not CLASS.fullmatch(item) for item in classes):
        at.error("Expected up to eight simple class names", None if legacy else "E_VALUE")
    identifiers.add(identifier)
    return identifier


def validate_legacy(file, source):
    elements = parse_elements(file, source, legacy=True)
    identifiers = set()
    for node in elements:
        spec = TYPES.get(node.tag)
        at = node.origin
        if node.namespace != NAMESPACE or spec is None or not spec.legacy_role:
            qualified = f"{node.namespace}|{node.tag}" if node.namespace else node.tag
            at.error(f"Unsupported GXML element {qualified}")
        if (node.parent is None) != (node.tag == "Window"):
            at.error("Expected one Window with at most 128 leaf children")
        allowed = set(spec.legacy_properties)
        if node.parent is not None:
            allowed.update(GEOMETRY.keys() | {"id", "class"})
        if unexpected := node.attributes.keys() - allowed:
            at.error(f"Unsupported {node.tag} attribute {min(unexpected)}")
        validate_identity(node, identifiers, legacy=True)
    return elements


@dataclass(frozen=True)
class ModelField:
    kind: str
    dependencies: tuple[str, ...] | None


@dataclass(frozen=True)
class ModelCommand:
    argument: str | None
    writes: str | None


@dataclass(frozen=True)
class Model:
    fields: dict[str, ModelField] = field(default_factory=dict)
    commands: dict[str, ModelCommand] = field(default_factory=dict)


def check_cycles(edges, label):
    active, done, trail = set(), set(), []

    def visit(name):
        active.add(name)
        trail.append(name)
        for target, origin in edges[name]:
            if target not in edges:
                origin.error(f"Unknown {label} {target!r}", "E_REFERENCE")
            if target in active:
                chain = " -> ".join(trail[trail.index(target):] + [target])
                origin.error(f"{label} cycle: {chain}", "E_CYCLE")
            if target not in done:
                visit(target)
        trail.pop()
        active.remove(name)
        done.add(name)

    for name in edges:
        if name not in done:
            visit(name)


def parse_model(file, source):
    at = Origin(file, 1, 1)
    try:
        size = len(source.encode("utf-8"))
    except UnicodeError:
        at.error("Expected UTF-8 model JSON", "E_MODEL")
    if size > MAX_BYTES:
        at.error("Model source exceeds 64 KiB", "E_LIMIT")

    def unique(entries):
        result = {}
        for name, value in entries:
            if name in result:
                at.error(f"Duplicate model key {name!r}", "E_MODEL")
            result[name] = value
        return result

    try:
        data = json.loads(source, object_pairs_hook=unique)
    except (ValueError, RecursionError) as error:
        if isinstance(error, Diagnostic):
            raise
        location = Origin(file, error.lineno, error.colno) if isinstance(error, json.JSONDecodeError) else at
        location.error("Invalid model JSON", "E_MODEL")
    if (not isinstance(data, dict) or data.keys() != {"format", "fields", "commands"} or type(data["format"]) is not int or
            data["format"] != 1):
        at.error("Expected model format 1 with fields and commands", "E_MODEL")
    for group in ("fields", "commands"):
        if not isinstance(data[group], dict) or len(data[group]) > 128:
            at.error(f"Expected an object with up to 128 {group}", "E_MODEL")
        for name, spec in data[group].items():
            if not MODEL_NAME.fullmatch(name) or not isinstance(spec, dict):
                at.error(f"Invalid {group} entry {name!r}", "E_MODEL")

    fields = {}
    for name, spec in data["fields"].items():
        kind = spec.get("type")
        if not isinstance(kind, str) or kind not in VALUE_TYPES or spec.keys() - {"type", "depends"}:
            at.error(f"Invalid field type or property for {name!r}", "E_MODEL")
        dependencies = None
        if "depends" in spec:
            values = spec["depends"]
            if (not isinstance(values, list) or len(values) > 128 or
                    any(not isinstance(item, str) or not MODEL_NAME.fullmatch(item) for item in values) or
                    len(values) != len(set(values))):
                at.error(f"Invalid dependencies for {name!r}", "E_MODEL")
            dependencies = tuple(values)
        fields[name] = ModelField(kind, dependencies)
    check_cycles({name: [(target, at) for target in spec.dependencies or ()] for name, spec in fields.items()}, "binding")

    commands = {}
    for name, spec in data["commands"].items():
        argument, writes = spec.get("argument"), spec.get("writes")
        if ("argument" not in spec or spec.keys() - {"argument", "writes"} or
                (argument is not None and (not isinstance(argument, str) or argument not in VALUE_TYPES))):
            at.error(f"Invalid argument type for command {name!r}", "E_MODEL")
        if "writes" in spec:
            target = fields.get(writes) if isinstance(writes, str) else None
            if not target or target.kind != argument or target.dependencies is not None:
                at.error(f"Command {name!r} needs a writable field matching its argument", "E_MODEL")
        commands[name] = ModelCommand(argument, writes)
    return Model(fields, commands)


@dataclass(frozen=True)
class Reference:
    category: str
    name: str
    mode: str | None = None


def literal(value, prop, at, name):
    kind = prop.kind
    if kind == "text" and not any(ord(char) < 32 and char not in "\t\n" for char in value):
        return value
    if kind == "bool" and value in ("true", "false"):
        return value == "true"
    if kind in ("integer", "font") and re.fullmatch(r"[0-9]{1,20}", value):
        number = int(value)
        minimum, maximum = (1, (1 << 64) - 1) if kind == "font" else (prop.minimum, prop.maximum)
        if minimum <= number <= maximum:
            return number
    if kind == "color" and re.fullmatch(r"#[0-9a-fA-F]{6}", value):
        return value.lower()
    if kind == "axis" and value in ("horizontal", "vertical"):
        return value
    if kind == "id" and NODE_ID.fullmatch(value):
        return value
    at.error(f"Invalid {kind} value for {name!r}: {value!r}", "E_VALUE")


def property_value(name, value, prop, at, model, resources):
    if prop.kind == "command":
        signature = model.commands.get(value)
        if not signature:
            at.error(f"Unknown command {value!r}", "E_REFERENCE")
        if signature.argument != prop.argument:
            at.error(f"Command {value!r} has the wrong argument for {name!r}", "E_TYPE")
        return Reference("command", value)
    if prop.binding and value.startswith("{Binding"):
        match = BINDING.fullmatch(value)
        mode = (match[2] or "OneWay") if match else None
        if mode != prop.binding:
            at.error(f"Property {name!r} requires a {prop.binding} binding", "E_BINDING")
        target = model.fields.get(match[1])
        if target is None:
            at.error(f"Unknown binding field {match[1]!r}", "E_REFERENCE")
        if target.kind != prop.kind or (mode == "TwoWay" and target.dependencies is not None):
            at.error(f"Binding field {match[1]!r} has the wrong type or is read-only", "E_TYPE")
        return Reference("binding", match[1], mode)
    if prop.binding == "TwoWay":
        at.error(f"Property {name!r} requires a TwoWay binding", "E_BINDING")
    if value.startswith("{Resource"):
        match = RESOURCE.fullmatch(value)
        if not prop.resource or not match:
            at.error(f"Property {name!r} does not support this resource reference", "E_RESOURCE")
        if match[1] not in resources:
            at.error(f"Unknown resource {match[1]!r}", "E_REFERENCE")
        if resources[match[1]] != prop.kind:
            at.error(f"Resource {match[1]!r} has the wrong type for {name!r}", "E_TYPE")
        return Reference("resource", match[1])
    return literal(value, prop, at, name)


@dataclass(frozen=True)
class RegisteredNode:
    element: Element
    slot: str | None
    properties: dict


def validate_document(file, source, model=None):
    model = model if model is not None else Model()
    elements = parse_elements(file, source)
    identifiers, components, resources, slots = set(), {}, {}, []
    counts, definitions, owners = {}, [], []
    for index, node in enumerate(elements):
        at = node.origin
        if node.namespace != NAMESPACE:
            at.error(f"Unregistered namespace {node.namespace!r}", "E_NAMESPACE")
        spec = TYPES.get(node.tag)
        if not spec:
            at.error(f"Unregistered GXML type {node.tag!r}", "E_TYPE")
        if invalid := node.attributes.keys() - spec.properties.keys():
            at.error(f"Unsupported {node.tag} attribute {min(invalid)!r}", "E_PROPERTY")
        for name, prop in spec.properties.items():
            if prop.required and name != "id" and name not in node.attributes:
                at.error(f"Missing required {node.tag} attribute {name!r}", "E_PROPERTY")
        identifier = validate_identity(node, identifiers)
        owner = owners[node.parent] if node.parent is not None else None
        selected = None
        if node.parent is None:
            if spec.category != "root" or "slot" in node.attributes:
                at.error("Expected Window as root without a slot attribute", "E_SLOT")
        else:
            parent = elements[node.parent]
            selected = node.attributes.get("slot", "definitions" if spec.category == "definition" else "content")
            slot = TYPES[parent.tag].slots.get(selected)
            if not slot or slot.category != spec.category:
                at.error(f"{node.tag} is not allowed in {parent.tag} slot {selected!r}", "E_SLOT")
            key = node.parent, selected
            counts[key] = counts.get(key, 0) + 1
            if counts[key] > slot.maximum:
                at.error(f"{parent.tag} slot {selected!r} accepts at most {slot.maximum} children", "E_SLOT")
        if node.tag in ("Component", "Template"):
            owner = identifier
            components[identifier] = []
        if node.tag == "Resource":
            kind = node.attributes["type"]
            if kind not in RESOURCE_TYPES:
                at.error(f"Unregistered resource type {kind!r}", "E_RESOURCE")
            resources[identifier] = kind
            definitions.append(node)
        slots.append(selected)
        owners.append(owner)
        if node.tag == "Use" and owner is not None:
            components[owner].append((node.attributes["component"], at))

    for index, node in enumerate(elements):
        for name, slot in TYPES[node.tag].slots.items():
            if counts.get((index, name), 0) < slot.minimum:
                node.origin.error(f"{node.tag} slot {name!r} requires {slot.minimum} child", "E_SLOT")

    resource_edges, resource_values = {}, {}
    for node in definitions:
        name, kind, value = (node.attributes[key] for key in ("id", "type", "value"))
        resource_values[name] = property_value("value", value, Property(kind, resource=True), node.origin, model, resources)
        ref = resource_values[name]
        resource_edges[name] = [(ref.name, node.origin)] if isinstance(ref, Reference) else []
    check_cycles(resource_edges, "resource")
    check_cycles(components, "component")

    validated = []
    for node, slot in zip(elements, slots, strict=True):
        properties = {}
        specs = TYPES[node.tag].properties
        for name, prop in specs.items():
            if name in IDENTITY or (node.tag == "Resource" and name in ("type", "value")):
                continue
            value = node.attributes.get(name, prop.default)
            if value is not None:
                properties[name] = property_value(name, value, prop, node.origin, model, resources)
        if node.tag == "Resource":
            properties.update(type=node.attributes["type"], value=resource_values[node.attributes["id"]])
        if node.tag == "Use" and properties["component"] not in components:
            node.origin.error(f"Unknown component {properties['component']!r}", "E_REFERENCE")
        for name, prop in specs.items():
            if prop.writes_binding and model.commands[properties[name].name].writes != properties[prop.writes_binding].name:
                node.origin.error(f"TwoWay binding requires {name!r} to write the same field", "E_BINDING")
        validated.append(RegisteredNode(node, slot, properties))
    return validated
