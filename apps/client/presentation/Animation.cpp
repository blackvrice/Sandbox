#include "apps/client/presentation/Animation.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include <nlohmann/json.hpp>

namespace sbx::client {

namespace {

constexpr f64 kTickRate = 30.0; // 복제 월드의 틱 (sim::kTickRate — 표시 쪽은 시뮬레이션 헤더를 들이지 않는다)

std::optional<AnimCondition> conditionOf(std::string_view s) {
    if (s == "always") {
        return AnimCondition::Always;
    }
    if (s == "moving") {
        return AnimCondition::Moving;
    }
    if (s == "still") {
        return AnimCondition::Still;
    }
    if (s == "end") {
        return AnimCondition::End;
    }
    return std::nullopt;
}

std::optional<std::array<i32, 2>> pairOf(const nlohmann::json& v, i32 lo) {
    if (!v.is_array() || v.size() != 2 || !v[0].is_number_integer() || !v[1].is_number_integer()) {
        return std::nullopt;
    }
    const i32 a = v[0].get<i32>();
    const i32 b = v[1].get<i32>();
    if (a < lo || b < lo || a > 4096 || b > 4096) {
        return std::nullopt;
    }
    return std::array<i32, 2>{a, b};
}

} // namespace

std::optional<u32> Animator::stateOf(std::string_view name) const {
    for (usize i = 0; i < states.size(); ++i) {
        if (states[i] == name) {
            return static_cast<u32>(i);
        }
    }
    return std::nullopt;
}

void AnimationLibrary::clear() {
    m_clips.clear();
    m_clipIndex.clear();
    m_animators.clear();
    m_animatorIndex.clear();
    m_pending.clear();
}

const AnimationClip* AnimationLibrary::clip(u32 index) const {
    return index < m_clips.size() ? &m_clips[index] : nullptr;
}

std::optional<u32> AnimationLibrary::findAnimator(std::string_view name) const {
    const auto it = m_animatorIndex.find(std::string(name));
    return it != m_animatorIndex.end() ? std::optional<u32>(it->second) : std::nullopt;
}

const Animator* AnimationLibrary::animator(u32 index) const {
    return index < m_animators.size() ? &m_animators[index] : nullptr;
}

std::vector<std::string> AnimationLibrary::load(std::string_view json, std::string_view origin) {
    std::vector<std::string> warnings;
    const nlohmann::json doc = nlohmann::json::parse(json, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        warnings.push_back(std::format("{}: {{ \"clips\": …, \"animators\": … }} 객체가 아니다", origin));
        return warnings;
    }
    const auto warn = [&](std::string_view what, std::string_view name, std::string_view why) {
        warnings.push_back(std::format("{}: {} '{}' — {}", origin, what, name, why));
    };
    if (const auto c = doc.find("clips"); c != doc.end() && c->is_object()) {
        for (const auto& [name, v] : c->items()) {
            AnimationClip clip;
            if (!v.is_object() || !v.contains("material") || !v["material"].is_string()) {
                warn("클립", name, "material (문자열) 이 필요하다");
                continue;
            }
            clip.material = v["material"].get<std::string>();
            const auto grid = v.contains("grid") ? pairOf(v["grid"], 1) : std::optional<std::array<i32, 2>>({1, 1});
            if (!grid) {
                warn("클립", name, "grid 는 [열 수, 행 수] (1 ~ 4096)");
                continue;
            }
            clip.grid = *grid;
            bool good = v.contains("frames") && v["frames"].is_array() && !v["frames"].empty() &&
                        v["frames"].size() <= kMaxAnimFrames;
            if (good) {
                for (const auto& f : v["frames"]) {
                    const auto cell = pairOf(f, 0);
                    if (!cell || (*cell)[0] >= clip.grid[0] || (*cell)[1] >= clip.grid[1]) {
                        good = false;
                        break;
                    }
                    clip.frames.push_back(*cell);
                }
            }
            if (!good) {
                warn("클립", name, std::format("frames 는 [[열, 행], …] (1 ~ {} 개, grid 안)", kMaxAnimFrames));
                continue;
            }
            if (const auto fps = v.find("fps"); fps != v.end()) {
                if (!fps->is_number() || !(fps->get<f64>() >= 1.0 && fps->get<f64>() <= 60.0)) {
                    warn("클립", name, "fps 는 1 ~ 60");
                    continue;
                }
                clip.fps = static_cast<f32>(fps->get<f64>());
            }
            if (const auto loop = v.find("loop"); loop != v.end()) {
                if (!loop->is_boolean()) {
                    warn("클립", name, "loop 는 true · false");
                    continue;
                }
                clip.loop = loop->get<bool>();
            }
            if (const auto it = m_clipIndex.find(name); it != m_clipIndex.end()) {
                m_clips[it->second] = std::move(clip);
            } else {
                m_clipIndex.emplace(name, static_cast<u32>(m_clips.size()));
                m_clips.push_back(std::move(clip));
            }
        }
    }
    if (const auto a = doc.find("animators"); a != doc.end() && a->is_object()) {
        for (const auto& [name, v] : a->items()) {
            if (!v.is_object() || !v.contains("states") || !v["states"].is_object() || v["states"].empty() ||
                v["states"].size() > kMaxAnimStates) {
                warn("애니메이터", name,
                     std::format("states 는 {{ \"<상태>\": \"<클립>\" }} (1 ~ {} 개)", kMaxAnimStates));
                continue;
            }
            Animator an;
            Pending pending;
            pending.origin = std::string(origin);
            bool good = true;
            for (const auto& [state, clipName] : v["states"].items()) {
                if (!clipName.is_string()) {
                    good = false;
                    break;
                }
                an.states.push_back(state);
                pending.clipNames.push_back(clipName.get<std::string>());
            }
            const std::string initial = v.contains("initial") && v["initial"].is_string()
                                            ? v["initial"].get<std::string>()
                                        : an.states.empty() ? std::string()
                                                            : an.states.front();
            const auto init = an.stateOf(initial);
            if (!good || !init) {
                warn("애니메이터", name, "states 의 값은 클립 이름, initial 은 있는 상태");
                continue;
            }
            an.initial = *init;
            if (const auto t = v.find("transitions"); t != v.end()) {
                if (!t->is_array()) {
                    warn("애니메이터", name, "transitions 는 배열");
                    continue;
                }
                for (const auto& tr : *t) {
                    const auto from =
                        tr.contains("from") && tr["from"].is_string() ? tr["from"].get<std::string>() : "*";
                    const auto to = tr.contains("to") && tr["to"].is_string() ? an.stateOf(tr["to"].get<std::string>())
                                                                              : std::nullopt;
                    const auto when = tr.contains("when") && tr["when"].is_string()
                                          ? conditionOf(tr["when"].get<std::string>())
                                          : std::optional<AnimCondition>(AnimCondition::Always);
                    const auto fromIndex = from == "*" ? std::optional<u32>() : an.stateOf(from);
                    if (!to || !when || (from != "*" && !fromIndex)) {
                        good = false;
                        break;
                    }
                    an.transitions.push_back({fromIndex ? static_cast<i32>(*fromIndex) : -1, *to, *when});
                }
                if (!good) {
                    warn("애니메이터", name,
                         "전이는 { from: 상태 | \"*\", to: 상태, when: moving · still · end · always }");
                    continue;
                }
            }
            an.flipWithVelocity = v.value("flipWithVelocity", false);
            u32 index = 0;
            if (const auto it = m_animatorIndex.find(name); it != m_animatorIndex.end()) {
                index = it->second;
                m_animators[index] = std::move(an);
            } else {
                index = static_cast<u32>(m_animators.size());
                m_animatorIndex.emplace(name, index);
                m_animators.push_back(std::move(an));
            }
            pending.animator = index;
            m_pending.push_back(std::move(pending));
        }
    }
    resolvePending();
    return warnings;
}

// 클립 이름 → 번호 (뒤 팩이 앞 팩의 클립을 쓸 수 있게 load 마다 다시) — 못 푼 것은 그 애니메이터를 비운다 (그리지
// 않는다)
std::vector<std::string> AnimationLibrary::resolvePending() {
    std::vector<std::string> warnings;
    std::vector<Pending> keep;
    for (Pending& p : m_pending) {
        Animator& an = m_animators[p.animator];
        an.clips.clear();
        bool all = true;
        for (const std::string& c : p.clipNames) {
            const auto it = m_clipIndex.find(c);
            if (it == m_clipIndex.end()) {
                all = false;
                break;
            }
            an.clips.push_back(it->second);
        }
        if (!all) {
            an.clips.clear();
            keep.push_back(std::move(p)); // 다음 팩에 있을 수 있다
            continue;
        }
    }
    m_pending = std::move(keep);
    return warnings;
}

std::vector<std::string> AnimationLibrary::unresolved() const {
    std::vector<std::string> out;
    for (const Pending& p : m_pending) {
        const auto name =
            std::ranges::find_if(m_animatorIndex, [&](const auto& kv) { return kv.second == p.animator; });
        out.push_back(std::format("{}: 애니메이터 '{}' 의 클립이 없다 — 그리지 않는다", p.origin,
                                  name != m_animatorIndex.end() ? name->first : std::string("?")));
    }
    return out;
}

usize clipFrameAt(const AnimationClip& clip, f64 elapsedTicks) {
    const f64 f = std::floor(std::max(0.0, elapsedTicks) * static_cast<f64>(clip.fps) / kTickRate);
    const usize n = clip.frames.size();
    if (n == 0) {
        return 0;
    }
    const auto i = static_cast<usize>(f);
    return clip.loop ? i % n : std::min(i, n - 1);
}

bool clipEnded(const AnimationClip& clip, f64 elapsedTicks) {
    if (clip.loop) {
        return false;
    }
    return std::max(0.0, elapsedTicks) * static_cast<f64>(clip.fps) / kTickRate >= static_cast<f64>(clip.frames.size());
}

AnimationFrame stepAnimation(const AnimationLibrary& lib, AnimationState& s, f64 renderTick, f32 speed, f32 vx) {
    AnimationFrame out;
    const Animator* an = lib.animator(s.animator);
    if (an == nullptr || an->clips.size() != an->states.size() || an->states.empty()) {
        return out;
    }
    if (s.state >= an->states.size()) {
        s.state = an->initial;
        s.start = renderTick;
    }
    const bool moving = speed >= kAnimMovingSpeed;
    for (const AnimTransition& t : an->transitions) {
        if ((t.from >= 0 && static_cast<u32>(t.from) != s.state) || t.to == s.state) {
            continue;
        }
        const AnimationClip* cur = lib.clip(an->clips[s.state]);
        const bool hit = t.when == AnimCondition::Always || (t.when == AnimCondition::Moving && moving) ||
                         (t.when == AnimCondition::Still && !moving) ||
                         (t.when == AnimCondition::End && cur != nullptr && clipEnded(*cur, renderTick - s.start));
        if (hit) {
            s.state = t.to;
            s.start = renderTick;
            break; // 프레임마다 한 번
        }
    }
    if (an->flipWithVelocity && std::abs(vx) >= kAnimMovingSpeed) {
        s.flipX = vx < 0.f; // 멈추면 마지막 방향 그대로
    }
    const AnimationClip* clip = lib.clip(an->clips[s.state]);
    if (clip == nullptr || clip->frames.empty()) {
        return out;
    }
    const std::array<i32, 2> cell = clip->frames[clipFrameAt(*clip, renderTick - s.start)];
    const f32 cw = 1.f / static_cast<f32>(clip->grid[0]);
    const f32 ch = 1.f / static_cast<f32>(clip->grid[1]);
    out.clip = clip;
    out.sub = {cw * static_cast<f32>(cell[0]), ch * static_cast<f32>(cell[1]), cw * static_cast<f32>(cell[0] + 1),
               ch * static_cast<f32>(cell[1] + 1)};
    out.flipX = s.flipX;
    return out;
}

} // namespace sbx::client
