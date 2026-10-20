#include "core/trigger/TriggerSchema.hpp"

#include <algorithm>
#include <array>

namespace sbx::trigger {
namespace {

using enum ParamType;
constexpr f64 kSeconds = 86'400;       // 하루
constexpr f64 kScore = 1'000'000'000;  // comp::kMaxScore
constexpr f64 kScoreDelta = 1'000'000; // content::kMaxScoreDelta
constexpr f64 kCount = 1'000'000;      // 끝 조건 count 와 같다
constexpr f64 kWorld = 1'000'000;      // 좌표 · 수치 비교 값의 상한 (유한한 수인지 보려는 것)

// --- 이벤트 ---
constexpr ParamSpec kTimer[] = {{"seconds", Number, true, 0, kSeconds, "초 (Running 경과)"},
                                {"repeat", Bool, false, 0, 0, "반복"}};
constexpr ParamSpec kArea[] = {{"min", Vec2, true, 0, 0, "영역 최소"},
                               {"max", Vec2, true, 0, 0, "영역 최대"},
                               {"tags", Tags, false, 0, 0, "들어온 개체 태그"}};
constexpr ParamSpec kScoreEvent[] = {{"team", Team, false, 0, 0, "팀 (없으면 공용)"},
                                     {"op", Op, true, 0, 0, "비교"},
                                     {"value", Integer, true, -kScore, kScore, "값"}};
constexpr ParamSpec kDeath[] = {{"tags", Tags, false, 0, 0, "죽은 개체 태그"}};

// --- 조건 ---
constexpr ParamSpec kCount_[] = {
    {"tags", Tags, true, 0, 0, "태그"}, {"op", Op, true, 0, 0, "비교"}, {"value", Integer, true, 0, kCount, "개수"}};
constexpr ParamSpec kScoreCond[] = {{"team", Team, false, 0, 0, "팀 (없으면 공용)"},
                                    {"op", Op, true, 0, 0, "비교"},
                                    {"value", Integer, true, -kScore, kScore, "값"}};
constexpr ParamSpec kTime[] = {{"op", Op, true, 0, 0, "비교"},
                               {"seconds", Number, true, 0, kSeconds, "초 (Running 경과)"}};
constexpr ParamSpec kFieldCond[] = {{"field", Field, true, 0, 0, "필드"},
                                    {"op", Op, true, 0, 0, "비교"},
                                    {"value", Number, true, -kWorld, kWorld, "값"}};
constexpr ParamSpec kTagsCond[] = {{"tags", Tags, true, 0, 0, "대상 태그"}};
constexpr ParamSpec kChance[] = {{"p", Number, true, 0, 1, "확률"}};

// --- 효과 ---
constexpr ParamSpec kFieldEffect[] = {{"field", Field, true, 0, 0, "필드"},
                                      {"value", Number, true, -kWorld, kWorld, "값"}};
constexpr ParamSpec kSpawn[] = {{"prefab", Prefab, true, 0, 0, "Prefab"},
                                {"count", Integer, false, 1, 16, "개수"},
                                {"at", Vec2, false, 0, 0, "자리 (없으면 대상 둘레)"}};
constexpr ParamSpec kTagEffect[] = {{"tag", Tag, true, 0, 0, "태그"}};
constexpr ParamSpec kEventEffect[] = {{"name", EventName, true, 0, 0, "이벤트 이름"}};
constexpr ParamSpec kScoreAdd[] = {{"value", Integer, true, -kScoreDelta, kScoreDelta, "더할 점수"},
                                   {"team", Team, false, 0, 0, "팀 (없으면 대상의 팀 · 공용)"}};
constexpr ParamSpec kGameEnd[] = {{"result", Result, true, 0, 0, "결과"}, {"team", Team, false, 0, 0, "팀"}};

constexpr std::array kTypes{
    NodeTypeSpec{"event.game_start", NodeCategory::Event, "게임 시작 (Running)", false, false, {}, {}},
    NodeTypeSpec{"event.tick", NodeCategory::Event, "매 틱 (Running)", false, false, {}, {}},
    NodeTypeSpec{"event.timer", NodeCategory::Event, "타이머", false, false, {}, kTimer},
    NodeTypeSpec{"event.area_enter", NodeCategory::Event, "영역에 들어옴", true, false, {}, kArea},
    NodeTypeSpec{"event.score", NodeCategory::Event, "점수가 조건을 넘음", false, false, {}, kScoreEvent},
    NodeTypeSpec{"event.death", NodeCategory::Event, "개체가 죽음", true, false, {}, kDeath},

    NodeTypeSpec{"cond.count", NodeCategory::Condition, "개체 수", false, false, {}, kCount_},
    NodeTypeSpec{"cond.score", NodeCategory::Condition, "점수", false, false, {}, kScoreCond},
    NodeTypeSpec{"cond.time", NodeCategory::Condition, "경과 시간", false, false, {}, kTime},
    NodeTypeSpec{"cond.field", NodeCategory::Condition, "대상 필드", false, true, {}, kFieldCond},
    NodeTypeSpec{"cond.tags", NodeCategory::Condition, "대상 태그", false, true, {}, kTagsCond},
    NodeTypeSpec{"cond.chance", NodeCategory::Condition, "확률", false, false, {}, kChance},

    NodeTypeSpec{"effect.field.add", NodeCategory::Effect, "대상 필드 더하기", false, true, {}, kFieldEffect},
    NodeTypeSpec{"effect.field.set", NodeCategory::Effect, "대상 필드 정하기", false, true, {}, kFieldEffect},
    NodeTypeSpec{"effect.destroy", NodeCategory::Effect, "대상 없애기", false, true, {}, {}},
    NodeTypeSpec{"effect.spawn", NodeCategory::Effect, "Prefab 만들기", false, false, {}, kSpawn},
    NodeTypeSpec{"effect.tag.add", NodeCategory::Effect, "대상 태그 붙이기", false, true, {}, kTagEffect},
    NodeTypeSpec{"effect.tag.remove", NodeCategory::Effect, "대상 태그 떼기", false, true, {}, kTagEffect},
    NodeTypeSpec{"effect.event", NodeCategory::Effect, "이벤트 내기", false, false, {}, kEventEffect},
    NodeTypeSpec{"effect.score.add", NodeCategory::Effect, "점수 더하기", false, false, {}, kScoreAdd},
    NodeTypeSpec{"effect.game.end", NodeCategory::Effect, "게임 끝내기", false, false, {}, kGameEnd},
};

} // namespace

std::span<const NodeTypeSpec> nodeTypes() noexcept {
    return kTypes;
}

const NodeTypeSpec* findNodeType(std::string_view type) noexcept {
    const auto it = std::find_if(kTypes.begin(), kTypes.end(), [&](const NodeTypeSpec& s) { return s.type == type; });
    return it != kTypes.end() ? &*it : nullptr;
}

const ParamSpec* findParam(const NodeTypeSpec& spec, std::string_view key) noexcept {
    const auto it =
        std::find_if(spec.params.begin(), spec.params.end(), [&](const ParamSpec& p) { return p.key == key; });
    return it != spec.params.end() ? &*it : nullptr;
}

std::string_view categoryName(NodeCategory c) noexcept {
    switch (c) {
    case NodeCategory::Event:
        return "event";
    case NodeCategory::Condition:
        return "condition";
    case NodeCategory::Effect:
        return "effect";
    }
    return "?";
}

} // namespace sbx::trigger
