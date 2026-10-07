#!/usr/bin/env bash
# Linux 에서 Windows 용 dxc.exe(NuGet Microsoft.Direct3D.DXC)를 Wine 으로 돌린다 — MinGW 교차 빌드 시험용. ADR-0019.
#   cmake ... -D SBX_BUILD_SHADERS=ON -D SBX_DXC=<저장소>/tools/wine/dxc.sh
# DXC_EXE 로 dxc.exe 위치를 준다 (기본: <저장소>/.cache/dxc/<버전>/build/native/bin/x64/dxc.exe).
# dxc 는 '/' 로 시작하는 인자를 옵션으로 읽으므로 절대 경로를 Wine 의 Z: 드라이브 경로로 바꾼다.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
exe="${DXC_EXE:-$(ls "$repo"/.cache/dxc/*/build/native/bin/x64/dxc.exe 2>/dev/null | tail -1)}"
if [[ -z "$exe" || ! -f "$exe" ]]; then
    echo "dxc.sh: dxc.exe 를 찾지 못했습니다 (DXC_EXE 또는 .cache/dxc/ — NuGet 패키지를 풀어 두십시오)" >&2
    exit 2
fi
args=()
for a in "$@"; do
    if [[ "$a" == /* && -e "$(dirname "$a")" ]]; then
        args+=("Z:${a//\//\\}")
    else
        args+=("$a")
    fi
done
export WINEDEBUG="${WINEDEBUG:--all}" LANG="${LANG:-C.UTF-8}"
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-sbx}"
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-mscoree,mshtml=}"
wine="${WINE:-$(command -v wine64 || echo /usr/lib/wine/wine64)}"
exec "$wine" "$exe" "${args[@]}"
