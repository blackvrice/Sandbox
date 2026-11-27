#!/usr/bin/env python3
"""Studio 디자인 토큰 생성기 (UI-R2-03, ADR-0176) — 정본 editor/services/studio_tokens.json 에서

    python tools/gen_studio_tokens.py --root .              editor/services/StudioTokens.cpp 를 다시 쓴다 (커밋한다)
    python tools/gen_studio_tokens.py --root . --check      커밋한 StudioTokens.cpp 가 JSON 과 같은지 (CTest studio_tokens_sync)
    python tools/gen_studio_tokens.py --root . --gcss dark --output <파일.gcss>
                                                            GXML 화면용 GCSS 변수 블록 (:root { --background: … }) — 빌드 때

C++ 표를 빌드 때 만들지 않는 까닭: ImGui 테마 (editor/ui) 는 Python 없이도 빌드되어야 한다 (GXML 은 Python 이 있을 때만 — ADR-0175).
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys

CPP = "editor/services/StudioTokens.cpp"
JSON = "editor/services/studio_tokens.json"
THEME_ENUM = {"dark": "Dark", "light": "Light", "high-contrast": "HighContrast"}


def load(root: pathlib.Path) -> dict:
    data = json.loads((root / JSON).read_text(encoding="utf-8"))
    names = data["tokens"]
    for theme, values in data["themes"].items():
        if theme not in THEME_ENUM:
            raise SystemExit(f"{JSON}: 모르는 테마 '{theme}'")
        if sorted(values) != sorted(names):
            raise SystemExit(f"{JSON}: '{theme}' 의 토큰이 목록과 다르다")
        for k, v in values.items():
            if not re.fullmatch(r"#[0-9A-Fa-f]{6}", v):
                raise SystemExit(f"{JSON}: {theme}.{k} = '{v}' — #RRGGBB 가 아니다")
    return data


def kebab(name: str) -> str:
    return re.sub(r"([A-Z])", lambda m: "-" + m.group(1).lower(), name)


def rgba(hex_: str) -> str:
    r, g, b = (int(hex_[i:i + 2], 16) for i in (1, 3, 5))
    return f"{{0x{r:02X}, 0x{g:02X}, 0x{b:02X}, 0xFF}}"


def cpp(data: dict) -> str:
    out = [
        "// 생성 파일 — tools/gen_studio_tokens.py 가 editor/services/studio_tokens.json 에서 만든다. 손으로 고치지 않는다",
        "// (CTest studio_tokens_sync 가 어긋남을 잡는다 — UI-R2-03, ADR-0176).",
        '#include "editor/services/StudioTokens.hpp"',
        "",
        "namespace sbx::editor::services {",
        "",
        "namespace {",
        "",
    ]
    for theme, enum in THEME_ENUM.items():
        values = data["themes"][theme]
        out.append(f"constexpr StudioColors k{enum}{{")
        for name in data["tokens"]:
            out.append(f"    {rgba(values[name])}, // {name} {values[name]}")
        out.append("};")
        out.append("")
    out += [
        "} // namespace",
        "",
        "const StudioColors& studioColors(StudioTheme theme) noexcept {",
        "    switch (theme) {",
        "    case StudioTheme::Light:",
        "        return kLight;",
        "    case StudioTheme::HighContrast:",
        "        return kHighContrast;",
        "    case StudioTheme::Dark:",
        "        break;",
        "    }",
        "    return kDark;",
        "}",
        "",
        "} // namespace sbx::editor::services",
        "",
    ]
    return "\n".join(out)


def gcss(data: dict, theme: str) -> str:
    # GCSS 규칙 하나는 선언 16 개까지 (GraphicsUI gcss_syntax) — :root 블록을 나눠 쓴다 (변수는 :root 끼리 합쳐진다)
    values = data["themes"][theme]
    names = data["tokens"]
    lines = [f"/* 생성 — tools/gen_studio_tokens.py ({theme}, {JSON}) */"]
    for i in range(0, len(names), 10):
        lines.append(":root {")
        lines += [f"  --{kebab(n)}: {values[n]};" for n in names[i:i + 10]]
        lines.append("}")
    return "\n".join(lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--gcss", choices=sorted(THEME_ENUM))
    ap.add_argument("--output", type=pathlib.Path)
    args = ap.parse_args()
    data = load(args.root)
    if args.gcss:
        if not args.output:
            ap.error("--gcss 는 --output 이 필요하다")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        text = gcss(data, args.gcss)
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != text:
            args.output.write_text(text, encoding="utf-8", newline="\n")
        return 0
    want = cpp(data)
    path = args.root / CPP
    if args.check:
        have = path.read_text(encoding="utf-8").replace("\r\n", "\n") if path.exists() else ""
        if have != want:
            print(f"{CPP} 가 {JSON} 과 다르다 — python tools/gen_studio_tokens.py --root . 로 다시 만든다", file=sys.stderr)
            return 1
        print("studio tokens: 같다")
        return 0
    path.write_text(want, encoding="utf-8", newline="\n")
    print(f"{CPP} ← {JSON}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
