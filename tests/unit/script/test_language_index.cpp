// 언어 서비스 색인 (v0.9 ST-06-8 조각 1 — script/lang/LanguageIndex · ProgramCache, ADR-0135): 정의로 (다른 파일 · 타입
// · 지역 · 매개변수 · SDK 는 없음), 참조 (선언 먼저 · import · 타입 자리 · as), 이름 바꾸기 (파일 사이 · as 그대로 ·
// 바꾼 뒤 컴파일 · 거절 — 이름 규칙 · 예약어 · SDK · 의존 팩 · 부딪힘), 빠른 수정 (sandbox · 다른 파일 import · 같은
// import 줄에 · 비슷한 이름), 편집 거리 · 상대 import 경로, 검사 결과 기억, 응답 시간 (MESSAGE).
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <string>
#include <vector>

#include "script/lang/LanguageIndex.hpp"
#include "script/lang/ProgramCache.hpp"

using namespace sbx;
using namespace sbx::script;

namespace {

constexpr std::string_view kMana = R"(import { defineComponent, field } from "sandbox"
export const Mana = defineComponent({ id: "t.mana", fields: { current: field.f32(1) } })
export interface Bonus { amount: number }
export function refill(current: number, b: Bonus): number {
  const next = current + b.amount
  return next
}
)";

constexpr std::string_view kRegen = R"(import { defineSystem } from "sandbox"
import { Mana, refill, Bonus } from "./mana"
import { cap } from "@inv/util"
const STEP = 2
function bonusOf(n: number): Bonus {
  return { amount: n * STEP }
}
export default defineSystem({
  id: "t.regen",
  onTick(ctx) {
    for (const e of ctx.query(Mana)) {
      const m = ctx.read(e, Mana)
      if (m !== null) {
        m.current = cap(refill(m.current, bonusOf(1)))
        ctx.write(e, Mana, m)
      }
    }
  },
})
)";

constexpr std::string_view kOther = R"(import { refill as fill } from "../mana"
export function twice(x: number): number { return fill(x, { amount: 1 }) }
)";

constexpr std::string_view kUtil = "export function cap(x: number): number { return Math.min(x, 100) }\n";

struct Pack {
    std::vector<SourceFile> files{{"scripts/mana.ts", std::string(kMana)},
                                  {"scripts/regen.ts", std::string(kRegen)},
                                  {"scripts/ai/other.ts", std::string(kOther)},
                                  {"@inv/scripts/util.ts", std::string(kUtil)}};
    std::vector<PackImports> imports{{std::string(), {"inv"}}, {"inv", {}}};

    [[nodiscard]] usize index(std::string_view path) const {
        for (usize i = 0; i < files.size(); ++i) {
            if (files[i].path == path) {
                return i;
            }
        }
        FAIL("파일 없음");
        return 0;
    }
    // 그 파일에서 needle 의 (n 번째) 자리 + shift
    [[nodiscard]] LanguageQuery at(std::string_view path, std::string_view needle, usize shift = 0,
                                   usize nth = 0) const {
        const usize fi = index(path);
        usize pos = files[fi].text.find(needle);
        for (usize k = 0; k < nth && pos != std::string::npos; ++k) {
            pos = files[fi].text.find(needle, pos + 1);
        }
        REQUIRE(pos != std::string::npos);
        return LanguageQuery{files, fi, pos + shift, imports};
    }
    [[nodiscard]] usize offset(std::string_view path, std::string_view needle, usize shift = 0) const {
        const usize pos = files[index(path)].text.find(needle);
        REQUIRE(pos != std::string::npos);
        return pos + shift;
    }
    [[nodiscard]] std::string_view slice(const SourceRange& r) const {
        return std::string_view(files[r.file].text).substr(r.begin, r.end - r.begin);
    }
    [[nodiscard]] usize errors() const { return analyzeCached(files, imports)->diagnostics.errorCount(); }
    void apply(const std::vector<FileEdit>& edits) {
        std::vector<FileEdit> sorted = edits;
        std::sort(sorted.begin(), sorted.end(), [](const FileEdit& a, const FileEdit& b) {
            return a.file != b.file ? a.file > b.file : a.begin > b.begin;
        });
        for (const FileEdit& e : sorted) {
            files[e.file].text.replace(e.begin, e.end - e.begin, e.text);
        }
    }
};

} // namespace

TEST_SUITE("script") {
    TEST_CASE(
        "language index (v0.9 ST-06-8): 정의로 — 다른 파일 · 타입 자리 · 모듈 상수 · 지역 · 매개변수 · import 이름") {
        const Pack p;
        REQUIRE_MESSAGE(p.errors() == 0, analyzeCached(p.files, p.imports)->diagnostics.describeAll());
        const auto def = [&](std::string_view path, std::string_view needle, usize shift = 0, usize nth = 0) {
            return findDefinition(p.at(path, needle, shift, nth));
        };
        // 다른 파일의 함수 (호출 자리 · import 자리 · as 로 붙인 이름)
        auto d = def("scripts/regen.ts", "refill(m.current");
        REQUIRE(d);
        CHECK(d->file == p.index("scripts/mana.ts"));
        CHECK(d->begin == p.offset("scripts/mana.ts", "refill("));
        CHECK(p.slice(*d) == "refill");
        CHECK(def("scripts/regen.ts", "refill, Bonus") == d);
        CHECK(def("scripts/ai/other.ts", "fill(x") == d);
        // 타입 자리 → interface
        d = def("scripts/regen.ts", "Bonus {", 0);
        REQUIRE(d);
        CHECK(d->file == p.index("scripts/mana.ts"));
        CHECK(d->begin == p.offset("scripts/mana.ts", "Bonus {"));
        // 모듈 상수 · 지역 · 매개변수 · 컴포넌트
        d = def("scripts/regen.ts", "STEP }");
        REQUIRE(d);
        CHECK(d->begin == p.offset("scripts/regen.ts", "STEP = 2"));
        d = def("scripts/regen.ts", "m.current =");
        REQUIRE(d);
        CHECK(d->begin == p.offset("scripts/regen.ts", "m = ctx.read"));
        d = def("scripts/regen.ts", "ctx.query");
        REQUIRE(d);
        CHECK(d->begin == p.offset("scripts/regen.ts", "ctx) {"));
        d = def("scripts/regen.ts", "Mana)) {");
        REQUIRE(d);
        CHECK(d->file == p.index("scripts/mana.ts"));
        CHECK(p.slice(*d) == "Mana");
        // 의존 팩의 파일
        d = def("scripts/regen.ts", "cap(refill");
        REQUIRE(d);
        CHECK(d->file == p.index("@inv/scripts/util.ts"));
        // 선언 자리 그대로 · SDK · 키워드 · 빈칸은 없음 (선언 위면 그 선언)
        CHECK(def("scripts/mana.ts", "refill(") == SourceRange{p.index("scripts/mana.ts"),
                                                               p.offset("scripts/mana.ts", "refill("),
                                                               p.offset("scripts/mana.ts", "refill(") + 6});
        CHECK_FALSE(def("scripts/regen.ts", "defineSystem({").has_value());
        CHECK_FALSE(def("scripts/regen.ts", "const STEP").has_value());
        CHECK_FALSE(def("scripts/regen.ts", "  return", 1).has_value());
    }

    TEST_CASE("language index (v0.9 ST-06-8): 참조 — 선언 먼저 · import · 타입 자리 · as · 컴포넌트") {
        const Pack p;
        auto refs = findReferences(p.at("scripts/mana.ts", "refill("));
        REQUIRE(refs.size() == 5);
        CHECK(refs[0].file == p.index("scripts/mana.ts")); // 선언
        std::vector<std::string> where;
        for (const SourceRange& r : refs) {
            where.push_back(std::format("{}:{}", p.files[r.file].path, p.slice(r)));
        }
        CHECK(where == std::vector<std::string>{"scripts/mana.ts:refill", "scripts/regen.ts:refill",
                                                "scripts/regen.ts:refill", "scripts/ai/other.ts:refill",
                                                "scripts/ai/other.ts:fill"});
        // interface — 선언 · 매개변수 타입 · import · 결과 타입
        refs = findReferences(p.at("scripts/regen.ts", "Bonus {"));
        CHECK(refs.size() == 4);
        // 컴포넌트 — 선언 · import · query · read · write
        refs = findReferences(p.at("scripts/regen.ts", "Mana, m)"));
        CHECK(refs.size() == 5);
        // 지역 — 선언 · 셋 (m.current 둘 · write 의 m)
        refs = findReferences(p.at("scripts/regen.ts", "m = ctx.read"));
        CHECK(refs.size() == 5);
        for (const SourceRange& r : refs) {
            CHECK(p.slice(r) == "m");
        }
        CHECK(findReferences(p.at("scripts/regen.ts", "defineSystem({")).empty());
    }

    TEST_CASE("language index (v0.9 ST-06-8): 이름 바꾸기 — 파일 사이 · as 그대로 · 바꾼 뒤 컴파일 · 거절") {
        Pack p;
        RenameResult r = renameSymbol(p.at("scripts/regen.ts", "refill(m.current"), "replenish");
        REQUIRE_MESSAGE(r.ok(), r.error);
        CHECK(r.edits.size() == 4); // as fill 의 쓰임은 그대로
        p.apply(r.edits);
        CHECK(p.errors() == 0);
        CHECK(p.files[p.index("scripts/regen.ts")].text.find("import { Mana, replenish, Bonus } from \"./mana\"") !=
              std::string::npos);
        CHECK(p.files[p.index("scripts/ai/other.ts")].text.starts_with("import { replenish as fill }"));
        CHECK(p.files[p.index("scripts/ai/other.ts")].text.find("return fill(x") != std::string::npos);
        CHECK(p.files[p.index("scripts/mana.ts")].text.find("export function replenish(") != std::string::npos);

        // 타입 · 지역
        r = renameSymbol(p.at("scripts/mana.ts", "Bonus {"), "Gain");
        REQUIRE_MESSAGE(r.ok(), r.error);
        CHECK(r.edits.size() == 4);
        p.apply(r.edits);
        CHECK(p.errors() == 0);
        CHECK_FALSE(renameSymbol(p.at("scripts/regen.ts", "m = ctx.read"), "마나").ok()); // 렉서는 ASCII 이름만 (S101)
        r = renameSymbol(p.at("scripts/regen.ts", "m = ctx.read"), "mana");
        REQUIRE_MESSAGE(r.ok(), r.error);
        p.apply(r.edits);
        CHECK(p.errors() == 0);
        CHECK(p.files[p.index("scripts/regen.ts")].text.find("mana.current = cap(") != std::string::npos);

        // 거절
        const Pack q;
        CHECK_FALSE(renameSymbol(q.at("scripts/regen.ts", "STEP }"), "1step").ok());
        CHECK_FALSE(renameSymbol(q.at("scripts/regen.ts", "STEP }"), "for").ok());
        CHECK_FALSE(renameSymbol(q.at("scripts/regen.ts", "STEP }"), "a b").ok());
        CHECK_FALSE(renameSymbol(q.at("scripts/regen.ts", "defineSystem({"), "make").ok());
        r = renameSymbol(q.at("scripts/regen.ts", "cap(refill"), "clamp");
        CHECK_FALSE(r.ok());
        CHECK(r.error.find("의존 팩") != std::string::npos);
        r = renameSymbol(q.at("scripts/regen.ts", "STEP }"), "bonusOf"); // 같은 모듈의 함수와 부딪힌다
        CHECK_FALSE(r.ok());
        CHECK(r.error.find("부딪힌다") != std::string::npos);
        CHECK(r.edits.empty());
        CHECK(renameSymbol(q.at("scripts/regen.ts", "STEP }"), "STEP").edits.empty()); // 같은 이름 — 할 일 없음
    }

    TEST_CASE("language index (v0.9 ST-06-8): 빠른 수정 — sandbox · 다른 파일 import (같은 줄에) · 비슷한 이름") {
        Pack p;
        p.files.push_back({"scripts/q.ts", R"(export function q(): number {
  const total = 1
  return totl + idiv(4, 2) + refill(1, { amount: 1 })
}
)"});
        const usize qi = p.files.size() - 1;
        auto fixes = quickFixes(p.at("scripts/q.ts", "idiv("));
        REQUIRE(fixes.size() >= 1);
        CHECK(fixes[0].title == "import { idiv } from \"sandbox\"");
        REQUIRE(fixes[0].edits.size() == 1);
        CHECK(fixes[0].edits[0].begin == 0); // import 가 없으면 맨 위
        fixes = quickFixes(p.at("scripts/q.ts", "totl"));
        REQUIRE_FALSE(fixes.empty());
        CHECK(fixes[0].title == "'total' 로 바꾸기");
        fixes = quickFixes(p.at("scripts/q.ts", "refill("));
        REQUIRE_FALSE(fixes.empty());
        CHECK(fixes[0].title == "import { refill } from \"./mana\"");
        // 셋을 다 고치면 오류 없음
        const usize before = p.errors();
        p.apply(quickFixes(p.at("scripts/q.ts", "totl"))[0].edits);
        p.apply(quickFixes(p.at("scripts/q.ts", "refill("))[0].edits);
        p.apply(quickFixes(p.at("scripts/q.ts", "idiv("))[0].edits);
        CHECK(p.errors() < before);
        CHECK(p.errors() == 0);
        CHECK(p.files[qi].text.starts_with("import { refill } from \"./mana\"\n"));
        CHECK(p.files[qi].text.find("import { idiv } from \"sandbox\"\n") != std::string::npos);

        // 같은 곳의 import 가 있으면 그 줄에 더한다
        Pack r;
        std::string& regen = r.files[r.index("scripts/regen.ts")].text;
        regen.replace(regen.find("refill, Bonus"), 13, "refill");
        REQUIRE(r.errors() > 0);
        fixes = quickFixes(r.at("scripts/regen.ts", "Bonus {"));
        REQUIRE_FALSE(fixes.empty());
        CHECK(fixes[0].title == "import { Bonus } from \"./mana\"");
        r.apply(fixes[0].edits);
        CHECK(r.errors() == 0);
        CHECK(r.files[r.index("scripts/regen.ts")].text.find("import { Mana, refill, Bonus } from \"./mana\"") !=
              std::string::npos);
        // 진단이 없는 자리는 없음
        CHECK(quickFixes(p.at("scripts/q.ts", "total")).empty());
    }

    TEST_CASE("language index (v0.9 ST-06-8): 편집 거리 · 상대 import 경로 · 검사 결과 기억") {
        CHECK(editDistance("total", "totl") == 1);
        CHECK(editDistance("", "abc") == 3);
        CHECK(editDistance("kitten", "sitting") == 3);
        CHECK(editDistance("same", "same") == 0);
        CHECK(relativeImportSpec("scripts/a.ts", "scripts/b.ts") == "./b");
        CHECK(relativeImportSpec("scripts/ai/a.ts", "scripts/util.ts") == "../util");
        CHECK(relativeImportSpec("scripts/a.ts", "scripts/ai/deep/b.ts") == "./ai/deep/b");
        CHECK(relativeImportSpec("scripts/x/a.ts", "scripts/y/b.ts") == "../y/b");

        const Pack p;
        clearProgramCache();
        (void)findReferences(p.at("scripts/mana.ts", "refill("));
        (void)findDefinition(p.at("scripts/regen.ts", "refill(m.current"));
        (void)hover(p.at("scripts/regen.ts", "STEP }"));
        const ProgramCacheStats s = programCacheStats();
        CHECK(s.misses == 1); // 같은 글 — 검사는 한 번
        CHECK(s.hits >= 2);
    }

    TEST_CASE("language index (v0.9 ST-06-8): 응답 시간 — 참조 · 이름 바꾸기 1,000 · 5,000 줄 (MESSAGE)") {
        for (const usize lines : {usize{1000}, usize{5000}}) {
            std::string text = "export function helper(x: number): number { return x * 2 }\n";
            usize n = 1;
            usize count = 0; // helper 를 부르는 함수 수
            for (usize i = 0; n < lines; ++i, ++count) {
                text += std::format(
                    "export function f{0}(a: number): number {{\n  const b = helper(a) + {0}\n  return b\n}}\n", i);
                n += 4;
            }
            const std::vector<SourceFile> files{{"scripts/big.ts", text}};
            const LanguageQuery q{files, 0, text.find("helper(") + 1, {}};
            clearProgramCache();
            using Clock = std::chrono::steady_clock;
            auto t0 = Clock::now();
            const auto refs = findReferences(q);
            const double cold = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            t0 = Clock::now();
            const auto refs2 = findReferences(q);
            const double warm = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            t0 = Clock::now();
            const RenameResult r = renameSymbol(q, "twice");
            const double rename = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            CHECK(refs.size() == count + 1);
            CHECK(refs2 == refs);
            CHECK(r.ok());
            MESSAGE(
                std::format("색인 {} 줄: 참조 {:.1f} ms (기억 {:.2f} ms) · 이름 바꾸기 {:.1f} ms (편집 {}) — 이 빌드",
                            lines, cold, warm, rename, r.edits.size()));
        }
    }
}
