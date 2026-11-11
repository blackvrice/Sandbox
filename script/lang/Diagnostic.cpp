#include "script/lang/Diagnostic.hpp"

#include <format>

#include "script/lang/DiagnosticHelp.hpp"

namespace sbx::script {

std::string Diagnostic::describe() const {
    return std::format("{}:{}:{}: {} {} — {}", file, pos.line, pos.column,
                       severity == Severity::Error ? "오류" : "경고", code, message);
}

std::string Diagnostic::describeWithHelp() const {
    return help.empty() ? describe() : std::format("{}\n    고치는 법: {}", describe(), help);
}

namespace {

std::string helpOf(std::string_view code) {
    const DiagnosticHelp* h = findDiagnosticHelp(code);
    return h != nullptr ? std::string(h->fix) : std::string();
}

} // namespace

void DiagnosticList::error(std::string code, std::string_view file, SourcePos pos, std::string message) {
    std::string help = helpOf(code);
    m_items.push_back(
        {Diagnostic::Severity::Error, std::move(code), std::string(file), pos, std::move(message), std::move(help)});
    ++m_errors;
}

void DiagnosticList::warning(std::string code, std::string_view file, SourcePos pos, std::string message) {
    std::string help = helpOf(code);
    m_items.push_back(
        {Diagnostic::Severity::Warning, std::move(code), std::string(file), pos, std::move(message), std::move(help)});
}

std::string DiagnosticList::describeAll() const {
    std::string out;
    for (const Diagnostic& d : m_items) {
        out += d.describe();
        out += '\n';
    }
    return out;
}

std::string DiagnosticList::describeAllWithHelp() const {
    std::string out;
    for (const Diagnostic& d : m_items) {
        out += d.describeWithHelp();
        out += '\n';
    }
    return out;
}

} // namespace sbx::script
