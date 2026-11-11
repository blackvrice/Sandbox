#pragma once
// SandboxScript 언어 서비스 (docs/27-SCRIPT-IDE.md 2 · 3장, ADR-0091 · ADR-0129). Studio 스크립트 편집기와 [계획] sbx
// lsp 가 같이 쓴다. 위치는 바이트 offset (텍스트 위치 계약 — NG-05).
//
// complete — 커서의 후보. 식 뒤 '.' 이면 그 식의 타입 (검사기) 의 멤버: 구조체 · 컴포넌트 데이터 필드, Host 멤버 (ctx ·
// Entity … — 도메인은 타입이 정한다), 내장 (배열 · 문자열 · Map · Set · 컬렉션 — MemberCatalog), enum 멤버, Math ·
// vec2. 그 타입은 커서 자리에 자리표 이름을 넣고 검사기를 돌려 얻는다 (편집 중인 불완전한 식도 문장이 된다). import { |
// } 안이면 "sandbox" 의 이름 · 그 모듈의 export (이미 가져온 이름은 뺀다). 그 밖에 접두가 있으면 이름: 그 함수의 지역 ·
// 매개변수 (커서 앞), 모듈 이름, 가져온 이름, 전역 (Math · NaN · Infinity). hover — 커서의 이름: Host 멤버 (서명 ·
// 설명), 필드 (타입), 내장 멤버, 함수 (서명 + 선언 위의 주석), 지역 · 상수 · 컴포넌트.
//
// 분석은 호출마다 검사기를 한 번 돌린다 (캐시 · Worker 는 [계획] — 응답 시간은 시험이 잰다, 27 7장). 팩의 다른 파일도
// 같이 넘긴다 (import · 다른 모듈의 타입).

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "script/api/HostApi.hpp"
#include "script/lang/Checker.hpp"

namespace sbx::script {

enum class CompletionKind : u8 {
    Property,
    Method,
    Field,
    Function,
    Variable,
    Constant,
    Type,
    Enum,
    EnumMember,
    Component,
    Namespace
};

struct CompletionItem {
    std::string label;  // 넣을 이름 ("tick", "read")
    std::string detail; // 서명 ("read(e: Entity, c: ComponentRef<T>): T | null")
    std::string doc;    // 설명 (hover 와 같다)
    CompletionKind kind = CompletionKind::Method;
};

struct LanguageQuery {
    std::span<const SourceFile> files; // 팩의 스크립트 (편집 중인 글 포함 — 저장 전 내용)
    usize file = 0;                    // 그 문서의 자리
    usize offset = 0;                  // 커서 (UTF-8 바이트 — 편집기 단위, 27 9장)
    std::span<const PackImports> imports{};
};

struct HoverInfo {
    std::string title; // 서명 · "필드 hp: number" …
    std::string doc;   // 설명 · 선언 위의 주석 (없으면 빈 글)
    usize begin = 0;   // 그 이름의 바이트 범위 [begin, end)
    usize end = 0;
};

[[nodiscard]] std::vector<CompletionItem> complete(const LanguageQuery& q);
[[nodiscard]] std::optional<HoverInfo> hover(const LanguageQuery& q);

// 그 도메인 콜백의 ctx 멤버 — 이름 순 (같은 이름 없음)
[[nodiscard]] std::vector<CompletionItem> contextMembers(api::HostDomain domain);
// 옛 프로토타입 (v0.5 감사) — 글만 보고 "ctx." 뒤를 도메인으로. 검사기가 쓸 수 없을 때 (파싱 오류) complete 가 이것으로
// 물러선다
[[nodiscard]] std::vector<CompletionItem> completeAt(std::string_view source, usize offset, api::HostDomain domain);

} // namespace sbx::script
