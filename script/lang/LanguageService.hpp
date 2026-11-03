#pragma once
// SandboxScript 언어 서비스 — 프로토타입 (v0.5 Studio 감사, docs/27-SCRIPT-IDE.md 3장, ADR-0091).
//
// 에디터 자동 완성의 첫 조각: "ctx." 뒤의 후보를 Host 표 (script/api/HostApi — 검사기 · d.ts · 지문과 같은 한 곳) 에서
// 콜백 도메인으로 걸러 낸다. 서버 콜백은 ServerContext 멤버만, 클라이언트 콜백은 ClientContext 멤버만 — 잘못된 도메인의
// API 는 후보에 나오지 않는다 (검사기의 S404 와 같은 기준). 타입 추론 · 사용자 컴포넌트 · 증분 분석은 [계획] (27 3장).

#include <string>
#include <string_view>
#include <vector>

#include "script/api/HostApi.hpp"

namespace sbx::script {

enum class CompletionKind : u8 { Property, Method };

struct CompletionItem {
    std::string label;  // 넣을 이름 ("tick", "read")
    std::string detail; // 서명 ("read(e: Entity, c: ComponentRef<T>): T | null")
    std::string doc;    // Host 표의 설명 (hover 와 같다)
    CompletionKind kind = CompletionKind::Method;
};

// 그 도메인 콜백의 ctx 멤버 — 이름 순 (같은 이름 없음)
[[nodiscard]] std::vector<CompletionItem> contextMembers(api::HostDomain domain);

// source 의 offset (바이트) 바로 앞이 "ctx." 또는 "ctx.<접두>" 이면 접두로 시작하는 ctx 멤버, 아니면 빈 목록.
// 프로토타입 — 수신자 이름이 ctx 일 때만 본다 (식의 타입으로 고르는 것은 검사기의 기호 표를 쓰는 [계획])
[[nodiscard]] std::vector<CompletionItem> completeAt(std::string_view source, usize offset, api::HostDomain domain);

} // namespace sbx::script
