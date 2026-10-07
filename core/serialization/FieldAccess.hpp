#pragma once
// 이름으로 컴포넌트 필드 하나에 접근하는 Visitor. Rule 조건·효과(field.add/set)와 콘텐츠 검증기가 쓴다.
// docs/03-SIMULATION.md 6.3, docs/11-CONTENT-SCHEMA.md 3장.
//
// 수치 필드 = bool 이 아닌 정수·실수·enum 제외. 쓰기는 FieldMeta 범위로 자르고, 정수 필드는 가장 가까운 정수로 반올림한
// 뒤 타입 범위로 자른다 (NaN 은 쓰지 않는다).

#include <cmath>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

#include "core/ecs/Reflection.hpp"

namespace sbx::ecs {

template <class F>
inline constexpr bool kNumericField = std::is_arithmetic_v<F> && !std::is_same_v<F, bool>;

struct FieldDesc {
    std::string_view name; // reflect 의 문자열 리터럴 — 정적 수명
    Hint hint = Hint::None;
    bool numeric = false;
    FieldMeta meta{};
};

class FieldListVisitor {
public:
    explicit FieldListVisitor(std::vector<FieldDesc>& out) : m_out(out) {}
    template <class F>
    void field(std::string_view name, F&, Hint hint = Hint::None, const FieldMeta& meta = {}) {
        m_out.push_back(FieldDesc{name, hint, kNumericField<F>, meta});
    }

private:
    std::vector<FieldDesc>& m_out;
};

class NumberGetVisitor {
public:
    explicit NumberGetVisitor(std::string_view name) : m_name(name) {}
    template <class F>
    void field(std::string_view name, F& v, Hint = Hint::None, const FieldMeta& = {}) {
        if constexpr (kNumericField<F>) {
            if (name == m_name) {
                result = static_cast<f64>(v);
            }
        }
    }
    std::optional<f64> result;

private:
    std::string_view m_name;
};

class NumberSetVisitor {
public:
    NumberSetVisitor(std::string_view name, f64 value, bool add) : m_name(name), m_value(value), m_add(add) {}
    template <class F>
    void field(std::string_view name, F& v, Hint = Hint::None, const FieldMeta& meta = {}) {
        if constexpr (kNumericField<F>) {
            if (name != m_name || std::isnan(m_value)) {
                return;
            }
            f64 x = m_add ? static_cast<f64>(v) + m_value : m_value;
            if (meta.hasRange) {
                x = std::fmin(std::fmax(x, meta.min), meta.max);
            }
            if constexpr (std::is_integral_v<F>) {
                x = std::nearbyint(x);
                x = std::fmin(std::fmax(x, static_cast<f64>(std::numeric_limits<F>::lowest())),
                              static_cast<f64>(std::numeric_limits<F>::max()));
            }
            v = static_cast<F>(x);
            done = true;
        }
    }
    bool done = false;

private:
    std::string_view m_name;
    f64 m_value;
    bool m_add;
};

} // namespace sbx::ecs
