# ADR-0137. 호출 토큰의 콜백 · Capability 실행 검사 — beginCall 이 콜백 이름으로 허락 집합을 적고 Host 호출마다 비교

- 상태: **Accepted** · 날짜: 2026-11-12
- 관련: [ADR-0086](0086-runtime-architecture-v2.md) 6 (콜백 표 · Capability — v0.5 M2-T7 계약), [22-RUNTIME-V2](../22-RUNTIME-V2.md) 3.2 (세 겹 검사),
  [24-SECURITY-GATES](../24-SECURITY-GATES.md) V3, [19-SANDBOXSCRIPT](../19-SANDBOXSCRIPT.md) 26장, task-registry RT-06-1

## 맥락

Capability 는 세 겹으로 지킨다 (22 3.2): 검사기 (콜백 이름 → 허락 집합), Host 표 (함수마다 capability 열 — v0.5 M2-T7 ✅), 실행 (호출
토큰). 검사기를 거치지 않은 · 손으로 고친 바이트코드는 앞의 둘을 건너뛸 수 있어 실행 검사가 마지막 겹이다. 계획 콜백 (onPrePhysics · onRender …)
이 생기기 전에 실행 경로가 콜백을 알아야 한다 — 그 콜백들은 지금의 것보다 좁다 (onPrePhysics 에 쓰기 · 구조 변경 없음, onRender 는 Draw 만).

## 결정

```text
1. ScriptHost::beginCall(tick, script, world, callback) · ClientScriptHost::beginCall(system, callback) — 호출 토큰 (ctx 핸들 — 기존) 에 콜백
   이름과 그 콜백의 허락 Capability (api::callbackCapabilities — kCallbacks 표, 도메인마다) 를 적는다. 표에 없는 이름 = 0 (아무것도 허락하지
   않는다 — 닫힌 쪽이 기본). 기본 인자 onTick · onUpdate (시험 · 옛 부르는 쪽)
2. Host 호출 (call) 마다: ctx 가 지금 토큰인가 (기존) → 함수의 capability 비트가 허락 집합 안인가 → 아니면 HostError "<id> — <콜백> 콜백에서는
   쓸 수 없다 (<Capability> 권한이 없다)" (구현에 닿기 전 — 부작용 없음, 트랩 정책 · 쓰기 버림은 기존 트랩 그대로)
3. 엔진이 넘기는 이름: ScriptSystemAdapter 의 onTick · onStart · onStop · onEvent · onMigrate (invoke 의 콜백 이름 그대로), ClientScriptSystems 의
   onUpdate. 계획 콜백은 생길 때 그 이름을 넘긴다 — 표의 집합이 그대로 실행 검사다
4. 지금의 콜백 (Active) 은 그 도메인의 모든 Host 함수를 허락한다 — 기존 게임 · 골든 해시 그대로 (시험이 Host 표 × Active 콜백 전수로 본다).
   kSimVersion · Host API 판 · 지문 그대로 (표 · 형식은 바뀌지 않았다)
5. 검사기의 콜백별 Capability (S404) 는 계획 콜백이 생길 때 (그 전에는 같은 도메인의 콜백이 모두 같은 집합 — 막을 것이 없다)
```

## 근거

```text
- 이름 → 표: 콜백 표 (Callbacks.hpp) 가 이미 계약 — 실행 검사가 같은 표를 읽으면 검사기 · d.ts · 실행이 어긋나지 않는다
- 비트 하나: Host 호출마다 AND 하나 — 측정 (Release sbx_bench script.regen, 이전 · 이후 번갈아 6 번): 최솟값 +0.1 % · 중앙값 +0.0 % (완료 기준 < 2 %)
- 닫힌 기본: 표에 없는 콜백 이름 (엔진 버그 · 새 경로가 이름을 빠뜨림) 이 모든 것을 허락하지 않게
```

## 결과

- 얻는 것: 계획 콜백의 단계 밖 호출 (onPrePhysics 에서 spawn · onRender 에서 setUi) 이 바이트코드를 고쳐도 HostError, 콜백 이름이 호출 토큰에 남는다
  (진단 · 디버거가 쓸 수 있다).
- 포기하는 것: 지금은 막는 호출이 없다 (Active 콜백이 모두 넓다) — 검사의 값은 계획 콜백이 생길 때. 검사기 쪽 S404 는 그때.
- 위험: 새 콜백 경로가 beginCall 에 이름을 넘기지 않으면 기본 (onTick · onUpdate) 이 된다 — 새 경로를 더할 때 이름을 넘기는 것을 시험이 본다
  (계획 콜백 태스크의 완료 기준).

## 대안

| 대안 | 기각 사유 |
|------|-----------|
| 콜백마다 다른 ctx 타입 (ABI) | .sbc · Host 표 · 검사기 전부를 바꾼다 — 22 3.2 가 Capability 조합으로 정했다 |
| 검사기만 믿는다 | 고친 · 직접 만든 바이트코드를 막지 못한다 (24 V3) |
| 열린 기본 (모르는 이름 = 다 허락) | 엔진 버그가 권한을 넓힌다 |

## 재검토 조건

계획 콜백 (RT-06-2 onPrePhysics · RT-06-7 client 수명주기 · onRender) 이 생길 때 — 그 콜백 이름을 넘기고 검사기 S404 를 더한다.
