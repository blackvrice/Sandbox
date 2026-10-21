#include "script/lang/Parser.hpp"

#include <format>
#include <string>
#include <vector>

#include "script/lang/Lexer.hpp"

namespace sbx::script {

namespace {

using Kids = std::vector<NodeId>;

// 이항 연산자 우선순위 (클수록 단단하다). 0 = 이항 연산자가 아니다
int binaryPrecedence(Tok t) noexcept {
    switch (t) {
    case Tok::QuestionQuestion:
        return 1;
    case Tok::PipePipe:
        return 2;
    case Tok::AmpAmp:
        return 3;
    case Tok::Pipe:
        return 4;
    case Tok::Caret:
        return 5;
    case Tok::Amp:
        return 6;
    case Tok::EqEqEq:
    case Tok::NotEqEq:
    case Tok::EqEq:
    case Tok::NotEq:
        return 7;
    case Tok::Less:
    case Tok::Greater:
    case Tok::LessEq:
    case Tok::GreaterEq:
    case Tok::KwInstanceof:
    case Tok::KwIn:
        return 8;
    case Tok::Shl:
    case Tok::Shr:
    case Tok::UShr:
        return 9;
    case Tok::Plus:
    case Tok::Minus:
        return 10;
    case Tok::Star:
    case Tok::Slash:
    case Tok::Percent:
        return 11;
    case Tok::StarStar:
        return 12;
    default:
        return 0;
    }
}
constexpr int kAsPrecedence = 8; // TS: "as" 는 관계 연산자 자리

class Parser {
public:
    Parser(std::vector<Token> tokens, std::string_view file, DiagnosticList& diags)
        : m_toks(std::move(tokens)), m_file(file), m_diags(diags), m_errorBase(diags.errorCount()) {}

    Ast run() {
        const SourcePos start = cur().pos;
        Kids items;
        while (!at(Tok::EndOfFile) && !m_stop) {
            const usize before = m_i;
            const NodeId item = parseModuleItem();
            if (item != kNoNode) {
                items.push_back(item);
            }
            if (m_i == before && !at(Tok::EndOfFile)) {
                advance(); // 언제나 나아간다
            }
        }
        m_ast.root = m_ast.add(NodeKind::Module, start, items);
        if (!m_stop) {
            checkAssignments(m_ast.root, Mode::Stmt);
        }
        return std::move(m_ast);
    }

private:
    // ---- 낱말 다루기 -------------------------------------------------------------------------------------------
    [[nodiscard]] const Token& cur() const { return m_toks[m_i]; }
    [[nodiscard]] const Token& ahead(usize n) const { return m_toks[std::min(m_i + n, m_toks.size() - 1)]; }
    [[nodiscard]] bool at(Tok t) const { return cur().kind == t; }
    [[nodiscard]] bool atWord(std::string_view w) const { return at(Tok::Identifier) && cur().value == w; }
    void advance() {
        if (m_i + 1 < m_toks.size()) {
            ++m_i;
        }
    }
    bool accept(Tok t) {
        if (at(t)) {
            advance();
            return true;
        }
        return false;
    }
    bool expect(Tok t, std::string_view what = {}) {
        if (accept(t)) {
            return true;
        }
        if (what.empty()) {
            error("S201", cur().pos, std::format("'{}' 가 필요하다 — '{}' 가 왔다", tokName(t), describe(cur())));
        } else {
            error("S201", cur().pos,
                  std::format("'{}' 가 필요하다 ({}) — '{}' 가 왔다", tokName(t), what, describe(cur())));
        }
        return false;
    }
    [[nodiscard]] static std::string describe(const Token& t) {
        if (t.kind == Tok::Identifier) {
            return t.value;
        }
        return std::string(tokName(t.kind));
    }

    void error(const char* code, SourcePos pos, std::string message) {
        if (m_stop) {
            return;
        }
        if (m_diags.errorCount() - m_errorBase >= kMaxParseErrors) {
            m_diags.error("S299", m_file, pos, std::format("오류가 너무 많아 ({} 개) 여기서 멈춘다", kMaxParseErrors));
            stopParsing();
            return;
        }
        m_diags.error(code, m_file, pos, std::move(message));
    }
    void stopParsing() {
        m_stop = true;
        m_i = m_toks.size() - 1; // EndOfFile
    }

    NodeId node(NodeKind kind, SourcePos pos, std::span<const NodeId> kids = {}) { return m_ast.add(kind, pos, kids); }
    NodeId named(NodeKind kind, SourcePos pos, std::string name, std::span<const NodeId> kids = {}) {
        const NodeId n = m_ast.add(kind, pos, kids);
        m_ast.node(n).str = m_ast.addString(std::move(name));
        return n;
    }
    NodeId errorNode(SourcePos pos) { return node(NodeKind::Null, pos); }

    // 깊이 상한 — 넘으면 남은 소스를 버린다 (재귀가 스택을 넘지 않게)
    struct DepthGuard {
        Parser& p;
        bool ok;
        explicit DepthGuard(Parser& parser) : p(parser), ok(++parser.m_depth <= kMaxParseDepth) {
            if (!ok && !p.m_stop) {
                p.m_diags.error("S204", p.m_file, p.cur().pos,
                                std::format("중첩이 너무 깊다 (> {}) — 여기서 멈춘다", kMaxParseDepth));
                p.stopParsing();
            }
        }
        ~DepthGuard() { --p.m_depth; }
        DepthGuard(const DepthGuard&) = delete;
        DepthGuard& operator=(const DepthGuard&) = delete;
    };

    // ASI: ; 이거나 } · 파일 끝 · 앞 줄바꿈이면 문장 끝
    void endStatement() {
        if (accept(Tok::Semicolon) || at(Tok::RBrace) || at(Tok::EndOfFile) || cur().newlineBefore) {
            return;
        }
        if (at(Tok::Comma)) {
            error("S318", cur().pos, "쉼표 연산자 · 여러 선언 (a, b) 은 없다 — 문장을 나눈다");
        } else {
            error("S201", cur().pos,
                  std::format("문장 끝 (; 또는 줄바꿈) 이 필요하다 — '{}' 가 왔다", describe(cur())));
        }
        recover();
    }

    // 문장 단위 회복: 다음 ; 뒤나 } 앞, 줄 처음의 문장 낱말까지 건너뛴다
    void recover() {
        int depth = 0;
        while (!at(Tok::EndOfFile)) {
            if (depth == 0 && accept(Tok::Semicolon)) {
                return;
            }
            if (depth == 0 && at(Tok::RBrace)) {
                return;
            }
            if (at(Tok::LBrace) || at(Tok::LParen) || at(Tok::LBracket)) {
                ++depth;
            } else if ((at(Tok::RBrace) || at(Tok::RParen) || at(Tok::RBracket)) && depth > 0) {
                --depth;
            }
            advance();
            if (depth == 0 && cur().newlineBefore && startsStatement(cur())) {
                return;
            }
        }
    }
    [[nodiscard]] static bool startsStatement(const Token& t) {
        switch (t.kind) {
        case Tok::KwConst:
        case Tok::KwLet:
        case Tok::KwVar:
        case Tok::KwIf:
        case Tok::KwFor:
        case Tok::KwWhile:
        case Tok::KwDo:
        case Tok::KwReturn:
        case Tok::KwBreak:
        case Tok::KwContinue:
        case Tok::KwSwitch:
        case Tok::KwFunction:
        case Tok::KwExport:
        case Tok::KwImport:
        case Tok::KwInterface:
        case Tok::KwEnum:
        case Tok::KwClass:
            return true;
        default:
            return false;
        }
    }

    // 짝 맞는 괄호 묶음을 건너뛴다 (cur 가 여는 괄호). 거절한 문법을 먹어 치울 때
    void skipBalanced() {
        const Tok open = cur().kind;
        const Tok close = open == Tok::LBrace ? Tok::RBrace : open == Tok::LParen ? Tok::RParen : Tok::RBracket;
        int depth = 0;
        while (!at(Tok::EndOfFile)) {
            if (at(open)) {
                ++depth;
            } else if (at(close)) {
                --depth;
                if (depth == 0) {
                    advance();
                    return;
                }
            }
            advance();
        }
    }

    // ---- 모듈 최상위 -------------------------------------------------------------------------------------------
    NodeId parseModuleItem() {
        const Token& t = cur();
        switch (t.kind) {
        case Tok::KwImport:
            if (ahead(1).kind == Tok::LParen) {
                return rejectStatement("S322", "동적 import() 는 없다 — 정적 import { … } from \"…\" 만");
            }
            return parseImport();
        case Tok::KwExport:
            return parseExport();
        case Tok::KwFunction:
        case Tok::KwConst:
        case Tok::KwInterface:
        case Tok::KwEnum:
            return parseDeclaration(0);
        case Tok::KwLet:
            error("S328", t.pos, "모듈 최상위에는 let 이 없다 — 틱을 넘는 상태는 컴포넌트에 둔다 (상수는 const)");
            return parseStatement();
        case Tok::KwVar:
        case Tok::KwClass:
            return parseStatement(); // 각자 S303 · S301
        case Tok::Identifier:
            if (isTypeAliasStart() || isUnsupportedDeclStart()) {
                return parseDeclaration(0);
            }
            break;
        default:
            break;
        }
        error("S328", t.pos,
              "모듈 최상위에는 선언만 둔다 (import · export · function · const · type · interface · enum)");
        parseStatement();
        return kNoNode;
    }

    [[nodiscard]] bool isTypeAliasStart() const { return atWord("type") && ahead(1).kind == Tok::Identifier; }
    [[nodiscard]] bool isUnsupportedDeclStart() const {
        return (atWord("declare") || atWord("namespace") || atWord("module") || atWord("abstract") ||
                atWord("async")) &&
               !ahead(1).newlineBefore && (ahead(1).kind == Tok::Identifier || isKeyword(ahead(1).kind));
    }

    NodeId parseImport() {
        const SourcePos pos = cur().pos;
        advance(); // import
        if (at(Tok::String)) {
            error("S321", pos, "부수 효과 import \"…\" 는 없다 — import { 이름 } from \"…\"");
            recover();
            return kNoNode;
        }
        if (atWord("type") && ahead(1).kind == Tok::LBrace) {
            error("S321", cur().pos, "import type 은 없다 — import { … } 로 타입도 가져온다");
            advance();
        }
        if (!at(Tok::LBrace)) {
            error("S321", cur().pos,
                  "기본 · 이름공간 import (import x · import * as x) 는 없다 — import { 이름 } from \"…\"");
            recover();
            return kNoNode;
        }
        advance();
        Kids names;
        while (!at(Tok::RBrace) && !at(Tok::EndOfFile)) {
            if (!at(Tok::Identifier)) {
                error("S201", cur().pos, std::format("가져올 이름이 필요하다 — '{}' 가 왔다", describe(cur())));
                recover();
                return kNoNode;
            }
            const SourcePos npos = cur().pos;
            std::string name = cur().value;
            advance();
            Kids alias;
            if (atWord("as")) {
                advance();
                if (at(Tok::Identifier)) {
                    alias.push_back(named(NodeKind::Ident, cur().pos, cur().value));
                    advance();
                } else {
                    error("S201", cur().pos, "as 다음에 이름이 필요하다");
                }
            }
            names.push_back(named(NodeKind::ImportName, npos, std::move(name), alias));
            if (!accept(Tok::Comma)) {
                break;
            }
        }
        expect(Tok::RBrace);
        if (!atWord("from")) {
            error("S201", cur().pos, "'from \"…\"' 이 필요하다");
            recover();
            return kNoNode;
        }
        advance();
        std::string path;
        if (at(Tok::String)) {
            path = cur().value;
            advance();
        } else {
            error("S201", cur().pos, "모듈 경로 문자열이 필요하다");
        }
        endStatement();
        return named(NodeKind::Import, pos, std::move(path), names);
    }

    NodeId parseExport() {
        const SourcePos pos = cur().pos;
        advance(); // export
        if (accept(Tok::KwDefault)) {
            const NodeId e = parseExpression();
            endStatement();
            const NodeId ids[] = {e};
            return node(NodeKind::ExportDefault, pos, ids);
        }
        switch (cur().kind) {
        case Tok::KwFunction:
        case Tok::KwConst:
        case Tok::KwInterface:
        case Tok::KwEnum:
            return parseDeclaration(NodeFlag::Exported);
        case Tok::KwLet:
            error("S328", cur().pos, "모듈 최상위에는 let 이 없다 — 틱을 넘는 상태는 컴포넌트에 둔다 (상수는 const)");
            return parseStatement();
        case Tok::Identifier:
            if (isTypeAliasStart() || isUnsupportedDeclStart()) {
                return parseDeclaration(NodeFlag::Exported);
            }
            break;
        default:
            break;
        }
        return rejectStatement(
            "S320", "이 export 모양은 없다 — export function · const · type · interface · enum 과 export default 만");
    }

    NodeId rejectStatement(const char* code, std::string message) {
        error(code, cur().pos, std::move(message));
        recover();
        return kNoNode;
    }

    // function · const · interface · type · enum (flags = Exported 등)
    NodeId parseDeclaration(u8 flags) {
        const Token& t = cur();
        NodeId n = kNoNode;
        if (t.kind == Tok::KwFunction) {
            n = parseFunction();
        } else if (t.kind == Tok::KwConst && ahead(1).kind == Tok::KwEnum) {
            advance();
            n = parseEnum(true);
        } else if (t.kind == Tok::KwConst) {
            n = parseVarDecl(/*statement=*/true);
        } else if (t.kind == Tok::KwInterface) {
            n = parseInterface();
        } else if (t.kind == Tok::KwEnum) {
            n = parseEnum(false);
        } else if (isTypeAliasStart()) {
            n = parseTypeAlias();
        } else if (t.value == "async") {
            return rejectStatement("S305", "async 는 없다 — 시뮬레이션은 기다리지 않는다");
        } else {
            return rejectStatement("S319",
                                   std::format("{} 는 없다 — 파일 하나 = 모듈 하나, 선언은 그대로 쓴다", t.value));
        }
        if (n != kNoNode) {
            m_ast.node(n).flags |= flags;
        }
        return n;
    }

    NodeId parseFunction() {
        const SourcePos pos = cur().pos;
        advance(); // function
        if (at(Tok::Star)) {
            error("S305", cur().pos, "제너레이터 (function*) 는 없다");
            advance();
        }
        std::string name;
        if (at(Tok::Identifier)) {
            name = cur().value;
            advance();
        } else {
            error("S201", cur().pos, "함수 이름이 필요하다");
        }
        if (at(Tok::Less)) {
            error("S327", cur().pos, "사용자 함수의 타입 매개변수 (function f<T>) 는 없다");
            skipTypeParams();
        }
        Kids kids = parseParams();
        kids.push_back(parseReturnType());
        if (!at(Tok::LBrace)) {
            error("S201", cur().pos, "함수 몸 { … } 이 필요하다 (선언만 있는 함수 · 겹치기 선언은 없다)");
            recover();
            kids.push_back(node(NodeKind::Block, cur().pos));
        } else {
            kids.push_back(parseBlock());
        }
        return named(NodeKind::Function, pos, std::move(name), kids);
    }

    void skipTypeParams() {
        int depth = 0;
        while (!at(Tok::EndOfFile)) {
            if (at(Tok::Less)) {
                ++depth;
            } else if (at(Tok::Greater)) {
                if (--depth == 0) {
                    advance();
                    return;
                }
            } else if (at(Tok::LParen) || at(Tok::LBrace)) {
                return;
            }
            advance();
        }
    }

    Kids parseParams() {
        Kids params;
        if (!expect(Tok::LParen, "매개변수 목록")) {
            return params;
        }
        while (!at(Tok::RParen) && !at(Tok::EndOfFile)) {
            const SourcePos ppos = cur().pos;
            if (at(Tok::Ellipsis)) {
                error("S313", ppos, "나머지 매개변수 (...xs) 는 없다 — 배열을 넘긴다");
                advance();
            }
            if (at(Tok::LBrace) || at(Tok::LBracket)) {
                error("S317", ppos, "구조 분해 매개변수는 없다 — 이름 하나로 받는다");
                skipBalanced();
            } else if (at(Tok::KwThis)) {
                error("S315", ppos, "this 매개변수는 없다");
                advance();
            } else if (at(Tok::Identifier)) {
                std::string name = cur().value;
                advance();
                if (at(Tok::Question)) {
                    error("S326", cur().pos, "선택 매개변수 (x?) 는 없다 — 모든 인자를 넘긴다");
                    advance();
                }
                Kids type;
                type.push_back(accept(Tok::Colon) ? parseType() : kNoNode);
                if (at(Tok::Assign)) {
                    error("S326", cur().pos, "기본값 매개변수 (x = 1) 는 없다 — 모든 인자를 넘긴다");
                    advance();
                    parseAssignment();
                }
                params.push_back(named(NodeKind::Param, ppos, std::move(name), type));
            } else {
                error("S201", ppos, std::format("매개변수 이름이 필요하다 — '{}' 가 왔다", describe(cur())));
                break;
            }
            if (!accept(Tok::Comma)) {
                break;
            }
        }
        expect(Tok::RParen);
        return params;
    }

    NodeId parseReturnType() { return accept(Tok::Colon) ? parseType() : kNoNode; }

    // forOf: for (const x of …) 의 선언 — 초기값이 없다
    NodeId parseVarDecl(bool statement, bool forOf = false) {
        const SourcePos pos = cur().pos;
        u8 flags = 0;
        if (at(Tok::KwVar)) {
            error("S303", pos, "var 는 없다 — const 나 let");
        } else if (at(Tok::KwConst)) {
            flags = NodeFlag::Const;
        }
        advance(); // const · let · var
        std::string name;
        if (at(Tok::LBrace) || at(Tok::LBracket)) {
            error("S317", cur().pos, "구조 분해 선언 (const { a } = …) 은 없다 — 이름 하나씩");
            skipBalanced();
        } else if (at(Tok::Identifier)) {
            name = cur().value;
            advance();
        } else {
            error("S201", cur().pos, std::format("변수 이름이 필요하다 — '{}' 가 왔다", describe(cur())));
            recover();
            return kNoNode;
        }
        if (at(Tok::Bang)) {
            error("S324", cur().pos, "확정 대입 단언 (let x!: T) 은 없다");
            advance();
        }
        Kids kids;
        kids.push_back(accept(Tok::Colon) ? parseType() : kNoNode);
        if (accept(Tok::Assign)) {
            kids.push_back(parseAssignment());
        } else {
            if ((flags & NodeFlag::Const) && !forOf) {
                error("S201", cur().pos, "const 에는 초기값이 필요하다");
            }
            kids.push_back(kNoNode);
        }
        if (statement) {
            endStatement();
        }
        const NodeId n = named(NodeKind::VarDecl, pos, std::move(name), kids);
        m_ast.node(n).flags = flags;
        return n;
    }

    NodeId parseTypeAlias() {
        const SourcePos pos = cur().pos;
        advance(); // type
        std::string name = cur().value;
        advance();
        if (at(Tok::Less)) {
            error("S327", cur().pos, "타입 매개변수 (type T<X>) 는 없다");
            skipTypeParams();
        }
        expect(Tok::Assign);
        const NodeId ids[] = {parseType()};
        endStatement();
        return named(NodeKind::TypeAlias, pos, std::move(name), ids);
    }

    NodeId parseInterface() {
        const SourcePos pos = cur().pos;
        advance(); // interface
        std::string name;
        if (at(Tok::Identifier)) {
            name = cur().value;
            advance();
        } else {
            error("S201", cur().pos, "인터페이스 이름이 필요하다");
        }
        if (at(Tok::Less)) {
            error("S327", cur().pos, "타입 매개변수 (interface I<T>) 는 없다");
            skipTypeParams();
        }
        if (at(Tok::KwExtends)) {
            error("S331", cur().pos, "interface extends 는 없다 — 필드를 그대로 쓴다");
            while (!at(Tok::LBrace) && !at(Tok::EndOfFile)) {
                advance();
            }
        }
        const Kids props = parsePropertySigs();
        return named(NodeKind::Interface, pos, std::move(name), props);
    }

    // { a: T; readonly b: U, … } — 구분은 ; , 줄바꿈
    Kids parsePropertySigs() {
        Kids props;
        if (!expect(Tok::LBrace)) {
            return props;
        }
        while (!at(Tok::RBrace) && !at(Tok::EndOfFile)) {
            const SourcePos ppos = cur().pos;
            u8 flags = 0;
            if (atWord("readonly") && ahead(1).kind != Tok::Colon && ahead(1).kind != Tok::Question) {
                flags |= NodeFlag::Readonly;
                advance();
            }
            if (at(Tok::LBracket)) {
                error("S331", ppos, "색인 서명 ([k: string]: T) 은 없다 — 배열이나 필드를 쓴다");
                skipBalanced();
                if (accept(Tok::Colon)) {
                    parseType();
                }
            } else if (at(Tok::Identifier) || isKeyword(cur().kind) || at(Tok::String)) {
                std::string name =
                    at(Tok::Identifier) || at(Tok::String) ? cur().value : std::string(tokName(cur().kind));
                advance();
                if (at(Tok::Question)) {
                    error("S326", cur().pos, "선택 필드 (x?: T) 는 없다 — T | null 을 쓴다");
                    advance();
                }
                if (at(Tok::LParen) || at(Tok::Less)) {
                    error("S331", cur().pos, "메서드 서명은 없다 — 데이터 필드만 (함수는 최상위 function)");
                    if (at(Tok::Less)) {
                        skipTypeParams();
                    }
                    if (at(Tok::LParen)) {
                        skipBalanced();
                    }
                    if (accept(Tok::Colon)) {
                        parseType();
                    }
                } else {
                    expect(Tok::Colon);
                    const NodeId ids[] = {parseType()};
                    const NodeId p = named(NodeKind::PropertySig, ppos, std::move(name), ids);
                    m_ast.node(p).flags = flags;
                    props.push_back(p);
                }
            } else {
                error("S201", ppos, std::format("필드 이름이 필요하다 — '{}' 가 왔다", describe(cur())));
                recover();
                if (at(Tok::RBrace)) {
                    break;
                }
                continue;
            }
            if (!accept(Tok::Semicolon) && !accept(Tok::Comma) && !at(Tok::RBrace) && !cur().newlineBefore) {
                error("S201", cur().pos,
                      std::format("필드 사이에 ; , 줄바꿈이 필요하다 — '{}' 가 왔다", describe(cur())));
                advance();
            }
        }
        expect(Tok::RBrace);
        return props;
    }

    NodeId parseEnum(bool isConst) {
        const SourcePos pos = cur().pos;
        advance(); // enum
        std::string name;
        if (at(Tok::Identifier)) {
            name = cur().value;
            advance();
        } else {
            error("S201", cur().pos, "enum 이름이 필요하다");
        }
        Kids members;
        if (expect(Tok::LBrace)) {
            while (!at(Tok::RBrace) && !at(Tok::EndOfFile)) {
                const SourcePos mpos = cur().pos;
                if (!at(Tok::Identifier)) {
                    error("S201", mpos, std::format("enum 멤버 이름이 필요하다 — '{}' 가 왔다", describe(cur())));
                    recover();
                    break;
                }
                std::string mname = cur().value;
                advance();
                Kids value;
                value.push_back(accept(Tok::Assign) ? parseAssignment() : kNoNode);
                members.push_back(named(NodeKind::EnumMember, mpos, std::move(mname), value));
                if (!accept(Tok::Comma)) {
                    break;
                }
            }
            expect(Tok::RBrace);
        }
        const NodeId n = named(NodeKind::Enum, pos, std::move(name), members);
        if (isConst) {
            m_ast.node(n).flags |= NodeFlag::Const;
        }
        return n;
    }

    // ---- 타입 --------------------------------------------------------------------------------------------------
    NodeId parseType() {
        DepthGuard guard(*this);
        if (!guard.ok) {
            return errorNode(cur().pos);
        }
        const SourcePos pos = cur().pos;
        accept(Tok::Pipe); // 앞의 | 는 TS 가 받는다
        Kids parts;
        parts.push_back(parseArrayType());
        while (accept(Tok::Pipe)) {
            parts.push_back(parseArrayType());
        }
        if (at(Tok::Amp)) {
            error("S331", cur().pos, "교차 타입 (A & B) 은 없다");
            while (accept(Tok::Amp)) {
                parseArrayType();
            }
        }
        if (at(Tok::Arrow)) {
            error("S331", cur().pos, "함수 타입 ((x) => T) 은 없다 — 함수 값은 최상위 함수 이름만");
            advance();
            parseType();
        }
        if (parts.size() == 1) {
            return parts[0];
        }
        return node(NodeKind::UnionType, pos, parts);
    }

    NodeId parseArrayType() {
        NodeId t = parsePrimaryType();
        while (at(Tok::LBracket) && ahead(1).kind == Tok::RBracket && !cur().newlineBefore) {
            const SourcePos pos = cur().pos;
            advance();
            advance();
            const NodeId ids[] = {t};
            t = node(NodeKind::ArrayType, pos, ids);
        }
        return t;
    }

    NodeId parsePrimaryType() {
        const Token& t = cur();
        const SourcePos pos = t.pos;
        switch (t.kind) {
        case Tok::KwNull:
            advance();
            return node(NodeKind::NullType, pos);
        case Tok::KwVoid:
            advance();
            return named(NodeKind::TypeName, pos, "void");
        case Tok::LBrace: {
            const Kids props = parsePropertySigs();
            return node(NodeKind::ObjectType, pos, props);
        }
        case Tok::LParen: {
            // (x: i32) => T — 함수 타입
            if (const usize close = matchParen(m_i); close != 0 && m_toks[close + 1].kind == Tok::Arrow) {
                error("S331", pos, "함수 타입 ((x) => T) 은 없다 — 함수 값은 최상위 함수 이름만");
                m_i = close + 2;
                parseType();
                return errorNode(pos);
            }
            advance();
            const NodeId inner = parseType();
            expect(Tok::RParen);
            return inner;
        }
        case Tok::Identifier: {
            if (t.value == "keyof" || t.value == "readonly" || t.value == "unique" || t.value == "infer") {
                error("S331", pos, std::format("타입 연산자 {} 는 없다", t.value));
                advance();
                return parsePrimaryType();
            }
            std::string name = t.value;
            advance();
            if (at(Tok::Dot)) {
                error("S331", cur().pos, "이름공간 타입 (A.B) 은 없다");
                while (accept(Tok::Dot)) {
                    if (at(Tok::Identifier)) {
                        advance();
                    }
                }
            }
            Kids args;
            if (at(Tok::Less) && !cur().newlineBefore) {
                advance();
                while (!at(Tok::EndOfFile)) {
                    args.push_back(parseType());
                    if (!accept(Tok::Comma)) {
                        break;
                    }
                }
                closeTypeArgs();
            }
            return named(NodeKind::TypeName, pos, std::move(name), args);
        }
        case Tok::String:
        case Tok::Number:
        case Tok::KwTrue:
        case Tok::KwFalse:
        case Tok::Template:
        case Tok::Minus:
            error("S331", pos, "리터럴 타입 (\"a\" · 1 · true) 은 없다 — enum 을 쓴다");
            if (at(Tok::Minus)) {
                advance();
            }
            advance();
            return errorNode(pos);
        case Tok::KwTypeof:
            error("S331", pos, "typeof 타입은 없다");
            advance();
            parsePostfix(parsePrimary());
            return errorNode(pos);
        case Tok::LBracket:
            error("S331", pos, "튜플 타입 ([A, B]) 은 없다 — interface 를 쓴다");
            skipBalanced();
            return errorNode(pos);
        default:
            error("S201", pos, std::format("타입이 필요하다 — '{}' 가 왔다", describe(t)));
            if (!at(Tok::EndOfFile) && !at(Tok::RBrace) && !at(Tok::RParen) && !at(Tok::Semicolon)) {
                advance();
            }
            return errorNode(pos);
        }
    }

    // 타입 인자를 닫는 > — >> · >>> · >= 등은 하나만 떼어 낸다 (Array<Array<i32>>)
    void closeTypeArgs() {
        Token& t = m_toks[m_i];
        Tok rest = Tok::EndOfFile;
        switch (t.kind) {
        case Tok::Greater:
            advance();
            return;
        case Tok::Shr:
            rest = Tok::Greater;
            break;
        case Tok::UShr:
            rest = Tok::Shr;
            break;
        case Tok::GreaterEq:
            rest = Tok::Assign;
            break;
        case Tok::ShrAssign:
            rest = Tok::GreaterEq;
            break;
        case Tok::UShrAssign:
            rest = Tok::ShrAssign;
            break;
        default:
            error("S201", t.pos, std::format("타입 인자를 닫는 '>' 가 필요하다 — '{}' 가 왔다", describe(t)));
            return;
        }
        t.kind = rest;
        ++t.pos.offset;
        ++t.pos.column;
        --t.length;
        t.newlineBefore = false;
    }

    // ---- 문장 --------------------------------------------------------------------------------------------------
    NodeId parseBlock() {
        DepthGuard guard(*this);
        const SourcePos pos = cur().pos;
        Kids stmts;
        expect(Tok::LBrace);
        while (guard.ok && !at(Tok::RBrace) && !at(Tok::EndOfFile)) {
            const usize before = m_i;
            const NodeId s = parseStatement();
            if (s != kNoNode) {
                stmts.push_back(s);
            }
            if (m_i == before && !at(Tok::RBrace) && !at(Tok::EndOfFile)) {
                advance();
            }
        }
        expect(Tok::RBrace);
        return node(NodeKind::Block, pos, stmts);
    }

    NodeId parseStatement() {
        DepthGuard guard(*this);
        if (!guard.ok) {
            return kNoNode;
        }
        const Token& t = cur();
        const SourcePos pos = t.pos;
        switch (t.kind) {
        case Tok::LBrace:
            return parseBlock();
        case Tok::Semicolon:
            advance();
            return node(NodeKind::Empty, pos);
        case Tok::KwConst:
            if (ahead(1).kind == Tok::KwEnum) {
                return rejectStatement("S201", "enum 은 모듈 최상위에만");
            }
            [[fallthrough]];
        case Tok::KwLet:
        case Tok::KwVar:
            return parseVarDecl(true);
        case Tok::KwIf:
            return parseIf();
        case Tok::KwWhile: {
            advance();
            Kids kids;
            kids.push_back(parseCondition());
            kids.push_back(parseStatement());
            return node(NodeKind::While, pos, kids);
        }
        case Tok::KwDo: {
            advance();
            Kids kids;
            kids.push_back(parseStatement());
            expect(Tok::KwWhile);
            kids.push_back(parseCondition());
            accept(Tok::Semicolon); // do-while 뒤의 ; 는 줄바꿈 없이도 생략된다 (JS)
            return node(NodeKind::DoWhile, pos, kids);
        }
        case Tok::KwFor:
            return parseFor();
        case Tok::KwBreak:
        case Tok::KwContinue: {
            advance();
            if (at(Tok::Identifier) && !cur().newlineBefore) {
                error("S307", cur().pos, "레이블 break · continue 는 없다");
                advance();
            }
            endStatement();
            return node(t.kind == Tok::KwBreak ? NodeKind::Break : NodeKind::Continue, pos);
        }
        case Tok::KwReturn: {
            advance();
            Kids kids;
            if (at(Tok::Semicolon) || at(Tok::RBrace) || at(Tok::EndOfFile) || cur().newlineBefore) {
                kids.push_back(kNoNode);
            } else {
                kids.push_back(parseExpression());
            }
            endStatement();
            return node(NodeKind::Return, pos, kids);
        }
        case Tok::KwSwitch:
            return parseSwitch();
        case Tok::KwFunction:
            error("S304", pos, "함수 안의 함수는 없다 (클로저 없음) — 모듈 최상위 function 으로 옮긴다");
            parseFunction();
            return kNoNode;
        case Tok::KwClass:
            error("S301", pos, "class 는 없다 — interface + 최상위 function, 상태는 컴포넌트에");
            advance();
            if (at(Tok::Identifier)) {
                advance();
            }
            while (!at(Tok::LBrace) && !at(Tok::EndOfFile)) {
                advance();
            }
            if (at(Tok::LBrace)) {
                skipBalanced();
            }
            return kNoNode;
        case Tok::KwTry:
        case Tok::KwThrow:
            error("S302", pos,
                  "예외 (try · catch · finally · throw) 는 없다 — 오류는 반환값으로, 실행 중 오류는 트랩 (ADR-0056 5)");
            if (t.kind == Tok::KwThrow) {
                recover();
                return kNoNode;
            }
            advance();
            while (at(Tok::LBrace) || at(Tok::KwCatch) || at(Tok::KwFinally)) {
                if (at(Tok::LBrace)) {
                    skipBalanced();
                } else {
                    advance();
                    if (at(Tok::LParen)) {
                        skipBalanced();
                    }
                }
            }
            return kNoNode;
        case Tok::KwWith:
        case Tok::KwDebugger:
            return rejectStatement("S308", std::format("{} 는 없다", tokName(t.kind)));
        case Tok::KwImport:
        case Tok::KwExport:
            return rejectStatement("S201", std::format("{} 는 모듈 최상위에만", tokName(t.kind)));
        case Tok::KwInterface:
        case Tok::KwEnum:
            return rejectStatement("S201", std::format("{} 는 모듈 최상위에만", tokName(t.kind)));
        case Tok::At:
            return rejectStatement("S316", "데코레이터 (@…) 는 없다");
        case Tok::Identifier:
            if (ahead(1).kind == Tok::Colon) {
                error("S307", pos, "레이블 문장 (이름:) 은 없다");
                advance();
                advance();
                return parseStatement();
            }
            if (isTypeAliasStart()) {
                return rejectStatement("S201", "type 은 모듈 최상위에만");
            }
            break;
        default:
            break;
        }
        return parseExpressionStatement();
    }

    NodeId parseCondition() {
        expect(Tok::LParen);
        const NodeId c = parseExpression();
        expect(Tok::RParen);
        return c;
    }

    NodeId parseIf() {
        const SourcePos pos = cur().pos;
        advance(); // if
        Kids kids;
        kids.push_back(parseCondition());
        kids.push_back(parseStatement());
        kids.push_back(accept(Tok::KwElse) ? parseStatement() : kNoNode);
        return node(NodeKind::If, pos, kids);
    }

    NodeId parseFor() {
        const SourcePos pos = cur().pos;
        advance(); // for
        if (atWord("await")) {
            error("S305", cur().pos, "for await 는 없다");
            advance();
        }
        expect(Tok::LParen);
        NodeId init = kNoNode;
        if (at(Tok::KwConst) || at(Tok::KwLet) || at(Tok::KwVar)) {
            // for (const x of …) — 초기값 없는 선언
            if (ahead(1).kind == Tok::Identifier && (ahead(2).kind == Tok::Identifier && ahead(2).value == "of")) {
                const NodeId decl = parseVarDecl(false, /*forOf=*/true);
                advance(); // of
                Kids kids;
                kids.push_back(decl);
                kids.push_back(parseAssignment());
                expect(Tok::RParen);
                kids.push_back(parseStatement());
                return node(NodeKind::ForOf, pos, kids);
            }
            if (ahead(1).kind == Tok::Identifier && ahead(2).kind == Tok::KwIn) {
                error("S306", ahead(2).pos, "for-in 은 없다 (키 순서가 없다) — 배열 · 질의를 for-of 로");
                skipToParenClose();
                parseStatement();
                return kNoNode;
            }
            init = parseVarDecl(false);
            if (at(Tok::Comma)) {
                error("S318", cur().pos, "여러 선언 (let i = 0, j = 0) 은 없다 — 선언을 나눈다");
                while (!at(Tok::Semicolon) && !at(Tok::RParen) && !at(Tok::EndOfFile)) {
                    advance();
                }
            }
        } else if (!at(Tok::Semicolon)) {
            const SourcePos ipos = cur().pos;
            const NodeId e = parseExpression();
            if (atWord("of") || at(Tok::KwIn)) {
                error(at(Tok::KwIn) ? "S306" : "S335", cur().pos,
                      at(Tok::KwIn) ? "for-in 은 없다 (키 순서가 없다) — 배열 · 질의를 for-of 로"
                                    : "for-of 는 const · let 선언으로 (for (const x of xs))");
                skipToParenClose();
                parseStatement();
                return kNoNode;
            }
            init = exprStatementNode(ipos, e);
        }
        Kids kids;
        kids.push_back(init);
        expect(Tok::Semicolon);
        kids.push_back(at(Tok::Semicolon) ? kNoNode : parseExpression());
        expect(Tok::Semicolon);
        if (at(Tok::RParen)) {
            kids.push_back(kNoNode);
        } else {
            const SourcePos upos = cur().pos;
            kids.push_back(exprStatementNode(upos, parseExpression()));
        }
        if (at(Tok::Comma)) {
            error("S318", cur().pos, "쉼표 연산자 (i++, j++) 는 없다");
            skipToParenClose();
        } else {
            expect(Tok::RParen);
        }
        kids.push_back(parseStatement());
        return node(NodeKind::For, pos, kids);
    }

    void skipToParenClose() {
        int depth = 0;
        while (!at(Tok::EndOfFile)) {
            if (at(Tok::LParen)) {
                ++depth;
            } else if (at(Tok::RParen)) {
                if (depth == 0) {
                    advance();
                    return;
                }
                --depth;
            }
            advance();
        }
    }

    NodeId parseSwitch() {
        const SourcePos pos = cur().pos;
        advance(); // switch
        Kids kids;
        kids.push_back(parseCondition());
        expect(Tok::LBrace);
        while (!at(Tok::RBrace) && !at(Tok::EndOfFile)) {
            const SourcePos cpos = cur().pos;
            Kids caseKids;
            if (accept(Tok::KwCase)) {
                caseKids.push_back(parseExpression());
            } else if (accept(Tok::KwDefault)) {
                caseKids.push_back(kNoNode);
            } else {
                error("S201", cpos, std::format("case · default 가 필요하다 — '{}' 가 왔다", describe(cur())));
                recover();
                continue;
            }
            expect(Tok::Colon);
            while (!at(Tok::KwCase) && !at(Tok::KwDefault) && !at(Tok::RBrace) && !at(Tok::EndOfFile)) {
                const usize before = m_i;
                const NodeId s = parseStatement();
                if (s != kNoNode) {
                    caseKids.push_back(s);
                }
                if (m_i == before) {
                    advance();
                }
            }
            kids.push_back(node(NodeKind::Case, cpos, caseKids));
        }
        expect(Tok::RBrace);
        return node(NodeKind::Switch, pos, kids);
    }

    NodeId parseExpressionStatement() {
        const SourcePos pos = cur().pos;
        const NodeId e = parseExpression();
        endStatement();
        return exprStatementNode(pos, e);
    }

    // 식 문장 · for 의 초기 · 갱신
    NodeId exprStatementNode(SourcePos pos, NodeId e) {
        const NodeId ids[] = {e};
        return node(NodeKind::ExprStmt, pos, ids);
    }

    // 대입 · ++ -- 는 식 문장 (for 의 초기 · 갱신 포함) 의 맨 위에만 (S334) — 모듈을 다 읽은 뒤 나무 전체를 한 번 본다.
    // Stmt = 문장 · 선언 · 타입, ExprTop = 식 문장의 식, ExprInner = 그 밖의 식 (조건 · 초기값 · 인자 · 대입의 양쪽 …)
    enum class Mode : u8 { Stmt, ExprTop, ExprInner };
    [[nodiscard]] static bool isExpression(NodeKind k) noexcept {
        return k >= NodeKind::Number && k != NodeKind::Method;
    }

    void checkAssignments(NodeId id, Mode mode) {
        if (id == kNoNode) {
            return;
        }
        const Node& n = m_ast.node(id);
        Mode kidMode = Mode::Stmt;
        if (n.kind == NodeKind::ExprStmt) {
            kidMode = Mode::ExprTop;
        } else if (isExpression(n.kind)) {
            if ((n.kind == NodeKind::Assign || n.kind == NodeKind::Update) && mode != Mode::ExprTop) {
                error("S334", n.pos, "대입 · ++ · -- 는 문장으로만 쓴다 (식 안에서는 없다)");
            }
            kidMode = Mode::ExprInner;
        }
        for (const NodeId k : m_ast.kids(id)) {
            checkAssignments(k, kidMode);
        }
    }

    // ---- 식 ----------------------------------------------------------------------------------------------------
    NodeId parseExpression() {
        const NodeId e = parseAssignment();
        return e;
    }

    NodeId parseAssignment() {
        DepthGuard guard(*this);
        if (!guard.ok) {
            return errorNode(cur().pos);
        }
        if (const NodeId arrow = rejectArrow(); arrow != kNoNode) {
            return arrow;
        }
        const NodeId lhs = parseConditional();
        if (isAssignOp(cur().kind)) {
            const Token op = cur();
            advance();
            if (!isAssignTarget(lhs)) {
                error("S203", op.pos, "대입할 수 없는 대상 — 이름 · 필드 (a.b) · 원소 (a[i]) 만");
            }
            if (op.kind == Tok::UShrAssign) {
                error("S312", op.pos, ">>> 는 없다 (결과가 u32 — i32 밖) — >> 를 쓴다");
            }
            const NodeId rhs = parseAssignment();
            const NodeId ids[] = {lhs, rhs};
            const NodeId n = node(NodeKind::Assign, op.pos, ids);
            m_ast.node(n).op = op.kind;
            return n;
        }
        return lhs;
    }

    [[nodiscard]] bool isAssignTarget(NodeId id) const {
        const NodeKind k = m_ast.node(id).kind;
        return k == NodeKind::Ident || k == NodeKind::Member || k == NodeKind::Index;
    }

    // 화살표 함수 (x => … · (a, b) => …) 를 알아보고 먹어 치운다. 아니면 kNoNode
    NodeId rejectArrow() {
        const SourcePos pos = cur().pos;
        usize arrowAt = 0;
        if (atWord("async") && !ahead(1).newlineBefore &&
            (ahead(1).kind == Tok::LParen || ahead(1).kind == Tok::Identifier)) {
            // async (…) => · async x =>
            if (ahead(1).kind == Tok::Identifier && ahead(2).kind == Tok::Arrow) {
                arrowAt = 2;
            } else if (ahead(1).kind == Tok::LParen) {
                const usize close = matchParen(m_i + 1);
                if (close != 0 && m_toks[close + 1].kind == Tok::Arrow) {
                    arrowAt = close + 1 - m_i;
                }
            }
        } else if (at(Tok::Identifier) && ahead(1).kind == Tok::Arrow) {
            arrowAt = 1;
        } else if (at(Tok::LParen)) {
            const usize close = matchParen(m_i);
            if (close != 0) {
                // (a): T => … 의 반환 타입도 본다
                usize after = close + 1;
                if (m_toks[after].kind == Tok::Colon) {
                    while (after < m_toks.size() - 1 && m_toks[after].kind != Tok::Arrow &&
                           m_toks[after].kind != Tok::Semicolon && !m_toks[after].newlineBefore) {
                        ++after;
                    }
                }
                if (m_toks[after].kind == Tok::Arrow) {
                    arrowAt = after - m_i;
                }
            }
        }
        if (arrowAt == 0) {
            return kNoNode;
        }
        error("S304", pos, "화살표 함수 · 함수 식은 없다 (클로저 없음) — 모듈 최상위 function 으로 옮긴다");
        for (usize i = 0; i <= arrowAt; ++i) {
            advance();
        }
        if (at(Tok::LBrace)) {
            skipBalanced();
        } else {
            parseAssignment();
        }
        return errorNode(pos);
    }

    // m_toks[open] 이 ( 일 때 짝 ) 의 자리 (없으면 0)
    [[nodiscard]] usize matchParen(usize open) const {
        int depth = 0;
        for (usize i = open; i < m_toks.size(); ++i) {
            const Tok k = m_toks[i].kind;
            if (k == Tok::LParen) {
                ++depth;
            } else if (k == Tok::RParen) {
                if (--depth == 0) {
                    return i;
                }
            } else if (k == Tok::EndOfFile) {
                return 0;
            }
        }
        return 0;
    }

    NodeId parseConditional() {
        const NodeId c = parseBinary(1);
        if (!at(Tok::Question)) {
            return c;
        }
        const SourcePos pos = cur().pos;
        advance();
        Kids kids;
        kids.push_back(c);
        kids.push_back(parseAssignment());
        expect(Tok::Colon);
        kids.push_back(parseAssignment());
        return node(NodeKind::Conditional, pos, kids);
    }

    NodeId parseBinary(int minPrec) {
        DepthGuard guard(*this);
        if (!guard.ok) {
            return errorNode(cur().pos);
        }
        NodeId lhs = parseUnary();
        while (true) {
            // e as const — 줄바꿈 뒤의 as 는 새 문장 (TS)
            if (atWord("as") && !cur().newlineBefore && kAsPrecedence >= minPrec) {
                const SourcePos pos = cur().pos;
                advance();
                if (accept(Tok::KwConst)) {
                    const NodeId ids[] = {lhs};
                    lhs = node(NodeKind::AsConst, pos, ids);
                } else {
                    error("S325", pos, "타입 단언 (as T) 은 없다 — as const 만");
                    parseType();
                }
                continue;
            }
            if (atWord("satisfies") && !cur().newlineBefore) {
                error("S325", cur().pos, "satisfies 는 없다");
                advance();
                parseType();
                continue;
            }
            const Tok op = cur().kind;
            const int prec = binaryPrecedence(op);
            if (prec == 0 || prec < minPrec) {
                return lhs;
            }
            const SourcePos pos = cur().pos;
            switch (op) {
            case Tok::EqEq:
            case Tok::NotEq:
                error("S311", pos,
                      std::format("{} 는 없다 (강제 변환) — {} 를 쓴다", tokName(op), op == Tok::EqEq ? "===" : "!=="));
                break;
            case Tok::UShr:
                error("S312", pos, ">>> 는 없다 (결과가 u32 — i32 밖) — >> 를 쓴다");
                break;
            case Tok::KwInstanceof:
            case Tok::KwIn:
                error("S309", pos, std::format("{} 연산자는 없다", tokName(op)));
                break;
            default:
                break;
            }
            advance();
            // ** 는 오른쪽 결합, 그 밖은 왼쪽
            const NodeId rhs = parseBinary(op == Tok::StarStar ? prec : prec + 1);
            if (op == Tok::StarStar && m_ast.node(lhs).kind == NodeKind::Unary) {
                error("S202", pos, "단항 연산 뒤의 ** 는 괄호로 묶는다 ((-a) ** 2) — JS 문법 오류");
            }
            checkCoalesceMix(op, lhs, rhs, pos);
            const NodeId ids[] = {lhs, rhs};
            lhs = node(NodeKind::Binary, pos, ids);
            m_ast.node(lhs).op = op;
        }
    }

    // ?? 와 && || 는 괄호 없이 섞지 않는다 (TS · JS 문법 오류)
    void checkCoalesceMix(Tok op, NodeId lhs, NodeId rhs, SourcePos pos) {
        auto isLogical = [&](NodeId id, bool coalesce) {
            const Node& n = m_ast.node(id);
            if (n.kind != NodeKind::Binary) {
                return false;
            }
            return coalesce ? n.op == Tok::QuestionQuestion : (n.op == Tok::AmpAmp || n.op == Tok::PipePipe);
        };
        if (op == Tok::QuestionQuestion && (isLogical(lhs, false) || isLogical(rhs, false))) {
            error("S202", pos, "?? 와 && · || 는 괄호로 나눈다");
        } else if ((op == Tok::AmpAmp || op == Tok::PipePipe) && (isLogical(lhs, true) || isLogical(rhs, true))) {
            error("S202", pos, "?? 와 && · || 는 괄호로 나눈다");
        }
    }

    NodeId parseUnary() {
        DepthGuard guard(*this);
        if (!guard.ok) {
            return errorNode(cur().pos);
        }
        const Token& t = cur();
        const SourcePos pos = t.pos;
        switch (t.kind) {
        case Tok::Bang:
        case Tok::Tilde:
        case Tok::Minus:
        case Tok::Plus: {
            const Tok op = t.kind;
            advance();
            const NodeId ids[] = {parseUnary()};
            const NodeId n = node(NodeKind::Unary, pos, ids);
            m_ast.node(n).op = op;
            return n;
        }
        case Tok::PlusPlus:
        case Tok::MinusMinus: {
            const Tok op = t.kind;
            advance();
            const NodeId target = parseUnary();
            if (!isAssignTarget(target)) {
                error("S203", pos, "++ · -- 의 대상은 이름 · 필드 · 원소만");
            }
            const NodeId ids[] = {target};
            const NodeId n = node(NodeKind::Update, pos, ids);
            m_ast.node(n).op = op;
            m_ast.node(n).flags = NodeFlag::Prefix;
            return n;
        }
        case Tok::KwTypeof:
        case Tok::KwVoid:
        case Tok::KwDelete:
            error("S309", pos, std::format("{} 연산자는 없다", tokName(t.kind)));
            advance();
            return parseUnary();
        case Tok::KwAwait:
            error("S305", pos, "await 는 없다 — 시뮬레이션은 기다리지 않는다");
            advance();
            return parseUnary();
        case Tok::Less:
            error("S325", pos, "타입 단언 (<T>e) 은 없다");
            skipTypeParams();
            return parseUnary();
        default:
            break;
        }
        return parsePostfix(parsePrimary());
    }

    NodeId parsePostfix(NodeId e) {
        while (true) {
            const Token& t = cur();
            const SourcePos pos = t.pos;
            if (t.kind == Tok::Dot || t.kind == Tok::QuestionDot) {
                if (t.kind == Tok::QuestionDot) {
                    error("S314", pos, "선택 연결 (?.) 은 없다 — if (x !== null) 로 좁힌다");
                }
                advance();
                if (at(Tok::LParen) || at(Tok::LBracket)) {
                    continue; // a?.() · a?.[i] — 위에서 오류를 냈다
                }
                if (at(Tok::Hash)) {
                    error("S316", cur().pos, "비공개 이름 (#x) 은 없다");
                    advance();
                }
                if (at(Tok::Identifier) || isKeyword(cur().kind)) {
                    std::string name = at(Tok::Identifier) ? cur().value : std::string(tokName(cur().kind));
                    advance();
                    const NodeId ids[] = {e};
                    e = named(NodeKind::Member, pos, std::move(name), ids);
                } else {
                    error("S201", cur().pos, std::format(". 다음에 이름이 필요하다 — '{}' 가 왔다", describe(cur())));
                    return e;
                }
            } else if (t.kind == Tok::LBracket) {
                advance();
                const NodeId index = parseExpression();
                expect(Tok::RBracket);
                const NodeId ids[] = {e, index};
                e = node(NodeKind::Index, pos, ids);
            } else if (t.kind == Tok::LParen) {
                advance();
                Kids kids;
                kids.push_back(e);
                while (!at(Tok::RParen) && !at(Tok::EndOfFile)) {
                    if (at(Tok::Ellipsis)) {
                        error("S313", cur().pos, "펼침 (...xs) 은 없다");
                        advance();
                    }
                    kids.push_back(parseAssignment());
                    if (!accept(Tok::Comma)) {
                        break;
                    }
                }
                expect(Tok::RParen);
                e = node(NodeKind::Call, pos, kids);
            } else if ((t.kind == Tok::PlusPlus || t.kind == Tok::MinusMinus) && !t.newlineBefore) {
                if (!isAssignTarget(e)) {
                    error("S203", pos, "++ · -- 의 대상은 이름 · 필드 · 원소만");
                }
                advance();
                const NodeId ids[] = {e};
                const NodeId n = node(NodeKind::Update, pos, ids);
                m_ast.node(n).op = t.kind == Tok::PlusPlus ? Tok::PlusPlus : Tok::MinusMinus;
                e = n;
            } else if (t.kind == Tok::Bang && !t.newlineBefore) {
                error("S324", pos, "널 아님 단언 (x!) 은 없다 — if (x !== null) 로 좁힌다");
                advance();
            } else if ((t.kind == Tok::Template || t.kind == Tok::TemplateHead) && !t.newlineBefore) {
                error("S333", pos, "태그 템플릿 (f`…`) 은 없다");
                parsePrimary();
            } else {
                return e;
            }
        }
    }

    NodeId parsePrimary() {
        DepthGuard guard(*this);
        const Token& t = cur();
        const SourcePos pos = t.pos;
        if (!guard.ok) {
            return errorNode(pos);
        }
        switch (t.kind) {
        case Tok::Number: {
            const NodeId n = node(NodeKind::Number, pos);
            m_ast.node(n).number = t.number;
            if (t.integer) {
                m_ast.node(n).flags = NodeFlag::Integer;
            }
            advance();
            return n;
        }
        case Tok::String: {
            const NodeId n = named(NodeKind::String, pos, t.value);
            advance();
            return n;
        }
        case Tok::Template: {
            const NodeId part = named(NodeKind::String, pos, t.value);
            advance();
            const NodeId ids[] = {part};
            return node(NodeKind::Template, pos, ids);
        }
        case Tok::TemplateHead:
            return parseTemplate();
        case Tok::KwTrue:
        case Tok::KwFalse: {
            const NodeId n = node(NodeKind::Bool, pos);
            if (t.kind == Tok::KwTrue) {
                m_ast.node(n).flags = NodeFlag::True;
            }
            advance();
            return n;
        }
        case Tok::KwNull:
            advance();
            return node(NodeKind::Null, pos);
        case Tok::Identifier: {
            const NodeId n = named(NodeKind::Ident, pos, t.value);
            advance();
            return n;
        }
        case Tok::LParen: {
            advance();
            const NodeId inner = parseExpression();
            if (at(Tok::Comma)) {
                error("S318", cur().pos, "쉼표 연산자 ((a, b)) 는 없다");
                skipToParenClose();
            } else {
                expect(Tok::RParen);
            }
            const NodeId ids[] = {inner};
            return node(NodeKind::Paren, pos, ids);
        }
        case Tok::LBracket:
            return parseArrayLiteral();
        case Tok::LBrace:
            return parseObjectLiteral();
        case Tok::KwFunction:
            error("S304", pos, "함수 식은 없다 (클로저 없음) — 모듈 최상위 function 으로 옮긴다");
            parseFunction();
            return errorNode(pos);
        case Tok::KwClass:
            error("S301", pos, "class 는 없다");
            advance();
            while (!at(Tok::LBrace) && !at(Tok::EndOfFile)) {
                advance();
            }
            if (at(Tok::LBrace)) {
                skipBalanced();
            }
            return errorNode(pos);
        case Tok::KwNew:
            error("S315", pos, "new 는 없다 — 객체 리터럴 { … } 과 Host 함수를 쓴다");
            advance();
            return parsePostfix(parsePrimary());
        case Tok::KwThis:
        case Tok::KwSuper:
            error("S315", pos, std::format("{} 는 없다 — 콜백은 ctx 를 매개변수로 받는다", tokName(t.kind)));
            advance();
            return errorNode(pos);
        case Tok::KwImport:
            error("S322", pos, "동적 import() 는 없다");
            advance();
            return errorNode(pos);
        case Tok::Hash:
            error("S316", pos, "비공개 이름 (#x) 은 없다");
            advance();
            return errorNode(pos);
        case Tok::Slash:
        case Tok::SlashAssign:
            error("S336", pos, "정규식 리터럴 (/…/) 은 없다");
            advance();
            return errorNode(pos);
        default:
            error("S201", pos, std::format("식이 필요하다 — '{}' 가 왔다", describe(t)));
            // 닫는 괄호 · 문장 끝은 부른 쪽이 처리한다 (여기서 먹으면 회복이 더 나빠진다)
            if (!at(Tok::EndOfFile) && !at(Tok::RBrace) && !at(Tok::RParen) && !at(Tok::RBracket) &&
                !at(Tok::Semicolon) && !at(Tok::Comma)) {
                advance();
            }
            return errorNode(pos);
        }
    }

    NodeId parseTemplate() {
        const SourcePos pos = cur().pos;
        Kids kids;
        kids.push_back(named(NodeKind::String, pos, cur().value));
        advance(); // TemplateHead
        while (true) {
            kids.push_back(parseExpression());
            if (at(Tok::TemplateMiddle) || at(Tok::TemplateTail)) {
                const bool tail = at(Tok::TemplateTail);
                kids.push_back(named(NodeKind::String, cur().pos, cur().value));
                advance();
                if (tail) {
                    break;
                }
            } else {
                error("S201", cur().pos,
                      std::format("템플릿의 ${{ … }} 를 닫는 }} 가 필요하다 — '{}' 가 왔다", describe(cur())));
                kids.push_back(named(NodeKind::String, cur().pos, ""));
                break;
            }
        }
        return node(NodeKind::Template, pos, kids);
    }

    NodeId parseArrayLiteral() {
        const SourcePos pos = cur().pos;
        advance(); // [
        Kids elems;
        while (!at(Tok::RBracket) && !at(Tok::EndOfFile)) {
            if (at(Tok::Comma)) {
                error("S201", cur().pos, "배열의 구멍 ([1, , 2]) 은 없다");
                advance();
                continue;
            }
            if (at(Tok::Ellipsis)) {
                error("S313", cur().pos, "펼침 (...xs) 은 없다");
                advance();
            }
            elems.push_back(parseAssignment());
            if (!accept(Tok::Comma)) {
                break;
            }
        }
        expect(Tok::RBracket);
        return node(NodeKind::ArrayLit, pos, elems);
    }

    NodeId parseObjectLiteral() {
        const SourcePos pos = cur().pos;
        advance(); // {
        Kids props;
        while (!at(Tok::RBrace) && !at(Tok::EndOfFile)) {
            const SourcePos ppos = cur().pos;
            if (at(Tok::Ellipsis)) {
                error("S313", ppos, "펼침 ({ ...o }) 은 없다");
                advance();
                parseAssignment();
            } else if (at(Tok::LBracket)) {
                error("S332", ppos, "계산된 키 ([k]: v) 는 없다");
                skipBalanced();
                if (accept(Tok::Colon)) {
                    parseAssignment();
                } else if (at(Tok::LParen)) {
                    parseMethodRest(ppos, "");
                }
            } else if (at(Tok::Number)) {
                error("S332", ppos, "숫자 키는 없다 — 배열이나 이름 키를 쓴다");
                advance();
                if (accept(Tok::Colon)) {
                    parseAssignment();
                }
            } else if ((atWord("get") || atWord("set")) &&
                       (ahead(1).kind == Tok::Identifier || isKeyword(ahead(1).kind) || ahead(1).kind == Tok::String)) {
                error("S323", ppos, "getter · setter 는 없다 — 필드나 함수를 쓴다");
                advance();
                advance();
                parseMethodRest(ppos, "");
            } else if ((atWord("async") && ahead(1).kind != Tok::Colon && ahead(1).kind != Tok::LParen &&
                        ahead(1).kind != Tok::Comma && ahead(1).kind != Tok::RBrace) ||
                       at(Tok::Star)) {
                error("S305", ppos, "async · 제너레이터 메서드는 없다");
                advance();
                if (at(Tok::Star)) {
                    advance();
                }
            } else if (at(Tok::Identifier) || at(Tok::String) || isKeyword(cur().kind)) {
                const bool ident = at(Tok::Identifier);
                std::string key = ident || at(Tok::String) ? cur().value : std::string(tokName(cur().kind));
                advance();
                if (accept(Tok::Colon)) {
                    const NodeId ids[] = {parseAssignment()};
                    props.push_back(named(NodeKind::Property, ppos, std::move(key), ids));
                } else if (at(Tok::LParen) || at(Tok::Less)) {
                    props.push_back(parseMethodRest(ppos, std::move(key)));
                } else if (ident && (at(Tok::Comma) || at(Tok::RBrace))) {
                    // { a } = { a: a }
                    const NodeId ids[] = {named(NodeKind::Ident, ppos, key)};
                    const NodeId p = named(NodeKind::Property, ppos, std::move(key), ids);
                    m_ast.node(p).flags = NodeFlag::Shorthand;
                    props.push_back(p);
                } else {
                    error("S201", cur().pos, std::format("':' 가 필요하다 — '{}' 가 왔다", describe(cur())));
                    recoverInObject();
                    continue;
                }
            } else {
                error("S201", ppos, std::format("속성 이름이 필요하다 — '{}' 가 왔다", describe(cur())));
                recoverInObject();
                continue;
            }
            if (!accept(Tok::Comma)) {
                break;
            }
        }
        expect(Tok::RBrace);
        return node(NodeKind::ObjectLit, pos, props);
    }

    void recoverInObject() {
        int depth = 0;
        while (!at(Tok::EndOfFile)) {
            if (depth == 0 && (at(Tok::Comma) || at(Tok::RBrace))) {
                accept(Tok::Comma);
                return;
            }
            if (at(Tok::LBrace) || at(Tok::LParen) || at(Tok::LBracket)) {
                ++depth;
            } else if (at(Tok::RBrace) || at(Tok::RParen) || at(Tok::RBracket)) {
                --depth;
            }
            advance();
        }
    }

    // 메서드: 이름 다음의 (매개변수): 타입 { 몸 }
    NodeId parseMethodRest(SourcePos pos, std::string name) {
        if (at(Tok::Less)) {
            error("S327", cur().pos, "메서드의 타입 매개변수는 없다");
            skipTypeParams();
        }
        Kids kids = parseParams();
        kids.push_back(parseReturnType());
        if (at(Tok::LBrace)) {
            kids.push_back(parseBlock());
        } else {
            error("S201", cur().pos, "메서드 몸 { … } 이 필요하다");
            kids.push_back(node(NodeKind::Block, cur().pos));
        }
        return named(NodeKind::Method, pos, std::move(name), kids);
    }

    std::vector<Token> m_toks;
    usize m_i = 0;
    std::string_view m_file;
    DiagnosticList& m_diags;
    usize m_errorBase;
    Ast m_ast;
    u32 m_depth = 0;
    bool m_stop = false;
};

} // namespace

Ast parseModule(std::string_view source, std::string_view file, DiagnosticList& diags) {
    std::vector<Token> tokens = tokenize(source, file, diags);
    return Parser(std::move(tokens), file, diags).run();
}

} // namespace sbx::script
