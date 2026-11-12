#pragma once
// 검사 결과 기억 (v0.9 ST-06-8 — docs/27-SCRIPT-IDE.md 2 · 7장, ADR-0135). 언어 서비스의 질의 (완성 · hover · 정의 ·
// 참조 · 이름 바꾸기 · 빠른 수정) 와 편집기의 진단은 같은 글로 검사기를 여러 번 부른다 — 마지막 입력 (파일 경로 · 글 ·
// 팩 import) 의 결과를 몇 개 기억해 같은 입력이면 다시 검사하지 않는다. 열쇠는 입력의 FNV-1a 64 (+ 크기 — 부딪힘에
// 대비해 글 전체도 비교한다). 파일 단위 증분 검사 (바뀐 파일과 그 파일을 가져오는 파일만) 는 [계획] — 검사기가 모듈
// 사이 상태를 나눠야 한다.

#include <memory>
#include <span>

#include "script/lang/Checker.hpp"
#include "script/lang/Diagnostic.hpp"

namespace sbx::script {

struct AnalyzedProgram {
    Program program;
    DiagnosticList diagnostics;
};

// 같은 입력이면 기억한 결과 (스레드 안전 — 잠금)
[[nodiscard]] std::shared_ptr<const AnalyzedProgram> analyzeCached(std::span<const SourceFile> files,
                                                                   std::span<const PackImports> imports = {});

struct ProgramCacheStats {
    u64 hits = 0;
    u64 misses = 0;
};
[[nodiscard]] ProgramCacheStats programCacheStats();
void clearProgramCache();

} // namespace sbx::script
