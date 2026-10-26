#pragma once
// 동적 컴포넌트 — C++ 타입 없이 로드 때 모양을 아는 컴포넌트 (스크립트의 defineComponent). ADR-0059,
// docs/02-ECS.md 9.6.
//
//   DynamicComponentDesc   설명 (이름 · 버전 · 선언 플래그 · 필드) — core/script 가 스크립트 스키마에서 만든다
//   DynamicComponentLayout 배치: 필드를 **이름 순**으로, 종류마다 자연 정렬 (i32 · f32 · enum 4, f64 · entityRef 8,
//   bool 1,
//                          vec2 = f32 둘), stride = 8 의 배수. 기본값 바이트 한 칸. 불변 — shared_ptr 로 카탈로그 ·
//                          풀이 같이 든다
//   reflectDynamic         필드마다 칸을 타입 있는 임시값으로 읽어 방문자의 field() 를 부르고 되쓴다 → 내장 컴포넌트와
//   같은 JSON ·
//                          해시 · 바이트 형식 (같은 필드를 이름 순으로 가진 C++ 구조체와 바이트 단위로 같다 — 시험)
//   DynamicComponentPool   ComponentPoolBase 의 sparse set + 바이트 배열 (칸마다 stride)
//
// entityRef 칸 = saveId (u64, 0 = 없음 — ADR-0015). 칸 읽기 · 쓰기는 memcpy (정렬 · 별칭 규칙에 기대지 않는다).

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/ecs/ComponentPool.hpp"
#include "core/ecs/Reflection.hpp"
#include "foundation/math/Vec2.hpp"

namespace sbx::ecs {

enum class DynamicFieldKind : u8 { I32, F32, F64, Bool, Vec2, EntityRef, Enum };

struct DynamicFieldDesc {
    std::string name;
    DynamicFieldKind kind = DynamicFieldKind::I32;
    f64 defaultValue = 0.0; // vec2 는 x. 정수 종류는 정수여야 한다
    f64 defaultY = 0.0;     // vec2 의 y
    bool hasRange = false;  // i32 · f32 · f64 만
    f64 min = 0.0;
    f64 max = 0.0;
};

struct DynamicComponentDesc {
    std::string name; // isValidComponentName
    u16 version = 1;
    ComponentFlags flags = ComponentFlags::None; // 선언 플래그 (effectiveFlags 를 거친다)
    std::vector<DynamicFieldDesc> fields;        // 순서 무관 — 배치가 이름 순으로 정렬한다
};

class DynamicComponentLayout {
public:
    static constexpr usize kMaxFields = 64;

    struct Field {
        std::string name;
        DynamicFieldKind kind = DynamicFieldKind::I32;
        u32 offset = 0;
        FieldMeta meta{};
    };

    // 이름 규칙 · 필드 이름 (비지 않음 · 중복 없음) · 수 · 버전 · 기본값 (범위 안 · 정수 종류는 정수 · 유한) 을 본다
    static Expected<std::shared_ptr<const DynamicComponentLayout>> make(DynamicComponentDesc desc);

    [[nodiscard]] std::string_view name() const noexcept { return m_name; }
    [[nodiscard]] StableId stableId() const noexcept { return m_stableId; }
    [[nodiscard]] u16 version() const noexcept { return m_version; }
    [[nodiscard]] ComponentFlags flags() const noexcept { return m_flags; } // effective
    [[nodiscard]] ComponentTypeId typeId() const noexcept { return m_typeId; }
    [[nodiscard]] usize stride() const noexcept { return m_stride; }
    [[nodiscard]] const std::vector<Field>& fields() const noexcept { return m_fields; }
    [[nodiscard]] const std::byte* defaults() const noexcept { return m_defaults.data(); }
    [[nodiscard]] const Field* findField(std::string_view name) const noexcept;

private:
    DynamicComponentLayout() = default;

    std::string m_name;
    StableId m_stableId = 0;
    u16 m_version = 1;
    ComponentFlags m_flags = ComponentFlags::None;
    ComponentTypeId m_typeId = kInvalidComponentTypeId;
    usize m_stride = 0;
    std::vector<Field> m_fields; // 이름 순
    std::vector<std::byte> m_defaults;
};

// 동적 컴포넌트의 ComponentTypeId — 프로세스 표 (이름 → 번호). 같은 이름 = 같은 번호 (카탈로그가 여럿이어도 Registry 의
// 풀 칸이 하나). 번호는 영속하지 않는다 (ADR-0059 5). 스레드 안전.
[[nodiscard]] ComponentTypeId dynamicComponentTypeId(std::string_view name);

namespace dynamic_detail {
template <class F>
F load(const std::byte* p) noexcept {
    F v{};
    std::memcpy(&v, p, sizeof(F));
    return v;
}
template <class F>
void store(std::byte* p, const F& v) noexcept {
    std::memcpy(p, &v, sizeof(F));
}
template <class F, class V>
void visitSlot(V& v, const DynamicComponentLayout::Field& f, std::byte* p) {
    F tmp = load<F>(p);
    v.field(f.name, tmp, Hint::None, f.meta);
    store(p, tmp);
}
} // namespace dynamic_detail

// 내장 컴포넌트의 reflect(v, c) 자리. 쓰기 전용 방문자 (JsonWriter · HashVisitor · BinaryWriteVisitor) 는 값을 바꾸지
// 않는다 — 되쓰기는 같은 바이트다 (visitConst 와 같은 계약). const 데이터는 reflectDynamicConst 로.
template <class V>
void reflectDynamic(V& v, const DynamicComponentLayout& layout, std::byte* data) {
    using dynamic_detail::visitSlot;
    for (const DynamicComponentLayout::Field& f : layout.fields()) {
        std::byte* p = data + f.offset;
        switch (f.kind) {
        case DynamicFieldKind::I32:
        case DynamicFieldKind::Enum: // enum 은 바탕 i32 와 같은 형식 (JSON 정수 · 해시 i64 · zigzag)
            visitSlot<i32>(v, f, p);
            break;
        case DynamicFieldKind::F32:
            visitSlot<f32>(v, f, p);
            break;
        case DynamicFieldKind::F64:
            visitSlot<f64>(v, f, p);
            break;
        case DynamicFieldKind::Bool:
            visitSlot<bool>(v, f, p);
            break;
        case DynamicFieldKind::Vec2:
            visitSlot<Vec2>(v, f, p);
            break;
        case DynamicFieldKind::EntityRef: // saveId — ai.behavior.target 과 같은 u64
            visitSlot<u64>(v, f, p);
            break;
        }
    }
}
template <class V>
void reflectDynamicConst(V& v, const DynamicComponentLayout& layout, const std::byte* data) {
    reflectDynamic(v, layout, const_cast<std::byte*>(data)); // NOLINT(cppcoreguidelines-pro-type-const-cast)
}

class DynamicComponentPool final : public ComponentPoolBase {
public:
    explicit DynamicComponentPool(std::shared_ptr<const DynamicComponentLayout> layout);

    [[nodiscard]] StableId stableId() const noexcept override { return m_layout->stableId(); }
    [[nodiscard]] std::string_view name() const noexcept override { return m_layout->name(); }
    [[nodiscard]] ComponentTypeId typeId() const noexcept override { return m_layout->typeId(); }
    [[nodiscard]] ComponentFlags flags() const noexcept override { return m_layout->flags(); }
    [[nodiscard]] u16 version() const noexcept override { return m_layout->version(); }
    [[nodiscard]] const DynamicComponentLayout& layout() const noexcept { return *m_layout; }

    bool remove(EntityId e) override;
    void clear() noexcept override;
    void* emplaceDefaultRaw(EntityId e, Tick tick) override;
    [[nodiscard]] const void* getRaw(EntityId e) const noexcept override;
    [[nodiscard]] void* getRawForWrite(EntityId e, Tick tick) noexcept override;
    [[nodiscard]] Expected<void> validate() const override;

private:
    std::shared_ptr<const DynamicComponentLayout> m_layout;
    std::vector<std::byte> m_data; // size = dense.size() × stride
};

} // namespace sbx::ecs
