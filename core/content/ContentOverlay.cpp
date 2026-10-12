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
        h.byte(i).u64le(m_items[i].size());
        for (const auto& [id, value] : m_items[i]) {
            h.string(id).byte(0).string(value.dump()).byte(0);
        }
    }
    return h.value();
}

} // namespace sbx::content
