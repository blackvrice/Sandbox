// 언어 서비스 프로토타입 (v0.5 Studio 감사, docs/27-SCRIPT-IDE.md 3장, ADR-0091): "ctx." 자동 완성이 Host 표 하나에서
// 도메인으로 걸러진다 — 서버 콜백에 클라이언트 API 가, 클라이언트 콜백에 서버 쓰기 API 가 나오지 않는다.
#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "script/lang/LanguageService.hpp"

using namespace sbx;
using namespace sbx::script;

namespace {

bool has(const std::vector<CompletionItem>& items, std::string_view label) {
    return std::any_of(items.begin(), items.end(), [&](const CompletionItem& c) { return c.label == label; });
}

} // namespace

TEST_SUITE("script") {
    TEST_CASE("language service: ctx 멤버 — 도메인으로 거른다, 이름 순 · 겹침 없음 · 설명 있음") {
        const std::vector<CompletionItem> server = contextMembers(api::HostDomain::Server);
        const std::vector<CompletionItem> client = contextMembers(api::HostDomain::Client);
        REQUIRE_FALSE(server.empty());
        REQUIRE_FALSE(client.empty());
        CHECK(has(server, "write"));
        CHECK(has(server, "spawn"));
        CHECK(has(server, "tick"));
        CHECK_FALSE(has(server, "label")); // 클라이언트 표시 API
        CHECK(has(client, "label"));
        CHECK_FALSE(has(client, "write")); // 서버만 쓴다
        CHECK_FALSE(has(client, "spawn"));
        for (const std::vector<CompletionItem>* list : {&server, &client}) {
            CHECK(std::is_sorted(list->begin(), list->end(),
                                 [](const CompletionItem& a, const CompletionItem& b) { return a.label < b.label; }));
            CHECK(std::adjacent_find(list->begin(), list->end(), [](const CompletionItem& a, const CompletionItem& b) {
                      return a.label == b.label;
                  }) == list->end());
            for (const CompletionItem& c : *list) {
                CAPTURE(c.label);
                CHECK_FALSE(c.doc.empty());
                CHECK(c.detail.starts_with(c.label));
            }
        }
        const auto tick =
            std::find_if(server.begin(), server.end(), [](const CompletionItem& c) { return c.label == "tick"; });
        REQUIRE(tick != server.end());
        CHECK(tick->kind == CompletionKind::Property);
    }

    TEST_CASE("language service: completeAt — \"ctx.\" · 접두 · 수신자가 ctx 가 아니면 없다") {
        const std::string src = "onTick(ctx) { ctx.re }";
        const usize at = src.find("ctx.re") + 6;
        const std::vector<CompletionItem> got = completeAt(src, at, api::HostDomain::Server);
        REQUIRE_FALSE(got.empty());
        for (const CompletionItem& c : got) {
            CHECK(c.label.starts_with("re"));
        }
        CHECK(has(got, "read"));
        CHECK(has(got, "requestPath"));
        const std::string dot = "ctx.";
        CHECK(completeAt(dot, dot.size(), api::HostDomain::Server).size() ==
              contextMembers(api::HostDomain::Server).size());
        const std::string other = "foo.re";
        CHECK(completeAt(other, other.size(), api::HostDomain::Server).empty());
        const std::string noDot = "ctxre";
        CHECK(completeAt(noDot, noDot.size(), api::HostDomain::Server).empty());
        CHECK(completeAt(src, src.size() + 5, api::HostDomain::Server).empty()); // 범위 밖
        const std::string clientSrc = "ctx.wr";
        CHECK(completeAt(clientSrc, clientSrc.size(), api::HostDomain::Client).empty()); // 클라이언트엔 write 가 없다
    }
}
