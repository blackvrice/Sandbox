#pragma once
// 컴포넌트 카탈로그 — 이름/stableId 로 타입을 모르는 채 컴포넌트를 다루는 곳(로드, 프리팹, 에디터, 해시)이 쓴다.
// docs/02-ECS.md 9장, ADR-0011.
//
// 등록은 명시적이다:  catalog.add<Transform>();  (registerCoreComponents(catalog) 가 엔진 컴포넌트를 모두 넣는다)
// 정적 초기화로 자동 등록하지 않는 이유: 정적 라이브러리에서 참조되지 않은 번역 단위의 초기화가 링커에 의해
// 사라질 수 있고, 초기화 순서가 플랫폼마다 다르다. 카탈로그 내용이 실행마다 달라지면 안 된다.

#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include "core/ecs/ComponentPool.hpp"
#include "core/ecs/Reflection.hpp"
#include "core/serialization/FieldAccess.hpp"
#include "core/serialization/HashVisitor.hpp"
#include "core/serialization/JsonVisitor.hpp"

namespace sbx::ecs {

struct ComponentInfo {
    std::string_view name;
    StableId stableId = 0;
    u16 version = 0;
    ComponentFlags flags = ComponentFlags::None;
    ComponentTypeId typeId = kInvalidComponentTypeId;
    usize size = 0;
    usize align = 0;

    std::unique_ptr<ComponentPoolBase> (*makePool)() = nullptr;
    void (*writeJson)(const void* component, Json& out) = nullptr;
    // 실패하면 component 가 일부만 바뀌었을 수 있다 — 기본값으로 막 만든 컴포넌트에만 쓴다.
    Expected<void> (*readJson)(void* component, const Json& in, std::string_view context) = nullptr;
    // 원자적 패치: 복사본에 적용해 전부 성공할 때만 덮어쓴다. 실패하면 component 는 그대로다.
    Expected<void> (*patchJson)(void* component, const Json& patch, std::string_view context) = nullptr;
    // 기본값 + in 으로 만들 수 있는지만 검사한다 (명령의 사전 검증용, 아무것도 바꾸지 않는다).
    Expected<void> (*validateJson)(const Json& in, std::string_view context) = nullptr;
    void (*hash)(const void* component, Fnv1a64& h) = nullptr;

    // 이름으로 수치 필드 읽기/쓰기 (Rule 조건·효과). 없거나 수치가 아니면 nullopt / false.
    std::optional<f64> (*getNumber)(const void* component, std::string_view field) = nullptr;
    bool (*applyNumber)(void* component, std::string_view field, f64 value, bool add) = nullptr;
    // reflect 순서의 필드 목록 (검증기: 필드 존재·수치 여부·PrefabRef 찾기)
    std::vector<FieldDesc> fields;

    [[nodiscard]] const FieldDesc* findField(std::string_view fieldName) const noexcept {
        for (const FieldDesc& f : fields) {
            if (f.name == fieldName) {
                return &f;
            }
        }
        return nullptr;
    }
};

class ComponentCatalog {
public:
    // 같은 이름 재등록 → AlreadyExists, 다른 이름인데 stableId 충돌 → ValidationFailed
    template <class T>
        requires Component<T> && Reflectable<T>
    Expected<void> add() {
        using Tr = ComponentTraits<T>;
        ComponentInfo info;
        info.name = Tr::kName;
        info.stableId = Tr::kStableId;
        info.version = Tr::kVersion;
        info.flags = Tr::kFlags;
        info.typeId = componentTypeId<T>();
        info.size = sizeof(T);
        info.align = alignof(T);
        info.makePool = []() -> std::unique_ptr<ComponentPoolBase> { return std::make_unique<ComponentPool<T>>(); };
        info.writeJson = [](const void* c, Json& out) { out = componentToJson(*static_cast<const T*>(c)); };
        info.readJson = [](void* c, const Json& in, std::string_view ctx) {
            return componentFromJson(*static_cast<T*>(c), in, ctx);
        };
        if constexpr (std::is_copy_constructible_v<T>) {
            info.patchJson = [](void* c, const Json& patch, std::string_view ctx) -> Expected<void> {
                T copy = *static_cast<const T*>(c);
                if (auto r = componentFromJson(copy, patch, ctx); !r) {
                    return r;
                }
                *static_cast<T*>(c) = std::move(copy);
                return {};
            };
        } // 복사할 수 없는 컴포넌트는 patchJson 이 nullptr — ChangeComponent 가 Unsupported 로 거절한다
        info.validateJson = [](const Json& in, std::string_view ctx) {
            T probe{};
            return componentFromJson(probe, in, ctx);
        };
        info.hash = [](const void* c, Fnv1a64& h) { hashComponent(h, *static_cast<const T*>(c)); };
        info.getNumber = [](const void* c, std::string_view field) {
            NumberGetVisitor v(field);
            visitConst(v, *static_cast<const T*>(c));
            return v.result;
        };
        info.applyNumber = [](void* c, std::string_view field, f64 value, bool add) {
            NumberSetVisitor v(field, value, add);
            reflect(v, *static_cast<T*>(c));
            return v.done;
        };
        {
            T probe{};
            FieldListVisitor v(info.fields);
            reflect(v, probe);
        }
        return insert(info);
    }

    [[nodiscard]] const ComponentInfo* find(StableId id) const noexcept;
    [[nodiscard]] const ComponentInfo* find(std::string_view name) const noexcept;
    // stableId 오름차순
    [[nodiscard]] std::span<const ComponentInfo> all() const noexcept { return m_infos; }
    [[nodiscard]] usize size() const noexcept { return m_infos.size(); }

private:
    Expected<void> insert(const ComponentInfo& info);

    std::vector<ComponentInfo> m_infos; // stableId 정렬 유지
};

} // namespace sbx::ecs
