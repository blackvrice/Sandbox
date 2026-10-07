#pragma once
// 고정 용량 문자열 (힙 할당 없음). 컴포넌트 필드에 콘텐츠 id ("eco.rabbit") 같은 짧은 이름을 담는다.
//
// - 용량 N-1 바이트 + 길이. 넘치는 대입은 실패를 돌려준다 (잘라내지 않는다 — id 가 조용히 바뀌면 안 된다).
// - 비교·해시는 바이트 기준. UTF-8 을 그대로 담지만 콘텐츠 id 는 ASCII 만 쓴다 (11-CONTENT-SCHEMA 1.1).

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include "foundation/types/Types.hpp"

namespace sbx {

template <usize N>
class FixedString {
    static_assert(N >= 2 && N <= 256, "FixedString 용량은 2~256");

public:
    static constexpr usize kCapacity = N - 1;

    constexpr FixedString() noexcept = default;

    // 용량을 넘으면 false 이고 내용은 바뀌지 않는다
    constexpr bool assign(std::string_view s) noexcept {
        if (s.size() > kCapacity) {
            return false;
        }
        std::fill(m_data.begin(), m_data.end(), '\0');
        std::copy(s.begin(), s.end(), m_data.begin());
        m_size = static_cast<u8>(s.size());
        return true;
    }
    // 컴파일 시점 리터럴 (넘치면 컴파일 오류가 아니라 단언 — constexpr 문맥이면 컴파일 오류)
    static constexpr FixedString from(std::string_view s) noexcept {
        FixedString f;
        const bool ok = f.assign(s);
        (void)ok;
        return f;
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept { return {m_data.data(), m_size}; }
    [[nodiscard]] std::string str() const { return std::string(view()); }
    [[nodiscard]] constexpr usize size() const noexcept { return m_size; }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0; }

    friend constexpr bool operator==(const FixedString& a, const FixedString& b) noexcept {
        return a.view() == b.view();
    }
    friend constexpr bool operator==(const FixedString& a, std::string_view b) noexcept { return a.view() == b; }

private:
    std::array<char, N> m_data{}; // 남는 바이트는 항상 0 — 객체 비교·복사가 결정적
    u8 m_size = 0;
};

// 콘텐츠 id 필드 (Prefab · Behavior 참조). 48 바이트면 "<pack>.<name>" 에 충분하다.
using ContentId = FixedString<48>;

} // namespace sbx
