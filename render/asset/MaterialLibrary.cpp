#include "render/asset/MaterialLibrary.hpp"

#include <algorithm>
#include <format>
#include <vector>

#include <nlohmann/json.hpp>

#include "foundation/hash/Fnv1a.hpp"
#include "foundation/io/FileIo.hpp"
#include "foundation/log/Log.hpp"

namespace sbx::render {

Expected<void> MaterialLibrary::loadFile(const std::filesystem::path& file, AssetManager* assets) {
    auto text = io::readFile(file);
    if (!text) {
        return std::unexpected(text.error());
    }
    return loadJson(*text, file.generic_string(), assets);
}

Expected<void> MaterialLibrary::loadJson(std::string_view json, std::string_view origin, AssetManager* assets) {
    const nlohmann::json doc = nlohmann::json::parse(json, nullptr, false);
    if (doc.is_discarded() || !doc.is_object() || !doc.contains("materials") || !doc["materials"].is_object()) {
        return makeError(ErrorCode::InvalidArgument, std::format("{}: {{\"materials\": {{…}}}} 객체가 아니다", origin));
    }
    for (const auto& [name, v] : doc["materials"].items()) {
        if (!v.is_object()) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{}: 머티리얼 '{}' 가 객체가 아니다", origin, name));
        }
        Material m;
        if (v.contains("sprite")) {
            if (!v["sprite"].is_string()) {
                return makeError(ErrorCode::InvalidArgument, std::format("{}: '{}'.sprite 는 문자열", origin, name));
            }
            if (assets != nullptr) {
                m.sprite = assets->requestSprite(v["sprite"].get<std::string>());
            }
        }
        if (v.contains("color")) {
            const auto& c = v["color"];
            if (!c.is_array() || (c.size() != 3 && c.size() != 4)) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("{}: '{}'.color 는 [r, g, b] 또는 [r, g, b, a] (0~255)", origin, name));
            }
            u8 ch[4] = {255, 255, 255, 255};
            for (usize i = 0; i < c.size(); ++i) {
                if (!c[i].is_number_integer() || c[i].get<int>() < 0 || c[i].get<int>() > 255) {
                    return makeError(ErrorCode::InvalidArgument,
                                     std::format("{}: '{}'.color[{}] 는 0~255 정수", origin, name, i));
                }
                ch[i] = static_cast<u8>(c[i].get<int>());
            }
            m.color = packRgba8(ch[0], ch[1], ch[2], ch[3]);
        }
        m_materials[name] = m;
    }
    return {};
}

Expected<usize> MaterialLibrary::loadAll(const std::filesystem::path& dir, AssetManager* assets) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        return usize{0};
    }
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        const auto f = entry.path() / "materials.json";
        if (entry.is_directory(ec) && std::filesystem::is_regular_file(f, ec)) {
            files.push_back(f);
        }
    }
    std::ranges::sort(
        files); // 같은 이름이 여러 파일에 있으면 이름 순으로 뒤 파일이 이긴다 (결과가 OS 의 나열 순서와 무관)
    for (const auto& f : files) {
        if (auto r = loadFile(f, assets); !r) {
            return std::unexpected(r.error());
        }
    }
    return files.size();
}

u32 MaterialLibrary::fallbackColor(std::string_view name) noexcept {
    // 해시 → 색상환 위의 밝은 색 (HSV, S 0.65 · V 0.95)
    const u64 h = fnv1a64(name);
    const f32 hue = static_cast<f32>(h % 360) / 60.f;
    const int i = static_cast<int>(hue) % 6;
    const f32 f = hue - static_cast<f32>(static_cast<int>(hue));
    const f32 v = 0.95f, s = 0.65f;
    const f32 p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    f32 r = v, g = t, b = p;
    switch (i) {
    case 1:
        r = q, g = v, b = p;
        break;
    case 2:
        r = p, g = v, b = t;
        break;
    case 3:
        r = p, g = q, b = v;
        break;
    case 4:
        r = t, g = p, b = v;
        break;
    case 5:
        r = v, g = p, b = q;
        break;
    default:
        break;
    }
    const auto c = [](f32 x) { return static_cast<u8>(x * 255.f + 0.5f); };
    return packRgba8(c(r), c(g), c(b));
}

Material MaterialLibrary::find(std::string_view name) {
    if (const auto it = m_materials.find(std::string(name)); it != m_materials.end()) {
        return it->second;
    }
    if (m_warned.insert(std::string(name)).second) {
        log::warn("render", "머티리얼 '{}' 이 materials.json 에 없다 — 색 사각형으로 그린다", name);
    }
    return {kWhiteSprite, fallbackColor(name)};
}

} // namespace sbx::render
