#pragma once
// 언어 서비스 색인 (v0.9 ST-06-8 — docs/27-SCRIPT-IDE.md 2 · 4장, ADR-0135): 정의로 · 참조 찾기 · 이름 바꾸기 · 빠른
// 수정. LanguageService 와 같은 질의 (팩의 파일 · 문서 · 바이트 offset) 를 받고 검사 결과 기억 (ProgramCache) 을 함께
// 쓴다.
//
// 기호 = 검사기의 Symbol (지역 · 매개변수 · 함수 · 모듈 상수 · 컴포넌트 · enum · interface · type). 쓰는 자리는 세
// 가지: 이름 마디 (검사기가 기호를 적었다), import { X } 의 X (대상 모듈의 export 로 푼다 — 검사기는 가져온 이름을
// 내보낸 쪽의 기호로 묶는다, 그래서 파일 사이 참조가 같은 기호다), 타입 자리의 이름 (타입 번호가 그 기호의 타입). 선언
// 자리는 선언 마디 자리부터 그 이름 낱말. SDK · 내장 · 필드 · enum 멤버 · Host 멤버는 색인하지 않는다 ([계획] — 필드
// 이름 바꾸기).

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "script/lang/LanguageService.hpp"

namespace sbx::script {

struct SourceRange {
    usize file = 0;  // LanguageQuery::files 의 자리
    usize begin = 0; // 바이트 [begin, end)
    usize end = 0;
    friend bool operator==(const SourceRange&, const SourceRange&) = default;
};

// 커서 이름의 기호가 선언된 자리 (다른 파일 · 의존 팩 파일 포함 — files 에 있을 때). SDK · 내장 · 색인 밖이면 없음
[[nodiscard]] std::optional<SourceRange> findDefinition(const LanguageQuery& q);
// 그 기호의 모든 자리 — 선언이 맨 앞, 그다음 파일 · 자리 차례 (같은 자리 없음). 커서가 기호 위가 아니면 빈 목록
[[nodiscard]] std::vector<SourceRange> findReferences(const LanguageQuery& q);

struct FileEdit {
    usize file = 0;
    usize begin = 0; // [begin, end) 를 text 로
    usize end = 0;
    std::string text;
    friend bool operator==(const FileEdit&, const FileEdit&) = default;
};

struct RenameResult {
    std::vector<FileEdit> edits; // 파일 · 자리 차례
    std::string error;           // 거절 이유 (비면 성공)
    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};
// 이름 바꾸기 — 그 기호의 모든 자리 (as 로 다른 이름을 붙인 자리는 그대로). 거절: 이름이 아님 · 예약어, SDK · 내장,
// 의존 팩 ("@") 의 기호, 바꾼 글을 검사해 오류가 늘면 (이름이 부딪힌다 — 그 진단을 이유로)
[[nodiscard]] RenameResult renameSymbol(const LanguageQuery& q, std::string_view newName);

struct QuickFix {
    std::string title; // "import { idiv } from \"sandbox\"" · "'counter' 로 바꾸기"
    std::vector<FileEdit> edits;
};
// 커서 자리의 진단 (S401 없는 이름) 의 빠른 수정: "sandbox" 의 이름 · 팩의 다른 파일이 내보내는 이름이면 import 더하기
// (같은 곳의 import 가 있으면 그 줄에), 비슷한 이름 (편집 거리 2 이하 — 이 모듈의 이름 · 가져온 이름 · 전역, 가까운
// 차례 셋)
[[nodiscard]] std::vector<QuickFix> quickFixes(const LanguageQuery& q);

// 편집 거리 (Levenshtein — 바이트 단위, 대소문자는 1 로 센다)
[[nodiscard]] usize editDistance(std::string_view a, std::string_view b);
// 파일 경로 사이의 import 경로 ("scripts/ai/a.ts" → "scripts/util.ts" = "../util") — 확장자 없이, 같은 폴더면 "./"
[[nodiscard]] std::string relativeImportSpec(std::string_view from, std::string_view to);

} // namespace sbx::script
