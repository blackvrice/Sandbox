# 19. SandboxScript (v0.3)

> **규범 문서.** 게임 스크립트 언어 SandboxScript — TypeScript 의 엄격한 부분집합 — 의 낱말 · 문법 · 부분집합 · 진단.
> 결정: [ADR-0056](adr/0056-sandboxscript-language-subset-own-compiler-no-gc.md) (언어 · D-01), [ADR-0057](adr/0057-host-api-single-definition-cpp-table-sdk-generated.md)
> (Host API · D-04). 계획: [18-PLATFORM](18-PLATFORM.md) 6.2. 결정론은 [04-DETERMINISM](04-DETERMINISM.md) 이 이 문서보다 우선한다.

---

## 0. 상태

| 단계                                   | 상태                  | 소스 · 시험                                                            |
|----------------------------------------|-----------------------|------------------------------------------------------------------------|
| 낱말 (렉서) · 문법 (파서 → AST) · 진단 | 구현 (v0.3 M1-T1)     | `script/lang/` Lexer · Parser · Ast · Diagnostic, unit_script          |
| 이름 · 타입 검사 (5장)                 | [계획] v0.3 M1-T2     |                                                                        |
| IR · 바이트코드 · 검증기               | [계획] v0.3 M1-T3     |                                                                        |
| VM (6장) · Host API                    | [계획] v0.3 M2        |                                                                        |

규칙 하나: **SandboxScript 가 받는 파일은 SDK 의 `sandbox.d.ts` 와 함께 `tsc --strict` 도 받고, 뜻이 같다.** 거꾸로는 아니다 —
TS 가 받지만 SandboxScript 가 거절하는 문법은 S3xx 로 이유와 대안을 말한다 (4장).

## 1. 파일 · 모듈

```text
자리       콘텐츠 팩의 scripts/**/*.ts (UTF-8, BOM 있어도 됨, ≤ 4 MiB — kMaxSourceBytes). 파일 하나 = 모듈 하나
최상위     선언만: import · export · function · const · type · interface · enum · const enum · export default 식
           (let · 문장은 S328 — 틱을 넘는 상태는 컴포넌트에, ADR-0056 4). 상수식인지는 검사기 [계획 M1-T2]
import     import { a, b as c } from "./상대경로" (같은 팩의 scripts/ 안) · from "sandbox" (SDK). 기본 · 이름공간 ·
           부수 효과 · import type · 동적 import 는 없다 (S321 · S322). 순환 import 는 오류 [계획 M1-T2]
export     export 선언 · export default 식 (defineSystem — [계획 M4]). export { } · export * · export = 는 없다 (S320)
```

## 2. 낱말 (렉서 — `script/lang/Lexer`)

```text
공백       스페이스 · 탭 · \v · \f · NBSP(U+00A0) · U+FEFF. 줄바꿈 \n · \r\n · \r · U+2028 · U+2029
주석       // 한 줄 · /* 여러 줄 */ — 줄바꿈이 든 /* */ 는 줄바꿈 하나로 친다 (ASI — JS 와 같다)
글자       코드 (이름 · 연산자) 는 ASCII 만. 문자열 · 템플릿 · 주석 안은 UTF-8 무엇이든. 이름의 한글 등은 S101
           [v0.3 뒤 검토]. UTF-8 이 아닌 바이트열은 S100 (렉서가 멈춘다)
이름       [A-Za-z_$][A-Za-z0-9_$]*. JS 엄격 모드 예약어 + TS 예약어 (let · static · interface · private …) 는 이름이 아니다.
           문맥 낱말 (type · as · from · of · readonly · async · get · set · declare · namespace) 은 이름이다
숫자       10진 (123 · 1.5 · .5 · 1. · 1e3 · 2.5E-1), 0x · 0b · 0o, 구분자 _ (숫자 사이에만). 값은 f64 로 정확하게 반올림
           (from_chars, 없는 표준 라이브러리는 C 로케일 strtod_l — 로케일 무관). "정수 리터럴" = 소수점 · 지수 없음 (i32 후보).
           오류 S106: 0 으로 시작하는 10진수 (017 — 0o17), 끝 · 겹 구분자, 숫자 뒤에 바로 글자, 2^53 을 넘는 진법 정수.
           BigInt (123n) 는 S310
문자열     '…' · "…". 이스케이프 \n \t \r \b \f \v \0 \xHH \uHHHH \u{H…} (≤ U+10FFFF), 서로게이트 짝 → 코드 포인트 하나,
           짝 없는 서로게이트 · 8진 이스케이프는 S105, 그 밖의 \x 는 x 자신, \ 줄바꿈 = 줄 이음. 닫히지 않으면 S103
템플릿     `…${식}…` — 끼운 식 안의 { } 는 깊이로 따라간다 (중첩 템플릿 됨). 템플릿 안의 \r\n 은 \n. 닫히지 않으면 S108
정규식     없다 — '/' 는 언제나 나눗셈 (식 자리의 / 는 S336)
```

## 3. 문법 (파서 — `script/lang/Parser`, AST — `script/lang/Ast`)

```text
Module      := Item*
Item        := Import | export? Decl | export default Expr ;
Decl        := function Name ( Params ) (: Type)? Block
             | const Name (: Type)? = Expr ;          | type Name = Type ;
             | interface Name { PropSig* }           | const? enum Name { Name (= Expr)?, … }
Params      := (Name : Type ,)*                       — 타입이 없는 매개변수는 검사기가 거절 [계획 M1-T2]
PropSig     := readonly? Name : Type  (; , 줄바꿈으로 나눈다)
Type        := Prim ([])* (| Prim ([])*)*             Prim := Name (< Type, … >)? | null | void | { PropSig* } | ( Type )
Stmt        := Block | ; | (const|let) Name (: Type)? (= Expr)? ; | Expr ; | if ( Expr ) Stmt (else Stmt)?
             | while ( Expr ) Stmt | do Stmt while ( Expr ) | for ( Init? ; Expr? ; Expr? ) Stmt
             | for ( (const|let) Name of Expr ) Stmt | switch ( Expr ) { (case Expr : | default :) Stmt* … }
             | break ; | continue ; | return Expr? ;
Expr        := 대입 (= += -= *= /= %= **= <<= >>= &= |= ^= &&= ||= ??=) > 삼항 ?: > ?? > || > && > | > ^ > & >
               === !== > < > <= >= (as const) > << >> > + - > * / % > ** (오른쪽 결합) > 단항 ! ~ + - ++ -- > 뒤붙이
               . [ ] ( ) ++ -- > 기본 (숫자 · 문자열 · 템플릿 · true · false · null · 이름 · ( 식 ) · [ … ] · { … })
Object      := { (Name : Expr | "키" : Expr | Name | Name ( Params ) (: Type)? Block) , … }   — 마지막은 메서드 (onTick(ctx) { })
```

```text
ASI        JS 규칙: 식은 줄을 넘어 이어지고 (f\n(1) 은 호출), 문장은 ; · } · 파일 끝 · 앞 줄바꿈에서 끝난다. return · break ·
           continue 다음 줄바꿈과 뒤붙이 ++ -- 앞 줄바꿈에서 끊긴다. 같은 줄의 else 앞에는 ; 가 필요하다 (JS 와 같이 S201).
           do-while 뒤의 ; 는 없어도 된다. 줄바꿈 뒤의 as 는 이어지지 않는다 (TS)
대입       대입 · ++ · -- 는 식 문장 (for 의 초기 · 갱신 포함) 의 맨 위에만 — 식 안 (if (a = 1) · f(i++) · a = b = c) 은 S334.
           대상은 이름 · 필드 · 원소만 (S203)
타입 인자  Array<Array<i32>> 의 >> 는 > 둘로 나눈다
?? 섞기    ?? 와 && · || 는 괄호 없이 섞지 않는다, 단항 뒤의 ** 는 괄호로 (S202 — JS 문법 오류와 같다)
AST        평평한 풀 + 자식 구간, 마디 종류마다 자식 자리가 정해져 있다 (Ast.hpp 의 표). dump() = S 식 한 줄 (시험의 기준)
```

## 4. 부분집합 밖 (S3xx — TS 는 받지만 SandboxScript 에 없다)

| 코드 | 문법                                                      | 대신                                         |
|------|-----------------------------------------------------------|----------------------------------------------|
| S301 | class                                                     | interface + 최상위 function, 상태는 컴포넌트 |
| S302 | try · catch · finally · throw                             | 반환값, 실행 중 오류는 트랩 (ADR-0056 5)     |
| S303 | var                                                       | const · let                                  |
| S304 | 화살표 함수 · 함수 식 · 함수 안의 함수 (클로저)           | 최상위 function                              |
| S305 | async · await · 제너레이터 · for await                    | 시뮬레이션은 기다리지 않는다                 |
| S306 | for-in                                                    | 배열 · 질의를 for-of                         |
| S307 | 레이블 문장 · 레이블 break / continue                     | —                                            |
| S308 | with · debugger                                           | —                                            |
| S309 | typeof · void · delete · instanceof · in 연산자           | —                                            |
| S310 | BigInt 리터럴                                             | number · i32                                 |
| S311 | == · !=                                                   | === · !==                                    |
| S312 | >>> · >>>=                                                | >> (i32 — ADR-0056 3)                        |
| S313 | 펼침 · 나머지 (...)                                       | 배열을 넘긴다                                |
| S314 | 선택 연결 ?.                                              | if (x !== null) 로 좁힌다                    |
| S315 | new · this · super                                        | 객체 리터럴 · ctx 매개변수                   |
| S316 | 데코레이터 @ · 비공개 이름 #x                             | —                                            |
| S317 | 구조 분해 (선언 · 매개변수)                               | 이름 하나씩                                  |
| S318 | 쉼표 연산자 · 여러 선언 (let a, b)                        | 문장을 나눈다                                |
| S319 | declare · namespace · module · abstract                   | 파일 하나 = 모듈 하나                        |
| S320 | export { } · export * · export =                          | export 선언                                  |
| S321 | 기본 · 이름공간 · 부수 효과 import, import type          | import { 이름 } from "…"                     |
| S322 | 동적 import()                                             | 정적 import                                  |
| S323 | getter · setter                                           | 필드 · 함수                                  |
| S324 | 널 아님 단언 x! · 확정 대입 let x!                        | if (x !== null)                              |
| S325 | 타입 단언 (as T · <T>e) · satisfies                       | as const 만                                  |
| S326 | 선택 매개변수 · 기본값 매개변수 · 선택 필드 x?: T         | 모든 인자, T \| null                          |
| S327 | 사용자 함수 · 타입의 타입 매개변수 (제네릭)              | —                                            |
| S328 | 모듈 최상위의 let · 문장                                  | 컴포넌트 · const                             |
| S331 | 타입 문법: 리터럴 · 튜플 · 함수 · 교차 · 색인 서명 · 메서드 서명 · keyof · typeof · A.B · extends | enum · interface |
| S332 | 계산된 키 · 숫자 키                                       | 이름 키 · 배열                               |
| S333 | 태그 템플릿                                               | —                                            |
| S334 | 식 안의 대입 · ++ · --                                    | 문장으로                                     |
| S335 | 선언 없는 for-of (for (x of xs))                          | for (const x of xs)                          |
| S336 | 정규식 리터럴                                             | —                                            |

검사기 [계획 M1-T2] 가 더하는 것: any · unknown · undefined · 타입 없는 매개변수 · 객체 키 순회 · Math.random · Date 등
이름 수준의 금지, 상수식이 아닌 최상위 const, switch 의 빈 case 가 아닌 fallthrough.

## 5. 타입 · 숫자 [계획 v0.3 M1-T2 — 규칙은 ADR-0056 3 · 6]

```text
number  f64 (JS 와 같은 뜻)       i32  32 비트로 감싼다 (+ - | 0, * = Math.imul), / → number, % 0 은 트랩, >>> 없음
f32     컴포넌트 필드 종류 — 읽으면 f64, 쓰면 f32 로 반올림     bool · string (불변 UTF-8, 바이트 순 비교)
T[]     참조, 길이 상한           T | null  좁히기는 if (x !== null)        interface · type  구조체 (객체 리터럴)
```

## 6. 메모리 · 실행 [계획 v0.3 M2 — ADR-0056 4 · 5, ADR-0057 5]

호출 하나 (콜백 한 번) 의 아레나, GC 없음, 스크립트 전역 가변 상태 없음. 트랩 = 그 호출을 멈추고 그 호출이 쌓은 쓰기 · 명령을 버린다.

## 7. 결정론

```text
- 렉서 · 파서 · (이후) 컴파일러의 출력은 입력 소스만의 함수다: 로케일 · 시간 · 디렉터리 순서를 보지 않는다 (04 4.5)
- 숫자 리터럴은 정확히 반올림한다 (툴체인이 달라도 같은 f64)
- SandboxScript 타깃은 sbx_simulation_flags (04 4.6) 를 쓴다 — VM 의 부동소수 연산 [계획 M2] 을 위해
```

## 8. 진단

```text
형식   "<파일>:<줄>:<열>: 오류 S201 — <설명>"   줄 · 열은 1 부터, 열은 코드 포인트 단위 (ContentIssue 와 같은 "오류/경고 규칙 — 설명")
번호   S100 소스 (UTF-8 · 크기)  S101 알 수 없는 글자  S103 문자열  S104 주석  S105 이스케이프  S106 숫자  S108 템플릿
       S201 기대한 낱말이 없다  S202 괄호가 필요한 섞기  S203 대입 대상  S204 중첩이 너무 깊다 (> 200 — kMaxParseDepth)
       S299 오류가 너무 많다 (> 64 — kMaxParseErrors, 거기서 멈춘다)  S3xx 부분집합 밖 (4장)  S4xx 타입 [계획 M1-T2]
모음   첫 오류에서 멈추지 않는다 — 문장 단위로 회복해 나머지를 계속 본다. 믿지 않는 소스라 깊이 · 오류 수에 상한이 있다
```

## 9. 구현 위치

```text
script/                SandboxScript (정적 라이브러리) — 의존 Foundation 만 (ECS · 그래픽 · OS 를 모른다, ADR-0056 9)
  lang/Diagnostic      진단 · 목록                 lang/Token · Lexer   낱말 · 렉서
  lang/Ast             마디 풀 · dump              lang/Parser          파서 · ASI · 부분집합 검사 · 회복
경계                   cmake: sbx_forbid_link(SandboxScript SandboxCore …), tools/check_includes.py: script/ → foundation · script,
                       그래픽 · OS 헤더 금지 (픽스처 tests/arch/fixtures/script)
시험                   tests/unit/script (unit_script) — 낱말 · 우선순위 · ASI · 선언 · 타입 · 부분집합 S3xx 전부 · 회복 · 상한 ·
                       잘린 소스와 의사 난수 바이트열 (죽지 않는다)
```
