#pragma once
// Hierarchy 검색 · 편집기 그룹 (v0.12 ST-06-6, ADR-0162). Studio 서비스 — ImGui · render 를 모른다 (ADR-0089).
//
// 검색 (공백으로 나눈 낱말 — 모두 맞아야 한다, 대소문자 무시):
//   낱말            Prefab id 에 들어 있다 (없는 개체는 "개체")      "rabbit"
//   #<번호>         그 netId                                          "#42"
//   prefab:<id>     Prefab id 가 그것으로 시작한다                    "prefab:eco."
//   tag:<이름>      그 태그가 있다 (콘텐츠 태그 표)                   "tag:prey"
//   group:<이름>    그 그룹의 개체 (이름에 들어 있다)                 "group:wolves"
//
// 그룹 = 편집기 전용 논리 묶음 — 시뮬레이션 · 해시 · 복제 밖. 개체는 saveId 로 기억한다 (세이브를 넘어 같다 — netId 는
// 아니다). 저장: 세이브 폴더의 editor.json {"format": "sandbox.editor", "version": 1, "groups": [{"name", "members":
// [saveId…], "collapsed"}]} — Studio 가 로컬 서버의 저장 · 불러오기 때 쓰고 읽는다. 개체 하나는 그룹 하나에만 (더하면
// 옮긴다).

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/components/core/Identity.hpp"
#include "core/content/TagSet.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::content {
class ContentDatabase;
}

namespace sbx::editor {

inline constexpr usize kMaxEditorGroups = 256;
inline constexpr usize kMaxGroupMembers = 65536;
inline constexpr usize kMaxGroupNameBytes = 64;

struct EditorGroup {
    std::string name;
    std::vector<SaveId> members; // 오름차순
    bool collapsed = false;
    friend bool operator==(const EditorGroup&, const EditorGroup&) = default;
};

class EditorGroups {
public:
    // 이름이 비었거나 길거나 겹치면 · 상한이면 false
    bool create(std::string name, std::span<const SaveId> members);
    bool rename(std::string_view from, std::string to);
    bool remove(std::string_view name);
    // 개체들을 그 그룹으로 (다른 그룹에 있으면 옮긴다). 없는 그룹이면 false
    bool add(std::string_view name, std::span<const SaveId> members);
    void removeMembers(std::span<const SaveId> members); // 어느 그룹에서든 뺀다
    bool setCollapsed(std::string_view name, bool collapsed);
    // 지금 월드에 없는 개체를 뺀다 (지워진 개체) — 남은 수
    usize prune(std::span<const SaveId> alive);

    [[nodiscard]] const std::vector<EditorGroup>& groups() const noexcept { return m_groups; }
    [[nodiscard]] const EditorGroup* find(std::string_view name) const noexcept;
    [[nodiscard]] const EditorGroup* groupOf(SaveId id) const noexcept;
    [[nodiscard]] bool empty() const noexcept { return m_groups.empty(); }
    void clear() noexcept { m_groups.clear(); }

    [[nodiscard]] std::string toJson() const;
    // 틀린 파일은 오류 (그룹은 그대로)
    Expected<void> fromJson(std::string_view text);

private:
    std::vector<EditorGroup> m_groups; // 만든 차례
};

// 목록의 한 줄 (Hierarchy 패널이 그린다)
struct HierarchyRow {
    NetEntityId id = kInvalidNetEntityId;
    SaveId saveId = kInvalidSaveId; // 모르면 무효 (원격 접속 — 그룹 없음)
    std::string_view prefab;
    content::TagSet tags;
};
struct HierarchyLine {
    enum class Kind : u8 { Group, Entity };
    Kind kind = Kind::Entity;
    usize group = 0; // Group · 그룹 안의 Entity — EditorGroups::groups() 의 번호
    usize row = 0;   // Entity — rows 의 번호
    bool inGroup = false;
    usize matches = 0; // Group — 검색에 맞은 개체 수 (접혀 있어도)
};

struct HierarchyQuery {
    std::vector<std::string> words; // 소문자
    std::vector<NetEntityId> ids;
    std::vector<std::string> prefixes;
    std::vector<content::TagIndex> tags;
    std::vector<std::string> unknownTags; // 콘텐츠에 없는 태그 — 아무것도 맞지 않는다
    std::vector<std::string> groups;
    [[nodiscard]] bool empty() const noexcept {
        return words.empty() && ids.empty() && prefixes.empty() && tags.empty() && unknownTags.empty() &&
               groups.empty();
    }
};

[[nodiscard]] HierarchyQuery parseHierarchyQuery(std::string_view text, const content::ContentDatabase* content);
[[nodiscard]] bool matchesHierarchyQuery(const HierarchyQuery& q, const HierarchyRow& row, const EditorGroup* group);

// 그룹 (만든 차례 — 머리 줄, 펼쳐져 있으면 맞은 개체) 다음에 그룹 없는 개체 (rows 차례). 검색이 있으면 맞은 것이 없는
// 그룹은 뺀다. rows 는 saveId 로 그룹을 찾는다
void buildHierarchy(std::span<const HierarchyRow> rows, const EditorGroups& groups, const HierarchyQuery& query,
                    std::vector<HierarchyLine>& out);

} // namespace sbx::editor
