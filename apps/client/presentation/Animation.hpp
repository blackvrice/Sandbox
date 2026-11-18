#pragma once
// 2D 애니메이션 클립 · 상태 기계 (v0.12 RT-06-14, ADR-0157). 표시 쪽만 — 시뮬레이션 · 해시 · 서버와 무관.
//
//   팩 presentation/animations.json (팩 로드 순 — 같은 이름은 뒤가 이긴다):
//   { "clips": { "<클립>": { "material": "<머티리얼 — 시트>", "grid": [열 수, 행 수], "frames": [[열, 행], …], "fps": 1
//   ~ 60,
//                            "loop": true } },
//     "animators": { "<이름>": { "initial": "<상태>", "states": { "<상태>": "<클립>" },
//                               "transitions": [ { "from": "<상태>" | "*", "to": "<상태>", "when": "moving" | "still" |
//                               "end" |
//                                                 "always" } ],
//                               "flipWithVelocity": false } } }
//   Prefab 의 "render.animator": { "animator": "<이름>" } 가 그 개체를 움직인다 (render.sprite 의 크기 · 레이어는
//   그대로).
// 시간은 복제 월드의 렌더 틱 (30 TPS) — 같은 스냅숏 · 같은 렌더 틱이면 어느 클라이언트에서나 같은 프레임 · 같은 전이.
// 전이는 프레임마다 한 번, 정의 차례로 첫째 맞는 것 (상태가 바뀌면 그 틱부터 새로). client 스크립트의 ctx.playAnimation
// 은 전이 없이 그 상태로 (처음부터).

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "foundation/types/Types.hpp"

namespace sbx::client {

inline constexpr f32 kAnimMovingSpeed = 0.05f; // 이 속도 (월드 단위/초) 이상이면 moving
inline constexpr usize kMaxAnimFrames = 256;
inline constexpr usize kMaxAnimStates = 64;

struct AnimationClip {
    std::string material;
    std::array<i32, 2> grid{1, 1};
    std::vector<std::array<i32, 2>> frames; // 칸 [열, 행]
    f32 fps = 8.f;
    bool loop = true;
};

enum class AnimCondition : u8 { Always, Moving, Still, End };

struct AnimTransition {
    i32 from = -1; // 상태 번호 (-1 = 어느 상태든 "*")
    u32 to = 0;
    AnimCondition when = AnimCondition::Always;
};

struct Animator {
    std::vector<std::string> states; // 이름 차례 (상태 번호)
    std::vector<u32> clips;          // 상태마다 클립 번호
    u32 initial = 0;
    std::vector<AnimTransition> transitions;
    bool flipWithVelocity = false;
    [[nodiscard]] std::optional<u32> stateOf(std::string_view name) const;
};

class AnimationLibrary {
public:
    // json 하나를 더한다 (같은 이름은 뒤가 이긴다). 틀린 항목은 그것만 빼고 경고 (origin 을 앞에)
    std::vector<std::string> load(std::string_view json, std::string_view origin);
    void clear();
    [[nodiscard]] const AnimationClip* clip(u32 index) const;
    [[nodiscard]] std::optional<u32> findAnimator(std::string_view name) const;
    [[nodiscard]] const Animator* animator(u32 index) const;
    [[nodiscard]] usize clipCount() const noexcept { return m_clips.size(); }
    [[nodiscard]] usize animatorCount() const noexcept { return m_animators.size(); }
    // 모든 팩을 읽은 뒤 — 클립을 찾지 못한 애니메이터 (그리지 않는다)
    [[nodiscard]] std::vector<std::string> unresolved() const;

private:
    std::vector<AnimationClip> m_clips;
    std::unordered_map<std::string, u32> m_clipIndex;
    std::vector<Animator> m_animators;
    std::unordered_map<std::string, u32> m_animatorIndex;
    // 애니메이터의 클립 이름은 모두 읽은 뒤 푼다 (다른 팩의 클립을 써도 된다)
    struct Pending {
        u32 animator = 0;
        std::vector<std::string> clipNames;
        std::string origin;
    };
    std::vector<Pending> m_pending;
    std::vector<std::string> resolvePending();
};

// 개체 하나의 상태 (렌더 틱 기준)
struct AnimationState {
    u32 animator = 0;
    u32 state = 0;
    f64 start = 0; // 이 상태가 시작한 렌더 틱
    bool flipX = false;
};

struct AnimationFrame {
    const AnimationClip* clip = nullptr;
    std::array<f32, 4> sub{0.f, 0.f, 1.f, 1.f};
    bool flipX = false;
};

// 상태 기계를 한 프레임 — 전이 (정의 차례 첫째) 후 지금 프레임. speed = 개체 속도 (월드 단위/초), vx = 좌우 방향
[[nodiscard]] AnimationFrame stepAnimation(const AnimationLibrary& lib, AnimationState& s, f64 renderTick, f32 speed,
                                           f32 vx);
// 클립의 지금 프레임 번호 (반복이면 감고, 아니면 마지막에 멈춘다)
[[nodiscard]] usize clipFrameAt(const AnimationClip& clip, f64 elapsedTicks);
[[nodiscard]] bool clipEnded(const AnimationClip& clip, f64 elapsedTicks);

} // namespace sbx::client
