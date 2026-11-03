#pragma once
// Host API 표 — 스크립트가 부르는 엔진 함수의 단일 정의. ADR-0057, docs/19-SANDBOXSCRIPT.md 10장.
//
// 이 표 하나를 컴파일러의 타입 검사 (v0.3 M1-T2), 로드 때 이름 묶기 · 검증기 (M1-T3), C++ 바인딩 등록 검사 (M2-T2),
// sandbox.d.ts 생성 (M2-T2) 이 읽는다. 항목의 id 는 영속 이름이다 (바이트코드 import 표가 이 이름으로 부른다 — 표의
// 순서 · 인덱스는 영속하지 않는다). 항목을 지우거나 시그니처를 바꾸면 kHostApiVersion 을 올린다 (더하기만이면 그대로).
//
// 클라이언트 항목 (v0.3 M5-T3, ADR-0067): 주인 ClientContext · 도메인 Client — client.tick · frame · dt · read · has ·
// query (읽기만 — 복제 월드) · label · print (화면 글자). 서버 항목과 이름 · 주인이 달라 서로 부를 수 없다 (검사기가
// 주인 타입으로 찾는다). 컴포넌트 항목 (v0.3 M3-T3, ADR-0060): ctx.read · has · query · write. 컴포넌트를 받는 항목은
// 제네릭이다 — 매개변수 Component (ComponentRef<T>) 가 T 를 정하고, ComponentData (T) · ComponentDataOrNull (T | null)
// 이 그 T 다 (검사기가 인자의 컴포넌트 타입으로 푼다). VM 은 T 를 칸 목록으로 펼쳐 넘기고 결과 칸 목록에서 T 를 만든다
// (Interpreter).

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
    ClientContext,       // 클라이언트 콜백의 ctx (v0.3 M5-T3 — 값은 .sbc 에 남는다: 끝에만 더한다)
    EntityOrNull,        // Entity | null (v0.5 M2-T3 — ctx.spawn 의 결과, 레지스터는 상자)
    NumberList,          // number[] (v0.5 M8-T8 — ctx.pathPoints 의 결과, 원소 = f64)
};

enum class HostDomain : u8 { Server, Client };

// 효과 종류 — 권한 · 비용 검토의 단위 (ADR-0057 1)
enum class HostEffect : u8 { Read, Write, Structural, Random, Log, Display }; // Display = 화면 표시 (클라이언트)

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
    // 구조 변경 (v0.5 M2-T3, ADR-0080 2 · 3) — 호출마다 모아 시스템 차례로 단계 16b · 17d 끝에 적용. 상한 · 잘못된
    // 연산은 트랩 없이 버리고 센다 (spawn 은 null)
    HostFunction{
        "ctx.spawn",
        AbiType::ServerContext,
        "spawn",
        false,
        AbiType::EntityOrNull,
        {AbiType::String, AbiType::Number, AbiType::Number},
        {"prefab", "x", "y"},
        3,
        HostDomain::Server,
        HostEffect::Structural,
        32,
        "Prefab 으로 새 개체 (이 틱 끝에 생긴다 — 이 호출에서는 핸들만: 읽기 null · add · destroy 가 된다). 없는 "
        "Prefab · 경계 밖 · 상한이면 null"},
    HostFunction{"ctx.destroy",
                 AbiType::ServerContext,
                 "destroy",
                 false,
                 AbiType::Void,
                 {AbiType::Entity},
                 {"entity"},
                 1,
                 HostDomain::Server,
                 HostEffect::Structural,
                 8,
                 "개체를 지운다 (이 틱 끝에)"},
    HostFunction{"ctx.add",
                 AbiType::ServerContext,
                 "add",
                 false,
                 AbiType::Void,
                 {AbiType::Entity, AbiType::Component, AbiType::ComponentData},
                 {"entity", "component", "value"},
                 3,
                 HostDomain::Server,
                 HostEffect::Structural,
                 16,
                 "스크립트 컴포넌트를 붙인다 (이 틱 끝에 — 이미 있으면 버린다, 엔진 컴포넌트는 [계획])"},
    HostFunction{"ctx.remove",
                 AbiType::ServerContext,
                 "remove",
                 false,
                 AbiType::Void,
                 {AbiType::Entity, AbiType::Component},
                 {"entity", "component"},
                 2,
                 HostDomain::Server,
                 HostEffect::Structural,
                 8,
                 "스크립트 컴포넌트를 뗀다 (이 틱 끝에)"},
    // 주인 (v0.5 M2-T5 (4b)) — game.owner 를 붙이거나 바꾼다 (구조 변경과 같은 버퍼 — 호출 끝 적용, 새 개체 핸들도
    // 된다)
    HostFunction{"ctx.setOwner",
                 AbiType::ServerContext,
                 "setOwner",
                 false,
                 AbiType::Void,
                 {AbiType::Entity, AbiType::I32},
                 {"e", "player"},
                 2,
                 HostDomain::Server,
                 HostEffect::Structural,
                 4,
                 "개체의 주인 플레이어를 정한다 (game.owner — 없으면 붙인다, 0 = 주인 없음, 0 ~ 65535 밖이면 "
                 "HostError). 이 호출이 끝나면 "
                 "적용"},
    // 플레이어 표 (v0.5 M2-T5 (4a), ADR-0093) — 서버 명령 PlayerJoin · PlayerLeave 로 바뀌는 시뮬레이션의 표, id 순
    HostFunction{"ctx.playerCount",
                 AbiType::ServerContext,
                 "playerCount",
                 false,
                 AbiType::I32,
                 {},
                 {},
                 0,
                 HostDomain::Server,
                 HostEffect::Read,
                 1,
                 "접속한 플레이어 수 (리플레이에 기록된 접속 · 나감 — 재생 때 같은 값)"},
    HostFunction{
        "ctx.playerId",
        AbiType::ServerContext,
        "playerId",
        false,
        AbiType::I32,
        {AbiType::I32},
        {"index"},
        1,
        HostDomain::Server,
        HostEffect::Read,
        1,
        "index 번째 플레이어의 번호 (id 순, 0 부터 — 범위 밖이면 -1). 번호 = 명령의 issuer = game.owner.player"},
    HostFunction{"ctx.playerRole",
                 AbiType::ServerContext,
                 "playerRole",
                 false,
                 AbiType::I32,
                 {AbiType::I32},
                 {"id"},
                 1,
                 HostDomain::Server,
                 HostEffect::Read,
                 1,
                 "그 플레이어의 역할 (0 observer · 1 player · 2 editor · 3 admin · 4 owner — 없으면 -1)"},
    // 시간 · 난수 · 반경 질의 (v0.5 M2-T4, ADR-0080 4 · 5)
    HostFunction{"ctx.time",
                 AbiType::ServerContext,
                 "time",
                 true,
                 AbiType::Number,
                 {},
                 {},
                 0,
                 HostDomain::Server,
                 HostEffect::Read,
                 1,
                 "시뮬레이션 시간 (초 = tick / 30)"},
    HostFunction{"ctx.random",
                 AbiType::ServerContext,
                 "random",
                 false,
                 AbiType::Number,
                 {},
                 {},
                 0,
                 HostDomain::Server,
                 HostEffect::Random,
                 2,
                 "0 이상 1 미만 — 시스템마다 흐름 (같은 월드 · 같은 틱 = 같은 수열, 시스템 차례와 무관)"},
    HostFunction{"ctx.randomInt",
                 AbiType::ServerContext,
                 "randomInt",
                 false,
                 AbiType::I32,
                 {AbiType::I32, AbiType::I32},
                 {"lo", "hi"},
                 2,
                 HostDomain::Server,
                 HostEffect::Random,
                 2,
                 "lo 이상 hi 이하의 정수 (lo > hi 면 HostError) — 흐름은 ctx.random 과 같다"},
    HostFunction{"ctx.near",
                 AbiType::ServerContext,
                 "near",
                 false,
                 AbiType::EntityList,
                 {AbiType::Number, AbiType::Number, AbiType::Number},
                 {"x", "y", "radius"},
                 3,
                 HostDomain::Server,
                 HostEffect::Read,
                 16,
                 "(x, y) 에서 반경 안의 개체 — 틱 처음 (단계 4) 의 위치, saveId 오름차순, 개체마다 연료 1, 1,024 개 · "
                 "반경 128 까지"},
    HostFunction{"ctx.nearWith",
                 AbiType::ServerContext,
                 "nearWith",
                 false,
                 AbiType::EntityList,
                 {AbiType::Number, AbiType::Number, AbiType::Number, AbiType::Component},
                 {"x", "y", "radius", "component"},
                 4,
                 HostDomain::Server,
                 HostEffect::Read,
                 16,
                 "ctx.near 중 컴포넌트를 가진 개체"},
    // 엔진 질의 · 경로 요청 (v0.5 M8-T8 — ADR-0085 11, 19 25.7)
    HostFunction{
        "ctx.nearest",
        AbiType::ServerContext,
        "nearest",
        false,
        AbiType::EntityOrNull,
        {AbiType::Number, AbiType::Number, AbiType::Number},
        {"x", "y", "radius"},
        3,
        HostDomain::Server,
        HostEffect::Read,
        16,
        "반경 안에서 가장 가까운 개체 (거리가 같으면 saveId 작은 것) — ctx.near 와 같은 색인 · 반경 상한, 없으면 null"},
    HostFunction{"ctx.kNearest",
                 AbiType::ServerContext,
                 "kNearest",
                 false,
                 AbiType::EntityList,
                 {AbiType::Number, AbiType::Number, AbiType::Number, AbiType::I32},
                 {"x", "y", "radius", "k"},
                 4,
                 HostDomain::Server,
                 HostEffect::Read,
                 16,
                 "반경 안의 가까운 개체 k 개 (가까운 차례, 같은 거리 = saveId 순) — k 는 0 ~ 1,024"},
    HostFunction{"ctx.inBox",
                 AbiType::ServerContext,
                 "inBox",
                 false,
                 AbiType::EntityList,
                 {AbiType::Number, AbiType::Number, AbiType::Number, AbiType::Number},
                 {"minX", "minY", "maxX", "maxY"},
                 4,
                 HostDomain::Server,
                 HostEffect::Read,
                 16,
                 "사각형 (경계 포함) 안의 개체 — saveId 순, 한 변 256 까지 · 1,024 개까지"},
    HostFunction{
        "ctx.requestPath",
        AbiType::ServerContext,
        "requestPath",
        false,
        AbiType::Bool,
        {AbiType::Entity, AbiType::Number, AbiType::Number},
        {"entity", "x", "y"},
        3,
        HostDomain::Server,
        HostEffect::Write,
        8,
        "개체의 경로를 요청한다 (ai.path — 호출 끝에 반영, 다음 틱 단계 8 에 제출 · 그다음 틱 단계 5 에 결과, 틱 "
        "예산을 넘으면 기다린다). ai.path 가 없으면 false"},
    HostFunction{"ctx.pathState",
                 AbiType::ServerContext,
                 "pathState",
                 false,
                 AbiType::I32,
                 {AbiType::Entity},
                 {"entity"},
                 1,
                 HostDomain::Server,
                 HostEffect::Read,
                 2,
                 "경로 상태 — 0 없음 · 1 요청됨 · 2 제출됨 · 3 따라가는 중 · 4 길 없음, ai.path 가 없으면 -1"},
    HostFunction{"ctx.pathPoints",
                 AbiType::ServerContext,
                 "pathPoints",
                 false,
                 AbiType::NumberList,
                 {AbiType::Entity},
                 {"entity"},
                 1,
                 HostDomain::Server,
                 HostEffect::Read,
                 4,
                 "경유점 [x0, y0, x1, y1, …] (새 배열 — 경로가 없으면 빈 배열)"},
    HostFunction{"ctx.cancelPath",
                 AbiType::ServerContext,
                 "cancelPath",
                 false,
                 AbiType::Void,
                 {AbiType::Entity},
                 {"entity"},
                 1,
                 HostDomain::Server,
                 HostEffect::Write,
                 2,
                 "경로를 지운다 (상태 0 — 호출 끝에 반영)"},
    // 충돌 질의 (v0.5 M8-T8 (2) — core.collider 원 · 단계 4 색인)
    HostFunction{
        "ctx.raycast",
        AbiType::ServerContext,
        "raycast",
        false,
        AbiType::EntityOrNull,
        {AbiType::Number, AbiType::Number, AbiType::Number, AbiType::Number},
        {"x0", "y0", "x1", "y1"},
        4,
        HostDomain::Server,
        HostEffect::Read,
        16,
        "선분이 처음 닿는 충돌 원 (core.collider) 의 개체 — 같은 자리면 saveId 작은 것, 시작점이 원 안이면 그 개체, "
        "길이 256 까지, 없으면 null"},
    HostFunction{"ctx.overlap",
                 AbiType::ServerContext,
                 "overlap",
                 false,
                 AbiType::EntityList,
                 {AbiType::Number, AbiType::Number, AbiType::Number},
                 {"x", "y", "radius"},
                 3,
                 HostDomain::Server,
                 HostEffect::Read,
                 16,
                 "원 (x, y, radius) 과 겹치는 충돌 원의 개체 — saveId 순, 반경 128 · 1,024 개까지"},
    HostFunction{"client.tick",
                 AbiType::ClientContext,
                 "tick",
                 true,
                 AbiType::I32,
                 {},
                 {},
                 0,
                 HostDomain::Client,
                 HostEffect::Read,
                 1,
                 "복제 월드의 서버 틱 (마지막으로 받은 스냅숏)"},
    HostFunction{"client.frame",
                 AbiType::ClientContext,
                 "frame",
                 true,
                 AbiType::I32,
                 {},
                 {},
                 0,
                 HostDomain::Client,
                 HostEffect::Read,
                 1,
                 "이 접속에서 몇 번째 onUpdate 인가 (1 부터)"},
    HostFunction{"client.dt",
                 AbiType::ClientContext,
                 "dt",
                 true,
                 AbiType::Number,
                 {},
                 {},
                 0,
                 HostDomain::Client,
                 HostEffect::Read,
                 1,
                 "앞 onUpdate 뒤 지난 실제 시간 (초 — 표시용, 시뮬레이션과 무관)"},
    HostFunction{"client.read",
                 AbiType::ClientContext,
                 "read",
                 false,
                 AbiType::ComponentDataOrNull,
                 {AbiType::Entity, AbiType::Component},
                 {"entity", "component"},
                 2,
                 HostDomain::Client,
                 HostEffect::Read,
                 8,
                 "복제 월드의 컴포넌트 값 복사 (없으면 null) — 개체 참조 필드는 null [계획]"},
    HostFunction{"client.has",
                 AbiType::ClientContext,
                 "has",
                 false,
                 AbiType::Bool,
                 {AbiType::Entity, AbiType::Component},
                 {"entity", "component"},
                 2,
                 HostDomain::Client,
                 HostEffect::Read,
                 4,
                 "개체에 컴포넌트가 있나 (복제 월드)"},
    HostFunction{"client.query",
                 AbiType::ClientContext,
                 "query",
                 false,
                 AbiType::EntityList,
                 {AbiType::Component},
                 {"component"},
                 1,
                 HostDomain::Client,
                 HostEffect::Read,
                 16,
                 "컴포넌트를 가진 복제 개체 (netId 오름차순 — 개체마다 연료 1)"},
    HostFunction{"client.label",
                 AbiType::ClientContext,
                 "label",
                 false,
                 AbiType::Void,
                 {AbiType::Entity, AbiType::String},
                 {"entity", "text"},
                 2,
                 HostDomain::Client,
                 HostEffect::Display,
                 16,
                 "이번 프레임 개체 위에 글자 (한 줄, 256 바이트까지)"},
    HostFunction{"client.print",
                 AbiType::ClientContext,
                 "print",
                 false,
                 AbiType::Void,
                 {AbiType::String},
                 {"text"},
                 1,
                 HostDomain::Client,
                 HostEffect::Display,
                 16,
                 "이번 프레임 화면 왼쪽 위 \"스크립트\" 칸에 한 줄 (256 바이트까지)"},
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
    OpaqueTypeInfo{AbiType::ClientContext, "ClientContext",
                   "클라이언트 콜백 (onUpdate) 의 ctx — 호출 안에서만 유효, 읽기 · 화면 표시만"},
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
