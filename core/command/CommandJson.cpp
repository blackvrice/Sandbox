#include "core/command/CommandJson.hpp"

#include <charconv>
#include <format>
#include <type_traits>

namespace sbx::cmd {
namespace {

using ecs::Json;

std::string hexId(ecs::StableId id) {
    return std::format("0x{:016x}", id);
}

Expected<ecs::StableId> parseId(const Json& j) {
    if (!j.is_string()) {
        return makeError(ErrorCode::ParseError, "id 는 \"0x…\" 문자열");
    }
    const std::string s = j.get<std::string>();
    ecs::StableId v = 0;
    if (s.size() < 3 || s[0] != '0' || s[1] != 'x') {
        return makeError(ErrorCode::ParseError, std::format("id 형식이 아니다: {}", s));
    }
    const auto [p, ec] = std::from_chars(s.data() + 2, s.data() + s.size(), v, 16);
    if (ec != std::errc{} || p != s.data() + s.size()) {
        return makeError(ErrorCode::ParseError, std::format("id 형식이 아니다: {}", s));
    }
    return v;
}

Json vec(Vec2 v) {
    return Json::array({static_cast<f64>(v.x), static_cast<f64>(v.y)});
}
Json veci(Vec2i v) {
    return Json::array({v.x, v.y});
}

Expected<Vec2> readVec(const Json& j, const char* key) {
    if (!j.contains(key) || !j[key].is_array() || j[key].size() != 2 || !j[key][0].is_number() ||
        !j[key][1].is_number()) {
        return makeError(ErrorCode::ParseError, std::format("'{}' 는 [x, y]", key));
    }
    return Vec2{static_cast<f32>(j[key][0].get<f64>()), static_cast<f32>(j[key][1].get<f64>())};
}

Expected<Vec2i> readVeci(const Json& j) {
    if (!j.is_array() || j.size() != 2 || !j[0].is_number_integer() || !j[1].is_number_integer()) {
        return makeError(ErrorCode::ParseError, "타일 좌표는 [x, y] 정수");
    }
    return Vec2i{j[0].get<i32>(), j[1].get<i32>()};
}

Expected<u64> readU64(const Json& j, const char* key) {
    if (!j.contains(key) || !j[key].is_number_unsigned()) {
        return makeError(ErrorCode::ParseError, std::format("'{}' 는 0 이상의 정수", key));
    }
    return j[key].get<u64>();
}

Json componentToJson(const ComponentValue& c) {
    return Json{{"id", hexId(c.stableId)}, {"value", c.value}};
}

Expected<ComponentValue> componentFromJson(const Json& j) {
    if (!j.is_object() || !j.contains("id") || !j.contains("value") || !j["value"].is_object()) {
        return makeError(ErrorCode::ParseError, "컴포넌트는 {\"id\", \"value\"}");
    }
    auto id = parseId(j["id"]);
    if (!id) {
        return std::unexpected(id.error());
    }
    return ComponentValue{*id, j["value"]};
}

Expected<std::vector<NetEntityId>> readTargets(const Json& j, const RefReader& ref) {
    if (!j.contains("targets") || !j["targets"].is_array()) {
        return makeError(ErrorCode::ParseError, "'targets' 는 배열");
    }
    std::vector<NetEntityId> out;
    for (const Json& t : j["targets"]) {
        if (!t.is_number_unsigned()) {
            return makeError(ErrorCode::ParseError, "'targets' 의 원소는 0 이상의 정수");
        }
        out.push_back(ref(t.get<u64>()));
    }
    return out;
}

} // namespace

Json payloadToJson(const CommandPayload& payload, const RefWriter& ref) {
    return std::visit(
        [&](const auto& p) -> Json {
            using P = std::decay_t<decltype(p)>;
            const auto targets = [&](const std::vector<NetEntityId>& ids) {
                Json a = Json::array();
                for (const NetEntityId id : ids) {
                    a.push_back(ref(id));
                }
                return a;
            };
            if constexpr (std::is_same_v<P, CreateEntity>) {
                Json comps = Json::array();
                for (const ComponentValue& c : p.components) {
                    comps.push_back(componentToJson(c));
                }
                return Json{
                    {"op", "create"}, {"position", vec(p.position)}, {"prefab", p.prefab}, {"components", comps}};
            } else if constexpr (std::is_same_v<P, DeleteEntity>) {
                return Json{{"op", "delete"}, {"targets", targets(p.targets)}};
            } else if constexpr (std::is_same_v<P, MoveEntity>) {
                return Json{
                    {"op", "move"}, {"targets", targets(p.targets)}, {"value", vec(p.value)}, {"absolute", p.absolute}};
            } else if constexpr (std::is_same_v<P, AddComponent>) {
                return Json{{"op", "add"}, {"target", ref(p.target)}, {"component", componentToJson(p.component)}};
            } else if constexpr (std::is_same_v<P, RemoveComponent>) {
                return Json{{"op", "remove"}, {"target", ref(p.target)}, {"id", hexId(p.stableId)}};
            } else if constexpr (std::is_same_v<P, ChangeComponent>) {
                return Json{{"op", "change"}, {"target", ref(p.target)}, {"id", hexId(p.stableId)}, {"patch", p.patch}};
            } else if constexpr (std::is_same_v<P, PaintTerrain>) {
                Json cells = Json::array();
                for (const Vec2i c : p.cells) {
                    cells.push_back(veci(c));
                }
                return Json{{"op", "paint"},
                            {"material", p.materialId},
                            {"cells", cells},
                            {"center", veci(p.center)},
                            {"shape", static_cast<u32>(p.shape)},
                            {"radius", p.radius}};
            } else if constexpr (std::is_same_v<P, PauseSimulation>) {
                return Json{{"op", "pause"}};
            } else if constexpr (std::is_same_v<P, ResumeSimulation>) {
                return Json{{"op", "resume"}};
            } else if constexpr (std::is_same_v<P, StepSimulation>) {
                return Json{{"op", "step"}, {"ticks", p.ticks}};
            } else {
                static_assert(std::is_same_v<P, SetSimulationSpeed>);
                return Json{{"op", "speed"}, {"speed", static_cast<f64>(p.speed)}};
            }
        },
        payload);
}

Expected<CommandPayload> payloadFromJson(const Json& j, const RefReader& ref) {
    if (!j.is_object() || !j.contains("op") || !j["op"].is_string()) {
        return makeError(ErrorCode::ParseError, "명령은 {\"op\": …} 객체");
    }
    const std::string op = j["op"].get<std::string>();
    if (op == "create") {
        CreateEntity c;
        auto pos = readVec(j, "position");
        if (!pos) {
            return std::unexpected(pos.error());
        }
        c.position = *pos;
        if (j.contains("prefab")) {
            if (!j["prefab"].is_string()) {
                return makeError(ErrorCode::ParseError, "'prefab' 은 문자열");
            }
            c.prefab = j["prefab"].get<std::string>();
        }
        if (j.contains("components")) {
            if (!j["components"].is_array()) {
                return makeError(ErrorCode::ParseError, "'components' 는 배열");
            }
            for (const Json& cj : j["components"]) {
                auto cv = componentFromJson(cj);
                if (!cv) {
                    return std::unexpected(cv.error());
                }
                c.components.push_back(std::move(*cv));
            }
        }
        return CommandPayload{std::move(c)};
    }
    if (op == "delete") {
        auto t = readTargets(j, ref);
        if (!t) {
            return std::unexpected(t.error());
        }
        return CommandPayload{DeleteEntity{std::move(*t)}};
    }
    if (op == "move") {
        auto t = readTargets(j, ref);
        auto v = readVec(j, "value");
        if (!t || !v || !j.contains("absolute") || !j["absolute"].is_boolean()) {
            return makeError(ErrorCode::ParseError, "move 는 {targets, value, absolute}");
        }
        return CommandPayload{MoveEntity{std::move(*t), *v, j["absolute"].get<bool>()}};
    }
    if (op == "add") {
        auto t = readU64(j, "target");
        if (!t || !j.contains("component")) {
            return makeError(ErrorCode::ParseError, "add 는 {target, component}");
        }
        auto cv = componentFromJson(j["component"]);
        if (!cv) {
            return std::unexpected(cv.error());
        }
        return CommandPayload{AddComponent{ref(*t), std::move(*cv)}};
    }
    if (op == "remove" || op == "change") {
        auto t = readU64(j, "target");
        if (!t || !j.contains("id")) {
            return makeError(ErrorCode::ParseError, std::format("{} 는 {{target, id}}", op));
        }
        auto id = parseId(j["id"]);
        if (!id) {
            return std::unexpected(id.error());
        }
        if (op == "remove") {
            return CommandPayload{RemoveComponent{ref(*t), *id}};
        }
        if (!j.contains("patch") || !j["patch"].is_object()) {
            return makeError(ErrorCode::ParseError, "change 의 'patch' 는 객체");
        }
        return CommandPayload{ChangeComponent{ref(*t), *id, j["patch"]}};
    }
    if (op == "paint") {
        PaintTerrain p;
        if (!j.contains("material") || !j["material"].is_string() || !j.contains("cells") || !j["cells"].is_array() ||
            !j.contains("center")) {
            return makeError(ErrorCode::ParseError, "paint 는 {material, cells, center, shape, radius}");
        }
        p.materialId = j["material"].get<std::string>();
        for (const Json& cj : j["cells"]) {
            auto c = readVeci(cj);
            if (!c) {
                return std::unexpected(c.error());
            }
            p.cells.push_back(*c);
        }
        auto center = readVeci(j["center"]);
        auto shape = readU64(j, "shape");
        auto radius = readU64(j, "radius");
        if (!center || !shape || *shape > 1 || !radius || *radius > 0xFFFF'FFFFull) {
            return makeError(ErrorCode::ParseError, "paint 의 center · shape(0|1) · radius 가 잘못됐다");
        }
        p.center = *center;
        p.shape = static_cast<BrushShape>(*shape);
        p.radius = static_cast<u32>(*radius);
        return CommandPayload{std::move(p)};
    }
    if (op == "pause") {
        return CommandPayload{PauseSimulation{}};
    }
    if (op == "resume") {
        return CommandPayload{ResumeSimulation{}};
    }
    if (op == "step") {
        auto n = readU64(j, "ticks");
        if (!n || *n > 0xFFFF'FFFFull) {
            return makeError(ErrorCode::ParseError, "step 의 'ticks'");
        }
        return CommandPayload{StepSimulation{static_cast<u32>(*n)}};
    }
    if (op == "speed") {
        if (!j.contains("speed") || !j["speed"].is_number()) {
            return makeError(ErrorCode::ParseError, "speed 의 'speed' 는 수");
        }
        return CommandPayload{SetSimulationSpeed{static_cast<f32>(j["speed"].get<f64>())}};
    }
    return makeError(ErrorCode::ParseError, std::format("모르는 명령 '{}'", op));
}

} // namespace sbx::cmd
