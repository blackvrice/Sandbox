#pragma once
// 아틀라스 한 층(page)의 선반(shelf) 패킹. docs/06-RENDERING.md 7.4, ADR-0020.
// 로드 시 하나씩 넣는다 (빌드 단계 패킹 도구 sbx_atlas 는 [계획]). 높이가 비슷한 스프라이트가 많을 때 낭비가 적다.
//   - 지금 선반에 맞으면 오른쪽에 붙인다
//   - 아니면 새 선반을 연다 (선반 높이 = 그 줄 첫 항목 높이)
// 지울 수 없다 (8A 는 스프라이트를 해제하지 않는다).

#include <optional>

#include "foundation/types/Types.hpp"

namespace sbx::render {

struct PackedRect {
    u32 x = 0;
    u32 y = 0;
};

class ShelfPacker {
public:
    ShelfPacker() = default;
    ShelfPacker(u32 width, u32 height) : m_width(width), m_height(height) {}

    // w×h 자리. 없으면 nullopt
    [[nodiscard]] std::optional<PackedRect> insert(u32 w, u32 h) {
        if (w == 0 || h == 0 || w > m_width || h > m_height) {
            return std::nullopt;
        }
        // 지금 선반: 높이가 맞고 오른쪽에 자리가 있으면
        if (m_shelfHeight > 0 && h <= m_shelfHeight && m_cursorX + w <= m_width) {
            const PackedRect r{m_cursorX, m_shelfY};
            m_cursorX += w;
            m_used += static_cast<u64>(w) * h;
            return r;
        }
        // 새 선반
        const u32 nextY = m_shelfY + m_shelfHeight;
        if (nextY + h > m_height) {
            return std::nullopt;
        }
        m_shelfY = nextY;
        m_shelfHeight = h;
        m_cursorX = w;
        m_used += static_cast<u64>(w) * h;
        return PackedRect{0, m_shelfY};
    }

    [[nodiscard]] u32 width() const noexcept { return m_width; }
    [[nodiscard]] u32 height() const noexcept { return m_height; }
    [[nodiscard]] u64 usedArea() const noexcept { return m_used; }

private:
    u32 m_width = 0;
    u32 m_height = 0;
    u32 m_shelfY = 0;
    u32 m_shelfHeight = 0;
    u32 m_cursorX = 0;
    u64 m_used = 0;
};

} // namespace sbx::render
