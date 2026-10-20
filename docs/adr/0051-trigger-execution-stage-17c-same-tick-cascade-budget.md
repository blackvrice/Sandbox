# ADR-0051. Trigger 실행 — 단계 17c, 틱 처음 스냅숏으로 이벤트, 같은 틱 이어짐, perTick · 틱 예산

- 상태: **Accepted** · 날짜: 2026-10-20
- 관련: [ADR-0050](0050-visual-trigger-schema-tree-graph-flat-program.md) (스키마 · 실행 표현), [ADR-0045](0045-game-mode-content-session-in-simulation-player-actions.md)
  (game.session · 17b), [ADR-0047](0047-player-action-content-command-point-targets.md), [ADR-0048](0048-game-score-team-tags-spawn-points.md),
  [03-SIMULATION](../03-SIMULATION.md) 2장, [04-DETERMINISM](../04-DETERMINISM.md), [11-CONTENT-SCHEMA](../11-CONTENT-SCHEMA.md) 10장

## 맥락

v0.2 M4-T2 "실행: Rule · Effect op 재사용, 결정적 순서 (Stage 고정), 폭주는 예산으로 끊고 월드는 무결". ADR-0050 이 실행 표현
(이벤트 + 조건 · 효과 표 + 명령열)을 정했다. 정할 것:

```text
1. 언제 (Stage) · 어떤 월드에서         2. 이벤트를 어떻게 알아내나 (영역 진입 · 점수 가장자리 · 죽음은 "앞" 상태가 필요)
3. 효과가 낸 이벤트 (재진입)            4. 예산과 끊기                      5. 효과 실행기 · 난수
```

## 결정

```text
1. 단계 17c Triggers — 17 (ECB · Prefab 생성) · 17b (GameRules) 다음. 그 틱의 생성 · 파괴 · 세션 전이를 본다. 세션이 Running 일
   때만 (Starting → Running 이 된 틱 포함, 17b 가 Finished 로 만든 틱은 없음). 세션 없는 월드는 아무것도 하지 않는다.
2. 앞 상태 = 단계 0 의 스냅숏 (그 틱의 명령보다 먼저): 점수 칸, (area_enter · death 트리거가 있으면) 개체마다 saveId · 위치 ·
   태그. 저장된 상태에서 다시 만들어지는 값만이라 세이브/로드한 월드도 같은 결과다 (메모리에만 남는 "지난 평가" 값을 두지 않는다).
     game_start  stateTick == now        tick  매 틱        timer  경과 e: once e == ticks · repeat e > 0 && e % ticks == 0
     score       스냅숏 값으로 거짓, 지금 값으로 참
     area_enter  스냅숏에서 영역 밖 (또는 없었다), 지금 안 — saveId 순. 명령으로 옮기거나 만든 개체도
     death       이번 틱의 Died 이벤트 (발생 순) — 대상은 이미 없다: 위치 · 태그는 스냅숏 (그 틱에 생겨 죽은 개체는 위치 없음)
3. 같은 틱 이어짐: 처음 목록 (트리거 정의 순서) 을 FIFO 로 실행하고, 효과가 낸 이벤트 (destroy → death — 지우기 직전 위치 · 태그,
   spawn → area_enter — 새 개체는 앞에 없었다, score.add → score — 바로 앞 값과 비교) 는 해당 트리거 실행을 목록 끝에 더한다.
4. 예산: 트리거마다 한 틱 perTick 번 (limits, 기본 1), 틱 전체 실행 1024 번 · 명령 줄 65536 줄. 넘은 실행은 건너뛰고 (cut —
   lastTriggerStats) 줄 예산이면 그 틱의 남은 실행을 모두 그만둔다. 효과는 하나씩 원자적이라 어디서 끊어도 월드는 무결하다.
   effect.game.end 로 세션이 끝나면 남은 실행은 하지 않는다 (Finished — rule 0, team = 모드의 팀 번호).
5. 효과 실행기 = SimulationWorld::applyEffectNow — PlayerAction 과 공용 (명령 단계와 같은 "바로" 의미: destroy 는 바로 구조 변경 +
   Died{Killed}, spawn 은 바로 만든다). 대상이 없거나 죽었으면 대상 효과 (field · destroy · tag) 는 아무것도 안 한다.
   spawn 자리 = at → 살아 있는 대상의 위치 → 죽은 대상의 위치. score.add 의 팀 = team → 살아 있는 대상의 팀 태그 → 공용.
   난수 Purpose::Trigger (9), 키 = (트리거 순서 << 40) ^ (이번 틱 실행 번호 << 12) ^ 명령 줄 — cond.chance · spawn 흩뿌리기.
```

kSimVersion 그대로: 트리거는 세션이 Running 인 월드에서만 돈다 (기존 골든 · 리플레이 해시 그대로). PlayerAction 을 공용 실행기로
옮겼지만 같은 동작 · 같은 난수 키다. 와이어 · 세이브 형식 그대로. eco 팩에 트리거 eco.rabbit_lost 를 더해 contentHash 가 바뀐다.

## 근거

```text
- 17c: 이번 틱의 죽음 · 생성 · 점수가 모두 끝난 자리. System 안 (5~16) 이면 ECB 가 적용되기 전이라 구조 변경을 다시 ECB 로
  미뤄야 하고 결과가 한 틱 늦게 보인다. 17b 다음이라 끝 조건이 먼저 이긴다 (이미 끝난 판에서 트리거가 돌지 않는다).
- 스냅숏: "지난번 평가 값" 을 메모리에 두면 세이브/로드한 월드가 첫 틱에 다른 가장자리를 본다. 단계 0 의 값은 저장된 상태
  그대로라 두 월드가 같다. 위치 스냅숏은 area_enter · death 트리거가 있을 때만 (비용).
- 같은 틱 이어짐: "덫에 걸리면 → 죽음 → 점수 → 점수 보너스" 가 한 틱에 끝나야 규칙이 읽히는 대로 동작한다. 다음 틱으로
  미루면 스냅숏 비교에서 가장자리를 잃는다. FIFO + 예산이라 재귀 · 무한 고리가 없다.
- 실행기 공용: Rule (ECB 경로) · PlayerAction · Trigger 의 효과 뜻이 같아야 콘텐츠를 옮겨 쓸 수 있다.
```

## 결과

- 얻는 것: 월드 수준 게임 규칙 (타이머 · 영역 · 점수 · 죽음) 이 서버 권위 · 결정적으로 돈다. 폭주는 예산이 끊는다.
- 포기하는 것: 세션 없는 샌드박스 월드의 트리거, 죽은 대상의 팀 (score.add 는 공용 — team 을 쓰면 된다), 같은 틱에 생겨 죽은
  개체의 death 위치 (spawn 은 at 이 필요), 영역 나감 · 머묾 이벤트 — [계획].
- 위험: area_enter · death 트리거가 있으면 매 틱 개체 전체 스냅숏 (50k 개체 ≈ 수 ms — 14-PERFORMANCE 측정은 [계획]).

## 대안

| 대안                                        | 기각 사유                                                                      |
|---------------------------------------------|--------------------------------------------------------------------------------|
| System (Stage 5~16) 으로                    | 구조 변경이 ECB 로 한 틱 늦고, 그 틱의 죽음 · 점수를 다 보지 못한다            |
| 효과가 낸 이벤트는 다음 틱에                | 스냅숏 비교에서 가장자리를 잃는다 (점수 · 영역)                                |
| 지난 평가 값을 메모리에                     | 세이브/로드한 월드가 첫 틱에 달라진다 (결정성)                                 |
| 지난 평가 값을 컴포넌트로 저장              | 트리거마다 상태 칸 · 콘텐츠가 바뀔 때의 대응이 필요 — 스냅숏으로 충분          |
| 세션 없는 월드에서도 실행                   | 시간 기준(Running 경과) 이 없고 기존 월드 결과가 바뀐다                        |

## 재검토 조건

샌드박스(세션 없는) 월드의 규칙이 필요할 때, 스냅숏 비용이 측정으로 문제일 때 (태그별 색인), 영역 나감 · 머묾 · 변수 노드가
필요할 때 (트리거 상태 컴포넌트).
