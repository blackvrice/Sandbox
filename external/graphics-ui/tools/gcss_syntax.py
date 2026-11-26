"""Bounded GCSS token scanning shared by the Settings and Core front ends."""

from bisect import bisect_right
import re

from gxml_schema import MAX_BYTES, Origin

COMMENT = r"/\*[^*]*(?:\*(?!/)[^*]*)*\*/"
SPACING = re.compile(rf"(?:[ \t\r\n\f]+|{COMMENT})*")
DECLARATION = re.compile(r"([a-z-][a-z0-9-]*)[ \t]*:[ \t]*([^;{}\r\n]+);")
NEWLINE = re.compile(r"\r\n|\r|\n")


def rules(file, source, selector, *, detailed=False):
    code = "E_GCSS" if detailed else None
    if len(source.encode("utf-8")) > MAX_BYTES:
        Origin(file, 1, 1 if detailed else None).error("GCSS source exceeds 64 KiB", code)
    cursor, count = 0, 0
    lines = [0, *(match.end() for match in NEWLINE.finditer(source))]

    def at():
        line = bisect_right(lines, cursor) - 1
        column = cursor - lines[line] + 1 if detailed else None
        return Origin(file, line + 1, column)

    while True:
        cursor = SPACING.match(source, cursor).end()
        if cursor == len(source):
            return
        match = selector.match(source, cursor)
        if not match or count == 128:
            at().error("Expected a selector (limit 128 rules)", code)
        start = cursor
        name, origin = match[0], at()
        cursor = SPACING.match(source, match.end()).end()
        if source[cursor:cursor + 1] != "{":
            at().error("Expected a GCSS declaration block", code)
        cursor += 1
        declarations = []
        while True:
            cursor = SPACING.match(source, cursor).end()
            if source[cursor:cursor + 1] == "}":
                cursor += 1
                break
            property_match = DECLARATION.match(source, cursor)
            if not property_match or len(declarations) == 16:
                at().error("Expected a property and ';' (limit 16 declarations per rule)", code)
            declarations.append((property_match[1], property_match[2].strip(), at()))
            cursor = property_match.end()
        if not declarations:
            at().error("Empty GCSS declaration block", code)
        count += 1
        if detailed:
            yield name, origin, declarations, (start, cursor)
        else:
            yield name, origin, declarations
