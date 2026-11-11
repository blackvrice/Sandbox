#pragma once
// 진단 코드마다 뜻 · 고치는 법 한 줄 (v0.9 M6-T4 조각 3 — docs/19-SANDBOXSCRIPT.md 8장, docs/20-CREATOR-ALPHA.md 9장,
// ADR-0126). 컴파일러가 내는 모든 S 코드가 표에 있다 (시험이 소스의 코드를 모아 본다). 진단 문장은 그 자리의 사실을,
// 고치는 법은 코드의 일반 안내를 — 둘을 섞지 않는다 (문장이 이미 대안을 말해도 표는 그대로).

#include <span>
#include <string_view>

namespace sbx::script {

struct DiagnosticHelp {
    std::string_view code;    // "S301"
    std::string_view meaning; // 짧은 뜻 ("class 는 없다")
    std::string_view fix;     // 고치는 법 한 줄
};

// 코드 차례 (오름차순)
[[nodiscard]] std::span<const DiagnosticHelp> diagnosticHelpTable() noexcept;
// 없으면 nullptr
[[nodiscard]] const DiagnosticHelp* findDiagnosticHelp(std::string_view code) noexcept;

} // namespace sbx::script
