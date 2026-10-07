#pragma once
// 컴포넌트 등록. docs/02-ECS.md 9장.
//
//   struct Transform { Vec2 position; f32 rotation = 0.f; };
//   template <class V> void reflect(V& v, Transform& c) { v.field("position", c.position, Hint::Position); … }
//   SBX_COMPONENT(sbx::comp::Transform, "core.transform", 1, Flags::Replicated | Flags::Persistent);
//
// 식별자 세 가지
//   ComponentTypeId  런타임 배열 인덱스. 처음 쓰인 순서로 매겨진다 → 저장·전송·해시에 절대 쓰지 않는다.
//   stableId         fnv1a64(name). 영구. 세이브·네트워크·해시·리플레이.
//   name             "core.transform". JSON·로그·에디터.

#include <string_view>
#include <type_traits>

#include "foundation/hash/Fnv1a.hpp"
#include "foundation/types/Types.hpp"

namespace sbx::ecs {

using ComponentTypeId = u16;
using StableId = u64;

inline constexpr ComponentTypeId kInvalidComponentTypeId = 0xFFFF;

// 비트 플래그. 의미는 docs/02-ECS.md 9.3.
enum class ComponentFlags : u32 {
    None = 0,
    Replicated = 1u << 0,    // 서버→클라 복제
    Persistent = 1u << 1,    // 세이브 대상
    Hashed = 1u << 2,        // WorldHash 대상 (Persistent 면 자동으로 켜진다)
    EditorVisible = 1u << 3, // Inspector 표시
    ServerOnly = 1u << 4,    // 서버에만 존재
    ClientOnly = 1u << 5,    // 클라에만 존재
    NotHashed = 1u << 6,     // Persistent 이지만 해시에서 뺀다 — 등록 옆에 사유 주석 필수
};

constexpr ComponentFlags operator|(ComponentFlags a, ComponentFlags b) noexcept {
    return static_cast<ComponentFlags>(static_cast<u32>(a) | static_cast<u32>(b));
}
constexpr ComponentFlags operator&(ComponentFlags a, ComponentFlags b) noexcept {
    return static_cast<ComponentFlags>(static_cast<u32>(a) & static_cast<u32>(b));
}
constexpr bool hasFlag(ComponentFlags set, ComponentFlags f) noexcept {
    return (set & f) != ComponentFlags::None;
}

// 선언된 플래그에서 실제 적용 플래그를 만든다: Persistent → Hashed (NotHashed 가 없으면). H3 를 구조로 막는다.
constexpr ComponentFlags effectiveFlags(ComponentFlags declared) noexcept {
    ComponentFlags f = declared;
    if (hasFlag(f, ComponentFlags::Persistent) && !hasFlag(f, ComponentFlags::NotHashed)) {
        f = f | ComponentFlags::Hashed;
    }
    if (hasFlag(f, ComponentFlags::NotHashed)) {
        f = static_cast<ComponentFlags>(static_cast<u32>(f) & ~static_cast<u32>(ComponentFlags::Hashed));
    }
    return f;
}

// 이름 규칙: <namespace>.<name>, 소문자·숫자·'_'·'.' 만, 점 하나 이상, 점으로 시작/끝/연속 금지.
constexpr bool isValidComponentName(std::string_view n) noexcept {
    if (n.empty() || n.front() == '.' || n.back() == '.') {
        return false;
    }
    bool dot = false;
    char prev = '\0';
    for (const char c : n) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
        if (!ok || (c == '.' && prev == '.')) {
            return false;
        }
        dot = dot || c == '.';
        prev = c;
    }
    return dot;
}

// 기본 템플릿은 정의하지 않는다 — SBX_COMPONENT 로 특수화되지 않은 타입은 컴포넌트가 아니다 (C5).
template <class T>
struct ComponentTraits;

template <class T>
concept Component = requires {
    { ComponentTraits<T>::kName } -> std::convertible_to<std::string_view>;
    { ComponentTraits<T>::kStableId } -> std::convertible_to<StableId>;
    { ComponentTraits<T>::kVersion } -> std::convertible_to<u16>;
    { ComponentTraits<T>::kFlags } -> std::convertible_to<ComponentFlags>;
} && std::is_move_constructible_v<T> && std::is_move_assignable_v<T> && std::is_default_constructible_v<T>;

namespace detail {
ComponentTypeId allocateComponentTypeId() noexcept;
} // namespace detail

// 프로세스 안에서 T 의 런타임 인덱스. 스레드 안전 (함수 지역 static).
template <Component T>
ComponentTypeId componentTypeId() noexcept {
    static const ComponentTypeId id = detail::allocateComponentTypeId();
    return id;
}

template <Component T>
inline constexpr StableId stableIdOf = ComponentTraits<T>::kStableId;

} // namespace sbx::ecs

// 전역 네임스페이스에서 호출한다. Type 은 완전한 이름으로.
#define SBX_COMPONENT(Type, Name, Version, DeclaredFlags)                                                              \
    template <>                                                                                                        \
    struct sbx::ecs::ComponentTraits<Type> {                                                                           \
        static_assert(::sbx::ecs::isValidComponentName(Name), "컴포넌트 이름 규칙 위반 (docs/02-ECS.md 9.2)");         \
        static_assert((Version) >= 1, "컴포넌트 버전은 1 부터");                                                       \
        static constexpr std::string_view kName = Name;                                                                \
        static constexpr ::sbx::ecs::StableId kStableId = ::sbx::fnv1a64(Name);                                        \
        static constexpr ::sbx::u16 kVersion = (Version);                                                              \
        static constexpr ::sbx::ecs::ComponentFlags kFlags = ::sbx::ecs::effectiveFlags(DeclaredFlags);                \
    }
