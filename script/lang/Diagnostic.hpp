#pragma once
// SandboxScript 진단. docs/19-SANDBOXSCRIPT.md 8장.
//
// 메시지 형식: "<파일>:<줄>:<열>: 오류 S201 — <설명>" (줄 · 열은 1 부터, 열은 코드 포인트 단위).
// 규칙 번호: S1xx 낱말 (렉서), S2xx 문법 (파서), S3xx 부분집합 밖 (TS 는 받지만 SandboxScript 는 받지 않는다).
// 컴파일러는 첫 오류에서 멈추지 않고 모은다 — ContentIssue (core/content) 와 같은 방식이고, core/script 가 그쪽으로
// 옮긴다.

#include <string>
#include <vector>

#include "foundation/types/Types.hpp"

namespace sbx::script {

// 소스 안의 자리. offset 은 바이트, line · column 은 1 부터 (column = 코드 포인트)
struct SourcePos {
    u32 offset = 0;
    u32 line = 1;
    u32 column = 1;
};

struct Diagnostic {
    enum class Severity : u8 { Error, Warning };
    Severity severity = Severity::Error;
    std::string code; // "S101"
    std::string file; // "eco/scripts/mana.ts"
    SourcePos pos;
    std::string message;
    std::string
        help{}; // 고치는 법 한 줄 (v0.9 M6-T4 — DiagnosticHelp 표, DiagnosticList 가 채운다. 표에 없으면 비었다)

    [[nodiscard]] std::string describe() const;
    // describe() + 고치는 법 줄 ("\n    고치는 법: …" — 없으면 describe() 그대로)
    [[nodiscard]] std::string describeWithHelp() const;
};

class DiagnosticList {
public:
    void error(std::string code, std::string_view file, SourcePos pos, std::string message);
    void warning(std::string code, std::string_view file, SourcePos pos, std::string message);

    [[nodiscard]] const std::vector<Diagnostic>& items() const noexcept { return m_items; }
    [[nodiscard]] usize errorCount() const noexcept { return m_errors; }
    [[nodiscard]] bool ok() const noexcept { return m_errors == 0; }
    // 줄 하나에 하나씩 describe() (시험 · 도구 출력)
    [[nodiscard]] std::string describeAll() const;
    // 진단마다 describeWithHelp() (사람에게 보일 출력 — 골든 시험)
    [[nodiscard]] std::string describeAllWithHelp() const;

private:
    std::vector<Diagnostic> m_items;
    usize m_errors = 0;
};

} // namespace sbx::script
