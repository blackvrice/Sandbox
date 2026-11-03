#include "script/lang/LanguageService.hpp"

#include <algorithm>

namespace sbx::script {
namespace {

[[nodiscard]] bool identChar(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$';
}

[[nodiscard]] std::string signatureOf(const api::HostFunction& f) {
    std::string s(f.member);
    if (!f.property) {
        s += '(';
        for (u8 i = 0; i < f.paramCount; ++i) {
            if (i > 0) {
                s += ", ";
            }
            s += f.paramNames[i];
            s += ": ";
            s += api::abiTypeName(f.params[i]);
        }
        s += ')';
    }
    s += ": ";
    s += api::abiTypeName(f.result);
    return s;
}

} // namespace

std::vector<CompletionItem> contextMembers(api::HostDomain domain) {
    const api::AbiType owner =
        domain == api::HostDomain::Server ? api::AbiType::ServerContext : api::AbiType::ClientContext;
    std::vector<CompletionItem> out;
    for (const api::HostFunction& f : api::kHostFunctions) {
        if (f.owner != owner || f.domain != domain) {
            continue;
        }
        out.push_back(CompletionItem{std::string(f.member), signatureOf(f), std::string(f.doc),
                                     f.property ? CompletionKind::Property : CompletionKind::Method});
    }
    std::sort(out.begin(), out.end(),
              [](const CompletionItem& a, const CompletionItem& b) { return a.label < b.label; });
    return out;
}

std::vector<CompletionItem> completeAt(std::string_view source, usize offset, api::HostDomain domain) {
    if (offset > source.size()) {
        return {};
    }
    // 커서 앞의 접두 (식별자 글자) → 그 앞이 '.' → 그 앞의 수신자 이름
    usize p = offset;
    while (p > 0 && identChar(source[p - 1])) {
        --p;
    }
    const std::string_view prefix = source.substr(p, offset - p);
    if (p == 0 || source[p - 1] != '.') {
        return {};
    }
    usize r = p - 1;
    const usize end = r;
    while (r > 0 && identChar(source[r - 1])) {
        --r;
    }
    if (source.substr(r, end - r) != "ctx") {
        return {};
    }
    std::vector<CompletionItem> all = contextMembers(domain);
    std::erase_if(all, [&](const CompletionItem& c) { return !c.label.starts_with(prefix); });
    return all;
}

} // namespace sbx::script
