# ADR-0080. 게임플레이 API — 엔진 컴포넌트 접근 표, 구조 변경은 호출 단위 버퍼 → ECB, 시스템마다 random 흐름, 행동 · 신호 이벤트, client 는 읽기 · 표시 · 명령만

- 상태: **Accepted** · 날짜: 2026-10-28
- 관련: [20-CREATOR-ALPHA](../20-CREATOR-ALPHA.md) 3장, [ADR-0057](0057-host-api-single-definition-cpp-table-sdk-generated.md) (Host 표),
  [ADR-0060](0060-script-component-host-read-query-write-buffer-entity-handles.md) (읽기 · 쓰기 버퍼), [ADR-0062](0062-script-systems-define-system-stage-16b-pack-id-order.md)
  (단계 · 순서), [ADR-0064](0064-script-budgets-tick-fuel-disable-after-traps-saved-state.md) (예산), [ADR-0045](0045-game-mode-content-session-in-simulation-player-actions.md) (행동), 04 (결정론)

## 맥락

v0.3 의 스크립트는 자기 팩의 스크립트 컴포넌트만 읽고 쓴다 (ctx.read · has · query · write). 엔진 컴포넌트 (위치 · 체력 · 점수),
개체 생성 · 파괴, 컴포넌트 붙이기 · 떼기, 난수, 입력은 v0.3 노트에서 v0.5 로 미뤘다. 오목 같은 규칙 게임은 돌을 놓을 (생성) 수 있어야
하고, HUD 는 내 개체의 체력을 볼 수 있어야 한다. 외부 제작자의 스크립트는 믿지 않는다 — 무단 상태 변경 · 결정론 깨짐 · DoS 를 막아야 한다.

## 결정

```text
1. 엔진 컴포넌트 접근 표 (C++ 표 하나 — Host 표와 같은 방식): 컴포넌트 · 필드마다 읽기 (server · client · both) · 쓰기 (none · server) ·
   쓰기 규칙 (범위 · 경계 자름 · 엔진이 따라 할 일). 표에 없는 컴포넌트 · 필드 = 검사 S404 · 실행 HostError. 검사기 · d.ts · 바인딩이 이
   표만 본다 (ComponentRef — import { Transform } from "sandbox"). v0.5 표 = 20 3.1
2. 구조 변경 ctx.spawn · destroy · add · remove 는 호출 단위 버퍼 (v0.3 쓰기 버퍼와 같다) → 호출이 끝나면 시스템 차례 (팩 순서 · 시스템
   id · 호출 순) 로 ECB 에 → 단계 16b · 17d 끝에 적용. 새 개체의 id · saveId 는 적용 차례로 정해 결정적. 트랩이면 그 호출의 버퍼를 버린다
3. 상한: 호출마다 spawn 64 · destroy 256 · add/remove 256, 틱마다 합 spawn 1,024. 넘으면 그 연산을 버리고 통계에 센다 (트랩 아님 —
   한 번의 실수로 시스템이 꺼지지 않게)
4. ctx.random · randomInt: 시스템마다 흐름 — seed = mix(월드 seed, 시스템 id 의 해시, tick), 04 9장의 생성기. 같은 월드 · 같은 틱 =
   같은 수열, 시스템 차례와 무관
5. ctx.near(pos, radius, Comp?) = 공간 색인 (05) — netId 오름차순 · 개체마다 연료 1 · 결과 상한
6. 이벤트: action (PlayerAction 이 받아들여진 뒤 — 행동 · 플레이어 · 위치 · 대상), signal (새 명령 PlayerSignal — 팩 signals.json 에
   선언한 이름 · 값 범위 · 속도, 아니면 서버가 명령을 거절). 프로토콜 15
7. 플레이어 · 소유: ctx.players · ctx.session (읽기), 새 엔진 컴포넌트 game.owner { player } (행동 · 신호 · spawn 이 적는다). 골든에
   닿는지 첫 단계에서 재고, 닿으면 kSimVersion 5 (이유를 커밋에)
8. client 도메인: 읽기 (스크립트 컴포넌트 + 접근 표의 client 읽기) · 표시 (label · print · ui) · 입력 (ctx.input) · 명령 보내기 (ctx.send →
   PlayerSignal) · 소리 이벤트. 쓰기 · 구조 변경 · random 은 없다 (주인 타입이 달라 검사 오류 — v0.3 의 방식 그대로)
9. Host 표 · 접근 표를 더하는 것은 지문만 바꾼다 (kHostApiVersion 1 그대로). 항목을 빼거나 쓰기를 거두면 버전 ++
```

## 근거

```text
- 접근 표: 엔진 컴포넌트마다 "누가 무엇을" 을 한 곳에서 — 검사 · 바인딩 · 문서 · 시험이 같은 표. 엔진 관리 필드 (AI 내부 · 식별자) 는
  아예 안 보인다
- 버퍼 → ECB: 시스템 실행 중 구조가 바뀌지 않는다 (질의 결과가 흔들리지 않음), 적용 차례가 고정이라 결정적, 트랩이면 흔적 없음
  (v0.3 보호 시험 규칙과 같다)
- 시스템마다 random 흐름: 시스템을 더하거나 빼도 다른 시스템의 수열이 바뀌지 않는다
- signal 선언: 클라이언트가 임의 이름 · 값을 보내지 못한다 — 서버가 스크립트까지 가기 전에 거른다
```

## 결과

- 얻는 것: 스크립트로 보드 · 수집 · 생성 규칙을 끝까지, HUD 바인딩의 "내 개체", 위조 입력을 서버가 거른다.
- 포기하는 것: 같은 호출 안에서 새 개체 읽기 (다음 틱부터), 임의 개체 참조 풀기 (v0.6), 물리 · 경로 요청 API (v0.6).
- 위험: 새 엔진 컴포넌트 (game.owner) 의 골든 영향, 상한 값의 적절성 (측정 뒤 조정 — 14), 프로토콜 15 로 0.4 와 접속 불가.

## 대안

| 대안                                        | 기각 사유                                                                   |
|---------------------------------------------|-----------------------------------------------------------------------------|
| 엔진 컴포넌트 전부 읽기 · 쓰기 허용          | AI 내부 · 식별자 · 세션 상태를 스크립트가 깨뜨린다 — 무단 상태 변경          |
| spawn 을 바로 적용 (호출 중)                 | 질의가 흔들리고 트랩 때 되돌리기가 어렵다                                    |
| 전역 random 하나                            | 시스템 하나를 더하면 다른 시스템의 결과가 바뀐다 (리플레이 · 디버깅이 어렵다) |
| client 가 직접 쓰기 (예측)                    | 서버 권위 · 결정론 (04) 와 맞지 않는다 — 예측은 v0.7 이후 검토               |
| 신호 없이 PlayerAction 만                   | 위치 없는 UI 버튼 (구매 · 다시 하기) 이 어색하다 — 둘 다 둔다                |

## 재검토 조건

예측 (client prediction) 이 필요해질 때 (v0.7 원격 플레이), 물리 엔진이 들어올 때, 상한이 실제 게임에서 모자랄 때.
