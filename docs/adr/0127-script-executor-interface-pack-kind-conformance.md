# ADR-0127. 스크립트 실행 경계 IScriptExecutor 구현 — 엔진은 인터페이스만 쥐고, 팩마다 실행자 종류, 계약 비교 하네스

- 상태: **Accepted** · 날짜: 2026-11-11
- 관련: [ADR-0081](0081-script-execution-backend-boundary-interpreter-first.md) (경계 결정 — 이 ADR 이 구현), [ADR-0125](0125-script-function-profile-self-fuel-protocol-18.md)
  (프로파일), [ADR-0126](0126-script-diagnostic-help-golden-trap-path.md) (호출 경로), [20-CREATOR-ALPHA](../20-CREATOR-ALPHA.md) 6장,
  task-registry M6-T4 (조각 4 — "Interpreter 결과 그대로 (골든)")

## 맥락

ADR-0081 은 IScriptExecutor 계약을 정했지만 코드는 vm::Interpreter 를 바로 만들어 썼다 (서버 · 클라이언트 어댑터). JIT · AOT 를 v1.x 에
검토하려면 엔진이 실행자 종류를 모르고, 새 실행자가 계약을 지키는지 견줄 기준이 있어야 한다.

## 결정

```text
1. vm::IScriptExecutor (script/vm/Interpreter.hpp): kind · call (기본 인자 · 구조체 칸) · limits · setLimits · heapBytes · profile ·
   resetProfile. Interpreter 가 구현한다 (final). 호출 단위의 가상 호출 하나 — 실행 루프에는 없다
2. vm::ExecutorKind (script/vm/ExecutorKind.hpp — 콘텐츠가 인터프리터 헤더 없이) = Interpreter 하나. makeScriptExecutor(kind, module,
   host, limits) — script/vm/ScriptExecutor
3. content::ScriptPackProgram.executor — 팩마다 고른다 (지금 늘 Interpreter, 콘텐츠가 고를 길은 [계획]). ScriptSystemAdapter ·
   ClientScriptSystems 는 IScriptExecutor 만 쥔다 (collectFunctionProfile 도)
4. 계약 비교: describeCallDifference(기준, 후보) — 트랩 · 값 비트 · 연료 · (트랩이면) 자리 · 호출 경로 · 메시지가 같으면 빈 글. 새 실행자는
   server 함수마다 Interpreter 와 견줘 빈 글이어야 들어온다 (ADR-0081 의 계약 — client onUpdate 는 결과만). 결정론 골든 (WorldHash) 은
   어댑터가 인터페이스로 바뀐 뒤에도 그대로
```

## 근거

```text
- 호출 단위 경계: 시스템 콜백 하나가 수천 명령 — 가상 호출 하나는 측정 밖 (sbx_bench script.regen)
- 종류를 콘텐츠 쪽에 두기: ADR-0081 "모듈마다 실행자를 고른다" — 팩 = 모듈 (ADR-0058)
- 비교 함수를 라이브러리에: 시험 · 미래의 차등 실행 (같은 입력을 두 실행자로) 이 같은 기준을 쓴다
```

## 결과

- 얻는 것: 엔진의 실행자 무관성, 계약 비교 하네스 (test_vm — 성공 · 트랩 · 연료 끝 · 깊이 · 메모리 12 호출), M6-T4 끝.
- 포기하는 것: 콘텐츠가 실행자를 고르는 설정 (실행자가 하나뿐 — 고를 것이 생기면), 실행자별 메모리 상한 (Limits 하나).
- 위험: 없음 (동작 그대로 — 골든 해시 · 단위 시험).

## 대안

| 대안 | 기각 사유 |
|------|-----------|
| 템플릿 (컴파일 시점 실행자) | 팩마다 고를 수 없다 |
| 실행 루프 안의 가상 디스패치 (명령마다) | 비용 — 경계는 호출 단위 |

## 재검토 조건

JIT (client onUpdate 먼저) · AOT 검토 (v1.x — ADR-0081), 차등 실행 시험 (같은 리플레이를 두 실행자로).
