#!/usr/bin/env python3
"""include 경계 린터. docs/01-ARCHITECTURE.md 2.2, AGENTS.md 4장.

검사하는 것
  1. 모듈 방향: 각 최상위 모듈은 허용된 모듈의 헤더만 "module/..." 형식으로 include 한다.
  2. 금지 헤더: 그래픽 API·OS 창·ImGui·오디오 헤더가 금지된 위치에 나타나지 않는다.
  3. 전역 금지: SFML, OpenGL (어디서도 쓰지 않는다).
  4. Objective-C #import 는 .mm 에서만.

사용
  check_includes.py --root <저장소 루트>              위반 0건이면 종료 코드 0
  check_includes.py --root <픽스처> --expect N        정확히 N건이면 0 (린터 자체 시험)
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

SOURCE_SUFFIXES = {".hpp", ".h", ".cpp", ".cc", ".inl", ".ipp", ".mm"}
SKIP_DIRS = {"external", "build", "out", "docs", ".git", "fixtures", "cmake-build-debug", "cmake-build-release"}

ALL = {"foundation", "core", "network", "platform", "render", "editor", "apps", "tests", "bench", "tools"}

# 모듈 → include 해도 되는 모듈 (docs/01-ARCHITECTURE.md 2장 의존 방향)
ALLOWED_MODULES = {
    "foundation": {"foundation"},
    "core": {"foundation", "core"},
    "network": {"foundation", "core", "network"},
    "platform": {"foundation", "platform"},
    "render": {"foundation", "platform", "render"},
    "editor": {"foundation", "core", "platform", "render", "editor"},
    "apps": ALL,
    "tests": ALL,
    "bench": ALL,
    "tools": ALL,
}

# 그래픽 API · OS 창 · UI · 오디오 · 전송 헤더 (꺾쇠 include 기준, 소문자 비교)
PLATFORM_HEADERS = [
    r"d3d12", r"d3dx12", r"dxgi", r"d3dcompiler", r"dxcapi", r"pix3", r"vulkan/", r"vk_mem_alloc", r"volk\.h",
    r"metal/", r"quartzcore/", r"cocoa/", r"appkit/", r"foundation/foundation\.h",
    r"windows\.h", r"x11/", r"xcb/", r"wayland-", r"xkbcommon",
    r"imgui", r"miniaudio", r"enet/",
]
GLOBALLY_BANNED = [r"sfml/", r"gl/", r"opengl/", r"glad/", r"glfw"]

# (경로 접두사, 그 아래에서 허용되는 금지 헤더 패턴). 가장 긴 접두사가 이긴다.
# 접두사가 매칭되지 않으면 PLATFORM_HEADERS 전부 허용 (예: apps/, tests/).
RESTRICTED = [
    ("foundation/", set()),
    ("core/", set()),
    ("network/", set()),
    ("network/transport/", {r"enet/"}),
    ("render/rhi/", set()),
    ("render/renderer/", set()),
    ("render/asset/", set()),
    ("render/shader/", set()),
    ("editor/", {r"imgui"}),
    ("platform/common/", set()),
]

INCLUDE_RE = re.compile(r'^\s*#\s*(include|import)\s*([<"])([^>"]+)[>"]')


def restriction_for(rel: str):
    best = None
    for prefix, allowed in RESTRICTED:
        if rel.startswith(prefix) and (best is None or len(prefix) > len(best[0])):
            best = (prefix, allowed)
    return best


def check_file(path: Path, rel: str) -> list[str]:
    problems: list[str] = []
    module = rel.split("/", 1)[0]
    allowed_modules = ALLOWED_MODULES.get(module)
    restriction = restriction_for(rel)

    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return [f"{rel}:1: encoding — UTF-8 이 아닙니다"]

    for lineno, line in enumerate(text.splitlines(), start=1):
        m = INCLUDE_RE.match(line)
        if not m:
            continue
        directive, delim, target = m.group(1), m.group(2), m.group(3)
        lower = target.lower()
        where = f"{rel}:{lineno}"

        if directive == "import" and path.suffix != ".mm":
            problems.append(f"{where}: objc-import — #import 는 .mm 에서만 허용 ({target})")

        if any(re.search(p, lower) for p in GLOBALLY_BANNED):
            problems.append(f"{where}: banned-global — SFML/OpenGL 은 사용하지 않습니다 ({target})")
            continue

        # 모듈 방향: "module/..." 형식의 프로젝트 include
        head = target.split("/", 1)[0]
        if delim == '"' and head in ALL and allowed_modules is not None and head not in allowed_modules:
            problems.append(f"{where}: module-direction — '{module}' 는 '{head}' 를 include 할 수 없습니다 ({target})")
            continue

        # 금지 헤더
        if restriction is not None:
            prefix, allowed = restriction
            for p in PLATFORM_HEADERS:
                if re.search(p, lower) and p not in allowed:
                    # 프로젝트 자신의 "foundation/..." 경로가 Apple 의 <Foundation/Foundation.h> 패턴에 걸리지 않게
                    if delim == '"' and head in ALL:
                        break
                    problems.append(f"{where}: banned-header — '{prefix}' 아래에서 금지된 헤더 ({target})")
                    break
    return problems


def main() -> int:
    # Windows 콘솔·CTest 의 기본 인코딩(cp949 등)은 메시지의 '—' 같은 문자를 못 쓴다 → UnicodeEncodeError 로 죽는다.
    # 실행 옵션(-X utf8)에 기대지 않고 출력 인코딩을 직접 UTF-8 로 고정한다.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", required=True, type=Path)
    ap.add_argument("--expect", type=int, default=None, help="정확히 이 수의 위반을 기대 (자체 시험용)")
    args = ap.parse_args()

    root: Path = args.root.resolve()
    if not root.is_dir():
        print(f"root 가 폴더가 아닙니다: {root}", file=sys.stderr)
        return 2

    problems: list[str] = []
    scanned = 0
    for path in sorted(root.rglob("*")):  # 정렬: 출력 순서를 플랫폼 독립으로
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        rel_parts = path.relative_to(root).parts
        if any(part in SKIP_DIRS for part in rel_parts[:-1]):
            continue
        rel = "/".join(rel_parts)
        if rel_parts[0] not in ALL:
            continue
        scanned += 1
        problems.extend(check_file(path, rel))

    for p in problems:
        print(p)
    print(f"check_includes: {scanned} files, {len(problems)} violation(s)")

    if args.expect is not None:
        if len(problems) != args.expect:
            print(f"selftest FAILED: expected {args.expect}, found {len(problems)}", file=sys.stderr)
            return 1
        return 0
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
