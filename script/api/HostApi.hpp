#pragma once
// Host API 표 — 스크립트가 부르는 엔진 함수의 단일 정의. ADR-0057, docs/19-SANDBOXSCRIPT.md 10장.
//
// 이 표 하나를 컴파일러의 타입 검사 (v0.3 M1-T2), 로드 때 이름 묶기 · 검증기 (M1-T3), C++ 바인딩 등록 검사 (M2-T2),
// sandbox.d.ts 생성 (M2-T2) 이 읽는다. 항목의 id 는 영속 이름이다 (바이트코드 import 표가 이 이름으로 부른다 — 표의
// 순서 · 인덱스는 영속하지 않는다). 항목을 지우거나 시그니처를 바꾸면 kHostApiVersion 을 올린다 (더하기만이면 그대로).
//
// 지금 (M1-T2) 은 컴포넌트가 필요 없는 첫 항목뿐이다. 컴포넌트 읽기 · 질의 (ctx.read · ctx.query) 는 컴포넌트 타입이
// 생기는 v0.3 M3 에서 더한다.

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
    Entity,        // 불투명 — 로컬 EntityId
    ServerContext, // 콜백의 ctx (서버 도메인)
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
    std::array<std::string_view, kMaxHostParams> paramNames; // d.ts 의 매개변수 이름 (정규형 · 지문에는 들어가지 않는다)
    u8 paramCount;
    HostDomain domain;
    HostEffect effect;
    u32 fuelBase; // 연료 비용 — 부를 때마다 (항목당 비용은 질의 항목에서 [계획 M3])
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
};

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
