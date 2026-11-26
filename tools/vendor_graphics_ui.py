#!/usr/bin/env python3
"""external/graphics-ui 갱신 (ADR-0174) — GraphicsUI 체크아웃의 한 커밋에서 GraphicsUI::Core 만 복사한다.

    python tools/vendor_graphics_ui.py --ui D:/source/GraphicsUI --commit <40자 해시>

`git archive <commit>` 로 Core 의 공개 헤더 (include/graphics-ui/core) · 구현 (src/core) · 버전 헤더 틀
(cmake/GraphicsUIVersion.hpp.in) · LICENSE · Unicode 데이터 라이선스 · GXML 컴파일러의 Core 모드 6 파일 (tools/ — ADR-0175) 을 external/graphics-ui 에 풀고, 그 커밋의 CMakeLists.txt
에서 GraphicsUICore 의 소스 목록과 project VERSION 을 읽어 CORE_SOURCES.txt · VERSION 에 적는다. Sandbox 는 GraphicsUI 의
최상위 CMakeLists 를 add_subdirectory 하지 않는다 (연구 · 예제 · Python 도구 · SDK 스냅숏 검사가 함께 붙는다) —
cmake/SbxGraphicsUi.cmake 가 이 목록으로 같은 GraphicsUICore 타깃을 만든다.

체크아웃의 작업 트리 · 인덱스는 건드리지 않는다 (커밋 내용만 읽는다 — 사용자가 고치는 중인 파일과 무관). 수정 없이 복사한다 —
고칠 것은 GraphicsUI 저장소에 고치고 다시 가져온다 (docs/graphics-ui-refactor/UPSTREAM_REQUIREMENTS.md). 갱신은 단독 커밋으로,
external/README.md 의 표 · ADR-0174 · docs/graphics-ui-refactor/DEPENDENCY_CONTRACT.md 의 고정 커밋을 함께 고친다.
"""

from __future__ import annotations

import argparse
import io
import pathlib
import re
import shutil
import subprocess
import sys
import tarfile

PATHS = [
    "include/graphics-ui/core",
    "src/core",
    "cmake/GraphicsUIVersion.hpp.in",
    "LICENSE",
    "third_party/unicode/17.0.0/license.txt",
    # GXML · GCSS 컴파일러의 Core 모드 (--compile-core · --compile-ir) — 순수 Python 3.11 + 표준 expat (ADR-0175).
    # 이 목록은 그 커밋 gxmlc.py 의 compiler_inputs 와 같아야 한다 (아래에서 검사)
    "tools/gxmlc.py",
    "tools/gxml_schema.py",
    "tools/gxml_core.py",
    "tools/gcss_syntax.py",
    "tools/gcss_core.py",
    "tools/gxml_ir.py",
]
GXML_TOOLS = [p.removeprefix("tools/") for p in PATHS if p.startswith("tools/")]


def git(repo: pathlib.Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", "-C", str(repo), *args], capture_output=True, check=True)


def core_sources(cmake_text: str) -> list[str]:
    """그 커밋의 add_library(GraphicsUICore STATIC ...) 소스 목록 — 다르면 Core 의 빌드 정의가 바뀐 것."""
    m = re.search(r"add_library\(\s*GraphicsUICore\s+STATIC\s+([^)]*)\)", cmake_text)
    if not m:
        raise SystemExit("CMakeLists.txt 에 add_library(GraphicsUICore STATIC ...) 가 없다 — Core 빌드 정의가 바뀌었다")
    return m.group(1).split()


def project_version(cmake_text: str) -> str:
    m = re.search(r"project\(\s*GraphicsUI\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", cmake_text)
    if not m:
        raise SystemExit("CMakeLists.txt 에 project(GraphicsUI VERSION x.y.z) 가 없다")
    return m.group(1)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ui", required=True, type=pathlib.Path, help="GraphicsUI git 체크아웃")
    ap.add_argument("--commit", required=True, help="고정할 커밋 (40자 해시)")
    ap.add_argument("--root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    args = ap.parse_args()

    if not re.fullmatch(r"[0-9a-f]{40}", args.commit):
        print("커밋은 전체 40자 소문자 해시여야 한다 (짧은 해시 · 브랜치 이름 금지 — 재현성)", file=sys.stderr)
        return 2
    resolved = subprocess.run(["git", "-C", str(args.ui), "rev-parse", "--verify", f"{args.commit}^{{commit}}"],
                              capture_output=True, text=True)
    if resolved.returncode != 0 or resolved.stdout.strip() != args.commit:
        print(f"{args.ui} 에 커밋 {args.commit} 이 없다", file=sys.stderr)
        return 2

    cmake_text = git(args.ui, "show", f"{args.commit}:CMakeLists.txt").stdout.decode("utf-8")
    gxmlc = git(args.ui, "show", f"{args.commit}:tools/gxmlc.py").stdout.decode("utf-8")
    m = re.search(r"compiler_inputs = \(([^)]*)\)", gxmlc)
    if not m or sorted(re.findall(r'"([^"]+)"', m.group(1))) != sorted(GXML_TOOLS):
        print("gxmlc.py 의 compiler_inputs 가 바뀌었다 — PATHS 의 tools 목록을 맞춘다", file=sys.stderr)
        return 1
    sources = core_sources(cmake_text)
    version = project_version(cmake_text)
    archive = git(args.ui, "archive", "--format=tar", args.commit, *PATHS).stdout

    dest = args.root / "external" / "graphics-ui"
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        tar.extractall(dest, filter="data")

    missing = [s for s in sources if not (dest / s).is_file()]
    if missing:
        print(f"Core 소스가 복사본에 없다: {missing}", file=sys.stderr)
        return 1
    # Core 의 비공개 헤더 (src/core/*.hpp · *.inc) 는 구현 안에서만 쓴다 — Sandbox 는 include/graphics-ui/core 만 본다
    (dest / "CORE_SOURCES.txt").write_text("\n".join(sources) + "\n", encoding="ascii", newline="\n")
    (dest / "VERSION").write_text(version + "\n", encoding="ascii", newline="\n")
    (dest / "VENDORED_COMMIT").write_text(args.commit + "\n", encoding="ascii", newline="\n")
    count = sum(1 for p in dest.rglob("*") if p.is_file())
    print(f"external/graphics-ui ← {args.commit} (GraphicsUI {version}, Core 소스 {len(sources)} · {count} 파일)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
