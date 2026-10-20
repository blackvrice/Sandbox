#include "core/scenarios/PlayScript.hpp"

#include <format>

#include "core/command/CommandJson.hpp"
#include "core/game/GameRules.hpp"
#include "core/replay/WorldHash.hpp"
#include "foundation/BuildInfo.hpp"
#include "foundation/io/FileIo.hpp"

namespace sbx::scenario {
namespace {

// 스크립트를 단계마다 명령으로 (그 밖에는 아무것도 하지 않는다 — 세이브를 이어 돌리는 것과 같다)
class ScriptScenario final : public IScenario {
public:
    explicit ScriptScenario(PlayScript script) : m_script(std::move(script)) {}
    [[nodiscard]] std::string_view name() const noexcept override { return "script"; }
    [[nodiscard]] std::string_view description() const noexcept override { return m_script.name; }
    [[nodiscard]] sim::Tick defaultTicks() const noexcept override { return m_script.maxTicks; }
    void setup(sim::SimulationWorld&) override {}
    void beforeTick(sim::SimulationWorld& world) override {
        ++m_step;
        while (m_next < m_script.steps.size() && m_script.steps[m_next].at == m_step) {
            // 다음 틱 몫 — 편집 단계 (Lobby · 일시정지) 면 지금 적용된다 (서버가 접속자 명령을 넣는 것과 같다)
            world.enqueue(cmd::SimCommand{cmd::CommandHeader{world.currentTick() + 1, cmd::kServerIssuer, ++m_sequence},
                                          m_script.steps[m_next].command});
            ++m_next;
        }
    }

private:
    PlayScript m_script;
    u64 m_step = 0;
    usize m_next = 0;
    u32 m_sequence = 0;
};

std::optional<u64> parseHashText(const ecs::Json& v) {
    if (!v.is_string()) {
        return std::nullopt;
    }
    const auto h = replay::parseHash(v.get<std::string>());
    return h ? std::optional<u64>(*h) : std::nullopt;
}

} // namespace

Expected<PlayScript> parsePlayScript(std::string_view text, std::string_view source) {
    const std::string where(source);
    const auto bad = [&](std::string why) { return makeError(ErrorCode::ParseError, std::move(why), where); };
    const ecs::Json j = ecs::Json::parse(text, nullptr, false);
    if (!j.is_object()) {
        return bad("스크립트는 JSON 객체");
    }
    for (const auto& [key, _] : j.items()) {
        if (key != "format" && key != "name" && key != "maxTicks" && key != "steps" && key != "expect") {
            return bad(std::format("모르는 키 '{}'", key));
        }
    }
    if (!j.contains("format") || j["format"] != kPlayScriptFormat) {
        return makeError(ErrorCode::VersionMismatch, std::format("format 은 {}", kPlayScriptFormat), where);
    }
    PlayScript s;
    s.name = j.value("name", "");
    if (j.contains("maxTicks")) {
        if (!j["maxTicks"].is_number_unsigned()) {
            return bad("maxTicks 는 0 이상의 정수");
        }
        s.maxTicks = j["maxTicks"].get<sim::Tick>();
    }
    if (!j.contains("steps") || !j["steps"].is_array() || j["steps"].size() > kMaxScriptSteps) {
        return bad(std::format("steps 는 {} 개까지의 배열", kMaxScriptSteps));
    }
    const cmd::RefReader noRefs = [](u64 v) { return static_cast<NetEntityId>(v); };
    for (usize i = 0; i < j["steps"].size(); ++i) {
        const ecs::Json& st = j["steps"][i];
        const std::string at = std::format("/steps/{}", i);
        if (!st.is_object() || !st.contains("at") || !st["at"].is_number_unsigned() || st["at"].get<u64>() == 0 ||
            !st.contains("command")) {
            return bad(std::format("{}: {{\"at\": 1 이상의 단계, \"command\": {{…}}}}", at));
        }
        auto command = cmd::payloadFromJson(st["command"], noRefs);
        if (!command) {
            return bad(std::format("{}/command: {}", at, command.error().message));
        }
        const u64 when = st["at"].get<u64>();
        if (!s.steps.empty() && when < s.steps.back().at) {
            return bad(std::format("{}: at 은 오름차순", at));
        }
        s.steps.push_back(PlayScript::Step{when, std::move(*command)});
    }
    if (j.contains("expect")) {
        const ecs::Json& e = j["expect"];
        if (!e.is_object()) {
            return bad("expect 는 객체");
        }
        PlayExpectation& x = s.expect;
        if (e.contains("state")) {
            x.state = e["state"].get<std::string>();
        }
        if (e.contains("result")) {
            x.result = e["result"].get<std::string>();
        }
        if (e.contains("team")) {
            x.team = e["team"].get<u32>();
        }
        if (e.contains("rule")) {
            x.rule = e["rule"].get<u32>();
        }
        if (e.contains("tick")) {
            x.tick = e["tick"].get<sim::Tick>();
        }
        if (e.contains("hash")) {
            // 툴체인 키별 (골든과 같다 — ADR-0012). 이 툴체인 항목이 없으면 해시는 보지 않는다
            const ecs::Json& h = e["hash"];
            if (h.is_object()) {
                for (const auto& [key, value] : h.items()) {
                    if (!parseHashText(value)) {
                        return bad(std::format("expect.hash.{} 는 \"0x…\" 16 진", key));
                    }
                }
                if (const auto it = h.find(std::string(build::kToolchainKey)); it != h.end()) {
                    x.hash = parseHashText(*it);
                }
            } else {
                x.hash = parseHashText(h);
                if (!x.hash) {
                    return bad("expect.hash 는 \"0x…\" 16 진 (또는 {툴체인 키: 해시})");
                }
            }
        }
    }
    return s;
}

Expected<PlayScript> loadPlayScript(const std::filesystem::path& file) {
    auto text = io::readFile(file);
    if (!text) {
        return std::unexpected(text.error());
    }
    return parsePlayScript(*text, io::displayPath(file));
}

void attachPlayScript(ScenarioRunner& runner, PlayScript script) {
    runner.replaceScenario(std::make_unique<ScriptScenario>(std::move(script)));
}

bool playFinished(const sim::SimulationWorld& world, sim::Tick startTick, const PlayScript& script) {
    if (script.maxTicks != 0 && world.currentTick() >= startTick + script.maxTicks) {
        return true;
    }
    const comp::GameSession* s = game::findSession(world.registry());
    if (s == nullptr || s->gameState() != comp::GameState::Finished) {
        return false;
    }
    const content::GameMode* mode = world.content().findMode(std::string_view(s->mode.view()));
    return mode == nullptr || !mode->autoRestart; // 자동 재시작 모드는 다음 판이 온다
}

std::string describePlayResult(const sim::SimulationWorld& world) {
    const auto hash = world.worldHash();
    const std::string h = hash ? replay::formatHash(*hash) : std::string("?");
    const comp::GameSession* s = game::findSession(world.registry());
    if (s == nullptr) {
        return std::format("게임 없음 · tick {} · hash {}", world.currentTick(), h);
    }
    const comp::GameScore* score = game::findScore(world.registry());
    return std::format("게임 {} · {} · team {} · rule {} · 점수 {} · tick {} · hash {}",
                       game::gameStateName(s->gameState()), game::gameOutcomeName(s->outcome()), s->team, s->rule,
                       score != nullptr ? score->points[0] : 0, world.currentTick(), h);
}

std::vector<std::string> checkPlayExpectation(const sim::SimulationWorld& world, const PlayExpectation& x) {
    std::vector<std::string> out;
    const comp::GameSession* s = game::findSession(world.registry());
    const auto differ = [&](std::string_view what, const auto& want, const auto& got) {
        out.push_back(std::format("{}: 기대 {} · 실제 {}", what, want, got));
    };
    if (x.state) {
        const std::string got = s != nullptr ? std::string(game::gameStateName(s->gameState())) : "없음";
        if (got != *x.state) {
            differ("state", *x.state, got);
        }
    }
    if (x.result) {
        const std::string got = s != nullptr ? std::string(game::gameOutcomeName(s->outcome())) : "없음";
        if (got != *x.result) {
            differ("result", *x.result, got);
        }
    }
    if (x.team && (s == nullptr || s->team != *x.team)) {
        differ("team", *x.team, s != nullptr ? u32{s->team} : 0u);
    }
    if (x.rule && (s == nullptr || s->rule != *x.rule)) {
        differ("rule", *x.rule, s != nullptr ? u32{s->rule} : 0u);
    }
    if (x.tick && world.currentTick() != *x.tick) {
        differ("tick", *x.tick, world.currentTick());
    }
    if (x.hash) {
        const auto h = world.worldHash();
        if (!h || *h != *x.hash) {
            differ("hash", replay::formatHash(*x.hash), h ? replay::formatHash(*h) : std::string("?"));
        }
    }
    return out;
}

Expected<void> runPlayScript(ScenarioRunner& runner, const PlayScript& script, u64 maxSteps) {
    const sim::Tick start = runner.world().currentTick();
    attachPlayScript(runner, script);
    for (u64 i = 0; i < maxSteps; ++i) {
        if (playFinished(runner.world(), start, script)) {
            return {};
        }
        runner.step();
    }
    if (playFinished(runner.world(), start, script)) {
        return {};
    }
    return makeError(ErrorCode::OutOfRange,
                     std::format("{} 단계 안에 끝나지 않았다 — {}", maxSteps, describePlayResult(runner.world())));
}

} // namespace sbx::scenario
