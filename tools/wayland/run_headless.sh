#!/bin/sh
# 화면 없는 Wayland 컴포지터(weston headless) 위에서 명령 하나를 돌린다 — CTest platform_wayland · client_wayland_world_smoke,
# CI. docs/13-TESTING.md · 15-BUILD-AND-RUN.md, ADR-0034.
#   tools/wayland/run_headless.sh <명령> [인자...]
# 자기만의 XDG_RUNTIME_DIR · 소켓을 만들어 다른 세션과 섞이지 않는다. 명령의 종료 코드를 그대로 돌려준다.
set -u
if ! command -v weston >/dev/null 2>&1; then
    echo "run_headless: weston 이 없습니다 (apt install weston)" >&2
    exit 1
fi
runtime=$(mktemp -d "${TMPDIR:-/tmp}/sbx-wayland.XXXXXX") || exit 1
chmod 700 "$runtime"
socket=sbx-wayland-0
XDG_RUNTIME_DIR=$runtime weston --backend=headless --renderer=pixman --socket=$socket --width=1280 --height=800 \
    --idle-time=0 >"$runtime/weston.log" 2>&1 &
weston_pid=$!
cleanup() {
    kill "$weston_pid" 2>/dev/null
    wait "$weston_pid" 2>/dev/null
    rm -rf "$runtime"
}
trap cleanup EXIT INT TERM
# 소켓이 생길 때까지 (최대 10 초)
i=0
while [ ! -S "$runtime/$socket" ]; do
    i=$((i + 1))
    if [ "$i" -gt 100 ] || ! kill -0 "$weston_pid" 2>/dev/null; then
        echo "run_headless: weston 이 뜨지 않았습니다" >&2
        cat "$runtime/weston.log" >&2
        exit 1
    fi
    sleep 0.1
done
XDG_RUNTIME_DIR=$runtime WAYLAND_DISPLAY=$socket "$@"
status=$?
exit $status
