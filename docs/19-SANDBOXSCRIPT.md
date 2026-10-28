# 19. SandboxScript (v0.3)

> **규범 문서.** 게임 스크립트 언어 SandboxScript — TypeScript 의 엄격한 부분집합 — 와 그 컴파일러 · 바이트코드 · VM · Host API ·
> 스크립트 컴포넌트 · 시스템 · 도구. 상태는 0장 (v0.3 ✅ 0.3.0 — 2026-10-27, [릴리스 노트](releases/v0.3.0.md)).
> 결정: [ADR-0056](adr/0056-sandboxscript-language-subset-own-compiler-no-gc.md) (언어 · D-01), [ADR-0057](adr/0057-host-api-single-definition-cpp-table-sdk-generated.md)
> (Host API · D-04), [ADR-0058](adr/0058-bytecode-register-machine-typed-registers-verifier.md) (바이트코드), ADR-0059~0061 (컴포넌트),
> ADR-0062~0064 (시스템), ADR-0065 · 0066 (도구 · 오버레이), ADR-0067 · 0068 (클라이언트 스크립트 · 프로파일), ADR-0069 · 0070 (데모 · 보호). 계획: [18-PLATFORM](18-PLATFORM.md) 6.2. 결정론은 [04-DETERMINISM](04-DETERMINISM.md) 이 이 문서보다 우선한다.

---

## 0. 상태

| 단계                                   | 상태                  | 소스 · 시험                                                            |
|----------------------------------------|-----------------------|------------------------------------------------------------------------|
| 낱말 (렉서) · 문법 (파서 → AST) · 진단 | 구현 (v0.3 M1-T1)     | `script/lang/` Lexer · Parser · Ast · Diagnostic, unit_script          |
| 이름 · 타입 검사 (5 · 11장)            | 구현 (v0.3 M1-T2)     | `script/lang/` Types · Checker, unit_script (test_checker)             |
| Host API 표 (10장)                     | 첫 항목 (v0.3 M1-T2)  | `script/api/HostApi` — ctx.tick · ctx.log, 지문                        |
| 코드 생성 · 바이트코드 · 검증기 (12장) | 구현 (v0.3 M1-T3)     | `script/lang/Codegen`, `script/vm/` Bytecode · Verifier, ADR-0058      |
| VM 인터프리터 (6 · 13장)               | 구현 (v0.3 M2-T1)     | `script/vm/` Interpreter · Numbers, unit_script (test_vm)              |
| Host 바인딩 (엔진) · d.ts (14장)       | 구현 (v0.3 M2-T2)     | `core/script/ScriptHost`, `script/sdk/SdkGenerator`, `sdk/sandbox.d.ts` |
| 스크립트 컴포넌트 (15장)               | 정의 · 카탈로그 (M3-T1) · 콘텐츠 로드 (M3-T2) · 세이브 마이그레이션 (M3-T4, ADR-0061) | `script/api/ComponentSchema`, `core/ecs/DynamicComponent`, `core/script/ScriptComponents`, `core/content/ContentLoader`, ADR-0059 |
| 컴포넌트 읽기 · 질의 · 쓰기 (16장)     | 구현 (v0.3 M3-T3)     | Host 표 ctx.read · has · query · write, LOADK (.sbc 형식 2), `core/script/ScriptHost`, ADR-0060 |
| 스크립트 시스템 (17장)                 | 구현 (v0.3 M4-T1)     | defineSystem (검사기), 시스템 표 (.sbc 형식 3), `core/script/ScriptSystemAdapter` (단계 16b), ADR-0062 |
| 시스템 콜백 · 이벤트 (18장)            | 구현 (v0.3 M4-T2)     | onStart · onStop · onEvent, `script/api/SystemEvents`, .sbc 형식 4, `core/simulation/SimulationScripts` (단계 17d), ADR-0063 |
| 예산 · 트랩 정책 · 통계 (19장)         | 구현 (v0.3 M4-T3)     | `core/script/ScriptSystemAdapter` (틱 연료 · 끄기 · 상태 · 통계), vm::Limits::maxHostCalls, ScriptHost 로그 상한, ADR-0064 |
| 스크립트 오버레이 · 에디터 (20장)      | 구현 (v0.3 M5-T2)     | 오버레이 종류 Script (프로토콜 13), 로더의 다시 컴파일 · V11, `editor/ui/ContentPanel` 스크립트, ADR-0066 |
| 클라이언트 스크립트 (21장)             | 최소 (v0.3 M5-T3)     | domain "client" · onUpdate, Host 표 client.*, .sbc 형식 5, `core/script/ClientScriptHost` · `ClientScriptSystems`, `apps/client/ui/ScriptPanel`, ADR-0067 |
| 보호 시험 · 벤치 (24장)                | 구현 (v0.3 M6-T2)     | 나쁜 스크립트 = 흔적 없음 (해시), 바이트코드 변형 퍼즈, 검증기의 엔진 이름공간 거절, sbx_bench script.regen, ADR-0070 |
| 데모 samples/mana_regen (23장)         | 구현 (v0.3 M6-T1)     | 컴포넌트 · server 시스템 둘 · client 시스템 · 행동 eco.energize, CTest sample_mana_regen, unit_core test_mana_regen_sample, ADR-0069 |
| 스크립트 프로파일 (22장)               | 구현 (v0.3 M5-T4)     | `core/script/ScriptClock` (주입한 진단 시계), ServerStats 스크립트 표 (프로토콜 14), "스크립트 프로파일" 패널, ADR-0068 |

규칙 하나: **SandboxScript 가 받는 파일은 SDK 의 `sandbox.d.ts` 와 함께 `tsc --strict` 도 받고, 뜻이 같다.** 거꾸로는 아니다 —
TS 가 받지만 SandboxScript 가 거절하는 문법은 S3xx 로 이유와 대안을 말한다 (4장).

## 1. 파일 · 모듈

```text
자리       콘텐츠 팩의 scripts/**/*.ts (UTF-8, BOM 있어도 됨, ≤ 4 MiB — kMaxSourceBytes). 파일 하나 = 모듈 하나
최상위     선언만: import · export · function · const · type · interface · enum · const enum · export default 식
           (let · 문장은 S328 — 틱을 넘는 상태는 컴포넌트에, ADR-0056 4). 최상위 const 는 기본 타입 상수식 (S415 — 11장)
           이나 defineComponent({ … }) (15장)
import     import { a, b as c } from "./상대경로" · from "sandbox" (SDK). 기본 · 이름공간 · 부수 효과 · import type ·
           동적 import 는 없다 (S321 · S322). 경로 = 가져오는 파일의 폴더 기준, 확장자 없이 ("./b" → b.ts), "." · ".." 마디,
           첫 폴더 (scripts/) 밖으로 나갈 수 없다. 없는 파일 · 없는 export · 순환 import 는 S416 (resolveImportPath)
           (v0.4 M4-T1, ADR-0075) 다른 팩: from "@<팩 id>/<scripts 안 경로>" → 그 팩의 scripts/<경로>.ts. 그 팩이 이 팩의 직접
           의존 (pack.json dependencies · requires) 이어야 한다 — 아니면 · 모양이 틀리면 · 없는 파일이면 S416. 정적 링크:
           가져온 함수는 이 팩의 모듈로 컴파일되고 (쓰는 파일만), 의존 팩의 컴포넌트 · 시스템 · export 는 그 팩의 것이다
           (등록 · 실행은 한 번). IDE 는 tsconfig.json paths "@<팩>/*" (sbx install · add 가 갱신 — 14장)
export     export 선언 · export default defineSystem({ … }) 하나 (17장 — 다른 식은 S423). export { } · export * · export = 는 없다 (S320)
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
Params      := (Name : Type ,)*                       — 타입이 없는 매개변수는 검사기가 거절 (S414 — 시스템 메서드의 ctx · e 는 문맥 타입, 17 · 18장)
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

검사기 (11장) 가 더하는 것: any · unknown · undefined · 타입 없는 매개변수 · 객체 키 순회 · Math.random · Date 등
이름 수준의 금지, 상수식이 아닌 최상위 const, switch 의 빈 case 가 아닌 fallthrough.

## 5. 타입 · 숫자 (v0.3 M1-T2 — `script/lang/Types`, ADR-0056 3 · 6)

```text
타입 이름   number · boolean · string · void (반환 타입만) · null · T[] · Array<T> · T | null · { x: T; … } · interface ·
            type 별칭 (순환 S417) · enum · SDK 의 i32 · Entity · ServerContext (import { … } from "sandbox").
            any · unknown · undefined · never · object · Record · Map · ReadonlyArray … 은 S413. bool 은 없다 (boolean)
유니언      T | null 만 (null | T · 괄호 · 겹친 null 도 같다). 서로 다른 타입의 유니언은 S417
number      f64 (JS 와 같은 뜻)
i32         32 비트로 감싼다. i32 ∘ i32 (+ - * %) → i32, 비트 연산 (& | ^ << >> ~) 은 언제나 i32 (number 도 JS ToInt32),
            / · ** → number, 섞으면 number. number → i32 는 toI32(x) 만 — 단 정수 · i32 범위의 상수식 (리터럴 · const ·
            접은 식) 은 그대로 들어간다 (S409). idiv(a, b) = 정수 나눗셈. 상수의 i32 산술도 감싼다 (2147483647 + 1 → -2^31)
enum        정수 값 (자동 증가 · 상수식, S418), 이름 있는 타입 — i32 · number 로 넓어지고 거꾸로는 아니다, 다른 enum 과 비교 없음
넣기        같은 타입 · i32 → number · enum → i32 · number · null 과 T → T | null. 배열 원소 · 구조체 필드는 양쪽으로 맞아야
            한다 (참조라 — readonly 필드도, 실행 배치가 같아야 해서 — 12장). 구조체는 필드 이름 집합이 같아야 한다 (객체
            리터럴의 빠진 · 남는 필드 S404)
연산        조건 · && · || · ! 는 boolean 만 (S408 · S422 — x !== null · x !== 0 로 쓴다), === !== 는 맞는 타입끼리,
            < > <= >= 는 두 수 · 두 문자열, 문자열 + 기본 타입 → string, ?? 는 T | null 의 왼쪽에만 (S422)
f32         컴포넌트 필드 종류 — 읽으면 f64, 쓰면 f32 로 반올림 (15 · 16장)
string      불변 UTF-8, 비교는 바이트 순서. .length (i32)
T[]         참조, .length (i32) · .push(x) (i32). 빈 리터럴 [] 은 타입이 필요하다 (S414). 첨자는 i32 · number
Math        min · max (모두 i32 면 i32) · abs · floor · ceil · round · trunc · sign · sqrt · sin · cos · tan · atan2 · pow · exp ·
            log · hypot → number, 상수 PI · E · LN2 · LN10 · LOG2E · LOG10E · SQRT2 · SQRT1_2. Math.random 은 S413
```

## 6. 메모리 · 실행 (v0.3 M2-T1 — 13장, ADR-0056 4 · 5, ADR-0057 5)

호출 하나 (콜백 한 번) 의 아레나, GC 없음, 스크립트 전역 가변 상태 없음. 트랩 = 그 호출을 멈춘다 — 그 호출이 쌓은 쓰기 ·
명령을 버리는 것은 Host 바인딩 (호출 단위 쓰기 버퍼 — 16장).

## 7. 결정론

```text
- 렉서 · 파서 · 검사기 · 코드 생성의 출력은 입력 소스만의 함수다: 로케일 · 시간 · 디렉터리 순서를 보지 않는다 (04 4.5) —
  모듈은 경로 정렬 순, 의존이 먼저 (위상 순서)
- 상수 접기는 실행과 같은 규칙 (f64 연산 · i32 감싸기 · fmod · pow). 숫자 → 문자열은 접지 않는다 (JS Number#toString 은 VM — 13장)
- 숫자 리터럴은 정확히 반올림한다 (툴체인이 달라도 같은 f64)
- SandboxScript 타깃은 sbx_simulation_flags (04 4.6) 를 쓴다 — VM 의 부동소수 연산 (13장) 을 위해
```

## 8. 진단

```text
형식   "<파일>:<줄>:<열>: 오류 S201 — <설명>"   줄 · 열은 1 부터, 열은 코드 포인트 단위 (ContentIssue 와 같은 "오류/경고 규칙 — 설명")
번호   S100 소스 (UTF-8 · 크기)  S101 알 수 없는 글자  S103 문자열  S104 주석  S105 이스케이프  S106 숫자  S108 템플릿
       S201 기대한 낱말이 없다  S202 괄호가 필요한 섞기  S203 대입 대상  S204 중첩이 너무 깊다 (> 200 — kMaxParseDepth)
       S299 오류가 너무 많다 (> 64 — kMaxParseErrors, 거기서 멈춘다)  S3xx 부분집합 밖 (4장)  S4xx 이름 · 타입 (11장)
       S500 검사 오류가 있어 코드를 만들지 않는다  S501 상한 (레지스터 4096 · 상수 65535 · 명령) — 함수를 나눈다
       S502 생성한 코드가 검증을 통과하지 못했다 (컴파일러 버그 — 보고한다)
모음   첫 오류에서 멈추지 않는다 — 문장 단위로 회복해 나머지를 계속 본다. 믿지 않는 소스라 깊이 · 오류 수에 상한이 있다
```

## 9. 구현 위치

```text
script/                SandboxScript (정적 라이브러리) — 의존 Foundation 만 (ECS · 그래픽 · OS 를 모른다, ADR-0056 9)
  lang/Diagnostic      진단 · 목록                 lang/Token · Lexer   낱말 · 렉서
  lang/Ast             마디 풀 · dump              lang/Parser          파서 · ASI · 부분집합 검사 · 회복
  lang/Types           타입 표 · 넣기 규칙          lang/Checker         이름 · 타입 검사 · 좁히기 · 상수 접기 → Program
  api/HostApi          Host 함수 표 · 불투명 타입 · 정규형 · 지문 (10장)
  api/MathBuiltins     Math 함수 · 상수 표 (검사기 · 검증기 · VM 이 같이 — 자리가 MATHF 의 번호)
  api/ComponentSchema  스크립트 컴포넌트의 정규 스키마 · 이름 규칙 · 엔진 이름공간 · f32 반올림 (15장)
  api/SystemEvents     시스템 이벤트 종류 · GameEvent 필드 · 예산 상한 (18장)
  lang/Codegen         Program → bc::Module (compileScripts = 검사 + 생성 + 검증)
  vm/Bytecode          명령 · 타입 표 · 모듈 · serialize · deserialize · disassemble
  vm/Verifier          verify · sameType · loadModule (읽기 + 검증)
  vm/Numbers           JS 수 규칙 (ToInt32 · Number#toString · round · pow · min/max · UTF-16 길이) — 검사기 접기와 VM 공용
  vm/Interpreter       Interpreter (call · 아레나 · 예산 · 트랩) · Value · IHost
  sdk/SdkGenerator     sandbox.d.ts 생성 (Host 표 · 불투명 타입 · SDK 내장)
core/script/          엔진 쪽 (SandboxCore — SandboxScript 를 링크한다)
  ScriptHost           Host 바인딩 목록 · 계약 검사 (도메인마다) · ScriptHost (IHost — 호출마다 ctx 핸들, 로그)
  ScriptValues         컴포넌트 값 바이트 → VM 칸 (서버 · 클라이언트 Host 공용)
  ClientScriptHost     클라이언트 바인딩 목록 · ClientScriptHost (복제 월드 읽기 · 글자 · 줄 — 21장)
  ClientScriptSystems  client 시스템을 프레임마다 (onUpdate · 프레임 연료 · 트랩이면 끔 — 21장)
  ScriptClock          진단 시계의 타입 (부르는 쪽이 넣는다 — 22장)
  ScriptComponents     스키마 → ecs 동적 컴포넌트 설명 → 카탈로그 (15장)
  ScriptSystemAdapter  단계 16b — 팩 바이트코드마다 인터프리터, (팩 순서, id) 차례로 onTick (17장) · 17d — onStart · onEvent ·
                       onStop (18장)
sdk/sandbox.d.ts      생성한 SDK 선언 (커밋 — 시험이 생성 결과와 같은지 본다)
경계                   cmake: sbx_forbid_link(SandboxScript SandboxCore …), tools/check_includes.py: script/ → foundation · script,
                       그래픽 · OS 헤더 금지 (픽스처 tests/arch/fixtures/script)
시험                   tests/unit/script (unit_script) — 낱말 · 우선순위 · ASI · 선언 · 타입 · 부분집합 S3xx 전부 · 회복 · 상한 ·
                       잘린 소스와 의사 난수 바이트열 (죽지 않는다), 검사기 (test_checker — 규칙마다 받는 · 거절하는 예, 모듈
                       사이, Host 표 지문)
```

## 10. Host API 표 (v0.3 M1-T2 — `script/api/HostApi`, ADR-0057)

```text
단일 정의   constexpr 표 kHostFunctions: id (영속 이름) · 주인 타입 · 멤버 · 속성/메서드 · 결과 · 매개변수 (≤ 4) · 도메인 ·
            효과 (read · write · structural · random · log · display) · 연료 · 설명. 검사기는 이 표로 ctx.멤버 를 찾는다 (d.ts 를 읽지 않는다)
지문        hostApiFingerprint = 정규형 줄 ("ctx.log ServerContext.log(string): void server log 16") 을 정렬해 FNV-1a 64,
            kHostApiVersion 과 함께 — .sbc 머리 (12장). 시험이 값을 고정한다 (표를 바꾸면 같은 커밋에서)
지금 항목   ctx.tick: i32 (read) · ctx.log(string): void (log). 불투명 타입 Entity · ServerContext.
            (v0.3 M3-T3 — 16장) ctx.read(entity, ref): T | null · ctx.has(entity, ref): boolean (read) · ctx.query(ref): Entity[]
            (read, 16 + 개체마다 1) · ctx.write(entity, ref, value: T): void (write). 제네릭 ABI Component · ComponentData ·
            ComponentDataOrNull · EntityList — T 는 Component 인자가 정한다 (isComponentGeneric).
            (v0.3 M5-T3 — 21장) 주인 ClientContext · 도메인 Client: client.tick · frame · dt · read · has · query · label · print.
            지문 e02f109682ecb304 (더하기만이라 kHostApiVersion 1 그대로)
[계획]      난수 (ctx.random) · 생성 · 파괴 · 점수 · 컴포넌트 붙이기 · 떼기, 엔진 컴포넌트의 ComponentRef (권한 표와 함께) — v0.5 M2
            (20 3장, ADR-0080)
```

## 11. 이름 · 타입 검사 (v0.3 M1-T2 — `script/lang/Checker`)

```text
차례       1 파일마다 파싱 (경로 정렬)  2 import 그래프 (S416 — 없는 파일 · 순환은 그 import 를 끊는다)
           3 모듈마다 의존 순서로: import 묶기 → 최상위 이름 (값 · 타입 이름공간 — enum 은 둘 다, 같은 이름 S402) →
             interface 필드 (재귀 됨) → type 별칭 → 함수 서명 → enum · 최상위 const (소스 순서 — 앞 것만 쓴다, S403)
           4 함수 몸. 반환 타입이 없으면 처음 부를 때 몸을 먼저 검사해 추론 (return 타입이 다르면 S412, 재귀면 S421)
파싱 오류  그 모듈은 검사하지 않고, 그 모듈에서 가져온 이름은 Error 타입 — 이어지는 오류가 없다
범위       블록마다 (for · for-of · switch 도). 함수 몸 = 매개변수와 같은 범위. 같은 블록의 뒤 선언을 앞에서 쓰면 S403,
           안쪽 블록의 같은 이름 (가리기) 은 된다
좁히기     if (x !== null) · x === null 의 else · && · || · ! · ? : · 한쪽이 빠져나가는 if (if (x === null) return) 뒤.
           좁히는 것은 대입이 없는 이름 (const · 최상위 const · 몸에서 대입하지 않는 let · 매개변수) 뿐 — 보수적, 필드는 아니다.
           좁히지 않은 T | null 의 필드 · 메서드 · 첨자 · for-of 는 S407
흐름       return 이 모든 길에 있어야 한다 (void 아니면 S412 — if/else 양쪽 · default 있는 switch). switch 의 빈 case 가
           아닌 떨어짐은 S420. break · continue 자리 S419
최상위     const = number · i32 · boolean · string · enum 상수식 (리터럴 · 연산 · 앞의 const · enum 값 · NaN · Infinity ·
           Math 상수). 함수 호출 · 배열 · 구조체는 S415 (표 상수는 [v0.3 뒤 검토]). export default · 객체 메서드는 defineSystem 만 (17장 — 밖은 S423).
           예외 하나: const X = defineComponent({ … }) — 상수 인자를 스키마로 읽는다 (15장, S424 · S425)
금지 이름  undefined · Date · console · eval · JSON · Map · Promise · setTimeout · parseInt … 은 S413 (이유와 대안을 말한다).
           SDK 이름을 가져오지 않으면 import { i32 } from "sandbox" 를 안내한다
결과       Program: 타입 표 · 심볼 · 함수 (서명 · 추론한 반환) · 모듈마다 마디 → (타입, 가리키는 것 — 심볼 · 필드 · Host 함수 ·
           내장 · enum 멤버 · length · push), 마디 → 상수값. IR (M1-T3) 이 이것을 읽는다
TS 와 다른 점 (더 엄격 — 받으면 tsc 도 받는다): 조건은 boolean 만, x === null 은 x 가 T | null 일 때만, 반환 타입을 추론한 함수도
           모든 길이 return, 배열 · 구조체 필드는 양쪽으로 맞아야 한다, let 에 초기값, 재귀 함수에 반환 타입
```

| 코드 | 뜻                                                                                     |
|------|----------------------------------------------------------------------------------------|
| S401 | 없는 이름 · 타입                                                                       |
| S402 | 같은 범위에 같은 이름 (변수 · 매개변수 · 필드 · enum 멤버 · 객체 키 · import)           |
| S403 | 선언하기 전에 쓴다                                                                     |
| S404 | 타입이 맞지 않다 (넣기 · 인자 · 반환 · 객체 리터럴 필드 · case · ? : · 배열 원소)       |
| S405 | 인자 수                                                                                |
| S406 | 그 타입에 없는 필드 · 메서드 · 멤버                                                    |
| S407 | null 일 수 있다 — 좁힌다                                                               |
| S408 | 조건은 boolean                                                                         |
| S409 | number → i32 (toI32), i32 범위 밖 · 정수 아닌 상수, i32 % 0                            |
| S410 | 대입할 수 없다 (const · 최상위 const · readonly · Host 속성)                           |
| S411 | 값이 아닌 것 (함수 · 타입 · enum · 내장을 값으로), 부를 수 없는 것                      |
| S412 | 반환 (void 가 값을 · 값이 없이 · 모든 길이 아님 · 추론 타입이 다름)                     |
| S413 | SandboxScript 에 없는 전역 · 타입 이름                                                 |
| S414 | 타입을 적어야 한다 (매개변수 · 빈 배열 · null 초기값 · 초기값 없는 let)                 |
| S415 | 최상위 const 는 기본 타입 상수식                                                       |
| S416 | import (경로 · 없는 파일 · 없는 export · 순환 · 같은 경로 둘)                           |
| S417 | 지원하지 않는 타입 모양 (T \| null 이 아닌 유니언 · 별칭 순환 · void 자리 · 타입 인자) |
| S418 | enum 값 (i32 정수 상수만)                                                              |
| S419 | break · continue 자리                                                                  |
| S420 | switch 떨어짐                                                                          |
| S421 | 재귀 함수의 반환 타입                                                                  |
| S422 | 연산자 · 피연산자 타입 (조건 밖의 && · ! 포함)                                         |
| S423 | export default 의 defineSystem 아닌 식 · defineSystem 밖의 객체 메서드                  |
| S424 | defineComponent 의 정의 (모양 · id · version · 필드 · 기본값 · 범위 · 플래그 — 15장)      |
| S425 | defineComponent · field 를 최상위 const 의 정의 밖에서                                    |
| S426 | defineSystem 의 정의 (모양 · id · domain · intervalTicks · 콜백 서명 · events · eventsPerTick · 모르는 키 · 하나 더 — 17 · 18장) |
| S427 | defineSystem 을 export default 밖에서 (const · 식 · 함수 안)                              |

## 12. 바이트코드 (v0.3 M1-T3 — `script/vm/Bytecode` · `Verifier`, `script/lang/Codegen`, ADR-0058)

```text
모듈        팩 하나 = 모듈 하나 (모든 스크립트 파일을 잇는다). 함수 번호 = 검사기의 함수 순서, export = (파일 경로, 이름) → 함수,
            시스템 표 (v0.3 M4-T1 — id 오름차순: id · 파일 · onTick 함수 번호 · intervalTicks, 17장)
기계        레지스터 기계. 함수마다 레지스터 표 (≤ 4096, 레지스터마다 타입) + 명령열 (8 바이트 고정 {op, 0, a, b, c}).
            매개변수 = r0 … rn-1, 호출 인자 = 연속 레지스터 c … c+n-1, 점프 대상 · 32 비트 즉시값 = b | c << 16
값          8 바이트 칸 하나 — i32 · f64 비트 · bool · 참조 · 불투명 핸들. 프레임 시작에 모두 0 (null · false · 0)
타입 표     Void · I32 · F64 · Bool · String · Opaque(abi) · Array(원소) · Box(안쪽) · Struct(필드 — 이름 순).
            검사기 타입 → 표: enum = I32, T | null = (T 가 참조면) T · (기본 · 불투명이면) Box(T), 구조체는 필드 이름 (바이트)
            순 배치 — 같은 필드 집합 + 같은 필드 타입 = 같은 배치 (그래서 5장의 구조체 필드는 양쪽으로 맞는다)
명령        이동 · 상수 (MOV LOADI LOADB LOADF LOADS LOADNULL), i32 (ADDI … SHRI · MINI MAXI · IDIV · NEGI NOTI — 감싼다,
            REMI · IDIV 는 0 이면 트랩), f64 (ADDF … POWF · NEGF · MATHF), 변환 (I2F · F2I = JS ToInt32 · F2IDX = 정수가
            아니면 트랩), 비교 (EQ · LT · LE × I F S, EQB · EQR 참조 · EQO 불투명 · NOTB — > · >= 는 자리를 바꾼다), 문자열
            (CONCAT · TOSTRI · TOSTRF · TOSTRB = JS Number#toString · STRLEN = UTF-16 단위), 배열 (NEWARR ARRLEN ARRGET ARRSET
            ARRPUSH — 늘리기는 push 만, 범위 밖은 트랩), 구조체 · 상자 (NEWOBJ GETF SETF BOX UNBOX), 흐름 (JMP JMPT JMPF CALL
            CALLH RET RETV TRAP), 컴포넌트 (LOADK a ← components[b] — v0.3 M3-T3, 값 = stableId). 원소 · 필드 · 상자 명령은
            타입마다 나누지 않는다 (검증기가 타입을 안다). ComponentRef<T> = Opaque(Component) — T 는 검사기만 안다 (ADR-0060 4)
코드 생성   지역 변수 · 매개변수는 레지스터 하나씩, 식의 임시는 문장이 끝나면 타입별로 돌려 쓴다. 넣는 자리의 변환 (i32 → f64 ·
            T → T | null 상자 · null 리터럴 = 그 자리 타입의 LOADNULL · 좁힌 이름의 UNBOX · Nullable(A) → Nullable(B))
            을 명령으로. 계산 순서는 소스 순서 (JS — a() > b() 도 a 먼저, 대입은 대상 → 값), 상수는 접힌 값을 그대로.
            가변 Math (min · max · hypot) 는 둘씩 잇는다 (hypot(a, b, c) = hypot(hypot(a, b), c), hypot(x) = abs(x)).
            함수 끝: void 면 RET, 아니면 TRAP 1 (검사기가 막는다). 만든 모듈은 바로 검증한다 (못 지나면 S502)
검증기      함수마다 한 번 훑기 (흐름 분석 없음): 번호 (레지스터 · 상수 · 함수 · import · 필드 · 점프) 가 범위 안, 명령마다 피연산자
            타입, 호출 인자 · 결과 = 서명, 마지막 명령은 RET · RETV · JMP · TRAP, pad = 0, 타입 표 (원소 · 상자 안쪽 · 필드
            이름 순 · void 아님), 문자열 UTF-8, 컴포넌트 표 (이름 규칙 · 겹침 · 엔진 이름공간 아님 — v0.3 M6-T2), import = Host 표의
            id · 서명, export 겹침, 머리 (형식 · Host 버전 · 지문 —
            다르면 VersionMismatch: 소스에서 다시 컴파일). 실행 중 확인 (null · 범위 · 0 · 연료 · 스택) 은 VM 의 트랩 (13장)
파일 .sbc   "SBXC" · 형식 5 (M3-T3 형식 2 = 컴포넌트 표 · LOADK, M4-T1 3 = 시스템 표, M4-T2 4 = 콜백 · 구독, M5-T3 5 = 시스템의
            도메인 · onUpdate — 옛 형식은 VersionMismatch) ·
            hostApiVersion · 지문, 타입 · 문자열 · 수 · import · 컴포넌트 (id — 이름 규칙 · 겹침 없음) · 함수 · export · 시스템 표 (리틀 엔디안, 개수마다
            상한과 남은 크기 검사 — 거대한 개수에 할당하지 않는다). 남는 바이트 · 잘림은 ParseError. 체크섬 없음 (ADR-0058 8)
결정성      같은 소스 = 같은 바이트 (상수 표 · 타입 표의 순서는 처음 쓴 순서, 계산 순서를 C++ 인자 순서에 맡기지 않는다)
```

## 13. 인터프리터 (v0.3 M2-T1 — `script/vm/Interpreter` · `Numbers`)

```text
입력        검증한 Module 만 (Debug 에서 다시 검증). 부르기 = call(함수 · 또는 (파일, export)) + 인자 (기본 · 불투명 값 — 참조는
            넘길 수 없다). 결과 CallResult {trap, value, function, pc, message, fuelUsed}. (v0.3 M4-T2) call(함수, 인자, 칸) — 구조체
            매개변수는 칸 목록으로 이 호출의 아레나에 만든다 (Host 결과와 같은 잎 규칙, 칸 수가 다르면 BadCall — onEvent 의 GameEvent)
아레나      호출마다 새로: call 처음에 앞 호출의 객체 (문자열 · 배열 · 구조체 · 상자) 를 통째로 버린다 (GC 없음). 결과의 문자열 ·
            배열은 다음 call 전까지 stringOf · arrayAt · fieldOf 로 읽는다. 상수 문자열은 모듈의 표를 그대로 가리킨다 (복사 없음)
참조 칸     0 = null, 1 … = 아레나 객체 (자리 + 1), 최상위 비트 = 상수 문자열. 불투명 칸은 Host 가 준 핸들 그대로 (VM 은 해석하지 않는다)
프레임      레지스터 칸을 한 줄로 (프레임마다 그 함수의 레지스터 수, 0 으로). 호출 = 인자 칸을 새 프레임의 r0 … 로 복사
예산        연료 (기본 1,000,000 — 명령마다 1, 문자열 잇기는 + 바이트/32, Host 함수는 + 표의 fuelBase), 호출 깊이 128, 레지스터 칸
            합 2^18, 아레나 4 MiB (객체 머리 32 + 칸 8 + 문자열 바이트 — 어림), 배열 길이 2^20, 문자열 1 MiB, Host 호출 65,536 번
            (v0.3 M4-T3 — 넘으면 HostError). 시스템 호출의 연료는 틱 예산이 줄인다 (19장)
트랩        Unreachable (TRAP) · NullReference · IndexOutOfRange (늘리기는 push 만) · NotAnIndex (xs[1.5]) · DivideByZero (i32 % 0 ·
            idiv(a, 0)) · OutOfFuel · StackOverflow · OutOfMemory · HostError (Host 실패 · Host 없음) · BadCall (바깥 호출의 번호 ·
            인자 수). 트랩은 그 호출만 멈춘다 — 같은 인터프리터로 다음 호출을 할 수 있다. 예외를 쓰지 않는다
수 규칙     i32 감싸기 (u32 로 계산), -2^31 / -1 = -2^31 · -2^31 % -1 = 0, 시프트는 & 31. f64 는 한 번에 한 연산
            (sbx_simulation_flags). REMF = fmod, POWF · Math.pow = jsPow (pow(1, NaN) · (-1) ** ±∞ = NaN), Math.round = 반은 +∞ 쪽 ·
            -0 지킴, min · max 는 NaN 우선 · -0 < +0, sign 은 ±0 · NaN 그대로. F2I = ToInt32, F2IDX 는 정수 · i32 범위만
문자열      TOSTRF = JS Number#toString (가장 짧은 왕복 자릿수 — to_chars, 지수는 1e+21 · 1e-7 부터), TOSTRI 10 진, STRLEN = UTF-16
            단위, 비교는 UTF-8 바이트 순
Host        CALLH → IHost::call(표의 항목, 인자) — 문자열 인자는 내용 (string_view) 으로, 첫 인자는 주인 (ctx). 실패하면 HostError.
            (v0.3 M3-T3) 구조체 T 는 칸 목록으로: 인자 ComponentData 는 펼치고 (HostArg::fields), 결과 ComponentDataOrNull ·
            EntityList 는 HostResult::values 로 만든다 — 레지스터 타입을 따라, 잎은 i32 · f64 · bool · 불투명 · 상자 (불투명) ·
            중첩 구조체 (깊이 4) 만, 칸 수가 다르거나 다른 모양이면 HostError (16장). HostResult::extraFuel 만큼 연료를 더 쓴다
결정성      같은 모듈 · 같은 인자 · 같은 Host 응답 = 같은 결과 · 같은 연료 (시험이 본다)
```

## 14. Host 바인딩 · SDK (v0.3 M2-T2 — `core/script/ScriptHost`, `script/sdk/SdkGenerator`, ADR-0057 2 · 4)

```text
바인딩      hostBindings() = 엔진 구현의 명시 목록 (id · 서명 — 주인 ctx 가 첫째 · 구현 함수). checkHostBindings 가 계약을 본다:
            표의 Server 도메인 항목마다 구현이 정확히 하나, 서명 = 표, 표에 없는 · 다른 도메인 항목의 구현 없음, 함수 포인터 있음.
            단위 시험 + ScriptHost 생성 (Debug). Client 도메인은 clientHostBindings · checkClientHostBindings (21장, ADR-0067)
ScriptHost  VM 의 IHost. beginCall(tick, 스크립트 이름) 이 ctx 핸들을 새로 낸다 (표지 0x5C78… + 호출 번호) → 그 값을 ctx 인자로
            넘기고, 끝나면 endCall. Host 함수는 첫 인자가 지금 열린 호출의 ctx 인지 본다 — 지난 호출의 ctx · 위조한 핸들 · 열린
            호출 없음 = HostError (ADR-0057 4)
지금 항목   ctx.tick = 열린 호출의 틱. ctx.log = 서버 로그 "script" 범주 "<스크립트> t<틱>: <글>" + recentLogs (256 줄 — 틱마다 256
            줄까지, 넘은 줄은 버리고 droppedLogs 로 센다 — v0.3 M4-T3), 한 줄
            1024 바이트 (넘으면 UTF-8 글자 경계에서 자르고 …). 로그는 세계 상태가 아니라 바로 나간다 (트랩이어도 남는다)
컴포넌트    (v0.3 M3-T3) beginCall(tick, 이름, 월드) · endCall(commit) — 컴포넌트 항목 · 개체 핸들 · 쓰기 버퍼는 16장.
            엔진의 시스템 단계 (16b) 에서 부르기는 17장 (v0.3 M4-T1)
SDK         sbx sdk [<폴더>] 가 sandbox.d.ts 를 쓴다 (기본 지금 폴더). declare module "sandbox" { i32 · toI32 · idiv · 불투명
            interface (__sandboxOpaque 표지 — 리터럴로 만들 수 없게) + 주인 타입의 멤버 (속성 = readonly, 메서드 = 표의 매개변수
            이름) }. 머리에 Host API 버전 · 지문. 저장소의 sdk/sandbox.d.ts 가 생성 결과와 같아야 한다 (시험) — Host 표를 바꾸면
            같은 커밋에서 sbx sdk sdk. 컴포넌트 정의 선언 (Vec2 · Field · field · defineComponent · ComponentRef) 은 M3-T1 — 15장
프로젝트    (v0.3 M5-T1, ADR-0065) sbx new 가 <프로젝트>/sdk/sandbox.d.ts · tsconfig.json (generateTsconfig — strict · noEmit,
            include = sdk/sandbox.d.ts · content/*/scripts/**/*.ts) 을 만든다. sbx check 가 낡은 · 없는 d.ts · JSON 아닌 tsconfig 를 P6
            경고로, 스크립트 요약 · 시스템 목록을 보인다. 패키지에는 넣지 않는다
            (v0.4 M4-T1, ADR-0075) 팩 사이 import 용 paths: sbx install · update · add · remove 가 스크립트가 있는 팩 (content/ ·
            .sandbox/packages) 마다 "@<팩>/*" → "<팩 폴더>/scripts/*" (baseUrl ".") 와 include ".sandbox/packages/*/scripts/**/*.ts"
            를 넣는다 (project::refreshTsconfig). 만든 그대로의 파일만 고친다 — compilerOptions · include 를 사용자가 고쳤으면 둔다.
            팩이 없으면 v0.3 의 바이트 그대로. sandbox.d.ts 는 그대로 (다른 팩의 타입은 그 팩의 소스가 준다)
```

## 15. 스크립트 컴포넌트 (v0.3 M3-T1 — `script/api/ComponentSchema`, 검사기, `core/script/ScriptComponents`, ADR-0059)

```ts
import { defineComponent, field } from "sandbox";
export const enum ManaState { Idle, Regen, Empty }
export const Mana = defineComponent({
  id: "rpg.mana",                                   // 이름공간.이름 — 엔진 이름공간 (core · ai · life · game · net · persist ·
  version: 1,                                       //   debug · render · editor · client · server · script · sandbox · sbx) 금지
  fields: {
    current: field.f32(100, { min: 0, max: 1000 }), // 기본값?, { min, max }? — i32 · f32 · f64
    maximum: field.f32(100),
    resting: field.bool(false),                     // bool (기본값?)
    anchor: field.vec2(0, 0),                       // vec2 (x?, y?) — f32 둘
    caster: field.entity(),                         // 개체 참조 — saveId 로 저장, 기본 null
    state: field.enumOf(ManaState.Idle),            // enum — 인자 = 기본 멤버이자 필드 타입
  },
  // persistent · replicated · editorVisible (기본 true), serverOnly (기본 false — 켜면 replicated 기본 false)
});
```

```text
읽기       검사기가 실행하지 않고 읽는다: 최상위 const 의 초기값이 defineComponent(객체 리터럴 하나) 일 때만. 모든 값은 상수식
           (리터럴 · 앞의 const · enum 멤버 · 연산). 결과 = Program::components (스키마 · 심볼 · 값 구조체 타입) — 오류가 있는
           정의는 넣지 않는다 (그 const 는 Error 타입 — 이어지는 오류 없음)
스키마     api::ComponentSchema: id · version (1 ~ 65535, 기본 1) · 플래그 · 필드 (≤ 64, 이름 = 식별자, **이름 순으로 정렬** —
           선언 순서를 바꿔도 같은 컴포넌트) · 정의 자리. 기본값은 범위 안 · 유한, i32 기본값과 범위는 정수, f32 · vec2 는 f32 범위
           안이고 f32 로 한 번 반올림 (roundToF32 — 엔진도 같다). 한 프로그램 안에서 id 하나
값 타입    i32 → i32, f32 · f64 → number, bool → boolean, vec2 → Vec2 { x, y: number }, entity → Entity | null, enumOf → 그 enum.
           값 구조체 (ComponentDecl::dataType) 는 ctx.read 의 결과 타입이다 (16장). 컴포넌트 const 의 타입 = ComponentRef<이름>
           (지역 const 에 담고 넘길 수 있다, 같은 컴포넌트끼리 === — 최상위 const 에는 담을 수 없다 S415)
금지       함수 안 · 식 안의 defineComponent · field (S425), const 에 타입 적기 · 모르는 키 · 축약 { id } (S424)
엔진       core/script registerScriptComponents(카탈로그, 스키마들): 엔진 이름공간 · 이미 있는 이름 (내장 · 앞 팩) 은 거절하고
           (ScriptComponentProblem — "파일:줄: 컴포넌트 'id' — 이유"), 나머지는 ecs 동적 컴포넌트로 넣는다 (02 9.6 — 형식은 같은
           필드를 이름 순으로 가진 C++ 컴포넌트와 바이트 단위로 같다). Persistent → Hashed (내장과 같다)
콘텐츠     (M3-T2) 콘텐츠 팩의 scripts/ 를 로드 0 단계에서 팩마다 컴파일 → 콘텐츠의 카탈로그 (11-CONTENT-SCHEMA 11장). 진단은
           콘텐츠 문제 (sbx check · 서버 · 클라이언트). Prefab · Rule · 명령 · Inspector · 세이브 · WorldHash · 복제가 스크립트
           컴포넌트를 내장처럼 다룬다
d.ts       sandbox.d.ts 의 defineComponent<F> 가 ComponentRef<{ 필드: 값 타입 }> 를 돌려준다 (tsc 쪽 타입). enumOf<E> 는 제약 없는
           타입 매개변수라 tsc 가 enum 리터럴을 enum 타입으로 넓힌다
세이브     (M3-T4, ADR-0061) version 을 올리면 세이브를 자동으로 옮긴다 — 뺀 필드는 버리고 (경고), 더한 필드는 기본값, i32 가 된
           필드의 정수인 실수는 정수로, 그 밖의 종류 · 범위 불일치는 로드 실패. 같은 version 에서 필드를 빼면 실패, 더하기만이면
           기본값 + 경고 (09 6장). 더 새 version 의 세이브 = VersionMismatch
```

## 16. 컴포넌트 읽기 · 질의 · 쓰기 (v0.3 M3-T3 — Host 표 · `script/vm/Interpreter` · `core/script/ScriptHost`, ADR-0060)

```ts
import { ServerContext, i32 } from "sandbox";
import { Mana } from "./mana";
export function regen(ctx: ServerContext): i32 {
  let n: i32 = 0;
  for (const e of ctx.query(Mana)) {          // Entity[] — saveId 오름차순
    const m = ctx.read(e, Mana);              // Mana | null — 값 복사
    if (m !== null) {
      m.current = Math.min(m.current + m.regen, m.max);
      ctx.write(e, Mana, m);                  // 값 통째 — 호출이 끝나면 적용
      n = n + 1;
    }
  }
  return n;
}
```

```text
타입       read(e, ref) → T | null · has(e, ref) → boolean · query(ref) → Entity[] · write(e, ref, T). ref 는 ComponentRef<T>
           (아니면 S404), T 는 구조로 맞춘다 (같은 필드의 interface 도 된다). 인자 수 S405
값         읽기 = 복사 (고쳐도 월드가 바뀌지 않는다 — write 로). f32 는 f64 로 넓혀 읽고, 쓸 때 한 번 반올림. vec2 = Vec2 구조체,
           entity 필드 = Entity | null (저장은 saveId — 없는 · 죽은 개체면 null)
쓰기 검사  범위 (min · max — f32 는 반올림한 값으로) · 유한 (NaN · ±Infinity 는 HostError — 세이브 JSON 에 없다) · f32 범위 · 개체
           필드는 이 호출의 핸들 (→ saveId) 이나 null. 개체에 그 컴포넌트가 없으면 HostError (붙이기 · 떼기는 [계획] — 10장).
           enum 필드의 값이 멤버인지는 보지 않는다 (i32 — 내장 enum 과 같다)
버퍼       (개체, 컴포넌트) 마다 마지막 값 하나 (처음 쓴 차례, 호출당 65,536). 같은 호출의 read 는 버퍼를 먼저 본다. endCall(true)
           가 차례대로 풀에 쓰고, 트랩 (endCall(false)) 이면 통째로 버린다 — 반쯤 적용된 상태가 없다
질의       saveId 오름차순 (풀 순서와 무관 — 세이브 → 로드 뒤 같다), 65,536 개를 넘으면 HostError, 연료 16 + 개체마다 1.
           여러 컴포넌트를 한 번에 묻는 질의는 [검토 M6]
핸들       호출마다 새 표 — 표지 0x5E00 · 호출 번호 아래 16 비트 · 자리. 같은 개체 = 같은 핸들 (=== 가 된다), 지난 호출 · 위조 ·
           죽은 개체 = HostError. ScriptHost::entityHandle 이 바깥 (시스템 단계 — M4) 의 개체를 인자로 넘긴다
컴포넌트   LOADK 의 값 = stableId. Host 는 월드 카탈로그에서 찾고 스크립트 컴포넌트만 받는다 (모르는 컴포넌트 · 엔진 컴포넌트 =
           HostError). 월드 없이 연 호출 (beginCall 에 월드 없음) 의 컴포넌트 항목도 HostError
VM 경계    T 는 칸 목록 (필드 이름 순, Vec2 는 그 자리에서 x · y, 상자는 null = 0). 칸 수 · 잎 모양이 레지스터 타입과 다르면 HostError
           — 위조한 바이트코드가 Host 값을 참조로 읽지 못한다 (13장)
결정성     같은 월드 · 같은 호출 = 같은 결과 · 같은 WorldHash (시험), 쓰기 뒤 세이브 → 로드 해시 일치
```

## 17. 스크립트 시스템 (v0.3 M4-T1 — 검사기 · 시스템 표 · `core/script/ScriptSystemAdapter`, ADR-0062)

```ts
import { defineSystem } from "sandbox";
import { regen } from "./regen";               // 공용 코드는 최상위 function (16장의 예)
export default defineSystem({
  id: "rpg.mana.regen",                         // 이름공간.이름 (컴포넌트 id 와 같은 규칙) — 팩들 사이에 하나
  domain: "server",                             // 선택 — 기본 "server" ("client" 는 21장)
  intervalTicks: 3,                             // 선택 — 1 ~ 65535, 기본 1
  onTick(ctx) {                                 // ctx 는 적지 않아도 ServerContext, 반환은 void
    regen(ctx);
  },
});
```

```text
정의       모듈 (파일) 하나에 export default defineSystem({ … }) 하나. 값은 상수식 (검사기가 실행하지 않고 읽는다 — 15장과 같다).
           onTick 은 메서드로만 (onTick: f 는 S426) — 메서드 하나 = 함수 하나 (Program::functions, 이름 onTick), 매개변수는 ctx 하나나
           없음, 타입을 적으면 ServerContext, 반환 타입을 적으면 void. onStart · onStop · onEvent · events 는 18장 (onTick 도 선택 —
           콜백이 하나는 있어야 한다), stage 키는 없다. 결과 Program::systems (id · 파일 · 줄 · intervalTicks · 콜백 · 구독) — 오류
           있는 정의는 없다
진단       S426 정의 (모양 · id 규칙 · 엔진 이름공간 · 겹친 id · domain · intervalTicks · onTick 서명 · 모르는 키 · export default 둘),
           S427 자리 (const · 식 · 함수 안의 defineSystem), S423 export default 의 다른 식 · 밖의 메서드
바이트코드 모듈의 시스템 표 (.sbc 형식 3 ~ 5 — 12장): id 오름차순. 검증기가 id 규칙 · 순서 · 겹침 · 함수 번호 · onTick 서명
           ((ServerContext?) → void) · 주기 범위를 본다
콘텐츠     로드 0 단계가 팩마다 만든 모듈을 ContentDatabase::scriptPrograms 에 둔다 (11-CONTENT-SCHEMA 11장). 팩 사이의 같은 시스템
           id 는 V1
단계       시뮬레이션 단계 16b (03 2장) — Native System (5 ~ 16) 뒤, 17 Structural② 앞. System 이 도는 틱에만 (편집 단계 없음).
           순서 = (팩 로드 순서, 시스템 id 바이트 순), 틱 번호 % intervalTicks == 0 인 틱에. 시스템이 없는 콘텐츠는 단계가 없다
호출       시스템마다 ScriptHost::beginCall(틱, 시스템 id, 월드) → onTick(ctx) → endCall(트랩 아님). 쓰기는 호출이 끝나면 바로 적용 —
           뒤 시스템 · 17b · 17c 가 같은 틱에 본다. ctx.log 는 "<시스템 id> t<틱>: …"
트랩       그 호출의 쓰기만 버리고 다음 시스템으로 (그 시스템은 다음 주기에 다시 돈다). 로그는 시스템마다 처음 한 줄
           ("<id> t<틱>: 트랩 <종류> — <글> (<팩>/<함수> pc n)"), 그 뒤는 세기만 (ScriptSystemAdapter::trapCount · lastStats). 트랩이
           3 번 이어지면 시스템을 끈다 · 틱 연료 예산은 19장
d.ts       sandbox.d.ts 의 SystemDefinition · defineSystem (tsc 쪽 타입 — onTick(ctx: ServerContext): void, domain?: "server" |
           "client", onUpdate?(ctx: ClientContext): void)
결정성     시스템 상태가 없다 (주기는 틱 번호로만) — 세이브 → 로드 · 늦은 합류 뒤에도 같은 틱에 같은 시스템이 돈다. 같은 콘텐츠 =
           같은 해시 (시험 · sbx_sim_check D1 · D2 · D5)
```

## 18. 시스템 콜백 · 이벤트 (v0.3 M4-T2 — 검사기 · `script/api/SystemEvents` · `core/simulation/SimulationScripts`, ADR-0063)

```ts
import { defineSystem, GameEventKind } from "sandbox";
import { Mana } from "./mana";
export default defineSystem({
  id: "rpg.mana.events",
  eventsPerTick: 64,                                   // 선택 — 한 틱에 받는 onEvent (1 ~ 4096, 기본 256)
  events: {                                            // 구독 — 종류마다 하나, 값은 상수
    death: { tags: { all: ["prey"] } },                // tags = { all?, any?, none? } — 콘텐츠 태그 이름
    spawn: {},
    areaEnter: { min: [-4, -4], max: [4, 4], tags: { none: ["plant"] } },
    score: {},
  },
  onStart(ctx) { ctx.log("game start"); },             // 게임이 Running 이 된 틱 (세션이 없으면 틱 1)
  onEvent(ctx, e) {                                    // e: GameEvent
    const who = e.entity;                              // spawn · areaEnter 만 (death · score 는 null)
    if (e.kind === GameEventKind.Spawn && who !== null) {
      const m = ctx.read(who, Mana);
      if (m !== null) { m.current = m.max; ctx.write(who, Mana, m); }
    }
  },
  onStop(ctx) { ctx.log("game over"); },               // 게임이 Finished 가 된 틱 (세션이 없으면 없다)
});
```

```text
단계       17d — 17c Triggers 다음 (그 틱의 명령 · System · ECB · Prefab 생성 · 게임 규칙 · 트리거가 끝난 뒤), System 이 도는
           틱에만. ① onStart (시스템 순서) ② onEvent (시스템 순서 → 구독 종류 death · spawn · areaEnter · score → 일어난 차례)
           ③ onStop (시스템 순서). 콜백이 없는 콘텐츠는 단계가 없다 (03 2장)
시작 · 끝  onStart = 세션이 있으면 그 틱에 Running 이 됐다 (stateTick == 틱), 없으면 틱 1. onStop = 세션이 그 틱에 Finished 가
           됐다 (끝 조건 · game.end · abort). 저장된 값으로 알아내 세이브 → 로드한 월드가 onStart 를 다시 부르지 않는다. 일시정지
           중 abort · 스크립트를 나중에 더한 월드는 부르지 않는다
이벤트     그 틱 안에서만 (다음 틱으로 넘기지 않는다 — 스크립트 상태가 생기지 않는다). 앞 상태 = 트리거와 같은 단계 0 스냅숏
             death      Died (발생 순) — entity null, x · y 틱 처음의 위치 (그 틱에 생겨 죽었으면 0 · 태그 없음), value = 까닭
             spawn      EntitySpawned 중 17d 에 살아 있는 것 (생성 순) — entity · x · y 지금
             areaEnter  틱 처음에 사각형 밖 (또는 없었다), 지금 안 — saveId 순 (트리거 area_enter 와 같은 규칙)
             score      점수 칸마다 틱 처음 ≠ 지금 — team = 칸 (0 공용 · 팀 + 1), value = 지금, previous = 틱 처음 (세션이 있을 때)
           tags 거르기는 death 는 틱 처음의 태그, spawn · areaEnter 는 지금 태그 (Rule · 트리거의 TagExpr 와 같다)
GameEvent  { entity: Entity | null, kind: GameEventKind, previous · team · value: i32, x · y: number } (읽기 전용, 칸 = 이름 순).
           GameEventKind { Death = 1, Spawn, AreaEnter, Score } 는 SDK enum. onEvent 의 매개변수는 (ctx?, e?) — 적으면
           ServerContext · GameEvent
구독       events 의 키 = death · spawn · areaEnter (min · max: [x, y] 필수, f32 로 반올림) · score (키 없음). tags 의 이름은
           콘텐츠 로드가 태그 비트로 푼다 (없는 태그 V7). events 와 onEvent 는 함께 (S426). 바이트코드는 이름을 싣는다 (.sbc 형식 4)
예산       시스템마다 eventsPerTick (기본 256), 틱 전체 4096 — 넘은 이벤트는 건너뛰고 센다 (ScriptSystemAdapter::lastStats().cut).
           연료는 호출마다 (VM 기본). 이어짐 없음 — 콜백은 컴포넌트 값만 써서 새 이벤트를 내지 않는다 (생성 · 파괴 Host 항목이
           생기면 그 이벤트를 같은 틱 17d 목록 끝에 [계획])
트랩       그 호출의 쓰기만 버리고 다음 호출로 (17장과 같다 — 로그는 시스템마다 처음 한 줄, 콜백 이름을 붙인다)
d.ts       GameEventKind (const enum) · GameEvent · TagFilter · SystemEvents, SystemDefinition 의 onTick? · onStart? · onStop? ·
           onEvent? · events? · eventsPerTick?
결정성     시뮬레이션 상태가 없다 — 같은 콘텐츠 = 같은 해시, 세이브 → 로드 뒤 이어 돌려도 같은 끝 해시 (시험)
```

## 19. 예산 · 트랩 정책 · 통계 (v0.3 M4-T3 — `core/script/ScriptSystemAdapter`, ADR-0064)

```text
호출 예산  연료 1,000,000 (kCallFuel) · 아레나 4 MiB · 깊이 128 · 칸 2^18 · 배열 2^20 · 문자열 1 MiB · Host 호출 65,536 (13장),
           질의 하나 65,536 개체 · 쓰기 65,536 (16장). 넘으면 트랩 — 그 호출의 쓰기를 버린다
틱 예산    16b + 17d 의 모든 스크립트 호출이 연료 4,000,000 (kTickFuel) 을 나눠 쓴다. 호출마다 min(1,000,000, 남은 연료) 로 돌고,
           남은 연료로 멈춘 호출은 "예산" (쓰기 버림 · 트랩 정책 밖), 남은 연료가 0 이면 그 틱의 남은 호출은 "건너뜀".
           onEvent 는 시스템마다 eventsPerTick · 틱 4,096 (18장). ctx.log 는 틱 256 줄 (넘으면 버리고 센다). 연료는 결정적이라
           끊기는 자리도 결정적이다 (벽시계를 보지 않는다 — 04)
트랩 정책  한 시스템의 호출 (콜백 무엇이든) 이 트랩으로 3 번 이어지면 (kDisableAfterTraps) 그 시스템을 끈다 — 그 틱의 남은 호출부터
           부르지 않는다. 트랩 없이 끝난 호출이 이어짐을 0 으로, 예산 · 건너뜀은 세지 않는다. 끌 때 경고 한 줄 (마지막 트랩)
상태       이어진 트랩 · 꺼짐은 시뮬레이션 상태 — 세이브 world.json "scriptSystems" (09 2장): 로드가 같은 id · 팩 바이트코드
           digest 면 되살리고, 스크립트를 고쳤으면 (digest 다름) 버리고 다시 켠다 (경고). 손으로 켜고 끄는 명령은 [계획]
통계       시스템마다 누적 (진단 — 세이브 · 해시에 없음): 호출 · 트랩 · 예산 · 건너뜀 · 연료 합 · 호출 하나의 최대 연료 · 마지막
           트랩 글 · 꺼짐 (ScriptSystemAdapter::counters · disabled). 틱 통계 lastStats (호출 · 트랩 · 예산 · 건너뜀 · 연료 · 버린
           로그). SandboxServer · sbx_sim_check 가 끝날 때 시스템마다 한 줄:
             스크립트 rpg.mana.regen: 호출 10 · 트랩 0 · 예산 0 · 건너뜀 0 · 연료 합 12345 · 최대 2000 (꺼졌으면 " · 꺼짐")
           시간 (진단 시계) · 패널 표시는 22장 (v0.3 M5-T4)
```

## 20. 스크립트 오버레이 · 에디터 (v0.3 M5-T2 — `core/content/ContentOverlay` · `ContentLoader` · `editor/ui/ContentPanel`, ADR-0066)

```text
항목       오버레이 종류 Script (4) — 팩의 scripts/ 파일 하나. id = "<팩>.<경로 마디를 '.' 로, .ts 없이>" ("eco.ai.wander" =
           eco 팩 scripts/ai/wander.ts), 값 {"id", "source"} (16 KiB). 팩에 없는 경로면 새 파일, null = 팩 파일로
다시 만들기 오버레이 DB 를 만들 때 Script 항목이 있으면 그 소스를 팩에 넣어 팩을 다시 컴파일 — 진단은 팩 파일 자리, 하나라도 오류면
           그 편집을 거절. 컴포넌트 정의 (지문 · id 목록) 가 팩과 다르면 V11. 카탈로그는 팩 DB 의 것
월드       EditContent (명령 단계 2) 로 바이트코드가 바뀌면 어댑터를 다시 만든다 — 그 틱의 16b 부터. 이어진 트랩 · 꺼짐은 바뀌지 않은
           팩의 시스템만 옮긴다. 세이브 overlay/scripts/<id>.json, 복제 ContentOverlay (프로토콜 13), 리플레이 {"op": "content",
           "kind": "script"} — 클라이언트도 같은 로더로 같은 바이트코드
에디터     "콘텐츠" 창 종류 "스크립트": 소스 편집 · 검사 (오류마다 "  12 | <그 줄>") · 적용 · 팩 값으로 · 새 파일 (시스템 틀) ·
           이 파일의 시스템 요약 (10-EDITOR 8장)
```

## 21. 클라이언트 스크립트 (v0.3 M5-T3 — `core/script/ClientScriptHost` · `ClientScriptSystems`, `apps/client`, ADR-0067)

```ts
import { defineSystem } from "sandbox";
import { Mana } from "./mana";
export default defineSystem({
  id: "rpg.hud.mana",
  domain: "client",                             // 리터럴로 — ctx 의 타입을 정한다 (키 순서와 무관)
  onUpdate(ctx) {                               // ctx 는 적지 않아도 ClientContext. 클라이언트 프레임마다
    for (const e of ctx.query(Mana)) {          // 복제 월드 — netId 오름차순
      const m = ctx.read(e, Mana);
      if (m !== null) ctx.label(e, `마나 ${Math.floor(m.current)}`);
    }
    ctx.print(`틱 ${ctx.tick}`);
  },
});
```

```text
정의       client 시스템의 콜백은 onUpdate 하나 ((ClientContext?) → void). intervalTicks · events · eventsPerTick · onTick · onStart ·
           onStop · onEvent 는 S426 (서버의 것), server 시스템의 onUpdate 도 S426, domain 은 문자열 리터럴 (상수 이름은 S426).
           id 는 도메인과 무관하게 팩들 사이에 하나 (V1). .sbc 형식 5 의 시스템 표에 도메인 · onUpdate (12장) — 검증기가 같은 규칙
ctx        ClientContext — 서버 항목을 부를 수 없다 (ctx.write · ctx.log 없음 — 주인 타입이 다르다):
             tick (복제 월드의 서버 틱) · frame (1 부터) · dt (앞 프레임 뒤 실제 초 — 표시용)
             read · has · query — 복제 월드의 스크립트 컴포넌트 (복제되지 않는 것은 null · 빈 질의), 질의 = netId 오름차순 (개체마다
             연료 1), 개체 참조 필드는 null [계획]
             label(entity, text) — 이번 프레임 개체 위 글자 · print(text) — "스크립트" 칸 한 줄. 줄마다 256 바이트 (UTF-8 글자 경계
             에서 자르고 …), 프레임마다 글자 1024 · 줄 64 (넘으면 버리고 센다 — droppedDisplay)
서버       client 시스템을 돌리지 않는다 (ScriptSystemAdapter 가 건너뛴다). server 시스템이 없으면 어댑터도 단계도 없다 — client 시스템만
           있는 콘텐츠의 해시 · 세이브는 스크립트 없는 콘텐츠와 같다
클라이언트 NetworkSession 이 콘텐츠 (오버레이 포함 — 20장) 에 client 시스템이 있을 때 ClientScriptSystems 를 두고, update 마다
           스냅숏을 적용한 뒤 (팩 순서, id) 차례로 onUpdate. 바이트코드가 바뀌면 (오버레이) 새로. 예산: 호출 연료 1,000,000 · 프레임
           연료 2,000,000 (kFrameFuel — 넘으면 "예산" · "건너뜀"), 트랩이면 그 호출의 표시를 버리고 3 번 이어지면 그 시스템을 끈다 —
           이 접속의 메모리에만 (세이브 · 서버와 무관, 콘텐츠가 바뀌면 다시 켠다). 첫 트랩 · 끔 · 첫 예산은 로그 한 줄
화면       Application 이 글자를 개체의 그린 자리 (보간 위치 — 스프라이트가 없으면 복제 위치) 위에 (ImGui 배경 목록 — 패널 아래),
           줄을 "스크립트" 칸에 그린다 — 패널을 숨겨도 (게임 화면). 칸은 줄이 있을 때만. 시스템 통계는 "스크립트 프로파일"
           패널 (22장 — M5-T4 에 옮겼다)
헤드리스   SandboxClient 가 끝날 때 시스템마다 "클라이언트 스크립트 <id>: 호출 · 트랩 · 예산 · 건너뜀 · 연료 합 · 최대 (· 꺼짐)",
           마지막 프레임의 글자 · 줄 수와 줄
결정론     클라이언트 스크립트는 시뮬레이션 밖이다 — 표시는 그 화면에만 남고 서버로 아무것도 가지 않는다 (04 의 규칙이 닿지 않는다).
           frame · dt 는 클라이언트마다 다르다
[계획]     클라이언트 스크립트의 명령 보내기 (ctx.send) · 입력 · 소리 이벤트 · 엔진 컴포넌트 읽기 · 게임 UI (ctx.ui) — v0.5 M2-T6 · M3
           (20 2.5 · 3.8). 도형 · 스프라이트 그리기 · 개체 참조 풀기는 v0.6
```

## 22. 스크립트 프로파일 (v0.3 M5-T4 — `core/script/ScriptClock`, ServerStats, `apps/client/ui/ScriptPanel`, ADR-0068)

```text
시계       ScriptClock = u64 (*)() noexcept (단조 나노초). 엔진 (core) 은 시계를 모른다 — 부르는 쪽이 넣는다: SandboxServer · 로컬 서버
           (ServerHost 가 단계마다 그 월드에 — SimulationWorld::setScriptClock, 다시 만든 어댑터에도), 클라이언트 (NetworkSession →
           ClientScriptSystems::setClock). sbx_sim_check · 시험 · 리플레이는 넣지 않는다. 시계가 있으면 호출마다 앞뒤를 재어
           통계에만 더한다 (nanos · maxNanos, 틱 · 프레임 통계의 nanos) — 어떤 분기도 시간을 보지 않는다 (04): 결과 · WorldHash ·
           세이브 · 복제는 시계가 있든 없든 같다 (시험이 본다)
서버 표    ServerStats (1 초마다 — 08 5장, 프로토콜 14) 끝에 windowSeconds · 시스템마다 (실행 순서, 64 줄까지 · 넘으면 scriptsOmitted):
           id · 창의 호출 · 연료 · 시간 (앞 통계 뒤의 누적 차이 — 어댑터를 다시 만들어 누적이 줄면 0 부터) · 누적 트랩 · 예산 ·
           건너뜀 · 최대 연료 · 최대 시간 · 꺼짐. 모든 접속자에게
클라이언트 IWorldSession::scriptProfile = 서버 표 + client 시스템의 최근 1 초 (실제 시간 창 — kClientProfileWindow). 줄 =
           호출/초 · 연료/호출 · ms/초 · ms/호출 · 최대 ms · 최대 연료 · 트랩 · 예산 · 건너뜀 · 꺼짐 (scriptProfileRow)
패널       "스크립트 프로파일" (패널이 보일 때만, 서버 · 클라이언트 표 — 10 9장). 스크립트 시스템이 없으면 그리지 않는다
끝 줄      시계가 있으면 시스템마다 " · 시간 <합> ms · 최대 <호출 하나> ms" (SandboxServer · SandboxClient 헤드리스).
           sbx_net_probe 는 받은 서버 표를 줄마다
측정 예    (Windows MSVC Debug, 2026-10-27 — 스크래치 clientroot: 생태계 + Mana 스크립트) 서버 eco.mana.regen 호출 하나 약 1.3 ~
           1.5 ms · 연료 약 4,550 (토끼 약 75), 클라이언트 eco.hud 프레임 하나 약 2.2 ms (글자 75). Debug 라 Release 보다 몇 배 느리다
[계획]     함수별 프로파일 (VM 계측 · 샘플링), 기록 그래프, 패널에서 시스템 끄고 켜기 (sbx_bench 스크립트 단계는 M6-T2 — 24장)
```

## 23. 데모 samples/mana_regen (v0.3 M6-T1 — ADR-0069, [README](../samples/mana_regen/README.md))

```text
게임       생태계 + 토끼마다 마나 (게임 모드 없음). content/ecosystem/scripts/ mana.ts (eco.mana {current · max · regen}) ·
           regen.ts (server — 15 틱마다 채움, 10 초마다 로그) · newborn.ts (server — spawn 이벤트, 마나 50) · hud.ts (client — 토끼 위
           값 · "스크립트" 칸). 행동 eco.energize (R — 조건 target.eco.mana.current >= 30, 효과 마나 −30 · 기운 +30)
만든 길    스크립트 · Prefab 의 eco.mana 를 넣은 콘텐츠 루트로 sbx new --from ecosystem_small --content-root <루트> — 시작 월드의 토끼가
           처음부터 마나를 가진다
검증       CTest sample_mana_regen: 자동 플레이 (8 배속 · 기운 주기 20 번 · 1800 틱) 를 헤드리스 서버 · 로컬 플레이 · 패키지로 —
           같은 "게임 결과:" 줄, 서버 끝 줄 "스크립트 eco.mana.regen: 호출 120 · 트랩 0", 클라이언트 끝 줄의 eco.mana.hud.
           unit_core test_mana_regen_sample: 두 번 같다 · 기운 주기 13 번 받아들여짐 (측정 — 이 툴체인), 세이브 (틱 907) → 로드 →
           이어 돌린 해시 = 곧장, 로컬 세션 — 멈춘 뒤 복제 월드의 마나 바이트 = 서버, client 글자 수 = 마나 개체 수, Inspector 값 ·
           편집 (ChangeComponent regen 7.5 → 서버 값)
측정 예    (Windows MSVC Debug, 2026-10-27) 자동 플레이 끝: tick 1801 · hash 0x51ddbf1f7d1ab7e9, 토끼 75 → 124, eco.mana.regen 호출 120 ·
           연료 합 744,258 · 최대 7,753 · 시간 합 약 280 ms (호출 하나 최대 약 5.6 ms)
```

## 24. 보호 시험 · 벤치 (v0.3 M6-T2 — `tests/unit/script/test_script_protection`, `bench/ScriptBench`, ADR-0070)

```text
실행량     무한 루프 (OutOfFuel — 호출 연료 1,000,000) · 무한 재귀 (StackOverflow — 깊이 128) · 메모리 폭탄 (OutOfMemory — 문자열 ·
           아레나) 시스템이 쓰기를 먼저 하고 끝나지 않아도: 그 시스템만 3 번 트랩 → 꺼짐, 건강한 시스템은 매 틱, 끝 WorldHash = 그
           시스템이 없는 콘텐츠의 해시 (트랩난 호출의 쓰기는 하나도 남지 않는다). client 시스템도 그 시스템만 (표시 버림 · 끔).
           실행 파일: 생태계 + 마나 스크립트 + 무한 루프 · 메모리 폭탄의 SandboxServer --ticks 90 --exit → 두 시스템 "꺼짐",
           틱 90 해시 0xe7f8e361bd4eb697 = 나쁜 스크립트가 없는 콘텐츠 (2026-10-27 측정, MSVC Debug)
권한       세 겹 — 검사기 (도메인 · 주인 타입), 검증기 (컴포넌트 표의 엔진 이름공간 거절 — 12장), Host (엔진 컴포넌트 ref 는 읽기 ·
           쓰기 모두 HostError)
위조       무작위 개체 핸들 · ctx (스크립트 함수를 거쳐 2,000 번) 는 HostError · 월드 그대로, 서버 Host 의 ctx · 개체는 클라이언트
           Host 에서 거절 (표지가 다르다)
바이트코드 직렬화한 모듈의 결정적 변형 3,000 개 (바이트 바꾸기 · 잘림 · 끼움) — 읽기 · 검증이 거절하거나 (측정 2,918), 검증된 변형
           (82) 의 기본 타입 함수를 모두 거절하는 Host · 연료 20,000 으로 불러도 (328 번) 죽지 않는다. 이 퍼즈가 검증기의 범위 밖 읽기
           (CALLH 결과가 Entity[] · 없는 결과 레지스터) 를 찾아 고쳤다. 한 번은 다른 씨앗으로 300,000 개 (크래시 0)
벤치       sbx_bench script.regen — 10k 개체 매 틱 질의 · 읽기 · 쓰기: 스크립트 단계 약 9.9 ms/틱 · 개체당 약 1 µs · 연료 59
           (Release — 14 7.13). 게임 스크립트는 intervalTicks 로 주기를 둔다
```