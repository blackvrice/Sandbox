#include "script/lang/DiagnosticHelp.hpp"

#include <algorithm>
#include <array>

namespace sbx::script {

namespace {

// 코드 오름차순 — findDiagnosticHelp 가 이진 탐색한다 (시험이 차례를 본다)
constexpr std::array kTable{
    // 낱말 (렉서)
    DiagnosticHelp{"S100", "소스 파일이 UTF-8 이 아니거나 너무 크다", "UTF-8 로 저장하고 큰 파일은 나눈다"},
    DiagnosticHelp{"S101", "알 수 없는 글자", "따옴표 밖의 전각 기호 · 보이지 않는 글자를 지우고 ASCII 연산자를 쓴다"},
    DiagnosticHelp{"S103", "닫히지 않은 문자열", "같은 따옴표로 닫는다 — 여러 줄 글은 `템플릿` 을 쓴다"},
    DiagnosticHelp{"S104", "닫히지 않은 주석", "/* 로 연 주석을 */ 로 닫는다"},
    DiagnosticHelp{"S105", "잘못된 이스케이프", "\\n · \\t · \\\\ · \\\" · \\u{1F600} 꼴만 쓴다"},
    DiagnosticHelp{"S106", "잘못된 숫자 리터럴", "10진수 · 0x · 0o · 0b 꼴로, 2^53 안에서 쓴다 (큰 정수는 나눠 계산)"},
    DiagnosticHelp{"S108", "닫히지 않은 템플릿 문자열", "` 로 닫고 ${ … } 의 괄호를 맞춘다"},
    // 문법 (파서)
    DiagnosticHelp{"S201", "기대한 낱말이 없다", "표시된 자리 앞뒤의 괄호 · 쉼표 · 콜론을 확인한다"},
    DiagnosticHelp{"S202", "괄호가 필요한 섞기",
                   "연산자 우선순위가 헷갈리는 식을 괄호로 묶는다 ((-a) ** 2, (a ?? b) || c)"},
    DiagnosticHelp{"S203", "대입할 수 없는 대상", "이름 · 필드 (a.b) · 원소 (a[i]) 에만 대입한다"},
    DiagnosticHelp{"S204", "중첩이 너무 깊다", "식 · 블록을 함수 · 변수로 나눈다"},
    DiagnosticHelp{"S299", "오류가 너무 많다", "앞의 오류부터 고친 뒤 다시 검사한다"},
    // 부분집합 밖 (TS 는 받지만 SandboxScript 에 없다 — 19 4장)
    DiagnosticHelp{"S301", "class 는 없다",
                   "interface 로 모양을, 최상위 function 으로 동작을, 틱을 넘는 상태는 컴포넌트에 둔다"},
    DiagnosticHelp{"S302", "try · catch · throw 는 없다",
                   "실패는 반환값 (T | null) 으로 알린다 — 실행 중 오류는 트랩이 된다"},
    DiagnosticHelp{"S303", "var 는 없다", "바뀌지 않으면 const, 바뀌면 let"},
    DiagnosticHelp{"S304", "함수 식 · 중첩 함수 · 클로저는 없다",
                   "최상위 function 으로 꺼내고, 콜백 자리에는 바깥 이름을 잡지 않는 화살표를 쓴다"},
    DiagnosticHelp{"S305", "async · await · 제너레이터는 없다",
                   "시뮬레이션은 기다리지 않는다 — 틱마다 상태를 보고 진행한다"},
    DiagnosticHelp{"S306", "for-in 은 없다", "배열 · 질의는 for (const x of xs)"},
    DiagnosticHelp{"S307", "레이블 문장은 없다", "안쪽 반복을 함수로 꺼내 return 으로 빠져나온다"},
    DiagnosticHelp{"S308", "with · debugger 는 없다", "지운다 — 값은 ctx.log 로 본다"},
    DiagnosticHelp{"S309", "typeof · void · delete · instanceof · in 은 없다",
                   "타입은 검사기가 안다 — null 은 x !== null 로, 필드는 interface 로"},
    DiagnosticHelp{"S310", "BigInt 는 없다", "number 나 i32 를 쓴다"},
    DiagnosticHelp{"S311", "== · != 는 없다", "=== · !== 를 쓴다"},
    DiagnosticHelp{"S312", ">>> 는 없다", "i32 의 >> 를 쓴다"},
    DiagnosticHelp{"S313", "펼침 · 나머지 (...) 는 없다", "배열을 그대로 넘기거나 for-of 로 하나씩 넣는다"},
    DiagnosticHelp{"S314", "선택 연결 ?. 은 없다", "if (x !== null) 로 좁힌 뒤 필드를 읽는다"},
    DiagnosticHelp{"S315", "new · this · super 는 없다",
                   "객체 리터럴 { … } 과 ctx 매개변수를 쓴다 (new Map · Set 은 된다)"},
    DiagnosticHelp{"S316", "데코레이터 · #비공개 이름은 없다", "지우고 보통 이름 · 함수로 쓴다"},
    DiagnosticHelp{"S317", "구조 분해는 없다", "const a = o.a 처럼 하나씩 꺼낸다"},
    DiagnosticHelp{"S318", "쉼표 연산자 · 여러 선언은 없다", "문장 · 선언을 하나씩 나눈다"},
    DiagnosticHelp{"S319", "declare · namespace · abstract 는 없다", "파일 하나가 모듈 하나 — 이름은 export 로 나눈다"},
    DiagnosticHelp{"S320", "export { } · export * 는 없다", "선언 앞에 export 를 붙인다 (export function f …)"},
    DiagnosticHelp{"S321", "그 import 꼴은 없다", "import { 이름 } from \"./파일\" 꼴만 쓴다"},
    DiagnosticHelp{"S322", "동적 import() 는 없다", "파일 맨 위의 정적 import 를 쓴다"},
    DiagnosticHelp{"S323", "getter · setter 는 없다", "필드나 함수로 쓴다"},
    DiagnosticHelp{"S324", "널 아님 단언 x! 는 없다", "if (x !== null) 로 좁힌다"},
    DiagnosticHelp{"S325", "타입 단언 as T 는 없다", "타입을 맞춰 선언한다 (상수 표는 as const 만)"},
    DiagnosticHelp{"S326", "선택 · 기본값 매개변수 · 선택 필드는 없다",
                   "모든 인자를 넘기고, 없을 수 있는 값은 T | null 로"},
    DiagnosticHelp{"S327", "제네릭 (타입 매개변수) 은 없다", "타입마다 함수를 따로 쓴다"},
    DiagnosticHelp{"S328", "모듈 최상위의 let · 문장은 없다",
                   "틱을 넘는 상태는 컴포넌트에, 상수는 const 로, 실행할 일은 시스템 콜백 (onTick) 안에"},
    DiagnosticHelp{"S331", "지원하지 않는 타입 문법", "interface · enum · T[] · T | null 로 적는다"},
    DiagnosticHelp{"S332", "계산된 키 · 숫자 키는 없다", "이름 키를 쓰고, 번호로 찾을 것은 배열 · Map 으로"},
    DiagnosticHelp{"S333", "태그 템플릿은 없다", "함수를 부른다 — f(`글 ${x}`)"},
    DiagnosticHelp{"S334", "식 안의 대입 · ++ 는 없다", "대입 · ++ 를 자기 문장으로 나눈다"},
    DiagnosticHelp{"S335", "선언 없는 for-of 는 없다", "for (const x of xs) 로 쓴다"},
    DiagnosticHelp{"S336", "정규식은 없다", "문자열 메서드 (includes · startsWith · split) 를 쓴다"},
    // 이름 · 타입 (검사기 — 19 11장)
    DiagnosticHelp{"S401", "없는 이름 · 타입", "철자를 확인하고, 다른 파일 · \"sandbox\" 의 것은 import 한다"},
    DiagnosticHelp{"S402", "같은 범위에 같은 이름이 둘", "한쪽 이름을 바꾼다"},
    DiagnosticHelp{"S403", "선언하기 전에 쓴다", "선언을 쓰는 곳보다 위로 옮긴다"},
    DiagnosticHelp{"S404", "타입이 맞지 않다",
                   "넣는 값 · 인자 · 반환의 타입을 받는 쪽 타입에 맞춘다 (number → i32 는 toI32)"},
    DiagnosticHelp{"S405", "인자 수가 맞지 않다", "함수 서명의 매개변수 수만큼 넘긴다"},
    DiagnosticHelp{"S406", "그 타입에 없는 필드 · 메서드",
                   "interface 의 필드 이름을 확인한다 (SDK 는 sdk/sandbox.d.ts)"},
    DiagnosticHelp{"S407", "null 일 수 있다", "if (x !== null) 로 좁히거나 x ?? 기본값 을 쓴다"},
    DiagnosticHelp{"S408", "조건은 boolean 이어야 한다", "x !== null · n !== 0 · s !== \"\" 처럼 비교로 쓴다"},
    DiagnosticHelp{"S409", "i32 로 넣을 수 없는 수", "toI32(x) 나 idiv(a, b) 로 정수로 만든다 (범위 · 정수 상수 확인)"},
    DiagnosticHelp{"S410", "대입할 수 없다",
                   "바꿀 값은 let 으로 선언한다 — 최상위 const · readonly · Host 값은 바꿀 수 없다"},
    DiagnosticHelp{"S411", "값이 아닌 것을 값으로 썼다",
                   "함수는 부르기만 하고 (f()), 타입 · enum 이름은 값 자리에 쓰지 않는다"},
    DiagnosticHelp{"S412", "반환이 맞지 않다",
                   "모든 길에 같은 타입의 return 을 두고, void 함수는 값을 돌려주지 않는다"},
    DiagnosticHelp{
        "S413", "SandboxScript 에 없는 전역 · 타입",
        "진단 문장의 대안을 쓴다 — any · unknown 은 실제 타입, console 은 ctx.log, Math.random 은 ctx.random"},
    DiagnosticHelp{"S414", "타입을 적어야 한다",
                   "매개변수 · 빈 배열 · null 초기값에 타입을 붙인다 (let xs: i32[] = [])"},
    DiagnosticHelp{"S415", "최상위 const 는 기본 타입 상수식",
                   "숫자 · 문자열 · boolean 상수식만 — 표 · 객체는 함수 안에서 만든다"},
    DiagnosticHelp{"S416", "import 를 풀 수 없다",
                   "경로 (./파일 — 확장자 없이) · export 이름 · 순환 import 를 확인한다"},
    DiagnosticHelp{"S417", "지원하지 않는 타입 모양", "유니언은 T | null 만 — 여러 종류는 enum · interface 로 나눈다"},
    DiagnosticHelp{"S418", "잘못된 enum 값", "enum 값은 i32 정수 상수만 (문자열 enum 은 없다)"},
    DiagnosticHelp{"S419", "break · continue 의 자리가 틀렸다", "반복문 · switch 안에서만 쓴다"},
    DiagnosticHelp{"S420", "switch 의 case 가 떨어진다", "case 끝에 break · return 을 적는다"},
    DiagnosticHelp{"S421", "재귀 함수에 반환 타입이 없다", "function f(n: i32): i32 처럼 반환 타입을 적는다"},
    DiagnosticHelp{"S422", "연산자에 맞지 않는 타입", "같은 타입끼리 계산 · 비교하고, && · ! 는 boolean 에만 쓴다"},
    DiagnosticHelp{"S423", "export default 는 defineSystem 만", "다른 것은 export function · export const 로 낸다"},
    DiagnosticHelp{"S424", "defineComponent 정의가 틀렸다",
                   "id · version · fields 모양과 기본값 · 범위를 19 15장대로 맞춘다"},
    DiagnosticHelp{"S425", "defineComponent · field 의 자리가 틀렸다",
                   "export const X = defineComponent({ … }) 의 초기값에서만 쓴다"},
    DiagnosticHelp{"S426", "defineSystem 정의가 틀렸다", "id · 콜백 이름 · 서명 · events 를 19 17 · 18장대로 맞춘다"},
    DiagnosticHelp{"S427", "defineSystem 의 자리가 틀렸다", "export default defineSystem({ … }) 로만 쓴다"},
    // 코드 생성
    DiagnosticHelp{"S500", "검사 오류가 있어 코드를 만들지 않았다", "위의 다른 오류를 먼저 고친다"},
    DiagnosticHelp{"S501", "함수 · 스크립트가 상한을 넘는다", "큰 함수 · 표를 작은 함수 · 여러 파일로 나눈다"},
    DiagnosticHelp{"S502", "컴파일러 버그 (생성 코드가 검증을 못 지났다)",
                   "sbx report 로 보고 묶음을 만들어 알린다 — 그 식을 다르게 써 피한다"},
};

} // namespace

std::span<const DiagnosticHelp> diagnosticHelpTable() noexcept {
    return kTable;
}

const DiagnosticHelp* findDiagnosticHelp(std::string_view code) noexcept {
    const auto it = std::lower_bound(kTable.begin(), kTable.end(), code,
                                     [](const DiagnosticHelp& h, std::string_view c) { return h.code < c; });
    return it != kTable.end() && it->code == code ? &*it : nullptr;
}

} // namespace sbx::script
