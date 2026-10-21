#pragma once
// SandboxScript 낱말. docs/19-SANDBOXSCRIPT.md 2장.
//
// 예약어는 낱말 종류 하나씩 (Kw*). TS 의 문맥 낱말 (type · as · from · of · readonly · async · get · set · declare ·
// namespace …) 은 Identifier 로 두고 파서가 글자로 본다 — JS 에서 이름으로도 쓰이기 때문이다.

#include <string>
#include <string_view>

#include "script/lang/Diagnostic.hpp"

namespace sbx::script {

enum class Tok : u8 {
    EndOfFile,
    Identifier,
    Number,
    String,
    Template,       // `…` (끼움 없음)
    TemplateHead,   // `…${
    TemplateMiddle, // }…${
    TemplateTail,   // }…`

    // 괄호 · 구두점
    LBrace,
    RBrace,
    LParen,
    RParen,
    LBracket,
    RBracket,
    Semicolon,
    Comma,
    Dot,
    Ellipsis,
    Question,
    QuestionDot,
    Colon,
    Arrow,
    At,
    Hash,
    // 연산자
    Less,
    Greater,
    LessEq,
    GreaterEq,
    EqEq,
    NotEq,
    EqEqEq,
    NotEqEq,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    StarStar,
    PlusPlus,
    MinusMinus,
    Shl,
    Shr,
    UShr,
    Amp,
    Pipe,
    Caret,
    Bang,
    Tilde,
    AmpAmp,
    PipePipe,
    QuestionQuestion,
    Assign,
    PlusAssign,
    MinusAssign,
    StarAssign,
    SlashAssign,
    PercentAssign,
    StarStarAssign,
    ShlAssign,
    ShrAssign,
    UShrAssign,
    AmpAssign,
    PipeAssign,
    CaretAssign,
    AmpAmpAssign,
    PipePipeAssign,
    QuestionQuestionAssign,

    // 예약어 (JS 엄격 모드 + TS 의 예약)
    KwBreak,
    KwCase,
    KwCatch,
    KwClass,
    KwConst,
    KwContinue,
    KwDebugger,
    KwDefault,
    KwDelete,
    KwDo,
    KwElse,
    KwEnum,
    KwExport,
    KwExtends,
    KwFalse,
    KwFinally,
    KwFor,
    KwFunction,
    KwIf,
    KwImport,
    KwIn,
    KwInstanceof,
    KwNew,
    KwNull,
    KwReturn,
    KwSuper,
    KwSwitch,
    KwThis,
    KwThrow,
    KwTrue,
    KwTry,
    KwTypeof,
    KwVar,
    KwVoid,
    KwWhile,
    KwWith,
    KwYield,
    KwLet,
    KwStatic,
    KwImplements,
    KwInterface,
    KwPackage,
    KwPrivate,
    KwProtected,
    KwPublic,
    KwAwait,
};

struct Token {
    Tok kind = Tok::EndOfFile;
    SourcePos pos;
    u32 length = 0;             // 소스 바이트 수
    bool newlineBefore = false; // 앞 낱말과 사이에 줄바꿈 (ASI · 제한된 생성 규칙)
    bool integer = false;       // Number: 소수점 · 지수 없는 리터럴 (i32 에 넣을 수 있는 후보 — 범위는 검사기가)
    f64 number = 0.0;
    std::string value; // Identifier 이름 · String/Template 의 풀린 값 (UTF-8)
};

[[nodiscard]] std::string_view tokName(Tok t) noexcept;
[[nodiscard]] bool isKeyword(Tok t) noexcept;
// 대입 연산자 (= += … ??=)
[[nodiscard]] bool isAssignOp(Tok t) noexcept;

} // namespace sbx::script
