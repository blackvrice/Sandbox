#include "editor/services/HierarchyService.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "core/content/ContentDatabase.hpp"

namespace sbx::editor {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

bool containsLower(std::string_view haystack, std::string_view needleLower) {
    if (needleLower.size() > haystack.size()) {
        return false;
    }
    for (usize i = 0; i + needleLower.size() <= haystack.size(); ++i) {
        bool ok = true;
        for (usize k = 0; k < needleLower.size() && ok; ++k) {
            char c = haystack[i + k];
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
            ok = c == needleLower[k];
        }
        if (ok) {
            return true;
        }
    }
    return false;
}

bool validName(std::string_view name) {
    return !name.empty() && name.size() <= kMaxGroupNameBytes;
}

void sortUnique(std::vector<SaveId>& v) {
    std::ranges::sort(v);
    v.erase(std::unique(v.begin(), v.end()), v.end());
}

} // namespace

// ---- 그룹 ----------------------------------------------------------------------------------------------------------

const EditorGroup* EditorGroups::find(std::string_view name) const noexcept {
    const auto it = std::ranges::find(m_groups, name, &EditorGroup::name);
    return it != m_groups.end() ? &*it : nullptr;
}

const EditorGroup* EditorGroups::groupOf(SaveId id) const noexcept {
    for (const EditorGroup& g : m_groups) {
        if (std::ranges::binary_search(g.members, id)) {
            return &g;
        }
    }
    return nullptr;
}

bool EditorGroups::create(std::string name, std::span<const SaveId> members) {
    if (!validName(name) || find(name) != nullptr || m_groups.size() >= kMaxEditorGroups) {
        return false;
    }
    m_groups.push_back({std::move(name), {}, false});
    return add(m_groups.back().name, members);
}

bool EditorGroups::rename(std::string_view from, std::string to) {
    const auto it = std::ranges::find(m_groups, from, &EditorGroup::name);
    if (it == m_groups.end() || !validName(to) || (to != from && find(to) != nullptr)) {
        return false;
    }
    it->name = std::move(to);
    return true;
}

bool EditorGroups::remove(std::string_view name) {
    const auto it = std::ranges::find(m_groups, name, &EditorGroup::name);
    if (it == m_groups.end()) {
        return false;
    }
    m_groups.erase(it);
    return true;
}

bool EditorGroups::add(std::string_view name, std::span<const SaveId> members) {
    const auto it = std::ranges::find(m_groups, name, &EditorGroup::name);
    if (it == m_groups.end()) {
        return false;
    }
    const std::string target = it->name;
    removeMembers(members); // 개체 하나는 그룹 하나에만
    EditorGroup& g = *std::ranges::find(m_groups, target, &EditorGroup::name);
    for (const SaveId id : members) {
        if (id != kInvalidSaveId && g.members.size() < kMaxGroupMembers) {
            g.members.push_back(id);
        }
    }
    sortUnique(g.members);
    return true;
}

void EditorGroups::removeMembers(std::span<const SaveId> members) {
    std::vector<SaveId> drop(members.begin(), members.end());
    sortUnique(drop);
    for (EditorGroup& g : m_groups) {
        std::erase_if(g.members, [&](SaveId id) { return std::ranges::binary_search(drop, id); });
    }
}

bool EditorGroups::setCollapsed(std::string_view name, bool collapsed) {
    const auto it = std::ranges::find(m_groups, name, &EditorGroup::name);
    if (it == m_groups.end()) {
        return false;
    }
    it->collapsed = collapsed;
    return true;
}

usize EditorGroups::prune(std::span<const SaveId> alive) {
    std::vector<SaveId> keep(alive.begin(), alive.end());
    sortUnique(keep);
    usize n = 0;
    for (EditorGroup& g : m_groups) {
        std::erase_if(g.members, [&](SaveId id) { return !std::ranges::binary_search(keep, id); });
        n += g.members.size();
    }
    return n;
}

std::string EditorGroups::toJson() const {
    nlohmann::json groups = nlohmann::json::array();
    for (const EditorGroup& g : m_groups) {
        groups.push_back({{"name", g.name}, {"members", g.members}, {"collapsed", g.collapsed}});
    }
    return nlohmann::json{{"format", "sandbox.editor"}, {"version", 1}, {"groups", std::move(groups)}}.dump(2) + "\n";
}

Expected<void> EditorGroups::fromJson(std::string_view text) {
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    const auto bad = [](std::string why) { return makeError(ErrorCode::ParseError, "editor.json: " + std::move(why)); };
    if (j.is_discarded() || !j.is_object() || j.value("format", "") != "sandbox.editor") {
        return bad("{\"format\": \"sandbox.editor\", …} 가 아니다");
    }
    if (!j.contains("version") || !j["version"].is_number_integer() || j["version"].get<int>() != 1) {
        return bad("version 1 만 읽는다");
    }
    EditorGroups next;
    if (const auto g = j.find("groups"); g != j.end()) {
        if (!g->is_array() || g->size() > kMaxEditorGroups) {
            return bad(std::format("groups 는 배열 ({} 개까지)", kMaxEditorGroups));
        }
        for (const auto& item : *g) {
            if (!item.is_object() || !item.contains("name") || !item["name"].is_string() || !item.contains("members") ||
                !item["members"].is_array() || item["members"].size() > kMaxGroupMembers) {
                return bad("그룹은 {\"name\": 문자열, \"members\": [saveId…], \"collapsed\": bool}");
            }
            std::vector<SaveId> members;
            for (const auto& m : item["members"]) {
                if (!m.is_number_unsigned()) {
                    return bad("members 는 saveId (0 이상의 정수)");
                }
                members.push_back(m.get<SaveId>());
            }
            const std::string name = item["name"].get<std::string>();
            if (!next.create(name, members)) {
                return bad(std::format("그룹 이름 '{}' 이 비었거나 길거나 겹친다", name));
            }
            (void)next.setCollapsed(name, item.value("collapsed", false));
        }
    }
    *this = std::move(next);
    return {};
}

// ---- 검색 ----------------------------------------------------------------------------------------------------------

HierarchyQuery parseHierarchyQuery(std::string_view text, const content::ContentDatabase* content) {
    HierarchyQuery q;
    usize i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
            ++i;
        }
        const usize start = i;
        while (i < text.size() && text[i] != ' ' && text[i] != '\t') {
            ++i;
        }
        const std::string_view word = text.substr(start, i - start);
        if (word.empty()) {
            continue;
        }
        if (word.size() > 1 && word.front() == '#') {
            u64 id = 0;
            const auto [p, ec] = std::from_chars(word.data() + 1, word.data() + word.size(), id);
            if (ec == std::errc{} && p == word.data() + word.size()) {
                q.ids.push_back(static_cast<NetEntityId>(id));
                continue;
            }
        }
        const std::string lw = lower(word);
        if (lw.starts_with("prefab:") && lw.size() > 7) {
            q.prefixes.push_back(lw.substr(7));
        } else if (lw.starts_with("tag:") && lw.size() > 4) {
            const std::string tag(word.substr(4));
            const auto t = content != nullptr ? content->findTag(tag) : std::nullopt;
            if (t) {
                q.tags.push_back(*t);
            } else {
                q.unknownTags.push_back(tag);
            }
        } else if (lw.starts_with("group:") && lw.size() > 6) {
            q.groups.push_back(lw.substr(6));
        } else {
            q.words.push_back(lw);
        }
    }
    return q;
}

bool matchesHierarchyQuery(const HierarchyQuery& q, const HierarchyRow& row, const EditorGroup* group) {
    if (!q.unknownTags.empty()) {
        return false;
    }
    const std::string_view prefab = row.prefab.empty() ? std::string_view("개체") : row.prefab;
    for (const std::string& w : q.words) {
        if (!containsLower(prefab, w)) {
            return false;
        }
    }
    for (const NetEntityId id : q.ids) {
        if (row.id != id) {
            return false;
        }
    }
    for (const std::string& p : q.prefixes) {
        if (p.size() > row.prefab.size() || !containsLower(row.prefab.substr(0, p.size()), p)) {
            return false;
        }
    }
    for (const content::TagIndex t : q.tags) {
        if (!row.tags.test(t)) {
            return false;
        }
    }
    for (const std::string& g : q.groups) {
        if (group == nullptr || !containsLower(group->name, g)) {
            return false;
        }
    }
    return true;
}

void buildHierarchy(std::span<const HierarchyRow> rows, const EditorGroups& groups, const HierarchyQuery& query,
                    std::vector<HierarchyLine>& out) {
    out.clear();
    const std::vector<EditorGroup>& gs = groups.groups();
    // saveId → 그룹 번호 (한 번 — 개체 하나는 그룹 하나). 10k 개체 · 그룹 50 은 14 의 측정
    std::unordered_map<SaveId, usize> groupOf;
    usize members = 0;
    for (const EditorGroup& g : gs) {
        members += g.members.size();
    }
    groupOf.reserve(members);
    for (usize g = 0; g < gs.size(); ++g) {
        for (const SaveId id : gs[g].members) {
            groupOf.emplace(id, g);
        }
    }
    std::vector<std::vector<usize>> inGroup(gs.size());
    std::vector<usize> loose;
    loose.reserve(rows.size());
    for (usize r = 0; r < rows.size(); ++r) {
        usize found = gs.size();
        if (rows[r].saveId != kInvalidSaveId) {
            if (const auto it = groupOf.find(rows[r].saveId); it != groupOf.end()) {
                found = it->second;
            }
        }
        const EditorGroup* group = found < gs.size() ? &gs[found] : nullptr;
        if (!query.empty() && !matchesHierarchyQuery(query, rows[r], group)) {
            continue;
        }
        (found < gs.size() ? inGroup[found] : loose).push_back(r);
    }
    for (usize g = 0; g < gs.size(); ++g) {
        if (!query.empty() && inGroup[g].empty()) {
            continue; // 검색에 맞은 것이 없는 그룹은 뺀다
        }
        out.push_back({HierarchyLine::Kind::Group, g, 0, false, inGroup[g].size()});
        if (!gs[g].collapsed) {
            for (const usize r : inGroup[g]) {
                out.push_back({HierarchyLine::Kind::Entity, g, r, true, 0});
            }
        }
    }
    for (const usize r : loose) {
        out.push_back({HierarchyLine::Kind::Entity, 0, r, false, 0});
    }
}

} // namespace sbx::editor
