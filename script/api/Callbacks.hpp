#pragma once
// 시스템 콜백 표 · Capability (v0.5 M2-T7 — Runtime v2 계약 동결, ADR-0086 6, docs/22-RUNTIME-V2.md 3장).
//
// defineSystem 의 콜백 이름 하나하나를 (도메인, 상태, 허락 Capability) 로 한 곳에 적는다. 지금 쓸 수 있는 콜백 (Active)
// 과 나중 버전에 생길 이름 (Planned — 예약: 지금 쓰면 검사기 S426 "[계획 vX]") 을 같이 두어, 이름이 생길 때 기존
// 스크립트의 뜻이 바뀌지 않게 한다. Capability 는 Host 표의 열 (HostFunction::capability) 과 같은 이름 — 콜백 단계마다
// 검사는 [계획 v0.6] (호출 토큰).

#include <array>
#include <string_view>

#include "foundation/types/Types.hpp"

namespace sbx::script::api {

// Host 함수 · 콜백의 권한 묶음 (22 3.2). ★ 값은 Host API 지문에 든다 — 바꾸지 않고 끝에 더한다
enum class HostCapability : u8 {
    EcsRead,        // 컴포넌트 · 질의 읽기
    EcsWrite,       // 컴포넌트 쓰기 (호출 끝 커밋)
    Structural,     // spawn · destroy · add · remove · setOwner
    Random,         // 결정적 난수
    Time,           // 틱 · 시간 · 프레임
    Log,            // 로그
    Display,        // 클라이언트 표시 (label · print)
    PhysicsCommand, // [계획 v0.6] 힘 · 충격 · 순간 이동
    InputRead,      // [계획 v0.5 M2-T6] 행동 · 축
    ClientSend,     // [계획 v0.5 M2-T6] 신호 보내기
    Ui,             // [계획 v0.5 M3-T4] UI 값
    Draw,           // [계획 v0.6] onRender 의 그리기 명령
    Sound,          // [계획 v0.5 M2-T6] 소리 이벤트
    NetworkIo,      // [계획 v0.8] 외부 요청 (서버만 · 허락 목록)
    WorkerTask,     // [검토 v0.9] 순수 함수 작업
};
inline constexpr u32 kCapabilityCount = 15;

[[nodiscard]] constexpr std::string_view capabilityName(HostCapability c) noexcept {
    constexpr std::array<std::string_view, kCapabilityCount> kNames{
        "EcsRead",   "EcsWrite",   "Structural", "Random", "Time",  "Log",       "Display",   "PhysicsCommand",
        "InputRead", "ClientSend", "Ui",         "Draw",   "Sound", "NetworkIo", "WorkerTask"};
    return kNames[static_cast<u32>(c)];
}

[[nodiscard]] constexpr u32 capBit(HostCapability c) noexcept {
    return 1u << static_cast<u32>(c);
}

enum class CallbackStatus : u8 { Active, Planned };

struct CallbackInfo {
    std::string_view name; // defineSystem 의 메서드 이름
    bool client;           // false = server 시스템, true = domain: "client"
    CallbackStatus status;
    std::string_view since; // Active = 생긴 버전, Planned = 예정 버전
    u32 capabilities;       // 허락 Capability (capBit 의 합)
    std::string_view doc;
};

namespace detail {
inline constexpr u32 kServerAll = capBit(HostCapability::EcsRead) | capBit(HostCapability::EcsWrite) |
                                  capBit(HostCapability::Structural) | capBit(HostCapability::Random) |
                                  capBit(HostCapability::Time) | capBit(HostCapability::Log);
inline constexpr u32 kClientDisplay =
    capBit(HostCapability::EcsRead) | capBit(HostCapability::Time) | capBit(HostCapability::Display);
} // namespace detail

// 이름 · 도메인 순. 같은 이름이 도메인마다 따로 있다 (onStart · onStop · onEvent · onTick)
inline constexpr std::array kCallbacks{
    CallbackInfo{"onEvent", false, CallbackStatus::Active, "v0.3", detail::kServerAll, "단계 17d — 구독한 이벤트"},
    CallbackInfo{"onPrePhysics", false, CallbackStatus::Planned, "v0.6",
                 capBit(HostCapability::EcsRead) | capBit(HostCapability::PhysicsCommand) |
                     capBit(HostCapability::Random) | capBit(HostCapability::Time) | capBit(HostCapability::Log),
                 "새 단계 8b — Movement 앞 (구조 변경 없음)"},
    CallbackInfo{"onStart", false, CallbackStatus::Active, "v0.3", detail::kServerAll, "단계 17d — 게임 시작 틱"},
    CallbackInfo{"onStop", false, CallbackStatus::Active, "v0.3", detail::kServerAll, "단계 17d — 게임 끝 틱"},
    CallbackInfo{"onTick", false, CallbackStatus::Active, "v0.3", detail::kServerAll,
                 "단계 16b — Native System 뒤 (물리 뒤 — 옮기지 않는다)"},
    CallbackInfo{"onAction", true, CallbackStatus::Planned, "v0.6",
                 detail::kClientDisplay | capBit(HostCapability::InputRead) | capBit(HostCapability::ClientSend) |
                     capBit(HostCapability::Ui) | capBit(HostCapability::Sound),
                 "프레임 — UI 가 먹지 않은 행동"},
    CallbackInfo{"onEvent", true, CallbackStatus::Planned, "v0.6",
                 detail::kClientDisplay | capBit(HostCapability::Sound), "프레임 — 복제된 서버 이벤트"},
    CallbackInfo{"onLateUpdate", true, CallbackStatus::Planned, "v0.6",
                 detail::kClientDisplay | capBit(HostCapability::Ui), "프레임 — 모든 onUpdate 뒤 (카메라 · 따라가기)"},
    CallbackInfo{"onRender", true, CallbackStatus::Planned, "v0.6",
                 capBit(HostCapability::EcsRead) | capBit(HostCapability::Draw),
                 "프레임 — 그리기 명령만 (GPU · 권위 ECS 없음)"},
    CallbackInfo{"onStart", true, CallbackStatus::Planned, "v0.6", detail::kClientDisplay, "세션 시작"},
    CallbackInfo{"onStop", true, CallbackStatus::Planned, "v0.6", detail::kClientDisplay, "세션 끝"},
    CallbackInfo{"onTick", true, CallbackStatus::Planned, "v0.7",
                 capBit(HostCapability::EcsRead) | capBit(HostCapability::Time) | capBit(HostCapability::InputRead) |
                     capBit(HostCapability::ClientSend),
                 "Client 고정 Tick — network.model \"predicted\" 게임만 (ADR-0087)"},
    CallbackInfo{"onUpdate", true, CallbackStatus::Active, "v0.3", detail::kClientDisplay,
                 "프레임 — 스냅숏 적용 · 보간 뒤 (표시만)"},
};

// 이 이름 · 도메인의 콜백 (없으면 nullptr)
[[nodiscard]] constexpr const CallbackInfo* findCallback(std::string_view name, bool client) noexcept {
    for (const CallbackInfo& c : kCallbacks) {
        if (c.name == name && c.client == client) {
            return &c;
        }
    }
    return nullptr;
}

// 어느 도메인에든 있는 이름인가 (예약 포함)
[[nodiscard]] constexpr bool isCallbackName(std::string_view name) noexcept {
    for (const CallbackInfo& c : kCallbacks) {
        if (c.name == name) {
            return true;
        }
    }
    return false;
}

} // namespace sbx::script::api
