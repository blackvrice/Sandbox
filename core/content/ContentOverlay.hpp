#pragma once
// 월드 오버레이 — 이 월드가 콘텐츠 팩 위에 덮어쓴 Prefab · Rule · BehaviorGraph. docs/10-EDITOR.md 8장,
// docs/09-SERIALIZATION.md 3장, ADR-0030.
//
//   항목 = (종류, id) → 팩 파일과 같은 형식의 JSON 객체 하나 (Rule 도 배열이 아니라 객체 하나). "id" 는 항목 id 와
//   같다. 같은 id 가 팩에 있으면 그 항목을 **바꾸고**, 없으면 **더한다** (id 의 팩은 읽은 팩이어야 한다 — V1 · V2
//   그대로). 팩 원본 파일은 바꾸지 않는다. 태그 · 머티리얼은 오버레이로 바꿀 수 없다 (태그 비트 · 머티리얼 번호가
//   흔들리지 않게).
//
// 오버레이를 적용한 ContentDatabase 는 ContentLoader::applyOverlay 가 팩 원본(ContentSources) + 오버레이로 다시 만든다.
// contentHash 는 팩의 것 그대로 (접속 검사 · 리플레이 머리 — 오버레이는 서버가 ContentOverlay 로 내려준다). 오버레이의
// 지문(hash)은 WorldHash 에 들어간다 (04 5장 — 비어 있으면 넣지 않아 옛 골든이 그대로).

#include <array>
#include <map>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "foundation/types/Types.hpp"

namespace sbx::content {

enum class ContentKind : u8 { Prefab = 0, Rule = 1, Behavior = 2 };
inline constexpr u8 kContentKindCount = 3;

[[nodiscard]] std::string_view contentKindName(ContentKind k) noexcept; // "prefab" "rule" "behavior"
// 세이브 폴더 이름 · 팩 폴더 이름과 같다: "prefabs" "rules" "behaviors"
[[nodiscard]] std::string_view contentKindFolder(ContentKind k) noexcept;
[[nodiscard]] bool parseContentKind(std::string_view name, ContentKind& out) noexcept; // 단수 이름

inline constexpr usize kMaxOverlayItems = 1024;
inline constexpr usize kMaxOverlayItemBytes = 16 * 1024; // JSON 텍스트 (compact dump) — 명령 JSON 상한과 같다

class ContentOverlay {
public:
    using Items = std::map<std::string, nlohmann::json, std::less<>>; // id 정렬

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    [[nodiscard]] usize size() const noexcept;
    [[nodiscard]] const Items& items(ContentKind k) const noexcept { return m_items[static_cast<u8>(k)]; }
    [[nodiscard]] const nlohmann::json* find(ContentKind k, std::string_view id) const noexcept;
    // 값은 검사하지 않는다 (ContentLoader::applyOverlay 가 본다)
    void set(ContentKind k, std::string id, nlohmann::json value);
    bool erase(ContentKind k, std::string_view id);

    // 지문 — 종류 순, id 순, compact dump(키 정렬). 비어 있으면 0
    [[nodiscard]] u64 hash() const;

    friend bool operator==(const ContentOverlay& a, const ContentOverlay& b) noexcept { return a.m_items == b.m_items; }

private:
    std::array<Items, kContentKindCount> m_items;
};

} // namespace sbx::content
