#!/usr/bin/env python3
"""GXML 화면의 테마 변형 하나를 C++ 헤더로 (UI-R2-03, ADR-0176) — cmake/SbxGxml.cmake 의 sbx_gxml_core(… THEMES) 가 부른다.

    python tools/gxml_theme_variant.py --gxmlc <gxmlc.py> --markup a.gxml --prelude tokens-dark.gcss --style a.gcss
                                       [--model a.model.json] --symbol studio_settings_dark --output a_dark.hpp

prelude (gen_studio_tokens.py --gcss <테마> — :root 의 토큰 변수) 뒤에 화면 GCSS (var(--토큰) 만 쓴다) 를 붙인 GCSS 로 gxmlc
--compile-core 를 부르고, 생성 함수 이름 document_<루트 id> 를 document_<symbol> 로 바꾼다 (같은 화면의 테마 변형이 한 실행 파일에 함께
들어간다). 결과가 같으면 파일을 다시 쓰지 않는다 (불필요한 다시 컴파일 없음).
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for name in ("gxmlc", "markup", "prelude", "style", "output"):
        ap.add_argument(f"--{name}", type=pathlib.Path, required=True)
    ap.add_argument("--model", type=pathlib.Path)
    ap.add_argument("--symbol", required=True)
    args = ap.parse_args()
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", args.symbol):
        print(f"--symbol 은 C++ 이름이어야 한다: {args.symbol}", file=sys.stderr)
        return 2

    work = args.output.with_suffix(".work")
    work.mkdir(parents=True, exist_ok=True)
    # gxmlc 는 GCSS 파일 이름을 진단에 쓴다 — 화면 GCSS 의 이름을 그대로 둔다
    merged = work / args.style.name
    merged.write_text(args.prelude.read_text(encoding="utf-8") + "\n" + args.style.read_text(encoding="utf-8"),
                      encoding="utf-8", newline="\n")
    raw = work / "document.hpp"
    cmd = [sys.executable, "-X", "utf8", str(args.gxmlc), "--compile-core", "--markup", str(args.markup), "--style",
           str(merged), "--output", str(raw)]
    if args.model:
        cmd += ["--model", str(args.model)]
    run = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8")
    if run.returncode != 0:
        sys.stderr.write(run.stdout + run.stderr)
        return run.returncode
    text = raw.read_text(encoding="utf-8")
    renamed, n = re.subn(r"inline core::Document document_[A-Za-z0-9_]+\(\)",
                         f"inline core::Document document_{args.symbol}()", text, count=1)
    if n != 1:
        print("gxmlc 출력에 'inline core::Document document_…()' 가 없다 — 생성 형식이 바뀌었다", file=sys.stderr)
        return 1
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != renamed:
        args.output.write_text(renamed, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
