# ADR-0062. 스크립트 시스템 — export default defineSystem({ id, intervalTicks, onTick }), 단계 16b 하나, 순서 = (팩 순서, 시스템 id)

- 상태: **Accepted** · 날짜: 2026-10-26
- 관련: [ADR-0056](0056-sandboxscript-language-subset-own-compiler-no-gc.md) 4 · 5 · 7 (export default 는 defineSystem 하나 — "M4 에서
  확정"), [ADR-0057](0057-host-api-single-definition-cpp-table-sdk-generated.md) 5 · 7 (단계 · 순서의 조건), [ADR-0058](0058-bytecode-register-machine-typed-registers-verifier.md)
  (바이트코드), [ADR-0060](0060-script-component-host-read-query-write-buffer-entity-handles.md) (호출 단위 쓰기 버퍼), [ADR-0051](0051-trigger-execution-stage-17c-same-tick-cascade-budget.md)
  (단계 17c), [03-SIMULATION](../03-SIMULATION.md) 2장, [19-SANDBOXSCRIPT](../19-SANDBOXSCRIPT.md) 17장, [18-PLATFORM](../18-PLATFORM.md) 6.2 (v0.3 M4-T1)

## 맥락

스크립트는 컴포넌트를 정의하고 (M3) 읽고 쓸 수 있지만 (ADR-0060), 시뮬레이션이 스크립트를 부르는 자리가 없다. ADR-0057 7 이
조건만 정했다 — 고정 단계 하나, 순서 = (팩 순서, 시스템 id), 스크립트가 없는 월드는 아무것도 하지 않는다. 정할 것: 시스템을
적는 모양, 단계의 자리, 같은 단계 안의 순서, 몇 틱마다 부르나, 시스템 정보가 어디에 실리나, 트랩이 나면 (끄는 정책 전까지).

## 결정

```text
1. 모양 — 모듈 하나에 시스템 하나:
     export default defineSystem({
       id: "rpg.mana.regen",      // 필수 — 컴포넌트 id 와 같은 규칙 (이름공간.이름, 엔진 이름공간 금지), 팩들 사이에 하나
       domain: "server",          // 선택 — "server" 만 (client · onUpdate 는 v0.3 M5)
       intervalTicks: 3,          // 선택 — i32 상수 1 ~ 65535, 기본 1
       onTick(ctx) { … },         // 필수 (M4-T1) — 메서드로. ctx 는 적지 않아도 ServerContext, 반환은 void
     })
   모든 값은 상수 (검사기가 실행하지 않고 읽는다 — defineComponent 와 같다). onStart · onStop · onEvent 는 M4-T2, stage 키는
   없다 (단계는 하나). 메서드는 최상위 함수와 같은 규칙 (클로저 없음 — 공용 코드는 최상위 function 을 부른다).
   defineSystem 을 다른 자리 (const · 식 안 · 함수 안) 에서 쓰면 S427, 정의의 잘못은 S426, export default 의 다른 식은 S423
2. 단계 = 16b Scripts — 16 Collision 뒤, 17 Structural② 앞 (SimulationWorld 의 단계, System 스케줄러 밖):
     Native System (5 ~ 16) 이 이번 틱에 쓴 값을 보고, 같은 틱의 17b GameRules · 17c Triggers 가 스크립트가 쓴 값을 본다.
     앞으로의 구조 변경 (생성 · 파괴 — ECB) 은 17 에서 Native System 의 ECB 뒤에 적용된다 (ADR-0057 5).
     System 이 도는 틱에만 (일시정지 · 게임 세션 Lobby · Finished 의 편집 단계에서는 없다 — Native System 과 같다)
3. 순서 = (팩 로드 순서, 시스템 id 바이트 순). 같은 틱에 앞 시스템의 쓰기 (endCall(commit) — 바로 적용) 를 뒤 시스템이 본다
4. intervalTicks = n → 틱 번호 % n == 0 인 틱에 부른다. 상태가 없다 (세이브 · 로드 · 늦은 합류 뒤에도 같은 틱에 돈다 — 04 D2)
5. 호출 하나 = 시스템 하나의 onTick 한 번: ScriptHost::beginCall(틱, 시스템 id, 월드) → Interpreter::call(onTick, ctx) →
   endCall(트랩 아님). 트랩이면 그 호출의 쓰기를 버리고 (ADR-0060) 다음 시스템으로 — 그 시스템은 다음 주기에 다시 돈다.
   로그는 시스템마다 처음 트랩 한 줄 (그 뒤는 세기만 — lastScriptStats). 끄는 정책 · 예산은 M4-T3 [계획]
6. 바이트코드 = 팩 하나 = 모듈 하나 (ADR-0058) 에 시스템 표를 싣는다 (.sbc 형식 3): 시스템마다 (id, 모듈 경로, onTick 함수 번호,
   intervalTicks), id 오름차순. 검증기: id 규칙 · 오름차순 (겹침 없음), 함수 번호 범위, onTick 서명 = (ServerContext?) → void,
   intervalTicks 범위. 콘텐츠 로드가 팩마다 만든 모듈을 콘텐츠에 둔다 (ContentDatabase::scriptPrograms — 오버레이 DB 와 나눈다),
   팩 사이의 같은 시스템 id 는 V1
7. 스크립트가 없는 월드 (시스템 없음) 는 단계 16b 가 아무것도 하지 않는다 → 골든 · kSimVersion 그대로 (17b · 17c 와 같은 근거)
```

## 근거

```text
- export default: 파일 하나 = 시스템 하나라 이름이 필요 없고 (id 가 이름), TS 에서 자연스럽다 (초안 예시와 같다). 시스템 여럿은
  파일 여럿 — 공용 코드는 import
- 메서드만 (onTick: 함수 이름 은 받지 않는다): 문맥 타입 (ctx) 을 검사기가 바로 안다, 함수 값을 넘기는 길이 하나 줄어든다
- 16b: 17 앞이라 스크립트의 생성 · 파괴가 같은 틱에 반영될 자리가 있고, 17b · 17c 가 스크립트 결과 (점수 · 상태) 를 같은 틱에
  본다. 스케줄러의 System 으로 넣지 않은 것은 ScriptHost 가 월드 (saveId 색인 · 카탈로그) 를 보고, 스크립트가 없는 월드의
  스케줄러 목록 · 프로파일이 바뀌지 않게 하려는 것이다
- (팩 순서, id): 팩 순서는 이미 결정적 (의존 먼저 — 11 2장), id 는 사람이 보는 이름이라 파일 이름 · 정의 순서를 바꿔도 같다
- tick % n: 저장할 상태가 없다. 같은 n 의 시스템이 같은 틱에 몰리는 것은 M6 벤치에서 보고 필요하면 offset 을 더한다
- 트랩 뒤 계속: 한 시스템의 버그가 월드를 멈추지 않는다. 같은 입력이면 같은 틱 · 같은 자리에서 트랩이 나므로 결정적이다
- 시스템 표를 바이트코드에: 모듈이 "실행할 수 있는 팩" 하나로 닫힌다 (.sbc 만으로 어댑터가 돈다), 검증기가 서명을 본다
```

## 결과

- 얻는 것: 스크립트가 틱마다 컴포넌트를 바꾼다 (Mana 재생 같은 게임 규칙), 순서 · 주기가 데이터만으로 정해진다, 저장할 스크립트
  상태가 없다.
- 포기하는 것: 시스템마다 단계 고르기 (하나로 고정), Native System 사이에 끼우기, 같은 파일에 시스템 여럿, 함수 값으로 onTick.
- 위험: 무거운 스크립트가 틱 시간을 잡아먹는다 — 완화: 호출마다 연료 상한 (VM 기본 1,000,000 — 넘으면 트랩), 시스템 · 틱 예산은
  M4-T3. 같은 주기의 시스템이 한 틱에 몰린다 — M6 벤치에서 본다.

## 대안

| 대안                                              | 기각 사유                                                                   |
|---------------------------------------------------|-----------------------------------------------------------------------------|
| export const X = defineSystem(…) (파일에 여럿)    | 이름이 둘 (const 와 id), 컴포넌트 const 처럼 값으로 쓸 데가 없다             |
| 시스템마다 stage 키                              | 단계 · 순서 조합이 계약이 된다 (03 2장 — 순서 자체가 규약), v0.3 범위 밖     |
| 스크립트를 스케줄러의 ISystem 으로 (Stage 16 끝) | Host 가 월드를 봐야 하고, 스크립트 없는 월드의 스케줄러 목록이 바뀐다       |
| 17c 뒤 (Triggers 다음)                           | 스크립트의 생성 · 파괴가 다음 틱으로 밀리고, 끝 조건 · 트리거가 한 틱 늦게 본다 |
| intervalTicks 를 마지막 실행 틱으로 (상태)        | 세이브 · 복제에 스크립트 상태가 생긴다 (ADR-0056 4 — 상태는 컴포넌트에만)    |
| 트랩 = 시스템을 바로 끈다                         | 끄는 기준 (횟수 · 연속) 은 예산과 함께 M4-T3 에서 정한다                     |

## 재검토 조건

시스템마다 단계 · 앞뒤 관계가 데모 · 사용자에게서 필요하면 (before/after 키), 같은 주기 시스템의 몰림이 틱 예산을 넘으면 (offset),
client 도메인 (M5) 이 같은 모양을 요구하면.
