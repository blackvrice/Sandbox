#include "core/scenarios/WorldSource.hpp"

#include <format>

#include "core/content/ContentLoader.hpp"
#include "core/persist/WorldSave.hpp"
#include "foundation/io/FileIo.hpp"

namespace sbx::scenario {
namespace {

// 세이브를 이어 돌릴 때의 시나리오: 아무것도 넣지 않는다 (명령은 접속자만)
class ContinueScenario final : public IScenario {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "save"; }
    [[nodiscard]] std::string_view description() const noexcept override { return "세이브 이어 돌리기"; }
    [[nodiscard]] sim::Tick defaultTicks() const noexcept override { return 0; }
    void setup(sim::SimulationWorld&) override {}
    void beforeTick(sim::SimulationWorld&) override {}
};

} // namespace

std::string scenarioNames() {
    std::string names;
    for (const auto& e : scenarioList()) {
        names += names.empty() ? "" : ", ";
        names += e.name;
    }
    return names;
}

Expected<WorldSource> openWorldSource(std::string_view spec, const std::filesystem::path& contentRoot, u64 seed,
                                      const ecs::ComponentCatalog& catalog) {
    WorldSource out;
    if (auto sc = makeScenario(spec)) {
        auto db = loadScenarioContent(*sc, contentRoot, catalog);
        if (!db) {
            return std::unexpected(db.error());
        }
        out.packs = sc->requiredPacks();
        out.content = std::make_unique<content::ContentDatabase>(std::move(*db));
        out.runner = std::make_unique<ScenarioRunner>(catalog, *out.content, std::move(sc), seed);
        out.name = std::string(spec);
        return out;
    }
    const std::filesystem::path dir{std::string(spec)};
    const auto worldJson = io::readFile(dir / "world.json");
    if (!worldJson) {
        return makeError(
            ErrorCode::NotFound,
            std::format("시나리오 이름도 아니고 세이브 폴더(world.json)도 아닙니다 (시나리오: {})", scenarioNames()),
            std::string(spec));
    }
    const auto j = ecs::Json::parse(*worldJson, nullptr, false);
    if (j.is_discarded()) {
        return makeError(ErrorCode::ParseError, "world.json 을 읽지 못했습니다", std::string(spec));
    }
    if (const auto it = j.find("content"); it != j.end() && it->is_object()) {
        if (const auto p = it->find("packs"); p != it->end() && p->is_array()) {
            for (const auto& id : *p) {
                if (id.is_string()) {
                    out.packs.push_back(id.get<std::string>());
                }
            }
        }
    }
    if (out.packs.empty()) {
        out.content = std::make_unique<content::ContentDatabase>(content::ContentDatabase::builtin());
    } else {
        auto db = content::loadContent(contentRoot, out.packs, catalog);
        if (!db) {
            return std::unexpected(db.error());
        }
        out.content = std::make_unique<content::ContentDatabase>(std::move(*db));
    }
    auto loaded = persist::loadWorld(catalog, *out.content, dir);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    out.warnings = std::move(loaded->warnings);
    out.runner = std::make_unique<ScenarioRunner>(std::move(loaded->world), std::make_unique<ContinueScenario>());
    out.name = dir.filename().empty() ? dir.parent_path().filename().string() : dir.filename().string();
    return out;
}

} // namespace sbx::scenario
