#include "core/script/ScriptHost.hpp"

#include <format>

#include "foundation/assert/Assert.hpp"
#include "foundation/log/Log.hpp"
#include "foundation/text/Utf8.hpp"

namespace sbx::script {

namespace {

using api::AbiType;

// ctx 핸들: 위쪽 16 비트 표지 + 호출 번호 (작은 정수 · 다른 핸들과 섞이지 않게)
constexpr u64 kCtxTag = 0x5C78'0000'0000'0000ull;

vm::HostResult ctxTick(ScriptHost& host, std::span<const vm::HostArg>) {
    return {true, vm::Value::i32v(static_cast<i32>(host.tick())), {}};
}

vm::HostResult ctxLog(ScriptHost& host, std::span<const vm::HostArg> args) {
    host.log(args[1].text);
    return {true, {}, {}};
}

// 표 (script/api/HostApi) 의 항목마다 하나 — 순서는 표와 같지 않아도 된다 (id 로 찾는다)
constexpr HostBinding kBindings[] = {
    {"ctx.tick", AbiType::I32, {AbiType::ServerContext}, 1, &ctxTick},
    {"ctx.log", AbiType::Void, {AbiType::ServerContext, AbiType::String}, 2, &ctxLog},
};

} // namespace

std::span<const HostBinding> hostBindings() {
    return kBindings;
}

std::vector<std::string> checkHostBindings(std::span<const HostBinding> bindings) {
    std::vector<std::string> problems;
    for (const api::HostFunction& f : api::kHostFunctions) {
        usize count = 0;
        for (const HostBinding& b : bindings) {
            if (b.id != f.id) {
                continue;
            }
            ++count;
            bool same = b.result == f.result && b.paramCount == f.paramCount + 1 && b.params[0] == f.owner;
            for (u8 i = 0; same && i < f.paramCount; ++i) {
                same = b.params[i + 1] == f.params[i];
            }
            if (!same) {
                problems.push_back(
                    std::format("{}: 구현의 서명이 표와 다르다 (표: {})", f.id, api::canonicalSignature(f)));
            }
            if (b.impl == nullptr) {
                problems.push_back(std::format("{}: 구현 함수가 없다", f.id));
            }
        }
        if (count == 0) {
            problems.push_back(std::format("{}: 구현이 없다", f.id));
        } else if (count > 1) {
            problems.push_back(std::format("{}: 구현이 {} 개다", f.id, count));
        }
    }
    for (const HostBinding& b : bindings) {
        if (api::findHostFunction(b.id) == nullptr) {
            problems.push_back(std::format("{}: 표에 없는 구현", b.id));
        }
    }
    return problems;
}

ScriptHost::ScriptHost() {
    SBX_ASSERT(checkHostBindings(hostBindings()).empty(),
               "Host 바인딩이 Host API 표와 맞지 않는다 (checkHostBindings)");
    m_byHostIndex.assign(api::kHostFunctions.size(), nullptr);
    for (const HostBinding& b : hostBindings()) {
        if (const api::HostFunction* f = api::findHostFunction(b.id)) {
            m_byHostIndex[api::hostFunctionIndex(*f)] = &b;
        }
    }
}

vm::Value ScriptHost::beginCall(sim::Tick tick, std::string script) {
    m_token = kCtxTag | (m_nextToken++ & 0x0000'FFFF'FFFF'FFFFull);
    m_tick = tick;
    m_script = std::move(script);
    return vm::Value::opaque(m_token);
}

void ScriptHost::endCall() {
    m_token = 0;
}

vm::HostResult ScriptHost::call(const api::HostFunction& fn, std::span<const vm::HostArg> args) {
    const usize index = api::hostFunctionIndex(fn);
    const HostBinding* b = index < m_byHostIndex.size() ? m_byHostIndex[index] : nullptr;
    if (b == nullptr) {
        return {false, {}, std::format("{} 의 구현이 없다", fn.id)};
    }
    // 첫 인자는 주인 — 지금 열린 호출의 ctx 여야 한다
    if (m_token == 0 || args.empty() || args[0].value.bits != m_token) {
        return {false, {}, "ctx 가 지금 호출의 것이 아니다 (지난 호출의 ctx · 위조한 핸들)"};
    }
    return b->impl(*this, args);
}

void ScriptHost::log(std::string_view text) {
    std::string line(text.substr(0, kMaxLogBytes));
    if (text.size() > kMaxLogBytes) {
        // UTF-8 글자 가운데를 자르지 않는다
        while (!line.empty() && (static_cast<unsigned char>(line.back()) & 0xC0) == 0x80) {
            line.pop_back();
        }
        if (!line.empty() && static_cast<unsigned char>(line.back()) >= 0xC0) {
            line.pop_back();
        }
        line += "…";
    }
    log::info("script", "{} t{}: {}", m_script, m_tick, line);
    if (m_logs.size() >= kKeptLogLines) {
        m_logs.erase(m_logs.begin());
    }
    m_logs.push_back({m_tick, m_script, std::move(line)});
}

} // namespace sbx::script
