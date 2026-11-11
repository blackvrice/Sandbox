// 언어 서비스 프로토타입 (v0.5 Studio 감사, docs/27-SCRIPT-IDE.md 3장, ADR-0091): "ctx." 자동 완성이 Host 표 하나에서
// 도메인으로 걸러진다 — 서버 콜백에 클라이언트 API 가, 클라이언트 콜백에 서버 쓰기 API 가 나오지 않는다.
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "script/lang/LanguageService.hpp"
#include "script/lang/MemberCatalog.hpp"
#include "script/vm/Intrinsics.hpp"

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

// ---- v0.9 M6-T8 — 타입으로 고르는 완성 · hover · 내장 멤버 표 (ADR-0129)
// ----------------------------------------------

namespace {

constexpr std::string_view kManaTs = R"(import { defineComponent, field } from "sandbox"
export const Mana = defineComponent({
  id: "eco.mana",
  fields: { current: field.f32(20), max: field.f32(100) },
})
)";

// 커서 자리 = 글의 "|" (지운다)
struct Doc {
    std::vector<SourceFile> files;
    usize offset = 0;
};
Doc docWith(std::string_view withCursor, std::vector<SourceFile> others = {}) {
    Doc d;
    std::string text(withCursor);
    const usize at = text.find('|');
    REQUIRE(at != std::string::npos);
    text.erase(at, 1);
    d.offset = at;
    d.files.push_back({"scripts/main.ts", std::move(text)});
    for (SourceFile& f : others) {
        d.files.push_back(std::move(f));
    }
    return d;
}
std::vector<CompletionItem> completeDoc(const Doc& d) {
    return complete(LanguageQuery{d.files, 0, d.offset});
}
std::optional<HoverInfo> hoverDoc(const Doc& d) {
    return hover(LanguageQuery{d.files, 0, d.offset});
}
std::vector<std::string> labels(const std::vector<CompletionItem>& items) {
    std::vector<std::string> out;
    for (const CompletionItem& c : items) {
        out.push_back(c.label);
    }
    return out;
}

} // namespace

TEST_SUITE("script") {
    TEST_CASE(
        "language service (v0.9 M6-T8): 식의 타입 뒤 '.' — 구조체 · 컴포넌트 데이터 · Host · 내장 · enum · Math") {
        // 서버 콜백의 ctx — 타입 (ServerContext) 이 도메인을 정한다
        const std::string sys = R"(import { defineSystem, Stack } from "sandbox"
import { Mana } from "./mana"
interface Unit { hp: number; name: string }
enum Mood { Calm, Angry }
function first(us: Unit[]): Unit { return us[0] }
export default defineSystem({
  id: "t.sys",
  onTick(ctx) {
    const us: Unit[] = []
    const st = new Stack<number>()
    for (const e of ctx.query(Mana)) {
      const m = ctx.read(e, Mana)
      if (m !== null) { @ }
    }
  },
})
)";
        const auto at = [&](std::string_view code) {
            std::string s = sys;
            s.replace(s.find('@'), 1, code);
            return docWith(s, {{"scripts/mana.ts", std::string(kManaTs)}});
        };
        // ctx. — 서버 멤버, 클라이언트 것 없음 · 접두
        auto c = completeDoc(at("ctx.|"));
        CHECK(has(c, "read"));
        CHECK(has(c, "write"));
        CHECK_FALSE(has(c, "label")); // ClientContext
        c = completeDoc(at("ctx.wr|"));
        CHECK(labels(c) == std::vector<std::string>{"write"});
        // 컴포넌트 데이터 (ctx.read 의 결과 — 좁힌 m) 의 필드
        c = completeDoc(at("m.|"));
        CHECK(has(c, "current"));
        CHECK(has(c, "max"));
        CHECK(c.front().kind == CompletionKind::Field);
        // 배열 · 구조체 (함수 결과의 타입) · 컬렉션 · 문자열 · enum · Math · vec2
        c = completeDoc(at("us.|"));
        CHECK(has(c, "push"));
        CHECK(has(c, "length"));
        CHECK(has(c, "filter"));
        c = completeDoc(at("first(us).|"));
        CHECK(labels(c) == std::vector<std::string>{"hp", "name"});
        c = completeDoc(at("first(us).name.|"));
        CHECK(labels(c) == std::vector<std::string>{"length"});
        c = completeDoc(at("st.p|"));
        CHECK(labels(c) == std::vector<std::string>{"peek", "pop", "push"});
        c = completeDoc(at("Mood.|"));
        CHECK(labels(c) == std::vector<std::string>{"Angry", "Calm"});
        c = completeDoc(at("Math.fl|"));
        CHECK(labels(c) == std::vector<std::string>{"floor"});
        c = completeDoc(at("e.|")); // Entity — Host 표의 Entity 멤버 (없으면 빈 목록이어도 죽지 않는다)
        (void)c;
        // 대소문자를 가리지 않는 접두 — 정확히 맞는 것 먼저
        c = completeDoc(at("ctx.RE|"));
        CHECK(has(c, "read"));
        // 호출 안의 자리 (뒤에 인자가 이어진다)
        c = completeDoc(at("ctx.re|(e, Mana)"));
        CHECK(has(c, "read"));
        // 클라이언트 콜백의 ctx
        const Doc client = docWith(R"(import { defineSystem } from "sandbox"
export default defineSystem({
  id: "t.hud",
  domain: "client",
  onUpdate(ctx) { ctx.| },
})
)");
        c = completeDoc(client);
        CHECK(has(c, "label"));
        CHECK_FALSE(has(c, "write"));
    }

    TEST_CASE("language service (v0.9 M6-T8): import { | } · 이름 완성 (지역 · 매개변수 · 모듈 · 가져온 이름)") {
        auto c = completeDoc(docWith("import { defineSys| } from \"sandbox\"\n"));
        CHECK(labels(c) == std::vector<std::string>{"defineSystem"});
        c = completeDoc(docWith("import { defineSystem, St| } from \"sandbox\"\n"));
        CHECK(has(c, "Stack"));
        c = completeDoc(docWith("import { | } from \"sandbox\"\n"));
        CHECK(has(c, "idiv"));
        CHECK(has(c, "Transform")); // 엔진 컴포넌트
        c = completeDoc(docWith("import { idiv, | } from \"sandbox\"\n"));
        CHECK_FALSE(has(c, "idiv")); // 이미 쓴 이름
        c = completeDoc(docWith("import { M| } from \"./mana\"\n", {{"scripts/mana.ts", std::string(kManaTs)}}));
        CHECK(labels(c) == std::vector<std::string>{"Mana"});

        const Doc d = docWith(R"(import { idiv } from "sandbox"
const LIMIT = 10
function helper(alpha: number): number { const inner = 1; return alpha + inner }
export function main(beta: number): number {
  const amount = 2
  return a|
}
)");
        c = completeDoc(d);
        CHECK(has(c, "amount"));
        CHECK_FALSE(has(c, "alpha")); // 다른 함수의 매개변수
        c = completeDoc(docWith(R"(const LIMIT = 10
function helper(alpha: number): number { return alpha }
export function main(beta: number): number {
  return L| + be
}
)"));
        CHECK(has(c, "LIMIT"));
        c = completeDoc(docWith(R"(function helper(alpha: number): number { return alpha }
export function main(beta: number): number { return he| }
)"));
        REQUIRE(has(c, "helper"));
        CHECK(c.front().detail == "function helper(alpha: number): number");
        c = completeDoc(docWith("import { idiv } from \"sandbox\"\nexport function f(): number { return id| }\n"));
        CHECK(has(c, "idiv"));
        // 접두가 없으면 쏟지 않는다
        CHECK(completeDoc(docWith("export function f(): number { return | }\n")).empty());
    }

    TEST_CASE("language service (v0.9 M6-T8): hover — Host · 필드 · 내장 멤버 · 함수 (주석) · 지역 · 컴포넌트") {
        const std::string src = R"(import { defineSystem } from "sandbox"
import { Mana } from "./mana"
interface Unit { hp: number }
// 체력을 반으로
// (0 은 그대로)
function halve(u: Unit): number { return u.hp / 2 }
export default defineSystem({
  id: "t.sys",
  onTick(ctx) {
    const xs: number[] = [1]
    xs.push(2)
    const t = ctx.tick
    const h = halve({ hp: 4 })
    const k = Math.floor(1.5)
    for (const e of ctx.query(Mana)) { ctx.log("x") }
  },
})
)";
        const auto hoverAt = [&](std::string_view needle, usize add = 1) {
            Doc d;
            d.files = {{"scripts/main.ts", src}, {"scripts/mana.ts", std::string(kManaTs)}};
            const usize at = src.find(needle);
            REQUIRE(at != std::string::npos);
            d.offset = at + add;
            return hoverDoc(d);
        };
        auto h = hoverAt("tick");
        REQUIRE(h.has_value());
        CHECK(h->title == "tick: i32");
        CHECK_FALSE(h->doc.empty());
        CHECK(src.substr(h->begin, h->end - h->begin) == "tick");
        h = hoverAt("u.hp", 3);
        REQUIRE(h.has_value());
        CHECK(h->title == "필드 Unit.hp: number");
        h = hoverAt("push");
        REQUIRE(h.has_value());
        CHECK(h->title == "Array.push(x: T): i32");
        h = hoverAt("floor");
        REQUIRE(h.has_value());
        CHECK(h->title.starts_with("Math.floor("));
        h = hoverAt("halve({");
        REQUIRE(h.has_value());
        CHECK(h->title == "function halve(u: Unit): number");
        CHECK(h->doc == "체력을 반으로\n(0 은 그대로)");
        h = hoverAt("halve(u"); // 선언 자리의 이름
        REQUIRE(h.has_value());
        CHECK(h->title == "function halve(u: Unit): number");
        h = hoverAt("xs.push", 0);
        REQUIRE(h.has_value());
        CHECK(h->title == "const xs: number[]");
        h = hoverAt("Mana)) {", 1);
        REQUIRE(h.has_value());
        CHECK(h->title.starts_with("컴포넌트 Mana — \"eco.mana\""));
        h = hoverAt("log(");
        REQUIRE(h.has_value());
        CHECK(h->title.starts_with("log("));
        CHECK_FALSE(hoverAt("  id:", 0).has_value()); // 빈칸
    }

    TEST_CASE("language service (v0.9 M6-T8): 내장 멤버 표 = 검사기 · 내장 표") {
        // 표의 모든 이름을 검사기가 받는다 (그 이름에 S406 없음) — 받는 쪽 타입마다 작은 함수
        const std::vector<std::pair<BuiltinReceiver, std::string>> decl{
            {BuiltinReceiver::Array, "const v: number[] = [1]"},
            {BuiltinReceiver::String, "const v = \"a\""},
            {BuiltinReceiver::Map, "const v = new Map<string, number>()"},
            {BuiltinReceiver::Set, "const v = new Set<number>()"},
            {BuiltinReceiver::Stack, "const v = new Stack<number>()"},
            {BuiltinReceiver::Queue, "const v = new Queue<number>()"},
            {BuiltinReceiver::Deque, "const v = new Deque<number>()"},
            {BuiltinReceiver::PriorityQueue, "const v = new PriorityQueue<number>()"},
            {BuiltinReceiver::MinHeap, "const v = new MinHeap()"},
            {BuiltinReceiver::MaxHeap, "const v = new MaxHeap()"},
            {BuiltinReceiver::BitSet, "const v = new BitSet(8)"},
            {BuiltinReceiver::Graph, "const v = new Graph(4, false)"},
        };
        for (const auto& [r, d] : decl) {
            const auto members = builtinMembers(r);
            REQUIRE_FALSE(members.empty());
            for (usize i = 1; i < members.size(); ++i) {
                CHECK(members[i - 1].name < members[i].name); // 이름 순
            }
            for (const MemberDoc& m : members) {
                CAPTURE(builtinReceiverName(r));
                CAPTURE(m.name);
                const std::string use =
                    m.property ? std::format("const w = v.{}", m.name) : std::format("v.{}()", m.name);
                const std::string src = std::format("import {{ Stack, Queue, Deque, PriorityQueue, MinHeap, MaxHeap, "
                                                    "BitSet, Graph }} from \"sandbox\"\n"
                                                    "export function f(): void {{\n  {}\n  {}\n}}\n",
                                                    d, use);
                DiagnosticList diags;
                const SourceFile f{"scripts/a.ts", src};
                (void)checkProgram(std::span(&f, 1), diags);
                for (const Diagnostic& x : diags.items()) {
                    CAPTURE(x.describe());
                    CHECK(x.code != "S406");
                }
            }
        }
        for (const auto r : {BuiltinReceiver::Math, BuiltinReceiver::Vec2}) {
            for (const MemberDoc& m : builtinMembers(r)) {
                CAPTURE(m.name);
                const std::string src = std::format(
                    "import {{ vec2, Vec2 }} from \"sandbox\"\nexport function f(): void {{\n  const w = {}.{}{}\n}}\n",
                    builtinReceiverName(r), m.name, m.property ? "" : "()");
                DiagnosticList diags;
                const SourceFile f{"scripts/a.ts", src};
                (void)checkProgram(std::span(&f, 1), diags);
                for (const Diagnostic& x : diags.items()) {
                    CAPTURE(x.describe());
                    CHECK(x.code != "S406");
                }
            }
        }
        // 내장 표 (Intrinsics) 의 받는 쪽 메서드가 모두 멤버 표에 (Array.sort(cmp) → sort, Coll · Seq · Heap 은 그
        // 종류들)
        const auto inTable = [](std::initializer_list<BuiltinReceiver> rs, std::string_view n) {
            for (const BuiltinReceiver r : rs) {
                for (const MemberDoc& m : builtinMembers(r)) {
                    if (m.name == n) {
                        return true;
                    }
                }
            }
            return false;
        };
        for (const Intrinsic& k : kIntrinsics) {
            const std::string_view full = k.name;
            const usize dot = full.find('.');
            if (dot == std::string_view::npos) {
                continue; // 받는 쪽이 없는 내장
            }
            const std::string_view recv = full.substr(0, dot);
            std::string_view method = full.substr(dot + 1);
            method = method.substr(0, method.find('('));
            if (method == "new" || method == "from" || recv == "Algo" || recv == "Math" || method == "next" ||
                method == "keyAt" || method == "valueAt") {
                continue; // 만들기 · 이름공간 함수 · for-of 커서 (검사기가 반복에 쓴다 — 사람이 부르는 멤버가 아니다)
            }
            CAPTURE(full);
            using B = BuiltinReceiver;
            if (recv == "Array") {
                CHECK(inTable({B::Array}, method));
            } else if (recv == "Map") {
                CHECK(inTable({B::Map}, method));
            } else if (recv == "Set") {
                if (method == "union" || method == "intersection" || method == "difference") {
                    continue; // setUnion … ("sandbox" 함수)
                }
                CHECK(inTable({B::Set}, method));
            } else if (recv == "Graph") {
                CHECK(inTable({B::Graph}, method));
            } else if (recv == "BitSet") {
                CHECK(inTable({B::BitSet}, method));
            } else if (recv == "vec2") {
                CHECK(inTable({B::Vec2}, method));
            } else if (recv == "Heap" && method == "extract") {
                CHECK(inTable({B::MinHeap}, "extractMin")); // 검사기가 종류로 이름을 고른다
                CHECK(inTable({B::MaxHeap}, "extractMax"));
            } else if (recv == "Coll" || recv == "Seq" || recv == "Heap" || recv == "PriorityQueue") {
                CHECK(inTable({B::Stack, B::Queue, B::Deque, B::PriorityQueue, B::MinHeap, B::MaxHeap}, method));
            }
        }
    }

    TEST_CASE("language service (v0.9 M6-T8): 응답 시간 — 1,000 · 5,000 줄 파일 (27 7장)") {
        for (const int lines : {1000, 5000}) {
            std::string src = "import { defineSystem } from \"sandbox\"\ninterface P { hp: number; name: string }\n";
            int written = 2;
            int fn = 0;
            while (written < lines - 12) {
                src += std::format("function f{}(p: P, k: number): number {{\n", fn);
                for (int j = 0; j < 8; ++j) {
                    src += std::format("  const a{} = p.hp * k + {}\n", j, j);
                }
                src += "  return a7\n}\n";
                written += 11;
                ++fn;
            }
            src += "export default defineSystem({\n  id: \"t.big\",\n  onTick(ctx) {\n    const p: P = { hp: 1, name: "
                   "\"a\" }\n    p.|\n  },\n})\n";
            const Doc d = docWith(src);
            const auto t0 = std::chrono::steady_clock::now();
            const auto c = completeDoc(d);
            const auto t1 = std::chrono::steady_clock::now();
            CHECK(labels(c) == std::vector<std::string>{"hp", "name"});
            std::vector<SourceFile> whole = d.files; // hover 는 완성된 글에서 ("p." 는 파싱 오류)
            whole[0].text.insert(d.offset, "hp");
            const auto h = hover(LanguageQuery{whole, 0, whole[0].text.find("f0(") + 1});
            const auto t2 = std::chrono::steady_clock::now();
            CHECK(h.has_value());
            const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            const double hoverMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
            MESSAGE("완성 응답 " << lines << " 줄: " << ms << " ms · hover " << hoverMs
                                 << " ms (이 빌드 — Debug 는 Release 보다 몇 배 느리다)");
            CHECK(ms < 5000.0); // 막힘 감시만 — 숫자는 문서에 측정값으로
        }
    }
}
