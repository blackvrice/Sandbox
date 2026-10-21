# ADR-0056. SandboxScript 언어 — TypeScript 문법의 엄격한 부분집합, 자체 C++ 컴파일러, 숫자 number(f64) · i32, 예외 · GC 없음, 상태는 컴포넌트에만

- 상태: **Accepted** · 날짜: 2026-10-21 (D-01 결정)
- 관련: [ADR-0039](0039-platform-scope-version-line-after-phase-15.md) 3 (스크립트 = TypeScript 부분집합 → 자체 VM),
  [ADR-0057](0057-host-api-single-definition-cpp-table-sdk-generated.md) (Host API — D-04), [04-DETERMINISM](../04-DETERMINISM.md) 4.6,
  [03-SIMULATION](../03-SIMULATION.md) 6.4, [18-PLATFORM](../18-PLATFORM.md) 6.1 · 6.2,
  초안 [architecture/04 SCRIPT_VM_AND_SDK](../platform/architecture/04_SCRIPT_VM_AND_SDK.md) 2 · 5 · 6장

## 맥락

v0.3 은 게임 개발자가 사용자 Component · System 을 스크립트로 만든다 (초안 roadmap/v0_3 요구 1 "언어 부분집합, 타입 · 숫자 · 할당 ·
예외 · 모듈 · 결정론 규범 동결"). ADR-0039 가 언어(TypeScript 문법 부분집합)와 실행(자체 바이트코드 VM — 게임 실행 파일에 JS 런타임
없음)을 정했고, 남은 것이 D-01 이다:

```text
1. 파서 · 타입 검사기를 직접 쓰나, TS 컴파일러(Node — 빌드 체인만)를 쓰나
2. 숫자: TS 의 number 는 f64 하나 — 엔진 컴포넌트는 f32 · i32 · u32 를 쓴다. 정수 overflow · NaN · 나눗셈
3. 메모리: GC 를 두나. 스크립트 전역 상태를 두나 (세이브 · 리플레이 · 해시가 그것을 모르면 결정론이 깨진다 — 04 H3)
4. 예외: throw / try 를 두나, 실행 중 오류(범위 밖 · 예산 초과)는 어떻게 되나
5. 모듈 · import 의 범위
```

조건: 04 가 최우선 (같은 바이너리 = 같은 결과, 툴체인별 골든 — ADR-0012), 서버 · `sbx build` · 에디터는 Node 없이 돈다 (지금
저장소에 Node 의존이 없다 — 15), 1 인 개발 규모 (ADR-0039 R1).

## 결정

```text
1. 소스 = 콘텐츠. 팩의 scripts/**/*.ts (UTF-8). 패키지(.sbpkg)에는 소스가 들어가고 로드할 때 컴파일한다 — 같은 컴파일러 = 같은
   바이트코드 (컴파일러 출력은 결정적: 입력 = 경로 정렬 순 소스 + Host API 표). 바이트코드 .sbc 는 형식이 있고 (버전 번호),
   읽을 때마다 검증기를 거친다 — 갓 컴파일한 것도 (방어 · 시험 경로가 하나)
2. 컴파일러는 이 저장소의 C++ (렉서 → 파서(AST) → 타입 검사 → 타입 있는 IR → 바이트코드). Node · tsc 를 쓰지 않는다.
   부분집합은 TS 의 **엄격한 부분집합**이다: 우리가 받는 파일은 SDK 의 sandbox.d.ts 와 함께 tsc --strict 도 받는다 →
   VS Code 등의 TS 언어 서비스가 자동 완성 · 타입 오류를 그대로 준다 (요구 8 의 IDE 쪽). 진단의 기준은 우리 컴파일러다
   (sbx check · 로드) — tsc 가 받고 우리가 거절하는 코드는 있어도, 거꾸로는 없게 한다 (시험으로 지킨다 [계획])
3. 숫자
   number   IEEE 754 f64 — JS 와 같은 뜻 (NaN · ±Infinity · -0, / 는 실수 나눗셈, % 는 fmod)
   i32      SDK 의 이름 있는 정수 타입 (d.ts 에서는 number 의 별칭, 우리 검사기는 구별한다). 정수 리터럴은 i32 로 넣을 수 있다.
            i32 ∘ i32 (+ - * % & | ^ << >> 단항 - ~) → i32 (>>> 는 없다 — JS 결과가 u32 라 i32 밖), 32 비트로 감싼다 (JS 의 (a ∘ b) | 0 과 같다 — * 는
            Math.imul 과 같다, f64 곱은 2^53 을 넘으면 정밀도를 잃으므로). i32 % 0 은 트랩 (JS 는 NaN — i32 에 NaN 이 없다).
            i32 / i32 → number (TS 와 같다 — 정수 나눗셈은 Math.trunc 나 idiv). i32 와 number 를 섞으면 number (정확한 넓힘).
            number → i32 는 명시적 toI32(x) 만 (JS ToInt32 — NaN · ±Infinity → 0, 2^32 로 감싸기)
   f32      스크립트 값 타입이 아니다 — 컴포넌트 필드 종류다. 읽으면 f64 로 넓히고, 쓰면 f32 로 반올림 (경계에서 한 번, 결정적)
   bool · string 그 밖의 기본 타입. string 은 불변 UTF-8, 비교는 바이트 순서 (보조 평면 문자에서 JS 의 UTF-16 순서와 다를 수 있다)
   Math.*   min · max · abs · floor · ceil · round · trunc · sign · sqrt · sin · cos · tan · atan2 · pow · exp · log · hypot 을
            컴파일러 내장으로 (같은 바이너리 = 같은 libm — 04 4.6). Math.random · Date · performance 는 없다 → ctx.random
4. 메모리 · 상태
   - GC 없음. 객체 · 배열 · 문자열은 **호출 하나(콜백 한 번)의 아레나**에 만들고 호출이 끝나면 통째로 버린다 (상한 = 메모리 예산)
   - 스크립트 전역 가변 상태 없음: 모듈 최상위는 선언만 (function · type · interface · const enum · 상수식 const ·
     defineComponent · defineSystem). let · 가변 객체는 함수 안에서만. 틱을 넘는 상태는 컴포넌트에만 둔다 →
     세이브 · 리플레이 · WorldHash · 복제가 스크립트 상태를 따로 알 필요가 없다 (04 H3 를 구조로 지킨다)
   - 객체 · 배열은 참조 타입 (TS 와 같은 별칭 동작), 길이 상한, 구멍 없음. interface / type 의 객체 리터럴 = 구조체
5. 예외 없음: throw · try · catch · finally 는 문법 오류. 실행 중 오류 (배열 범위 밖 · null 접근 · toI32 밖의 강제 · 예산 초과 ·
   스택 넘침) = 트랩 → 그 호출만 멈추고 그 호출이 낸 쓰기 · 명령을 버린다 (ADR-0057 의 호출 단위 버퍼). 트랩은 같은 입력에서
   같은 자리에서 나므로 결정적이다. 시스템을 끄는 정책 · 진단 이벤트는 v0.3 M4 에서
6. 제어 · 함수
   받음   if · else · while · do-while · for(;;) · for-of (배열 · 질의) · break · continue · switch (정수 · 문자열 · enum,
          fallthrough 는 빈 case 만) · 삼항 · && || ?? (단락) · 구조 분해 없음 [v0.3 뒤 검토] · 템플릿 문자열
   함수   최상위 function 선언과 defineSystem 의 메서드. 재귀는 스택 예산 안에서. 클로저 없음 — 함수 값은 최상위 함수 이름만
   null   T | null 만 (Host 반환 · 선택 필드), 좁히기는 if (x !== null) · ?. 없음 [검토]. undefined · 선택 매개변수 없음
   없음   class · this · prototype · new (Host 생성자 제외) · eval · Function · 동적 import · async · await · 제너레이터 ·
          any · unknown · 타입 단언 as (const 제외) · for-in · Map · Set [v0.4 뒤 검토] · label · with · var · delete · getter/setter
7. 모듈: 정적 import { … } from "./상대경로" (같은 팩의 scripts/ 안) 과 "sandbox" (SDK). 순환 import 는 오류 (초기화 순서가 없다 —
   최상위가 선언뿐이라 필요 없다). export default 는 defineSystem 하나 [M4 에서 확정]
8. 결정론 규칙 (04 에 더한다 — v0.3 M1-T1 의 규범 문서가 옮긴다): 반복 순서는 배열 순서와 질의 순서 (ADR-0057 — saveId 오름차순)
   뿐, 객체 키 순회 없음, 벽시계 · 난수 · 스레드 없음, 부동소수 연산은 VM 의 C++ 가 /fp:precise · -ffp-contract=off 로
   한 번에 한 연산 (04 4.6 의 sbx_simulation_flags 를 VM 에도)
9. 모듈: 최상위 script/ = 타깃 SandboxScript 하나 (lang 컴파일러 · vm · api Host 표 — 의존 Foundation 만, ECS 를 모른다).
   바인딩 · ScriptSystemAdapter 는 core/script (SandboxCore 가 SandboxScript 를 링크). 서버도 컴파일러를 링크한다 (로드 때 컴파일).
   ADR-0039 가 스케치한 세 타깃 (Runtime · CompilerTools · Bindings) 을 둘로 줄였다 — 컴파일러를 빼야 하는 실행 파일이 아직 없다.
   script/ 는 check_includes.py 의 그래픽 · OS 헤더 금지 목록에 든다 (v0.3 M1-T1 이 규칙을 더한다)
```

## 근거

```text
- 자체 컴파일러: 부분집합이 작다 (클래스 · 클로저 · 제너릭 추론 · 예외 · async 가 없다). Node 를 빌드 체인에 넣으면 sbx build ·
  에디터의 "적용" · 서버 로드가 외부 런타임 · 버전에 묶이고, tsc 의 AST 를 우리 IR 로 옮기는 층이 결국 의미 검사를 다시 한다.
  엄격한 부분집합이라 tsc 의 언어 서비스는 공짜로 쓴다
- number = f64: TS 의 뜻 그대로라 tsc 와 사람이 읽는 뜻이 같다. 정수가 필요한 곳(카운터 · 인덱스 · 비트)은 i32 로 —
  감싸기 규칙이 JS 의 | 0 과 같아 놀라움이 적다. f32 를 값 타입으로 두면 TS 에서 표현할 수 없다
- GC 없음 + 전역 상태 없음: 결정론 · 세이브 · 리플레이 · 해시를 VM 이 새로 책임지지 않는다. 아레나는 할당 순서가 결정적이고
  해제 비용이 0 이다. 대가는 "틱을 넘는 상태는 컴포넌트" 라는 규칙 — 초안의 ECS 원칙과 같다
- 예외 없음 + 트랩 = 호출 되돌리기: 반쯤 적용된 상태가 없다. 예산 초과도 같은 길이라 처리 경로가 하나다
```

## 결과

- 얻는 것: Node 없는 빌드 · 실행, IDE 지원, VM 밖 상태 없음, 하나의 오류 처리 경로.
- 포기하는 것: TS 의 큰 부분 (클래스 · 클로저 · 예외 · Map/Set · 제너릭 추론), 호출을 넘는 메모리 캐시.
- 위험: 컴파일러 규모 (초안 7장 위험 1) — 완화: M1 은 파서 · 검사기만 (실행 없음), 기능은 Mana 데모가 쓰는 것부터. tsc 와의 차이 —
  완화: "우리가 받으면 tsc --strict 도 받는다" 를 시험 자료로 묶는다 (Node 가 있는 CI 잡에서만 [계획]).

## 대안

| 대안                                   | 기각 사유                                                                                  |
|----------------------------------------|--------------------------------------------------------------------------------------------|
| TS 컴파일러 API (Node) 로 파싱 · 검사  | 빌드 · 서버 로드가 Node 에 묶인다. AST → IR 층이 의미 검사를 다시 한다                     |
| 숫자 i32 · f32 · f64 를 모두 값 타입으로 | f32 · 나눗셈 규칙이 TS 의 뜻과 갈린다 — tsc 와 사람이 다르게 읽는다                         |
| 참조 계수 · 추적 GC                     | 수거 시점 · 순서가 결정론 · 예산에 들어온다. 틱을 넘는 힙은 세이브 · 해시가 모른다         |
| 스크립트 전역 변수 (세이브에 직렬화)    | 세이브 · 복제 · 해시 · 마이그레이션이 둘이 된다 (컴포넌트 + VM 힙)                         |
| try / catch                            | 반쯤 적용된 쓰기를 되돌리는 의미가 필요하다 — 트랩 = 호출 되돌리기 하나로 충분하다         |

## 재검토 조건

Mana 데모 · v0.5 템플릿이 클로저 · Map 없이 읽기 어려운 코드를 3 건 이상 요구하면 (해당 기능을 결정론 조건과 함께 더한다),
호출 아레나 상한이 실제 스크립트에서 자주 트랩을 내면, tsc 와의 차이가 사용자 혼란으로 보고되면.
