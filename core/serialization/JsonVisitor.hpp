#pragma once
// 리플렉션 ↔ JSON. docs/09-SERIALIZATION.md, docs/11-CONTENT-SCHEMA.md 2장.
//
// 쓰기: 모든 필드를 이름 그대로 객체 키로.
// 읽기: 없는 키 = 기본값 유지 (Prefab 은 바꾸고 싶은 필드만 적는다 — 11 P2)
//       타입 불일치 · FieldMeta 범위 밖 · 모르는 키 = 오류 (11 P3, V3). 오류 문맥은 "<context>.<field>".
// float 은 double 로 기록한다. float→double→float 은 비트가 보존된다 (세이브 왕복 D2 의 전제).
// [Phase 5] Hint::EntityRef 는 saveId 로 기록·재연결하게 된다. 지금은 EntityId.raw 를 그대로 쓴다.

#include <format>
#include <limits>
#include <set>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "core/ecs/Reflection.hpp"
#include "core/serialization/FieldCodec.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::ecs {

using Json = nlohmann::json;

namespace json_detail {

template <class F>
Json encode(const F& v) {
    static_assert(codec::Supported<F>, "JSON: 지원하지 않는 필드 타입 (core/serialization/FieldCodec.hpp)");
    if constexpr (std::is_same_v<F, bool>) {
        return Json(v);
    } else if constexpr (std::is_floating_point_v<F>) {
        return Json(static_cast<f64>(v));
    } else if constexpr (std::is_enum_v<F>) {
        return Json(static_cast<std::underlying_type_t<F>>(v));
    } else if constexpr (std::is_integral_v<F>) {
        return Json(v);
    } else if constexpr (std::is_same_v<F, Vec2>) {
        return Json::array({static_cast<f64>(v.x), static_cast<f64>(v.y)});
    } else if constexpr (std::is_same_v<F, Vec2i>) {
        return Json::array({v.x, v.y});
    } else if constexpr (std::is_same_v<F, EntityId>) {
        return Json(v.raw);
    } else if constexpr (codec::IsFixedString<F>::value) {
        return Json(std::string(v.view()));
    } else if constexpr (codec::Sequence<F>) {
        Json arr = Json::array();
        for (const auto& e : v) {
            arr.push_back(encode(e));
        }
        return arr;
    }
}

inline std::unexpected<Error> typeError(std::string_view ctx, std::string_view expected, const Json& got) {
    return makeError(ErrorCode::ParseError, std::format("{} 이(가) 필요하지만 {} 입니다", expected, got.type_name()),
                     std::string(ctx));
}

template <class I>
Expected<void> decodeInteger(const Json& j, I& out, std::string_view ctx) {
    if (!j.is_number_integer()) {
        return typeError(ctx, "정수", j);
    }
    if constexpr (std::is_signed_v<I>) {
        const auto v = j.get<i64>();
        if (j.is_number_unsigned() && j.get<u64>() > static_cast<u64>(std::numeric_limits<I>::max())) {
            return makeError(ErrorCode::OutOfRange, "정수 범위 초과", std::string(ctx));
        }
        if (v < static_cast<i64>(std::numeric_limits<I>::min()) ||
            v > static_cast<i64>(std::numeric_limits<I>::max())) {
            return makeError(ErrorCode::OutOfRange, "정수 범위 초과", std::string(ctx));
        }
        out = static_cast<I>(v);
    } else {
        // 코드에서 만든 Json(100) 은 부호 있는 정수로 저장된다 (파서는 양수를 unsigned 로 만든다). 음수만 거절한다.
        if (!j.is_number_unsigned() && j.get<i64>() < 0) {
            return makeError(ErrorCode::OutOfRange, "음수를 부호 없는 필드에 넣을 수 없습니다", std::string(ctx));
        }
        const auto v = j.is_number_unsigned() ? j.get<u64>() : static_cast<u64>(j.get<i64>());
        if (v > static_cast<u64>(std::numeric_limits<I>::max())) {
            return makeError(ErrorCode::OutOfRange, "정수 범위 초과", std::string(ctx));
        }
        out = static_cast<I>(v);
    }
    return {};
}

template <class F>
Expected<void> decode(const Json& j, F& out, std::string_view ctx) {
    static_assert(codec::Supported<F>, "JSON: 지원하지 않는 필드 타입 (core/serialization/FieldCodec.hpp)");
    if constexpr (std::is_same_v<F, bool>) {
        if (!j.is_boolean()) {
            return typeError(ctx, "bool", j);
        }
        out = j.get<bool>();
    } else if constexpr (std::is_floating_point_v<F>) {
        if (!j.is_number()) {
            return typeError(ctx, "숫자", j);
        }
        out = static_cast<F>(j.get<f64>());
    } else if constexpr (std::is_enum_v<F>) {
        std::underlying_type_t<F> raw{};
        if (auto r = decodeInteger(j, raw, ctx); !r) {
            return r;
        }
        out = static_cast<F>(raw);
    } else if constexpr (std::is_integral_v<F>) {
        return decodeInteger(j, out, ctx);
    } else if constexpr (std::is_same_v<F, Vec2> || std::is_same_v<F, Vec2i>) {
        if (!j.is_array() || j.size() != 2) {
            return typeError(ctx, "[x, y] 배열", j);
        }
        if (auto r = decode(j[0], out.x, ctx); !r) {
            return r;
        }
        return decode(j[1], out.y, ctx);
    } else if constexpr (std::is_same_v<F, EntityId>) {
        u64 raw = 0;
        if (auto r = decodeInteger(j, raw, ctx); !r) {
            return r;
        }
        out = EntityId{raw};
    } else if constexpr (codec::IsFixedString<F>::value) {
        if (!j.is_string()) {
            return typeError(ctx, "문자열", j);
        }
        if (!out.assign(j.get_ref<const std::string&>())) {
            return makeError(ErrorCode::OutOfRange, std::format("문자열이 {}바이트를 넘는다", F::kCapacity),
                             std::string(ctx));
        }
    } else if constexpr (codec::IsStdArray<F>::value) {
        if (!j.is_array() || j.size() != out.size()) {
            return typeError(ctx, std::format("길이 {} 배열", out.size()), j);
        }
        for (usize i = 0; i < out.size(); ++i) {
            if (auto r = decode(j[i], out[i], std::format("{}[{}]", ctx, i)); !r) {
                return r;
            }
        }
    } else if constexpr (codec::IsSmallVector<F>::value) {
        if (!j.is_array()) {
            return typeError(ctx, "배열", j);
        }
        out.clear();
        for (usize i = 0; i < j.size(); ++i) {
            typename F::value_type e{};
            if (auto r = decode(j[i], e, std::format("{}[{}]", ctx, i)); !r) {
                return r;
            }
            out.push_back(std::move(e));
        }
    }
    return {};
}

template <class F>
Expected<void> checkRange(const F& v, const FieldMeta& meta, std::string_view ctx) {
    if constexpr (std::is_arithmetic_v<F> && !std::is_same_v<F, bool>) {
        if (meta.hasRange) {
            const auto x = static_cast<f64>(v);
            if (!(x >= meta.min && x <= meta.max)) {
                return makeError(ErrorCode::ValidationFailed,
                                 std::format("값 {} 이(가) 허용 범위 [{}, {}] 밖입니다", x, meta.min, meta.max),
                                 std::string(ctx));
            }
        }
    }
    return {};
}

} // namespace json_detail

class JsonWriter {
public:
    explicit JsonWriter(Json& out) : m_out(out) {
        if (!m_out.is_object()) {
            m_out = Json::object();
        }
    }
    template <class F>
    void field(std::string_view name, F& value, Hint = Hint::None, const FieldMeta& = {}) {
        m_out[std::string(name)] = json_detail::encode(value);
    }

private:
    Json& m_out;
};

class JsonReader {
public:
    JsonReader(const Json& in, std::string_view context) : m_in(in), m_context(context) {}

    template <class F>
    void field(std::string_view name, F& value, Hint = Hint::None, const FieldMeta& meta = {}) {
        m_known.emplace(name);
        if (!m_result) {
            return; // 첫 오류에서 멈춘다
        }
        const auto it = m_in.find(std::string(name));
        if (it == m_in.end()) {
            return; // 없는 키 = 기본값 유지
        }
        const std::string ctx = std::format("{}.{}", m_context, name);
        F tmp = value;
        if (auto r = json_detail::decode(*it, tmp, ctx); !r) {
            m_result = std::move(r);
            return;
        }
        if (auto r = json_detail::checkRange(tmp, meta, ctx); !r) {
            m_result = std::move(r);
            return;
        }
        value = std::move(tmp);
    }

    // reflect 를 마친 뒤 호출: 모르는 키가 있으면 오류
    [[nodiscard]] Expected<void> finish() {
        if (!m_result) {
            return m_result;
        }
        for (const auto& [key, _] : m_in.items()) {
            if (!m_known.contains(key)) {
                return makeError(ErrorCode::ValidationFailed, std::format("모르는 필드 '{}'", key),
                                 std::string(m_context));
            }
        }
        return {};
    }

private:
    const Json& m_in;
    std::string m_context;
    std::set<std::string, std::less<>> m_known;
    Expected<void> m_result{};
};

template <Reflectable T>
Json componentToJson(const T& component) {
    Json out = Json::object();
    JsonWriter w(out);
    visitConst(w, component);
    return out;
}

// 실패하면 component 는 일부 필드만 바뀐 상태일 수 있다 — 호출자는 실패 시 결과를 버린다.
template <Reflectable T>
Expected<void> componentFromJson(T& component, const Json& in, std::string_view context) {
    if (!in.is_object()) {
        return json_detail::typeError(context, "객체", in);
    }
    JsonReader r(in, context);
    reflect(r, component);
    return r.finish();
}

} // namespace sbx::ecs
