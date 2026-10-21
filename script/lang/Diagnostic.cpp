#include "script/lang/Diagnostic.hpp"

#include <format>

namespace sbx::script {

std::string Diagnostic::describe() const {
    return std::format("{}:{}:{}: {} {} — {}", file, pos.line, pos.column,
                       severity == Severity::Error ? "오류" : "경고", code, message);
}

void DiagnosticList::error(std::string code, std::string_view file, SourcePos pos, std::string message) {
    m_items.push_back({Diagnostic::Severity::Error, std::move(code), std::string(file), pos, std::move(message)});
    ++m_errors;
}

void DiagnosticList::warning(std::string code, std::string_view file, SourcePos pos, std::string message) {
    m_items.push_back({Diagnostic::Severity::Warning, std::move(code), std::string(file), pos, std::move(message)});
}

std::string DiagnosticList::describeAll() const {
    std::string out;
    for (const Diagnostic& d : m_items) {
        out += d.describe();
        out += '\n';
    }
    return out;
}

} // namespace sbx::script
