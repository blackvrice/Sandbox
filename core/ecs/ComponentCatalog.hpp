#pragma once
// 컴포넌트 카탈로그 — 이름/stableId 로 타입을 모르는 채 컴포넌트를 다루는 곳(로드, 프리팹, 에디터, 해시)이 쓴다.
// docs/02-ECS.md 9장, ADR-0011.
//
// 등록은 명시적이다:  catalog.add<Transform>();  (registerCoreComponents(catalog) 가 엔진 컴포넌트를 모두 넣는다)
// 스크립트 컴포넌트 (C++ 타입 없음) 는 catalog.addDynamic(설명) — 같은 넣기 규칙, 같은 방문자 → 같은 형식 (ADR-0059).
// 함수 칸이 std::function 인 것은 동적 컴포넌트가 배치 (문맥) 를 잡아야 해서다 — 부르는 모양은 함수 포인터와 같다.
// 정적 초기화로 자동 등록하지 않는 이유: 정적 라이브러리에서 참조되지 않은 번역 단위의 초기화가 링커에 의해
// 사라질 수 있고, 초기화 순서가 플랫폼마다 다르다. 카탈로그 내용이 실행마다 달라지면 안 된다.

#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include "core/ecs/ComponentPool.hpp"
#include "core/ecs/DynamicComponent.hpp"
#include "core/ecs/Reflection.hpp"
#include "core/serialization/BinaryCodec.hpp"
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

    std::function<std::unique_ptr<ComponentPoolBase>()> makePool;
    std::function<void(const void* component, Json& out)> writeJson;
    // 실패하면 component 가 일부만 바뀌었을 수 있다 — 기본값으로 막 만든 컴포넌트에만 쓴다.
    std::function<Expected<void>(void* component, const Json& in, std::string_view context)> readJson;
    // 원자적 패치: 복사본에 적용해 전부 성공할 때만 덮어쓴다. 실패하면 component 는 그대로다.
    std::function<Expected<void>(void* component, const Json& patch, std::string_view context)> patchJson;
    // 기본값 + in 으로 만들 수 있는지만 검사한다 (명령의 사전 검증용, 아무것도 바꾸지 않는다).
    std::function<Expected<void>(const Json& in, std::string_view context)> validateJson;
    std::function<void(const void* component, Fnv1a64& h)> hash;
    // 바이트 형식 (복제 — BinaryCodec.hpp). refs 는 EntityId 필드를 바꾼다 (null 이면 raw)
    std::function<void(const void* component, std::vector<u8>& out, const EntityRefCodec* refs)> writeBinary;
    // 바이트를 정확히 다 읽어야 true. 실패하면 component 가 일부만 바뀌었을 수 있다 (새로 만든 값에 읽는다)
    std::function<bool(void* component, const u8* data, usize size, const EntityRefCodec* refs)> readBinary;

    // 이름으로 수치 필드 읽기/쓰기 (Rule 조건·효과). 없거나 수치가 아니면 nullopt / false.
    std::function<std::optional<f64>(const void* component, std::string_view field)> getNumber;
    std::function<bool(void* component, std::string_view field, f64 value, bool add)> applyNumber;
    // reflect 순서의 필드 목록 (검증기: 필드 존재·수치 여부·PrefabRef 찾기). 동적 컴포넌트는 이름 순
    std::vector<FieldDesc> fields;
    // 동적 컴포넌트의 배치 (내장은 null) — name · fields 의 문자열을 살려 두고, Host 가 칸을 찾는다
    std::shared_ptr<const DynamicComponentLayout> dynamicLayout;

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
        info.writeBinary = [](const void* c, std::vector<u8>& out, const EntityRefCodec* refs) {
            componentToBinary(*static_cast<const T*>(c), out, refs);
        };
        info.readBinary = [](void* c, const u8* data, usize size, const EntityRefCodec* refs) {
            return componentFromBinary(*static_cast<T*>(c), data, size, refs);
        };
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

    // 동적 컴포넌트 (ADR-0059). 배치 오류 → ValidationFailed, 넣기 규칙은 add<T>() 와 같다
    Expected<void> addDynamic(DynamicComponentDesc desc);
    Expected<void> addDynamic(std::shared_ptr<const DynamicComponentLayout> layout);

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
