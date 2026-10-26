#pragma once
// Host API 표 — 스크립트가 부르는 엔진 함수의 단일 정의. ADR-0057, docs/19-SANDBOXSCRIPT.md 10장.
//
// 이 표 하나를 컴파일러의 타입 검사 (v0.3 M1-T2), 로드 때 이름 묶기 · 검증기 (M1-T3), C++ 바인딩 등록 검사 (M2-T2),
// sandbox.d.ts 생성 (M2-T2) 이 읽는다. 항목의 id 는 영속 이름이다 (바이트코드 import 표가 이 이름으로 부른다 — 표의
// 순서 · 인덱스는 영속하지 않는다). 항목을 지우거나 시그니처를 바꾸면 kHostApiVersion 을 올린다 (더하기만이면 그대로).
//
// 컴포넌트 항목 (v0.3 M3-T3, ADR-0060): ctx.read · has · query · write. 컴포넌트를 받는 항목은 제네릭이다 — 매개변수
// Component (ComponentRef<T>) 가 T 를 정하고, ComponentData (T) · ComponentDataOrNull (T | null) 이 그 T 다 (검사기가
// 인자의 컴포넌트 타입으로 푼다). VM 은 T 를 칸 목록으로 펼쳐 넘기고 결과 칸 목록에서 T 를 만든다 (Interpreter).

#include <array>
#include <string>
#include <string_view>

#include "foundation/types/Types.hpp"

namespace sbx::script::api {

inline constexpr u32 kHostApiVersion = 1;

// ABI 타입 (ADR-0057 4). 스크립트 타입 이름은 abiTypeName
enum class AbiType : u8 {
    Void,
    Bool,
    I32,
    Number,
    String,
    Entity,        // 불투명 — 호출 안에서만 유효한 개체 핸들
    ServerContext, // 콜백의 ctx (서버 도메인)
    // 컴포넌트 (v0.3 M3-T3) — 한 항목 안에서 같은 T
    Component,           // ComponentRef<T> — 불투명 (stableId), 바이트코드 LOADK
    ComponentData,       // T — 구조체 (필드 이름 순)
    ComponentDataOrNull, // T | null
    EntityList,          // Entity[] (질의 결과 — saveId 오름차순)
};

enum class HostDomain : u8 { Server, Client };

// 효과 종류 — 권한 · 비용 검토의 단위 (ADR-0057 1)
enum class HostEffect : u8 { Read, Write, Structural, Random, Log };

inline constexpr usize kMaxHostParams = 4;

struct HostFunction {
    std::string_view id;     // "ctx.tick" — 영속 이름
    AbiType owner;           // 이 타입의 멤버다 (ctx.tick → ServerContext)
    std::string_view member; // 스크립트에서 쓰는 이름 ("tick")
    bool property;           // true = 속성 (ctx.tick), false = 메서드 (ctx.log(…))
    AbiType result;
    std::array<AbiType, kMaxHostParams> params;
    std::array<std::string_view, kMaxHostParams>
        paramNames; // d.ts 의 매개변수 이름 (정규형 · 지문에는 들어가지 않는다)
    u8 paramCount;
    HostDomain domain;
    HostEffect effect;
    u32 fuelBase; // 연료 비용 — 부를 때마다. 질의는 결과 개체마다 더 (HostResult::extraFuel)
    std::string_view doc;
};

// 표. 순서는 의미가 없다 (사람이 읽기 좋은 순서) — 찾기는 id · (owner, member) 로
inline constexpr std::array kHostFunctions{
    HostFunction{"ctx.tick",
                 AbiType::ServerContext,
                 "tick",
                 true,
                 AbiType::I32,
                 {},
                 {},
                 0,
                 HostDomain::Server,
                 HostEffect::Read,
                 1,
                 "지금 시뮬레이션 틱 (30 TPS)"},
    HostFunction{"ctx.log",
                 AbiType::ServerContext,
                 "log",
                 false,
                 AbiType::Void,
                 {AbiType::String},
                 {"message"},
                 1,
                 HostDomain::Server,
                 HostEffect::Log,
                 16,
                 "서버 로그에 한 줄 (스크립트 이름과 틱이 붙는다)"},
    HostFunction{"ctx.read",
                 AbiType::ServerContext,
                 "read",
                 false,
                 AbiType::ComponentDataOrNull,
                 {AbiType::Entity, AbiType::Component},
                 {"entity", "component"},
                 2,
                 HostDomain::Server,
                 HostEffect::Read,
                 8,
                 "컴포넌트 값의 복사 (없으면 null) — 이 호출에서 쓴 값이 보인다"},
    HostFunction{"ctx.has",
                 AbiType::ServerContext,
                 "has",
                 false,
                 AbiType::Bool,
                 {AbiType::Entity, AbiType::Component},
                 {"entity", "component"},
                 2,
                 HostDomain::Server,
                 HostEffect::Read,
                 4,
                 "개체에 컴포넌트가 있나"},
    HostFunction{"ctx.query",
                 AbiType::ServerContext,
                 "query",
                 false,
                 AbiType::EntityList,
                 {AbiType::Component},
                 {"component"},
                 1,
                 HostDomain::Server,
                 HostEffect::Read,
                 16,
                 "컴포넌트를 가진 개체 (saveId 오름차순 — 개체마다 연료 1)"},
    HostFunction{"ctx.write",
                 AbiType::ServerContext,
                 "write",
                 false,
                 AbiType::Void,
                 {AbiType::Entity, AbiType::Component, AbiType::ComponentData},
                 {"entity", "component", "value"},
                 3,
                 HostDomain::Server,
                 HostEffect::Write,
                 16,
                 "컴포넌트 값을 쓴다 (호출이 끝나면 적용, 트랩이면 버림 — 범위 밖 · 없는 컴포넌트는 트랩)"},
};

// 제네릭 항목 (Component 매개변수가 있다) 인가
[[nodiscard]] constexpr bool isComponentGeneric(const HostFunction& f) noexcept {
    for (u8 i = 0; i < f.paramCount; ++i) {
        if (f.params[i] == AbiType::Component) {
            return true;
        }
    }
    return false;
}

// 불투명 SDK 타입 — "sandbox" 모듈이 내보내는 타입 이름
struct OpaqueTypeInfo {
    AbiType abi;
    std::string_view name;
    std::string_view doc;
};
inline constexpr std::array kOpaqueTypes{
    OpaqueTypeInfo{AbiType::Entity, "Entity", "개체 핸들 — 같음 비교만, 틱을 넘겨 두려면 EntityRef 필드에"},
    OpaqueTypeInfo{AbiType::ServerContext, "ServerContext", "서버 콜백의 ctx — 호출 안에서만 유효"},
};

[[nodiscard]] std::string_view abiTypeName(AbiType t) noexcept;
[[nodiscard]] const HostFunction* findHostFunction(std::string_view id) noexcept;
[[nodiscard]] const HostFunction* findHostMember(AbiType owner, std::string_view member) noexcept;
[[nodiscard]] usize hostFunctionIndex(const HostFunction& f) noexcept;
// 표 지문 — 항목 정규형의 FNV-1a 64 (.sbc 머리 [M1-T3]). 순서와 무관하게 id 정렬 순으로 만든다
[[nodiscard]] u64 hostApiFingerprint();
// 항목 정규형 한 줄: "ctx.log ServerContext.log(string): void server log 16"
[[nodiscard]] std::string canonicalSignature(const HostFunction& f);

} // namespace sbx::script::api
