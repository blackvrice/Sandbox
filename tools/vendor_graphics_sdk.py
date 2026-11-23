#!/usr/bin/env python3
"""external/graphics-sdk 갱신 (ADR-0169) — Graphics SDK 체크아웃의 한 커밋에서 코어만 복사한다.

    python tools/vendor_graphics_sdk.py --sdk D:/source/GraphicsSDK --commit <40자 해시>

`git archive <commit> include src cmake CMakeLists.txt LICENSE` 를 external/graphics-sdk 에 풀고 VENDORED_COMMIT 에
해시를 적는다. 체크아웃의 작업 트리 · 인덱스는 건드리지 않는다 (커밋 내용만 읽는다). 수정 없이 복사한다 — 고칠 것은
SDK 저장소에 고치고 다시 가져온다. 갱신은 단독 커밋으로, external/README.md 의 표와 ADR-0169 의 고정 커밋을 함께 고친다.
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

PATHS = ["include", "src", "cmake", "CMakeLists.txt", "LICENSE"]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sdk", required=True, type=pathlib.Path, help="Graphics SDK git 체크아웃")
    ap.add_argument("--commit", required=True, help="고정할 커밋 (40자 해시)")
    ap.add_argument("--root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    args = ap.parse_args()

    if not re.fullmatch(r"[0-9a-f]{40}", args.commit):
        print("커밋은 전체 40자 소문자 해시여야 한다 (짧은 해시 · 브랜치 이름 금지 — 재현성)", file=sys.stderr)
        return 2
    resolved = subprocess.run(["git", "-C", str(args.sdk), "rev-parse", "--verify", f"{args.commit}^{{commit}}"],
                              capture_output=True, text=True)
    if resolved.returncode != 0 or resolved.stdout.strip() != args.commit:
        print(f"{args.sdk} 에 커밋 {args.commit} 이 없다", file=sys.stderr)
        return 2
    archive = subprocess.run(["git", "-C", str(args.sdk), "archive", "--format=tar", args.commit, *PATHS],
                             capture_output=True, check=True)

    dest = args.root / "external" / "graphics-sdk"
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    with tarfile.open(fileobj=io.BytesIO(archive.stdout)) as tar:
        tar.extractall(dest, filter="data")
    (dest / "VENDORED_COMMIT").write_text(args.commit + "\n", encoding="ascii")
    count = sum(1 for p in dest.rglob("*") if p.is_file())
    print(f"external/graphics-sdk ← {args.commit} ({count} 파일)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
