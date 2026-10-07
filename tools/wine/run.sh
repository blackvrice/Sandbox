#!/usr/bin/env bash
# Linux 에서 MinGW 로 교차 빌드한 Windows 실행 파일을 Wine + vkd3d(D3D12) + lavapipe 로 돌린다. ADR-0018.
#
# 준비 (Ubuntu 24.04):  apt install wine64 mesa-vulkan-drivers libvulkan1 libvulkan-dev xvfb
#                       gcc -shared -fPIC -O2 -o tools/wine/libsbx_subpixel.so tools/wine/sbx_subpixel_layer.c
# 사용:                 xvfb-run -a tools/wine/run.sh <빌드>/bin/sbx_render_tests.exe --fl11
#                       xvfb-run -a tools/wine/run.sh <빌드>/bin/SandboxClient.exe --rhi-fl11 --frames 300
#
# Phase 7B 그리기(DXIL 셰이더)에는 DXIL 을 받는 vkd3d 가 필요하다. Ubuntu 24.04 의 wine 9.0(vkd3d 1.10)은 PSO 생성에서
# "Failed to compile shader" 로 거절한다 → WineHQ 의 wine-devel(11.x 에서 확인) 을 WINE=<경로>/bin/wine 으로 준다.
# 패키지를 설치하지 않고 dpkg -x 로 풀어 써도 된다 (ADR-0019, 15-BUILD).
#
# 환경 변수: WINE (wine 실행 파일), WINEPREFIX (기본 ~/.wine-sbx — Wine 버전마다 따로 두십시오),
#            MINGW_DLL_DIRS (libstdc++ · winpthread DLL 폴더, ';' 구분)
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ ! -f "$here/libsbx_subpixel.so" ]]; then
    echo "먼저 레이어를 빌드하십시오: gcc -shared -fPIC -O2 -o $here/libsbx_subpixel.so $here/sbx_subpixel_layer.c" >&2
    exit 2
fi
export LANG="${LANG:-C.UTF-8}" LC_ALL="${LC_ALL:-C.UTF-8}" # 한글 경로 (C 로케일이면 Wine 이 유니코드 파일 이름을 못 만든다)
export WINEDEBUG="${WINEDEBUG:--all}"
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-mscoree,mshtml=}" # 새 접두사에서 Mono · Gecko 설치 창을 띄우지 않는다
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-sbx}"
export WINEPATH="${MINGW_DLL_DIRS:-/usr/lib/gcc/x86_64-w64-mingw32/13-posix;/usr/x86_64-w64-mingw32/lib}"
# lavapipe 는 viewportSubPixelBits 가 0 이라 vkd3d 가 FL 11_0 도 거절한다 → 시험 전용 레이어가 8 로 올린다
export VK_LAYER_PATH="$here"
export VK_INSTANCE_LAYERS=VK_LAYER_SBX_subpixel
wine="${WINE:-$(command -v wine64 || echo /usr/lib/wine/wine64)}"
exec "$wine" "$@"
