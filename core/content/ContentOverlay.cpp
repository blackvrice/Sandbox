#include "core/content/ContentOverlay.hpp"

#include "foundation/hash/Fnv1a.hpp"

namespace sbx::content {

std::string_view contentKindName(ContentKind k) noexcept {
    switch (k) {
    case ContentKind::Prefab:
        return "prefab";
    case ContentKind::Rule:
        return "rule";
    case ContentKind::Behavior:
        return "behavior";
    case ContentKind::Trigger:
        return "trigger";
    case ContentKind::Script:
        return "script";
    }
    return "?";
}

std::string_view contentKindFolder(ContentKind k) noexcept {
    switch (k) {
    case ContentKind::Prefab:
        return "prefabs";
    case ContentKind::Rule:
        return "rules";
    case ContentKind::Behavior:
        return "behaviors";
    case ContentKind::Trigger:
        return "triggers";
    case ContentKind::Script:
        return "scripts";
    }
    return "?";
}

bool parseContentKind(std::string_view name, ContentKind& out) noexcept {
    for (u8 i = 0; i < kContentKindCount; ++i) {
        const auto k = static_cast<ContentKind>(i);
        if (contentKindName(k) == name) {
            out = k;
            return true;
        }
    }
    return false;
}

namespace {

constexpr u8 kFirstOptionalHashKind = static_cast<u8>(ContentKind::Script);

[[nodiscard]] bool isSegment(std::string_view s) noexcept {
    if (s.empty()) {
        return false;
    }
    for (const char c : s) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<std::string> scriptOverlayId(std::string_view packId, std::string_view rel) {
    constexpr std::string_view kPrefix = "scripts/";
    constexpr std::string_view kSuffix = ".ts";
    if (!isSegment(packId) || !rel.starts_with(kPrefix) || !rel.ends_with(kSuffix) ||
        rel.size() <= kPrefix.size() + kSuffix.size()) {
        return std::nullopt;
    }
    std::string id(packId);
    std::string_view rest = rel.substr(kPrefix.size(), rel.size() - kPrefix.size() - kSuffix.size());
    while (true) {
        const usize slash = rest.find('/');
        const std::string_view seg = rest.substr(0, slash);
        if (!isSegment(seg)) {
            return std::nullopt;
        }
        id += '.';
        id += seg;
        if (slash == std::string_view::npos) {
            break;
        }
        rest = rest.substr(slash + 1);
    }
    return id;
}

std::optional<ScriptOverlayPath> scriptOverlayPath(std::string_view id) {
    const usize dot = id.find('.');
    if (dot == std::string_view::npos || !isSegment(id.substr(0, dot))) {
        return std::nullopt;
    }
    ScriptOverlayPath p{std::string(id.substr(0, dot)), "scripts/"};
    std::string_view rest = id.substr(dot + 1);
    while (true) {
        const usize next = rest.find('.');
        const std::string_view seg = rest.substr(0, next);
        if (!isSegment(seg)) {
            return std::nullopt;
        }
        p.rel += seg;
        if (next == std::string_view::npos) {
            break;
        }
        p.rel += '/';
        rest = rest.substr(next + 1);
    }
    p.rel += ".ts";
    return p;
}

usize ContentOverlay::size() const noexcept {
    usize n = 0;
    for (const Items& items : m_items) {
        n += items.size();
    }
    return n;
}

const nlohmann::json* ContentOverlay::find(ContentKind k, std::string_view id) const noexcept {
    const Items& items = m_items[static_cast<u8>(k)];
    const auto it = items.find(id);
    return it != items.end() ? &it->second : nullptr;
}

void ContentOverlay::set(ContentKind k, std::string id, nlohmann::json value) {
    m_items[static_cast<u8>(k)].insert_or_assign(std::move(id), std::move(value));
}

bool ContentOverlay::erase(ContentKind k, std::string_view id) {
    Items& items = m_items[static_cast<u8>(k)];
    const auto it = items.find(id);
    if (it == items.end()) {
        return false;
    }
    items.erase(it);
    return true;
}

u64 ContentOverlay::hash() const {
    if (empty()) {
        return 0;
    }
    Fnv1a64 h;
    h.string("SBXOVERLAY1");
    for (u8 i = 0; i < kContentKindCount; ++i) {
        // v0.2 의 넷 (Prefab ~ Trigger) 다음 종류는 비어 있으면 넣지 않는다 — 스크립트 없는 오버레이의 옛 지문이 그대로
        // (세이브의 worldHash 비교)
        if (i >= kFirstOptionalHashKind && m_items[i].empty()) {
            continue;
        }
        h.byte(i).u64le(m_items[i].size());
        for (const auto& [id, value] : m_items[i]) {
            h.string(id).byte(0).string(value.dump()).byte(0);
        }
    }
    return h.value();
}

} // namespace sbx::content
