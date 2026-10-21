#pragma once
// SandboxScript 구문 나무 (AST). docs/19-SANDBOXSCRIPT.md 3장.
//
// 마디는 평평한 풀(nodes)에 있고 자식은 kids 의 연속 구간이다 (마디를 다 만든 뒤 한 번에 넣는다). 빠진 선택 자식은
// kNoNode. 이름 · 문자열 값은 strings 의 칸 번호 (str). 자식의 자리는 종류마다 정해져 있다 (아래 표 — dump() 가 같은
// 순서로 쓴다).
//
//   Module         items…
//   Import         ImportName…                       str = 모듈 경로 ("./util" · "sandbox")
//   ImportName     —                                 str = 이름 (as 별칭이면 kids[0] = Ident 별칭)
//   ExportDefault  식
//   Function       Param… , 반환 타입|kNoNode, Block   str = 이름, flags Exported
//   Param          타입|kNoNode                       str = 이름
//   VarDecl        타입|kNoNode, 초기값|kNoNode        str = 이름, flags Const · Exported
//   TypeAlias      타입                              str = 이름
//   Interface      PropertySig…                      str = 이름
//   PropertySig    타입                              str = 이름, flags Readonly
//   Enum           EnumMember…                       str = 이름, flags Const (const enum)
//   EnumMember     값|kNoNode                         str = 이름
//   TypeName       타입 인자…                         str = 이름 ("number" · "Array" · "Mana")
//   ArrayType      원소 타입                          (T[])
//   UnionType      타입…                              (A | B | null)
//   NullType       —
//   ObjectType     PropertySig…                      ({ x: number })
//   Block          문장…
//   ExprStmt       식
//   If             조건, then, else|kNoNode
//   While          조건, 몸
//   DoWhile        몸, 조건
//   For            초기|kNoNode, 조건|kNoNode, 갱신|kNoNode, 몸   (초기 = VarDecl 이나 ExprStmt)
//   ForOf          VarDecl(초기값 없음), 반복 대상, 몸
//   Break · Continue —
//   Return         식|kNoNode
//   Switch         식, Case…
//   Case           값|kNoNode(default), 문장…
//   Empty          —                                 (;)
//   Number         —                                 number, flags Integer
//   String         —                                 str = 풀린 값
//   Template       String, 식, String, 식, … String   (조각과 끼운 식이 번갈아 — 조각이 언제나 하나 더 많다)
//   Bool           —                                 flags True
//   Null           —
//   Ident          —                                 str = 이름
//   Unary          피연산자                           op (- + ! ~)
//   Binary         왼쪽, 오른쪽                        op (+ … ?? && ||)
//   Assign         대상, 값                           op (= += …)
//   Update         대상                               op (++ --), flags Prefix
//   Conditional    조건, 참, 거짓
//   Call           호출 대상, 인자…
//   Member         대상                               str = 이름
//   Index          대상, 첨자
//   ArrayLit       원소…
//   ObjectLit      Property|Method…
//   Property       값                                 str = 키, flags Shorthand
//   Method         Param… , 반환 타입|kNoNode, Block   str = 이름 (객체 리터럴의 onTick(ctx) { … })
//   AsConst        식                                 (식 as const)
//   Paren          식                                 (괄호 — 대입 대상 검사 · 출력용)

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "script/lang/Token.hpp"

namespace sbx::script {

using NodeId = u32;
inline constexpr NodeId kNoNode = 0xFFFF'FFFFu;
inline constexpr u32 kNoString = 0xFFFF'FFFFu;

enum class NodeKind : u8 {
    Module,
    Import,
    ImportName,
    ExportDefault,
    Function,
    Param,
    VarDecl,
    TypeAlias,
    Interface,
    PropertySig,
    Enum,
    EnumMember,
    TypeName,
    ArrayType,
    UnionType,
    NullType,
    ObjectType,
    Block,
    ExprStmt,
    If,
    While,
    DoWhile,
    For,
    ForOf,
    Break,
    Continue,
    Return,
    Switch,
    Case,
    Empty,
    Number,
    String,
    Template,
    Bool,
    Null,
    Ident,
    Unary,
    Binary,
    Assign,
    Update,
    Conditional,
    Call,
    Member,
    Index,
    ArrayLit,
    ObjectLit,
    Property,
    Method,
    AsConst,
    Paren,
};

namespace NodeFlag {
inline constexpr u8 Exported = 1u << 0;
inline constexpr u8 Const = 1u << 1;
inline constexpr u8 Readonly = 1u << 2;
inline constexpr u8 Integer = 1u << 3;
inline constexpr u8 True = 1u << 4;
inline constexpr u8 Prefix = 1u << 5;
inline constexpr u8 Shorthand = 1u << 6;
} // namespace NodeFlag

struct Node {
    NodeKind kind = NodeKind::Empty;
    Tok op = Tok::EndOfFile; // 연산자 마디만
    u8 flags = 0;
    SourcePos pos;
    u32 str = kNoString;
    f64 number = 0.0;
    u32 firstKid = 0;
    u32 kidCount = 0;
};

class Ast {
public:
    NodeId add(NodeKind kind, SourcePos pos, std::span<const NodeId> kids = {});
    u32 addString(std::string s);

    [[nodiscard]] Node& node(NodeId id) { return m_nodes[id]; }
    [[nodiscard]] const Node& node(NodeId id) const { return m_nodes[id]; }
    [[nodiscard]] std::span<const NodeId> kids(NodeId id) const;
    // i 번째 자식 (없으면 kNoNode)
    [[nodiscard]] NodeId kid(NodeId id, u32 i) const;
    [[nodiscard]] std::string_view str(NodeId id) const;
    [[nodiscard]] usize size() const noexcept { return m_nodes.size(); }

    NodeId root = kNoNode;

    // 시험 · 디버그: S 식 한 줄. 예) (Function f (Param x (TypeName i32)) (TypeName i32) (Block (Return (Ident x))))
    [[nodiscard]] std::string dump(NodeId id) const;
    [[nodiscard]] std::string dump() const { return root == kNoNode ? std::string("()") : dump(root); }

private:
    void dumpInto(std::string& out, NodeId id) const;

    std::vector<Node> m_nodes;
    std::vector<NodeId> m_kids;
    std::vector<std::string> m_strings;
};

[[nodiscard]] std::string_view nodeKindName(NodeKind k) noexcept;

} // namespace sbx::script
