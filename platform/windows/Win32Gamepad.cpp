// XInput 게임패드 (docs/07-PLATFORM.md I5 — v0.7 NG-06 (4), ADR-0107).
// - DLL 은 실행 중에 연다 (xinput1_4 → xinput1_3 → xinput9_1_0): 링크에 묶이지 않고, 없으면 패드 없음으로.
// - 연결되지 않은 칸을 매 프레임 물으면 XInputGetState 가 느리다 (장치를 찾느라 수 ms) — 끊긴 칸은 120 번 물을 때마다
// 한 번만.
// - 창 포커스와 무관하게 상태를 준다 — 포커스 규칙 (I3) 은 InputSystem 이 지킨다.
#include "platform/windows/Win32Common.hpp"

#include <Xinput.h>

#include <array>

#include "foundation/log/Log.hpp"
#include "platform/common/Gamepad.hpp"
#include "platform/windows/XInputMap.hpp"

namespace sbx::platform {
namespace {

using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

class XInputGamepadSource final : public IGamepadSource {
public:
    XInputGamepadSource() {
        for (const wchar_t* name : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
            m_dll = LoadLibraryW(name);
            if (m_dll != nullptr) {
                m_getState =
                    reinterpret_cast<GetStateFn>(reinterpret_cast<void*>(GetProcAddress(m_dll, "XInputGetState")));
                if (m_getState != nullptr) {
                    log::info("platform", "게임패드: XInput ({})", win32::narrow(name));
                    return;
                }
                FreeLibrary(m_dll);
                m_dll = nullptr;
            }
        }
        log::info("platform", "게임패드: XInput DLL 이 없다 — 패드 없음");
    }
    ~XInputGamepadSource() override {
        if (m_dll != nullptr) {
            FreeLibrary(m_dll);
        }
    }
    XInputGamepadSource(const XInputGamepadSource&) = delete;
    XInputGamepadSource& operator=(const XInputGamepadSource&) = delete;

    void poll(std::span<GamepadState, kMaxGamepads> out) override {
        ++m_polls;
        for (usize i = 0; i < kMaxGamepads; ++i) {
            out[i] = GamepadState{};
            if (m_getState == nullptr ||
                (!m_connected[i] && m_polls - m_lastTry[i] < kRetryEvery && m_lastTry[i] != 0)) {
                continue;
            }
            m_lastTry[i] = m_polls;
            XINPUT_STATE st{};
            const bool ok = m_getState(static_cast<DWORD>(i), &st) == ERROR_SUCCESS;
            if (ok != m_connected[i]) {
                log::info("platform", "게임패드 {} {}", i, ok ? "연결" : "끊김");
            }
            m_connected[i] = ok;
            if (ok) {
                const XINPUT_GAMEPAD& g = st.Gamepad;
                out[i] = gamepadFromXInput(
                    {g.wButtons, g.bLeftTrigger, g.bRightTrigger, g.sThumbLX, g.sThumbLY, g.sThumbRX, g.sThumbRY});
            }
        }
    }

private:
    static constexpr u64 kRetryEvery = 120; // 끊긴 칸을 다시 묻는 간격 (poll 수 — 60 Hz 면 2 초)
    HMODULE m_dll = nullptr;
    GetStateFn m_getState = nullptr;
    u64 m_polls = 0;
    std::array<bool, kMaxGamepads> m_connected{};
    std::array<u64, kMaxGamepads> m_lastTry{};
};

} // namespace

std::unique_ptr<IGamepadSource> createGamepadSource() {
    return std::make_unique<XInputGamepadSource>();
}

} // namespace sbx::platform
