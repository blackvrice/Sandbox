#pragma once
// 리플렉션 ↔ 바이트 (컴포넌트 값의 와이어 형식 — 복제, docs/08-NETWORK.md 6장 · docs/09-SERIALIZATION.md 5장, Phase
// 10).
//
// 바이트 정렬 (비트스트림 안에 길이 붙은 바이트열로 들어간다 — 모르는 컴포넌트는 건너뛸 수 있게):
//   bool 1 바이트 · 부호 없는 정수 LEB128 · 부호 있는 정수 zigzag LEB128 · enum = 바탕 타입 규칙
//   f32 4 바이트 / f64 8 바이트 리틀 엔디안 IEEE 비트 그대로 (양자화 없음 — 서버 값과 비트 단위로 같다)
//   Vec2 = f32 둘 · Vec2i = zigzag 둘 · FixedString = 길이 + 바이트 · std::array = 원소 N 개 · SmallVector = 개수 +
//   원소 EntityId = EntityRefCodec 로 바꾼 u64 (복제: NetEntityId — 모르는 대상이면 0 → 받는 쪽은 null)
// 읽기 오류(끝 넘음 · 상한 · 너무 긴 varint)는 플래그 — 예외 없음. 실패하면 component 가 일부만 바뀌었을 수 있다.
// [계획] FieldMeta.quantizeBits 양자화 (측정 뒤, 08 6.5).

#include <bit>
#include <cstring>
#include <limits>
#include <string_view>
#include <type_traits>
#include <vector>

#include "core/ecs/Reflection.hpp"
#include "core/serialization/FieldCodec.hpp"

namespace sbx::ecs {

// EntityId 필드 ↔ 와이어 값 (복제는 NetEntityId). null 이면 둘 다 0 · kNullEntity
class EntityRefCodec {
public:
    EntityRefCodec() = default;
    EntityRefCodec(const EntityRefCodec&) = default;
    EntityRefCodec& operator=(const EntityRefCodec&) = default;
    EntityRefCodec(EntityRefCodec&&) = default;
    EntityRefCodec& operator=(EntityRefCodec&&) = default;
    virtual ~EntityRefCodec() = default;
    [[nodiscard]] virtual u64 toWire(EntityId e) const = 0;
    [[nodiscard]] virtual EntityId fromWire(u64 v) const = 0;
};

inline constexpr usize kMaxBinarySequence = 4096;

class ByteWriter {
public:
    explicit ByteWriter(std::vector<u8>& out) noexcept : m_out(out) {}
    void u8v(u8 v) { m_out.push_back(v); }
    void varU(u64 v) {
        while (v >= 0x80) {
            m_out.push_back(static_cast<u8>((v & 0x7F) | 0x80));
            v >>= 7;
        }
        m_out.push_back(static_cast<u8>(v));
    }
    void varI(i64 v) { varU((static_cast<u64>(v) << 1) ^ static_cast<u64>(v >> 63)); }
    void fixed32(u32 v) {
        for (int i = 0; i < 4; ++i) {
            m_out.push_back(static_cast<u8>(v >> (8 * i)));
        }
    }
    void fixed64(u64 v) {
        for (int i = 0; i < 8; ++i) {
            m_out.push_back(static_cast<u8>(v >> (8 * i)));
        }
    }
    void bytes(const char* p, usize n) { m_out.insert(m_out.end(), p, p + n); }

private:
    std::vector<u8>& m_out;
};

class ByteReader {
public:
    ByteReader(const u8* data, usize size) noexcept : m_data(data), m_size(size) {}
    [[nodiscard]] u8 u8v() {
        if (m_error || m_pos >= m_size) {
            m_error = true;
            return 0;
        }
        return m_data[m_pos++];
    }
    [[nodiscard]] u64 varU() {
        u64 v = 0;
        for (u32 shift = 0; shift < 64; shift += 7) {
            const u8 b = u8v();
            if (m_error) {
                return 0;
            }
            if (shift == 63 && (b & 0x7E) != 0) {
                break;
            }
            v |= static_cast<u64>(b & 0x7F) << shift;
            if ((b & 0x80) == 0) {
                return v;
            }
        }
        m_error = true;
        return 0;
    }
    [[nodiscard]] i64 varI() {
        const u64 z = varU();
        return static_cast<i64>(z >> 1) ^ -static_cast<i64>(z & 1);
    }
    [[nodiscard]] u32 fixed32() {
        u32 v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= static_cast<u32>(u8v()) << (8 * i);
        }
        return v;
    }
    [[nodiscard]] u64 fixed64() {
        u64 v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<u64>(u8v()) << (8 * i);
        }
        return v;
    }
    // n 바이트를 가리킨다 (남은 것보다 많으면 오류)
    [[nodiscard]] const u8* take(usize n) {
        if (m_error || m_size - m_pos < n) {
            m_error = true;
            return nullptr;
        }
        const u8* p = m_data + m_pos;
        m_pos += n;
        return p;
    }
    void fail() noexcept { m_error = true; }
    [[nodiscard]] bool error() const noexcept { return m_error; }
    [[nodiscard]] bool atEnd() const noexcept { return !m_error && m_pos == m_size; }

private:
    const u8* m_data;
    usize m_size;
    usize m_pos = 0;
    bool m_error = false;
};

class BinaryWriteVisitor {
public:
    BinaryWriteVisitor(ByteWriter& w, const EntityRefCodec* refs) noexcept : m_w(w), m_refs(refs) {}

    template <class F>
    void field(std::string_view /*name*/, F& value, Hint = Hint::None, const FieldMeta& = {}) {
        put(value);
    }

private:
    template <class F>
    void put(const F& v) {
        static_assert(codec::Supported<F>, "Binary: 지원하지 않는 필드 타입 (core/serialization/FieldCodec.hpp)");
        if constexpr (std::is_same_v<F, bool>) {
            m_w.u8v(v ? 1 : 0);
        } else if constexpr (std::is_same_v<F, f32>) {
            m_w.fixed32(std::bit_cast<u32>(v));
        } else if constexpr (std::is_same_v<F, f64>) {
            m_w.fixed64(std::bit_cast<u64>(v));
        } else if constexpr (std::is_enum_v<F>) {
            put(static_cast<std::underlying_type_t<F>>(v));
        } else if constexpr (std::is_integral_v<F> && std::is_signed_v<F>) {
            m_w.varI(static_cast<i64>(v));
        } else if constexpr (std::is_integral_v<F>) {
            m_w.varU(static_cast<u64>(v));
        } else if constexpr (std::is_same_v<F, Vec2>) {
            put(v.x);
            put(v.y);
        } else if constexpr (std::is_same_v<F, Vec2i>) {
            put(v.x);
            put(v.y);
        } else if constexpr (std::is_same_v<F, EntityId>) {
            m_w.varU(m_refs != nullptr ? m_refs->toWire(v) : v.raw);
        } else if constexpr (codec::IsFixedString<F>::value) {
            m_w.varU(v.size());
            m_w.bytes(v.view().data(), v.size());
        } else if constexpr (codec::IsStdArray<F>::value) {
            for (const auto& e : v) {
                put(e);
            }
        } else if constexpr (codec::IsSmallVector<F>::value) {
            m_w.varU(v.size());
            for (const auto& e : v) {
                put(e);
            }
        }
    }

    ByteWriter& m_w;
    const EntityRefCodec* m_refs;
};

class BinaryReadVisitor {
public:
    BinaryReadVisitor(ByteReader& r, const EntityRefCodec* refs) noexcept : m_r(r), m_refs(refs) {}

    template <class F>
    void field(std::string_view /*name*/, F& value, Hint = Hint::None, const FieldMeta& = {}) {
        get(value);
    }

private:
    template <class F>
    void get(F& v) {
        static_assert(codec::Supported<F>, "Binary: 지원하지 않는 필드 타입 (core/serialization/FieldCodec.hpp)");
        if constexpr (std::is_same_v<F, bool>) {
            const u8 b = m_r.u8v();
            if (b > 1) {
                m_r.fail();
            }
            v = b != 0;
        } else if constexpr (std::is_same_v<F, f32>) {
            v = std::bit_cast<f32>(m_r.fixed32());
        } else if constexpr (std::is_same_v<F, f64>) {
            v = std::bit_cast<f64>(m_r.fixed64());
        } else if constexpr (std::is_enum_v<F>) {
            std::underlying_type_t<F> raw{};
            get(raw);
            v = static_cast<F>(raw);
        } else if constexpr (std::is_integral_v<F> && std::is_signed_v<F>) {
            const i64 x = m_r.varI();
            if (x < static_cast<i64>(std::numeric_limits<F>::min()) ||
                x > static_cast<i64>(std::numeric_limits<F>::max())) {
                m_r.fail();
                return;
            }
            v = static_cast<F>(x);
        } else if constexpr (std::is_integral_v<F>) {
            const u64 x = m_r.varU();
            if (x > static_cast<u64>(std::numeric_limits<F>::max())) {
                m_r.fail();
                return;
            }
            v = static_cast<F>(x);
        } else if constexpr (std::is_same_v<F, Vec2> || std::is_same_v<F, Vec2i>) {
            get(v.x);
            get(v.y);
        } else if constexpr (std::is_same_v<F, EntityId>) {
            const u64 w = m_r.varU();
            v = m_refs != nullptr ? m_refs->fromWire(w) : EntityId{w};
        } else if constexpr (codec::IsFixedString<F>::value) {
            const u64 n = m_r.varU();
            if (n > F::kCapacity) {
                m_r.fail();
                return;
            }
            const u8* p = m_r.take(static_cast<usize>(n));
            if (p != nullptr) {
                (void)v.assign(std::string_view(reinterpret_cast<const char*>(p), static_cast<usize>(n)));
            }
        } else if constexpr (codec::IsStdArray<F>::value) {
            for (auto& e : v) {
                get(e);
            }
        } else if constexpr (codec::IsSmallVector<F>::value) {
            const u64 n = m_r.varU();
            if (n > kMaxBinarySequence) {
                m_r.fail();
                return;
            }
            v.clear();
            for (u64 i = 0; i < n && !m_r.error(); ++i) {
                typename F::value_type e{};
                get(e);
                v.push_back(std::move(e));
            }
        }
    }

    ByteReader& m_r;
    const EntityRefCodec* m_refs;
};

template <Reflectable T>
void componentToBinary(const T& c, std::vector<u8>& out, const EntityRefCodec* refs) {
    ByteWriter w(out);
    BinaryWriteVisitor v(w, refs);
    visitConst(v, c);
}

// bytes 를 정확히 다 써야 성공 (남거나 모자라면 false)
template <Reflectable T>
[[nodiscard]] bool componentFromBinary(T& c, const u8* data, usize size, const EntityRefCodec* refs) {
    ByteReader r(data, size);
    BinaryReadVisitor v(r, refs);
    reflect(v, c);
    return r.atEnd();
}

} // namespace sbx::ecs
