#pragma once
// 머티리얼 이름 → 스프라이트 + 색. docs/06-RENDERING.md 7.1, 11-CONTENT-SCHEMA (render.sprite.material), ADR-0020.
//
// assets/<팩>/materials.json:
//   { "materials": { "eco/rabbit": { "sprite": "ecosystem/rabbit.png", "color": [255, 255, 255, 255] } } }
//   sprite 는 에셋 루트 상대 경로 (없으면 흰색 사각형), color 는 RGBA 0~255 (없으면 흰색). 둘 다 선택.
// 모르는 이름은 흰색 사각형 + 이름에서 고른 색 (같은 이름은 늘 같은 색 — 어떤 엔티티인지 구별은 된다), 경고 한 번.
// 콘텐츠(서버)와 달리 contentHash 에 들어가지 않는다 (7.1 — 표현 전용).

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "foundation/types/Error.hpp"
#include "render/asset/AssetManager.hpp"

namespace sbx::render {

struct Material {
    SpriteId sprite = kWhiteSprite;
    u32 color = 0xFFFF'FFFFu;
};

class MaterialLibrary {
public:
    // json 하나를 읽어 더한다 (같은 이름은 뒤 파일이 이긴다). 스프라이트 요청은 assets 로 — nullptr 이면(렌더러 없는
    // 헤드리스) 스프라이트는 무시하고 색만 쓴다
    Expected<void> loadFile(const std::filesystem::path& file, AssetManager* assets);
    Expected<void> loadJson(std::string_view json, std::string_view origin, AssetManager* assets);
    // dir 아래 */materials.json 을 이름 순으로 모두 (없는 폴더면 0개, 오류는 첫 실패)
    Expected<usize> loadAll(const std::filesystem::path& dir, AssetManager* assets);
    void set(std::string name, Material m) { m_materials[std::move(name)] = m; }

    // 없으면 대체 머티리얼 (흰색 + 이름 해시 색)
    [[nodiscard]] Material find(std::string_view name);
    [[nodiscard]] bool contains(std::string_view name) const { return m_materials.contains(std::string(name)); }
    [[nodiscard]] usize size() const noexcept { return m_materials.size(); }

    // 이름에서 고른 눈에 띄는 색 (불투명)
    [[nodiscard]] static u32 fallbackColor(std::string_view name) noexcept;

private:
    std::unordered_map<std::string, Material> m_materials;
    std::unordered_set<std::string> m_warned;
};

} // namespace sbx::render
