#include "core/persist/WorldSave.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string_view>

#include <nlohmann/json.hpp>

#include "core/components/core/Identity.hpp"
#include "core/components/core/Tags.hpp"
#include "core/persist/ChunkFile.hpp"
#include "core/replay/WorldHash.hpp"
#include "core/simulation/SimVersion.hpp"
#include "foundation/io/FileIo.hpp"

namespace sbx::persist {
namespace {

namespace fs = std::filesystem;
using nlohmann::json;
using nlohmann::ordered_json;

bool isEngineIdentity(ecs::StableId id) noexcept {
    return id == ecs::stableIdOf<comp::Persistence> || id == ecs::stableIdOf<comp::NetIdentity>;
}

struct Keyed {
    SaveId saveId;
    ecs::EntityId entity;
};

std::vector<Keyed> entitiesBySaveId(const ecs::Registry& reg) {
    std::vector<Keyed> out;
    if (const auto* ids = reg.findPool<comp::Persistence>()) {
        out.reserve(ids->size());
        for (usize i = 0; i < ids->size(); ++i) {
            out.push_back(Keyed{ids->dataAt(i).saveId, ids->entityAt(i)});
        }
    }
    std::sort(out.begin(), out.end(), [](const Keyed& a, const Keyed& b) { return a.saveId < b.saveId; });
    return out;
}

// --- world.json 읽기 도우미 (오류 문맥 "world.json.<key>") ------------------------------------
class Fields {
public:
    Fields(const json& j, std::string ctx) : m_j(j), m_ctx(std::move(ctx)) {}

    Expected<const json*> get(std::string_view key) const {
        const auto it = m_j.find(std::string(key));
        if (it == m_j.end()) {
            return makeError(ErrorCode::ParseError, std::format("'{}' 가 없다", key), m_ctx);
        }
        return &*it;
    }
    template <class T>
    Expected<T> uint(std::string_view key) const {
        auto v = get(key);
        if (!v) {
            return std::unexpected(v.error());
        }
        if (!(*v)->is_number_unsigned() || (*v)->get<u64>() > std::numeric_limits<T>::max()) {
            return makeError(ErrorCode::ParseError, std::format("'{}' 는 0 이상의 정수", key), m_ctx);
        }
        return static_cast<T>((*v)->get<u64>());
    }
    Expected<std::string> str(std::string_view key) const {
        auto v = get(key);
        if (!v) {
            return std::unexpected(v.error());
        }
        if (!(*v)->is_string()) {
            return makeError(ErrorCode::ParseError, std::format("'{}' 는 문자열", key), m_ctx);
        }
        return (*v)->get<std::string>();
    }
    Expected<world::ChunkCoord> coord(const json& j, std::string_view what) const {
        if (!j.is_array() || j.size() != 2 || !j[0].is_number_integer() || !j[1].is_number_integer()) {
            return makeError(ErrorCode::ParseError, std::format("'{}' 는 [x, y] 정수 배열", what), m_ctx);
        }
        const i64 x = j[0].get<i64>();
        const i64 y = j[1].get<i64>();
        if (x < std::numeric_limits<i32>::min() || x > std::numeric_limits<i32>::max() ||
            y < std::numeric_limits<i32>::min() || y > std::numeric_limits<i32>::max()) {
            return makeError(ErrorCode::OutOfRange, std::format("'{}' 범위 밖", what), m_ctx);
        }
        return world::ChunkCoord{static_cast<i32>(x), static_cast<i32>(y)};
    }
    const std::string& context() const { return m_ctx; }

private:
    const json& m_j;
    std::string m_ctx;
};

#define SBX_TRY_ASSIGN(var, expr)                                                                                      \
    auto var##_r = (expr);                                                                                             \
    if (!var##_r) {                                                                                                    \
        return std::unexpected(var##_r.error());                                                                       \
    }                                                                                                                  \
    auto var = std::move(*var##_r)

} // namespace

// =====================================================================================================================
// 저장
// =====================================================================================================================
Expected<SaveStats> saveWorld(const sim::SimulationWorld& world, const fs::path& dir, const SaveOptions& options) {
    const ecs::Registry& reg = world.registry();
    const ecs::ComponentCatalog& catalog = world.catalog();
    const content::ContentDatabase& content = world.content();
    SaveStats stats;

    SBX_TRY_ASSIGN(hash, world.worldHash()); // 구조 오류(정체성 없는 엔티티 등)면 저장하지 않는다
    stats.worldHash = hash;

    // 저장할 컴포넌트 풀: Persistent, 정체성 제외, stableId 순
    struct PoolInfo {
        const ecs::ComponentPoolBase* pool;
        const ecs::ComponentInfo* info;
    };
    std::vector<PoolInfo> pools;
    for (const ecs::ComponentPoolBase* p : reg.poolsByStableId()) {
        if (!ecs::hasFlag(p->flags(), ecs::ComponentFlags::Persistent) || isEngineIdentity(p->stableId())) {
            continue;
        }
        const ecs::ComponentInfo* info = catalog.find(p->stableId());
        if (info == nullptr) {
            return makeError(ErrorCode::NotFound, std::format("Persistent 컴포넌트 '{}' 가 카탈로그에 없다", p->name()),
                             "saveWorld");
        }
        pools.push_back(PoolInfo{p, info});
    }

    // 컴포넌트 버전 표: 카탈로그의 Persistent 전부 + Opaque 로 보관 중인 것
    json versions = json::object();
    for (const ecs::ComponentInfo& info : catalog.all()) {
        if (ecs::hasFlag(info.flags, ecs::ComponentFlags::Persistent) && !isEngineIdentity(info.stableId)) {
            versions[std::string(info.name)] = info.version;
        }
    }
    for (const auto& [saveId, comps] : world.opaqueComponents()) {
        for (const auto& [name, entry] : comps.items()) {
            versions[name] = entry.at("version");
        }
    }

    // entities.jsonl
    std::string lines;
    const std::vector<Keyed> entities = entitiesBySaveId(reg);
    lines.reserve(entities.size() * 256);
    for (const Keyed& k : entities) {
        json comps = json::object();
        for (const PoolInfo& p : pools) {
            if (const void* raw = p.pool->getRaw(k.entity)) {
                json v;
                p.info->writeJson(raw, v);
                comps[std::string(p.info->name)] = std::move(v);
            }
        }
        if (const auto it = world.opaqueComponents().find(k.saveId); it != world.opaqueComponents().end()) {
            for (const auto& [name, entry] : it->second.items()) {
                comps[name] = entry.at("value");
            }
            ++stats.opaqueEntities;
        }
        lines += R"({"saveId":)";
        lines += std::to_string(k.saveId);
        lines += R"(,"components":)";
        lines += comps.dump();
        lines += "}\n";
    }
    stats.entities = entities.size();

    // world.json (키 순서를 사람이 읽기 좋게 고정)
    const world::WorldGrid& grid = world.grid();
    const sim::SimulationClock& clock = world.clock();
    ordered_json w;
    w["worldVersion"] = kWorldVersion;
    w["schemaVersion"] = kSchemaVersion;
    w["simVersion"] = sim::kSimVersion;
    w["name"] = options.name;
    w["seed"] = replay::formatHash(world.seed());
    w["tick"] = clock.tick();
    w["nextSaveId"] = world.nextSaveId();
    w["entityCount"] = entities.size();
    w["chunkSize"] = world::kChunkSize;
    w["bounds"] = {{"minChunk", {grid.bounds().minChunk.x, grid.bounds().minChunk.y}},
                   {"maxChunk", {grid.bounds().maxChunk.x, grid.bounds().maxChunk.y}}};
    w["fillMaterial"] = content.material(grid.fillMaterial()).id;
    ordered_json packs = ordered_json::array();
    for (const auto& pk : content.packs()) {
        packs.push_back(pk.id);
    }
    w["content"] = {{"packs", std::move(packs)}, {"contentHash", replay::formatHash(content.contentHash())}};
    ordered_json tags = ordered_json::array();
    for (const std::string& t : content.tags()) {
        tags.push_back(t); // 저장 당시 비트 → 이름 (core.tags 재매핑)
    }
    w["tags"] = std::move(tags);
    ordered_json mats = ordered_json::array();
    for (const auto& m : content.terrainMaterials()) {
        mats.push_back(m.id); // 저장 당시 인덱스 → id
    }
    w["terrainMaterials"] = std::move(mats);
    w["components"] = ordered_json::parse(versions.dump()); // 이름 정렬
    w["simulation"] = {{"paused", clock.paused()},
                       {"speed", clock.speed()},
                       {"pendingSteps", clock.pendingSteps()},
                       {"editSequence", clock.editSequence()}};
    w["worldHash"] = replay::formatHash(hash);
    // Opaque 를 가진 프로세스의 해시에는 그 컴포넌트가 빠져 있다. 로더는 자기가 모르는 이름 집합이 이것과 같을 때만
    // 해시를 비교한다 (같은 바이너리가 저장·로드하면 같다 — 예: 서버는 render.* 를 모른다)
    w["opaqueEntities"] = stats.opaqueEntities;
    {
        std::set<std::string> names;
        for (const auto& [saveId, comps] : world.opaqueComponents()) {
            for (const auto& [name, entry] : comps.items()) {
                names.insert(name);
            }
        }
        w["opaqueComponents"] = names;
    }

    // 임시 폴더에 전부 쓰고 바꿔 넣는다
    fs::path staging = dir;
    staging += ".saving";
    std::error_code ec;
    fs::remove_all(staging, ec);
    if (auto r = io::createDirectories(staging / "chunks"); !r) {
        return std::unexpected(r.error());
    }
    for (const world::Chunk& c : grid.chunks()) {
        if (c.terrainRevision() == 0) {
            continue; // 처음 채운 그대로 — 로드 시 fillMaterial 로 다시 만든다
        }
        if (auto r = io::writeFileAtomic(staging / "chunks" / chunkFileName(c.coord()), encodeChunk(c)); !r) {
            return std::unexpected(r.error());
        }
        ++stats.chunkFiles;
    }
    if (auto r = io::writeFileAtomic(staging / "entities.jsonl", lines); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = io::writeFileAtomic(staging / "world.json", w.dump(2) + "\n"); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = io::replaceDirectory(staging, dir); !r) {
        return std::unexpected(r.error());
    }
    return stats;
}

// =====================================================================================================================
// 로드
// =====================================================================================================================
Expected<LoadResult> loadWorld(const ecs::ComponentCatalog& catalog, const content::ContentDatabase& content,
                               const fs::path& dir, const LoadOptions& options) {
    LoadResult result;

    // --- world.json ----------------------------------------------------------
    SBX_TRY_ASSIGN(worldText, io::readFile(dir / "world.json"));
    const json wj = json::parse(worldText, nullptr, false);
    if (wj.is_discarded() || !wj.is_object()) {
        return makeError(ErrorCode::ParseError, "JSON 객체가 아니다", "world.json");
    }
    const Fields f(wj, "world.json");
    SBX_TRY_ASSIGN(worldVersion, f.uint<u32>("worldVersion"));
    if (worldVersion != kWorldVersion) {
        return makeError(ErrorCode::VersionMismatch,
                         std::format("worldVersion {} (지원: {})", worldVersion, kWorldVersion), f.context());
    }
    SBX_TRY_ASSIGN(schemaVersion, f.uint<u32>("schemaVersion"));
    if (schemaVersion != kSchemaVersion) {
        // [계획] 스키마 마이그레이션 단계 — schemaVersion 2 가 생길 때 여기에 체인을 둔다
        return makeError(ErrorCode::VersionMismatch,
                         std::format("schemaVersion {} (지원: {})", schemaVersion, kSchemaVersion), f.context());
    }
    SBX_TRY_ASSIGN(simVersion, f.uint<u32>("simVersion"));
    result.savedSimVersion = simVersion;
    SBX_TRY_ASSIGN(chunkSize, f.uint<u32>("chunkSize"));
    if (chunkSize != static_cast<u32>(world::kChunkSize)) {
        return makeError(ErrorCode::Unsupported, std::format("chunkSize {} (지원: {})", chunkSize, world::kChunkSize),
                         f.context());
    }
    SBX_TRY_ASSIGN(seedText, f.str("seed"));
    SBX_TRY_ASSIGN(seed, replay::parseHash(seedText));
    SBX_TRY_ASSIGN(tick, f.uint<sim::Tick>("tick"));
    SBX_TRY_ASSIGN(nextSaveId, f.uint<SaveId>("nextSaveId"));
    SBX_TRY_ASSIGN(entityCount, f.uint<u64>("entityCount"));
    SBX_TRY_ASSIGN(fillMaterial, f.str("fillMaterial"));
    SBX_TRY_ASSIGN(boundsJson, f.get("bounds"));
    if (!boundsJson->is_object() || !boundsJson->contains("minChunk") || !boundsJson->contains("maxChunk")) {
        return makeError(ErrorCode::ParseError, "bounds 에 minChunk·maxChunk 가 필요하다", f.context());
    }
    SBX_TRY_ASSIGN(minChunk, f.coord((*boundsJson)["minChunk"], "bounds.minChunk"));
    SBX_TRY_ASSIGN(maxChunk, f.coord((*boundsJson)["maxChunk"], "bounds.maxChunk"));

    // 머티리얼 재매핑: 저장 당시 인덱스 → id → 지금 인덱스
    SBX_TRY_ASSIGN(matsJson, f.get("terrainMaterials"));
    if (!matsJson->is_array()) {
        return makeError(ErrorCode::ParseError, "terrainMaterials 는 문자열 배열", f.context());
    }
    std::vector<world::MaterialIndex> remap;
    for (const json& id : *matsJson) {
        if (!id.is_string()) {
            return makeError(ErrorCode::ParseError, "terrainMaterials 는 문자열 배열", f.context());
        }
        const auto m = content.findMaterial(id.get<std::string>());
        if (!m) {
            return makeError(ErrorCode::NotFound,
                             std::format("세이브의 머티리얼 '{}' 가 지금 콘텐츠에 없다", id.get<std::string>()),
                             f.context());
        }
        remap.push_back(*m);
    }

    // 태그 표 (Phase 5A 에서 추가 — 없으면 빈 표). 저장 당시 비트 → 이름 → 지금 비트
    std::vector<std::optional<content::TagIndex>> tagRemap;
    bool tagTableSame = true;
    if (wj.contains("tags")) {
        const json& tj = wj["tags"];
        if (!tj.is_array() || tj.size() > content::kMaxTags) {
            return makeError(ErrorCode::ParseError, "tags 는 이름 배열 (최대 256)", f.context());
        }
        for (usize i = 0; i < tj.size(); ++i) {
            if (!tj[i].is_string()) {
                return makeError(ErrorCode::ParseError, "tags 는 이름 배열", f.context());
            }
            const auto t = content.findTag(tj[i].get<std::string>());
            if (!t) {
                result.warnings.push_back(std::format("태그 '{}' 가 지금 콘텐츠에 없다 — 그 태그는 엔티티에서 빠진다",
                                                      tj[i].get<std::string>()));
            }
            tagRemap.push_back(t);
            tagTableSame = tagTableSame && t && *t == i;
        }
        tagTableSame = tagTableSame && tj.size() == content.tags().size();
    } else {
        tagTableSame = content.tags().empty();
    }
    if (wj.contains("content") && wj["content"].is_object() && wj["content"].contains("contentHash") &&
        wj["content"]["contentHash"].is_string()) {
        const auto savedContent = replay::parseHash(wj["content"]["contentHash"].get<std::string>());
        if (savedContent && *savedContent != content.contentHash()) {
            result.warnings.push_back(std::format("콘텐츠가 저장 당시와 다르다 (contentHash {} → {})",
                                                  wj["content"]["contentHash"].get<std::string>(),
                                                  replay::formatHash(content.contentHash())));
        }
    }

    // 컴포넌트 버전 표
    SBX_TRY_ASSIGN(versionsJson, f.get("components"));
    if (!versionsJson->is_object()) {
        return makeError(ErrorCode::ParseError, "components 는 {이름: 버전} 객체", f.context());
    }
    std::map<std::string, u16, std::less<>> savedVersions;
    for (const auto& [name, v] : versionsJson->items()) {
        if (!v.is_number_unsigned() || v.get<u64>() == 0 || v.get<u64>() > 0xFFFF) {
            return makeError(ErrorCode::ParseError, std::format("components.{} 버전이 잘못되었다", name), f.context());
        }
        savedVersions[name] = static_cast<u16>(v.get<u64>());
    }

    // 시계
    SBX_TRY_ASSIGN(simJson, f.get("simulation"));
    if (!simJson->is_object()) {
        return makeError(ErrorCode::ParseError, "simulation 은 객체", f.context());
    }
    const Fields sf(*simJson, "world.json.simulation");
    SBX_TRY_ASSIGN(pausedJson, sf.get("paused"));
    SBX_TRY_ASSIGN(speedJson, sf.get("speed"));
    if (!pausedJson->is_boolean() || !speedJson->is_number()) {
        return makeError(ErrorCode::ParseError, "paused(bool)·speed(숫자)", sf.context());
    }
    SBX_TRY_ASSIGN(pendingSteps, sf.uint<u32>("pendingSteps"));
    SBX_TRY_ASSIGN(editSequence, sf.uint<u64>("editSequence"));

    SBX_TRY_ASSIGN(savedOpaque, f.uint<u64>("opaqueEntities"));
    // 저장한 프로세스가 몰랐던 컴포넌트 이름 (Phase 5A 에서 추가 — 없고 opaqueEntities 가 0 이면 빈 집합)
    std::optional<std::set<std::string>> savedOpaqueNames;
    if (wj.contains("opaqueComponents") && wj["opaqueComponents"].is_array()) {
        savedOpaqueNames.emplace();
        for (const json& n : wj["opaqueComponents"]) {
            if (n.is_string()) {
                savedOpaqueNames->insert(n.get<std::string>());
            }
        }
    } else if (savedOpaque == 0) {
        savedOpaqueNames.emplace();
    }
    std::set<std::string> loaderOpaqueNames;
    SBX_TRY_ASSIGN(savedHashText, f.str("worldHash"));
    SBX_TRY_ASSIGN(savedHash, replay::parseHash(savedHashText));

    // --- 월드 생성 ------------------------------------------------------------
    sim::WorldDesc desc;
    desc.seed = seed;
    desc.bounds = world::GridBounds{minChunk, maxChunk};
    desc.fillMaterial = fillMaterial;
    desc.registerDefaultSystems = options.registerDefaultSystems;
    if (auto r = sim::validateWorldDesc(desc, content); !r) {
        return std::unexpected(Error{r.error().code, r.error().message, "world.json"});
    }
    auto w = std::make_unique<sim::SimulationWorld>(catalog, content, desc);
    w->beginRestore(tick);

    // --- 청크 ----------------------------------------------------------------
    const fs::path chunkDir = dir / "chunks";
    std::error_code ec;
    if (fs::exists(chunkDir, ec)) {
        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(chunkDir, ec)) {
            files.push_back(entry.path());
        }
        if (ec) {
            return makeError(ErrorCode::IoError, ec.message(), io::displayPath(chunkDir));
        }
        std::sort(files.begin(), files.end()); // 순서 무관하지만 오류 보고를 결정적으로
        std::set<std::pair<i32, i32>> seen;
        for (const fs::path& p : files) {
            const std::string ctx = io::displayPath(p);
            if (p.extension() != ".chunk") {
                return makeError(ErrorCode::ParseError, "chunks/ 에 청크가 아닌 파일", ctx);
            }
            SBX_TRY_ASSIGN(bytes, io::readFile(p));
            SBX_TRY_ASSIGN(decoded, decodeChunk(bytes, ctx));
            if (p.filename().string() != chunkFileName(decoded.coord)) {
                return makeError(ErrorCode::ParseError, "파일 이름과 머리의 좌표가 다르다", ctx);
            }
            world::Chunk* chunk = w->mutableGrid().mutableChunk(decoded.coord);
            if (chunk == nullptr) {
                return makeError(ErrorCode::OutOfRange, "월드 경계 밖 청크", ctx);
            }
            if (!seen.insert({decoded.coord.x, decoded.coord.y}).second) {
                return makeError(ErrorCode::AlreadyExists, "같은 청크가 두 번", ctx);
            }
            for (auto& m : decoded.layers.material) {
                if (m >= remap.size()) {
                    return makeError(ErrorCode::OutOfRange, std::format("머티리얼 인덱스 {} 가 표 밖", m), ctx);
                }
                m = remap[m];
            }
            chunk->mutableLayers() = decoded.layers;
            chunk->setRevision(decoded.revision);
        }
    }

    // --- 엔티티 --------------------------------------------------------------
    SBX_TRY_ASSIGN(entText, io::readFile(dir / "entities.jsonl"));
    ecs::Registry& reg = w->registry();
    SaveId prev = kInvalidSaveId;
    u64 count = 0;
    usize lineNo = 0;
    usize pos = 0;
    while (pos < entText.size()) {
        usize end = entText.find('\n', pos);
        if (end == std::string::npos) {
            end = entText.size();
        }
        std::string_view line(entText.data() + pos, end - pos);
        pos = end + 1;
        ++lineNo;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1); // CRLF 로 바뀐 파일도 읽는다 (git autocrlf 등)
        }
        if (line.empty()) {
            continue;
        }
        const std::string ctx = std::format("entities.jsonl:{}", lineNo);
        const json ej = json::parse(line, nullptr, false);
        if (ej.is_discarded() || !ej.is_object() || ej.size() != 2 || !ej.contains("saveId") ||
            !ej.contains("components") || !ej["saveId"].is_number_unsigned() || !ej["components"].is_object()) {
            return makeError(ErrorCode::ParseError, R"({"saveId": n, "components": {...}} 형식이 아니다)", ctx);
        }
        const SaveId saveId = ej["saveId"].get<SaveId>();
        if (saveId == kInvalidSaveId || saveId <= prev || saveId >= nextSaveId) {
            return makeError(
                ErrorCode::ValidationFailed,
                std::format("saveId {} — 1 이상, 오름차순, nextSaveId({}) 미만이어야 한다", saveId, nextSaveId), ctx);
        }
        prev = saveId;
        const ecs::EntityId e = w->restoreEntity(saveId);
        json opaque = json::object();
        for (const auto& [name, value] : ej["components"].items()) {
            const std::string cctx = std::format("{}.{}", ctx, name);
            const auto vIt = savedVersions.find(name);
            if (vIt == savedVersions.end()) {
                return makeError(ErrorCode::ParseError, "world.json components 에 버전이 없다", cctx);
            }
            const ecs::ComponentInfo* info = catalog.find(std::string_view(name));
            if (info == nullptr) {
                opaque[name] = {{"version", vIt->second}, {"value", value}};
                ++result.opaqueComponents;
                loaderOpaqueNames.insert(name);
                continue;
            }
            if (isEngineIdentity(info->stableId) || !ecs::hasFlag(info->flags, ecs::ComponentFlags::Persistent)) {
                return makeError(ErrorCode::ValidationFailed, "저장되지 않는 컴포넌트가 들어 있다", cctx);
            }
            json v = value;
            if (vIt->second != info->version) {
                if (options.migrations == nullptr) {
                    return makeError(
                        ErrorCode::VersionMismatch,
                        std::format("저장 버전 {} ≠ 현재 {} (마이그레이션 없음)", vIt->second, info->version), cctx);
                }
                if (auto r = options.migrations->migrate(name, vIt->second, info->version, v); !r) {
                    return std::unexpected(Error{r.error().code, r.error().message, cctx});
                }
                ++result.migratedComponents;
            }
            ecs::ComponentPoolBase* pool = reg.poolByStableId(info->stableId);
            if (pool == nullptr) {
                pool = &reg.adoptPool(info->makePool());
            }
            void* raw = pool->emplaceDefaultRaw(e, tick);
            if (auto r = info->readJson(raw, v, cctx); !r) {
                return std::unexpected(r.error());
            }
            if (info->stableId == ecs::stableIdOf<comp::Tags>) {
                // 비트 재매핑: 저장 당시 표의 i 번 → 지금 표의 인덱스
                auto& tags = static_cast<comp::Tags*>(raw)->set;
                const content::TagSet saved = tags;
                tags = {};
                for (usize i = 0; i < content::kMaxTags; ++i) {
                    if (!saved.test(static_cast<content::TagIndex>(i))) {
                        continue;
                    }
                    if (i >= tagRemap.size()) {
                        return makeError(ErrorCode::OutOfRange, std::format("태그 비트 {} 가 표 밖", i), cctx);
                    }
                    if (tagRemap[i]) {
                        tags.set(*tagRemap[i]);
                    }
                }
            }
        }
        if (!opaque.empty()) {
            w->restoreOpaque(saveId, std::move(opaque));
        }
        ++count;
    }
    if (count != entityCount) {
        return makeError(ErrorCode::ValidationFailed,
                         std::format("엔티티 {}개 (world.json 은 {}개)", count, entityCount), "entities.jsonl");
    }

    sim::RestoreState state;
    state.tick = tick;
    state.nextSaveId = nextSaveId;
    state.paused = pausedJson->get<bool>();
    state.speed = static_cast<f32>(speedJson->get<f64>());
    state.pendingSteps = pendingSteps;
    state.editSequence = editSequence;
    w->finishRestore(state);

    // --- D2 검증 -------------------------------------------------------------
    if (!options.verifyHash) {
        result.hashSkippedReason = "verifyHash = false";
    } else if (simVersion != sim::kSimVersion) {
        result.hashSkippedReason = std::format("simVersion {} → {} (규칙 차이)", simVersion, sim::kSimVersion);
    } else if (result.migratedComponents > 0) {
        result.hashSkippedReason = "마이그레이션이 적용됨";
    } else if (!tagTableSame) {
        result.hashSkippedReason = "태그 표가 저장 당시와 다르다 (core.tags 비트가 재매핑됨)";
    } else if (!savedOpaqueNames || *savedOpaqueNames != loaderOpaqueNames) {
        result.hashSkippedReason =
            "저장한 프로세스와 이 프로세스가 모르는 컴포넌트 집합이 다르다 (해시에 들어간 컴포넌트가 다르다)";
    } else {
        SBX_TRY_ASSIGN(loadedHash, w->worldHash());
        if (loadedHash != savedHash) {
            return makeError(ErrorCode::ValidationFailed,
                             std::format("로드한 월드의 해시 {} ≠ 저장 당시 {} (D2 위반)",
                                         replay::formatHash(loadedHash), replay::formatHash(savedHash)),
                             "world.json");
        }
        result.hashVerified = true;
    }
    result.world = std::move(w);
    return result;
}

} // namespace sbx::persist
