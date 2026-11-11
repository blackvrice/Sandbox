#include "script/vm/ScriptExecutor.hpp"

#include <format>

namespace sbx::script::vm {

std::string_view executorKindName(ExecutorKind k) noexcept {
    switch (k) {
    case ExecutorKind::Interpreter:
        return "interpreter";
    }
    return "?";
}

std::unique_ptr<IScriptExecutor> makeScriptExecutor(ExecutorKind kind, const bc::Module& module, IHost* host,
                                                    Limits limits) {
    switch (kind) {
    case ExecutorKind::Interpreter:
        return std::make_unique<Interpreter>(module, host, limits);
    }
    return std::make_unique<Interpreter>(module, host, limits); // 모르는 종류 — 기준 실행자
}

std::string describeCallDifference(const CallResult& expected, const CallResult& actual) {
    std::string out;
    const auto add = [&](std::string line) {
        out += out.empty() ? "" : "; ";
        out += line;
    };
    if (expected.trap != actual.trap) {
        add(std::format("트랩 {} ≠ {}", trapName(expected.trap), trapName(actual.trap)));
    }
    if (expected.value.bits != actual.value.bits) {
        add(std::format("값 비트 {:#x} ≠ {:#x}", expected.value.bits, actual.value.bits));
    }
    if (expected.fuelUsed != actual.fuelUsed) {
        add(std::format("연료 {} ≠ {}", expected.fuelUsed, actual.fuelUsed));
    }
    if (!expected.ok() || !actual.ok()) {
        if (expected.function != actual.function || expected.pc != actual.pc) {
            add(std::format("트랩 자리 {}:{} ≠ {}:{}", expected.function, expected.pc, actual.function, actual.pc));
        }
        if (expected.stack != actual.stack) {
            add(std::format("호출 경로 {} 칸 ≠ {} 칸 (또는 자리)", expected.stack.size(), actual.stack.size()));
        }
        if (expected.message != actual.message) {
            add(std::format("메시지 \"{}\" ≠ \"{}\"", expected.message, actual.message));
        }
    }
    return out;
}

} // namespace sbx::script::vm
