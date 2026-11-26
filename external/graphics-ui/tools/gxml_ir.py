"""Serialize the normalized Core document as a versioned .guir file."""

from hashlib import sha256
from struct import pack

from gxml_schema import Diagnostic, Reference

MAGIC = b"GUIIR\r\n\x1a"
BYTECODE_MAGIC = b"GUIBC\r\n\x1a"
HYBRID_MAGIC = b"GUIHY\r\n\x1a"
FORMAT, SCHEMA = 1, 2
MAX_FILE = 8 * 1024 * 1024
HEADER_BYTES = 48
ABSENT = 65535

KINDS = ("window", "panel", "stack_panel", "grid", "flex_panel", "text_block", "text_box", "button")
STATES = ("none", "disabled", "focus", "hover", "pressed")
RELATIONS = ("none", "child", "descendant")
VALUE_TYPES = ("text", "bool", "integer", "number")
RESOURCE_TYPES = ("text", "color", "font")
PROPERTIES = ("width", "height", "gap", "font-size", "padding", "color", "background-color", "font-id")


def write_shape(node, put, string, names):
    string(node.id)
    put("HB", ABSENT if node.parent is None else node.parent, KINDS.index(node.kind))
    names(node.classes)
    names(node.aliases)
    put("BB", node.horizontal, node.columns)


def topology_hash(nodes):
    shapes = bytearray(pack("<H", len(nodes)))

    def put(layout, *values):
        shapes.extend(pack("<" + layout, *values))

    def string(value):
        raw = value.encode("utf-8")
        put("I", len(raw))
        shapes.extend(raw)

    def names(values):
        put("H", len(values))
        for value in values:
            string(value)

    for node in nodes:
        write_shape(node, put, string, names)
    return sha256(shapes).digest()


def encode(nodes, rules, model, resources, resource_spans, sources, *, mode="binary"):
    payload = bytearray()
    interned = {}
    fields = sorted(model.fields)
    commands = sorted(model.commands)
    resource_ids = sorted(resources)
    field_index = {name: index for index, name in enumerate(fields)}
    command_index = {name: index for index, name in enumerate(commands)}
    resource_index = {name: index for index, name in enumerate(resource_ids)}
    bom_sizes = {role: 3 if raw.startswith(b"\xef\xbb\xbf") else 0 for role, _, raw in sources}

    def room():
        if len(payload) > MAX_FILE - HEADER_BYTES:
            raise Diagnostic("E_LIMIT: Core IR exceeds 8 MiB")

    def put(layout, *values):
        payload.extend(pack("<" + layout, *values))

    def string(value):
        raw = value.encode("utf-8")
        if mode == "bytecode":
            if raw not in interned:
                if len(interned) == ABSENT:
                    raise Diagnostic("E_LIMIT: bytecode string table exceeds 65535 entries")
                interned[raw] = len(interned)
            put("H", interned[raw])
            return
        put("I", len(raw))
        payload.extend(raw)
        room()

    def names(values):
        put("H", len(values))
        for value in values:
            string(value)

    def span(value, role):
        start, end = value
        bom = bom_sizes[role]
        put("II", start + bom, end + bom)

    def color(value):
        payload.extend(bytes.fromhex(value[1:]))

    def opcode(value):
        if mode == "bytecode":
            put("B", value)

    def style(values):
        put("B", sum(1 << index for index, name in enumerate(PROPERTIES) if name in values))
        for index, name in enumerate(PROPERTIES):
            if name not in values:
                continue
            value = values[name]
            if index >= 5:
                if isinstance(value, Reference):
                    put("BH", 1, resource_index[value.name])
                    continue
                put("B", 0)
            if name == "padding":
                put("4H", *value)
            elif name in ("color", "background-color"):
                color(value)
            else:
                put("Q" if name == "font-id" else "H", value)

    put("BB", 0, len(sources))
    for role, name, raw in sources:
        if len(name.encode("utf-8")) > 1024 or any(ord(char) < 32 or char in "/\\:" for char in name):
            raise Diagnostic("E_IR: Source must have a portable file name of at most 1024 UTF-8 bytes")
        opcode(1)  # Source metadata
        put("B", role)
        string(name)
        put("I", len(raw))
        payload.extend(sha256(raw).digest())

    put("H", len(fields))
    for name in fields:
        opcode(2)  # Register field
        field = model.fields[name]
        string(name)
        put("BH", VALUE_TYPES.index(field.kind), ABSENT if field.dependencies is None else len(field.dependencies))
        for dependency in sorted(field.dependencies or ()):
            put("H", field_index[dependency])
    put("H", len(commands))
    for name in commands:
        opcode(3)  # Register command
        command = model.commands[name]
        string(name)
        put("BH", 255 if command.argument is None else VALUE_TYPES.index(command.argument),
            field_index.get(command.writes, ABSENT))

    put("H", len(resource_ids))
    for name in resource_ids:
        opcode(4)  # Define resource
        kind, value = resources[name]
        string(name)
        put("B", RESOURCE_TYPES.index(kind))
        span(resource_spans[name], 0)
        if kind == "text":
            string(value)
        elif kind == "color":
            color(value)
        else:
            put("Q", value)

    if mode == "hybrid":
        payload.extend(topology_hash(nodes))
    put("H", len(nodes))
    for node in nodes:
        opcode(5)  # Create node
        if mode != "hybrid":
            write_shape(node, put, string, names)
        style(node.style)
        if isinstance(node.text, Reference):
            binding = node.text.category == "binding"
            put("BH", 1 if binding else 2, (field_index if binding else resource_index)[node.text.name])
        else:
            put("B", 0)
            string(node.text)
        if isinstance(node.enabled, Reference):
            put("BH", 2, field_index[node.enabled.name])
        else:
            put("B", node.enabled)
        put("H", command_index.get(node.command, ABSENT))
        span(node.span, 0)
        put("H", len(node.use_spans))
        for source in node.use_spans:
            span(source, 0)
        room()

    put("H", len(rules))
    for rule in rules:
        opcode(6)  # Append cascade rule in source order
        put("B", len(rule.selector))
        for part in rule.selector:
            put("B", KINDS.index(part.kind) if part.kind else 255)
            string(part.id)
            names(part.classes)
            put("BBB", STATES.index(part.state), part.root, RELATIONS.index(part.relation))
        style(rule.style)
        span(rule.span, 1)

    opcode(0)
    magic = {"binary": MAGIC, "bytecode": BYTECODE_MAGIC, "hybrid": HYBRID_MAGIC}[mode]
    if mode == "bytecode":
        pool_size = 2 + sum(4 + len(raw) for raw in interned)
        if pool_size + len(payload) > MAX_FILE - HEADER_BYTES:
            raise Diagnostic("E_LIMIT: Core IR exceeds 8 MiB")
        pool = bytearray(pack("<H", len(interned)))
        for raw in interned:
            pool.extend(pack("<I", len(raw)))
            pool.extend(raw)
        pool.extend(payload)
        payload = pool
    room()
    return magic + pack("<HHI", FORMAT, SCHEMA, len(payload)) + sha256(payload).digest() + payload
