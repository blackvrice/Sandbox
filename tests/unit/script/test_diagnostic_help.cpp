// 진단 고치는 법 (v0.9 M6-T4 조각 3 — script/lang/DiagnosticHelp, ADR-0126): 표는 코드 차례 · 빈 칸 없음, 컴파일러
// 소스의 모든 S 코드가 표에 있고 표의 모든 코드를 소스가 쓴다, DiagnosticList 가 help 를 채운다, 흔한 실수 묶음의 진단
// 문구 · 고치는 법 = 골든 (tests/data/script_diagnostics.golden.txt — SBX_UPDATE_GOLDEN=1 이면 다시 쓴다)
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "foundation/text/TextPosition.hpp"
#include "script/lang/Codegen.hpp"
#include "script/lang/DiagnosticHelp.hpp"

using namespace sbx;
using namespace sbx::script;
namespace fs = std::filesystem;

namespace {

std::string readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

// 흔한 실수 (TS 습관 · 타입) — 이름 · 소스. 골든의 차례
const std::vector<std::pair<std::string, std::string>>& mistakes() {
    static const std::vector<std::pair<std::string, std::string>> kList{
        {"class", "export class Rabbit { hp = 3 }\n"},
        {"var", "export function f(): number { var x = 1; return x }\n"},
        {"loose-equals", "export function f(a: number): boolean { return a == 1 }\n"},
        {"top-level-let", "let count = 0\nexport function f(): number { return 1 }\n"},
        {"any", "export function f(x: any): number { return 1 }\n"},
        {"math-random", "export function f(): number { return Math.random() }\n"},
        {"console", "export function f(): void { console.log(\"hi\") }\n"},
        {"try-catch", "export function f(): number { try { return 1 } catch (e) { return 0 } }\n"},
        {"optional-chain", "interface P { x: number }\nexport function f(p: P | null): number { return p?.x }\n"},
        {"null-unnarrowed", "interface P { x: number }\nexport function f(p: P | null): number { return p.x }\n"},
        {"number-to-i32",
         "import { i32 } from \"sandbox\"\nexport function f(a: number): i32 { const n: i32 = a; return n }\n"},
        {"missing-type", "export function f(a) { return a }\n"},
        {"arg-count", "function g(a: number): number { return a }\nexport function f(): number { return g(1, 2) }\n"},
        {"unknown-field", "interface P { x: number }\nexport function f(p: P): number { return p.y }\n"},
        {"missing-name", "export function f(): number { return speed * 2 }\n"},
        {"condition-number", "export function f(n: number): number { if (n) { return 1 } return 0 }\n"},
        {"missing-return", "export function f(n: number): number { if (n > 0) { return 1 } }\n"},
        {"import-missing", "import { Mana } from \"./mana\"\nexport function f(): number { return 1 }\n"},
    };
    return kList;
}

std::string goldenText() {
    std::string out;
    for (const auto& [name, src] : mistakes()) {
        DiagnosticList d;
        const SourceFile f{"scripts/" + name + ".ts", src};
        (void)compileScripts(std::span(&f, 1), d);
        out += "== " + name + "\n";
        out += d.describeAllWithHelp();
    }
    return out;
}

} // namespace

TEST_SUITE("script") {
    TEST_CASE("진단 고치는 법 (v0.9 M6-T4): 표 모양 · 소스의 모든 S 코드") {
        const auto table = diagnosticHelpTable();
        REQUIRE_FALSE(table.empty());
        const std::regex codeForm("S[0-9]{3}");
        for (usize i = 0; i < table.size(); ++i) {
            CAPTURE(table[i].code);
            CHECK(std::regex_match(std::string(table[i].code), codeForm));
            CHECK_FALSE(table[i].meaning.empty());
            CHECK_FALSE(table[i].fix.empty());
            if (i > 0) {
                CHECK(table[i - 1].code < table[i].code); // 이진 탐색
            }
            CHECK(findDiagnosticHelp(table[i].code) == &table[i]);
        }
        CHECK(findDiagnosticHelp("S999") == nullptr);
        CHECK(findDiagnosticHelp("") == nullptr);

        // 컴파일러 소스 (script/**) 의 "Sxxx" 리터럴 = 표
        std::set<std::string> used;
        const std::regex literal("\"(S[0-9]{3})\"");
        for (const auto& e : fs::recursive_directory_iterator(SBX_SCRIPT_SOURCE_DIR)) {
            const std::string ext = e.path().extension().string();
            if (!e.is_regular_file() || (ext != ".cpp" && ext != ".hpp") ||
                e.path().filename() == "DiagnosticHelp.cpp") {
                continue;
            }
            const std::string text = readAll(e.path());
            for (std::sregex_iterator it(text.begin(), text.end(), literal), end; it != end; ++it) {
                used.insert((*it)[1].str());
            }
        }
        REQUIRE(used.size() > 50);
        for (const std::string& c : used) {
            CAPTURE(c);
            CHECK(findDiagnosticHelp(c) != nullptr);
        }
        for (const DiagnosticHelp& h : table) {
            CAPTURE(h.code);
            CHECK(used.count(std::string(h.code)) == 1); // 쓰지 않는 칸은 지운다
        }

        // DiagnosticList 가 채운다 · 표에 없으면 비었다
        DiagnosticList d;
        d.error("S301", "a.ts", {}, "class 는 없다");
        d.warning("X1", "a.ts", {}, "다른 것");
        CHECK(d.items()[0].help == findDiagnosticHelp("S301")->fix);
        CHECK(d.items()[0].describeWithHelp() == d.items()[0].describe() + "\n    고치는 법: " + d.items()[0].help);
        CHECK(d.items()[1].help.empty());
        CHECK(d.items()[1].describeWithHelp() == d.items()[1].describe());
    }

    TEST_CASE("텍스트 위치 계약 (v0.9 NG-05): 컴파일러 진단의 (줄, 열) = offset — CRLF · CR · 한글 · 그림 문자 줄") {
        // 진단 자리 (렉서 · 파서 · 검사기) 의 (줄, 코드 포인트 열) 을 계약 함수로 바꾸면 진단의 바이트 offset 과 같다
        const std::string src = "// 가나다 \xF0\x9F\x98\x80\r\n"
                                "export function f(): number {\r\n"
                                "  const 마나 = \"😀\"; return 마나 * 2\r"
                                "}\n"
                                "export function g(): number { return 없음 + 1 }\n";
        DiagnosticList d;
        const SourceFile f{"scripts/pos.ts", src};
        (void)compileScripts(std::span(&f, 1), d);
        REQUIRE_FALSE(d.items().empty());
        for (const Diagnostic& x : d.items()) {
            CAPTURE(x.describe());
            const text::LineCol lc{x.pos.line, x.pos.column};
            CHECK(text::offsetAt(src, lc) == x.pos.offset);
            CHECK(text::lineColAt(src, x.pos.offset) == lc);
        }
    }

    TEST_CASE("진단 문구 골든 (v0.9 M6-T4): 흔한 실수 18 가지의 진단 · 고치는 법") {
        const fs::path golden = fs::path(SBX_TEST_DATA_DIR) / "script_diagnostics.golden.txt";
        const std::string actual = goldenText();
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996) // getenv — 시험 도구의 스위치만 읽는다 (foundation/diag/Report 와 같은 방식)
#endif
        const char* u = std::getenv("SBX_UPDATE_GOLDEN");
#ifdef _MSC_VER
#pragma warning(pop)
#endif
        if (u != nullptr && std::string(u) == "1") {
            std::ofstream(golden, std::ios::binary) << actual;
        }
        REQUIRE_MESSAGE(fs::exists(golden), "골든이 없다 — SBX_UPDATE_GOLDEN=1 로 만든다");
        std::string expected = readAll(golden);
        // 체크아웃이 줄 끝을 CRLF 로 바꿔도 같은 글 (Windows · core.autocrlf)
        constexpr char kCr = 0x0D;
        std::erase(expected, kCr);
        if (expected != actual) {
            MESSAGE("지금 출력 (문구를 일부러 바꿨다면 SBX_UPDATE_GOLDEN=1 로 다시 쓴다):\n" << actual);
        }
        CHECK(expected == actual);
        // 모든 실수가 진단을 하나 이상 내고, 첫 진단에 고치는 법이 있다
        for (const auto& [name, src] : mistakes()) {
            CAPTURE(name);
            DiagnosticList d;
            const SourceFile f{"scripts/" + name + ".ts", src};
            (void)compileScripts(std::span(&f, 1), d);
            REQUIRE_FALSE(d.items().empty());
            CHECK_FALSE(d.items().front().help.empty());
        }
    }
}
