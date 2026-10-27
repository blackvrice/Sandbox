# 마나 재생 (mana_regen) — v0.3 스크립트 데모

SandboxScript 로 만든 컴포넌트 · 시스템을 쓰는 게임 프로젝트 하나 ([docs/19-SANDBOXSCRIPT.md](../../docs/19-SANDBOXSCRIPT.md) —
v0.3 M6-T1, ADR-0069). 생태계 (풀 · 토끼 · 늑대) 위에 토끼마다 마나를 둔다.

```text
만든 길    content/ecosystem 복사본에 scripts/*.ts · 토끼 Prefab 의 eco.mana · 행동 eco.energize · R 키를 더한 콘텐츠 루트로
           sbx new samples/mana_regen --id sandbox.mana_regen --name "마나 재생" --from ecosystem_small --content-root <그 루트>
           (시작 월드의 토끼가 처음부터 마나를 가진다) + playthrough.json · 이 README
스크립트   content/ecosystem/scripts/
             mana.ts     컴포넌트 eco.mana {current 0~100 (기본 20) · max · regen} — 세이브 · 복제 · Inspector · 해시가 내장과 같다
             regen.ts    서버 시스템 eco.mana.regen — 15 틱 (0.5 초) 마다 current += regen (max 까지), 10 초마다 로그
             newborn.ts  서버 시스템 eco.mana.newborn — 태어난 토끼 (spawn 이벤트, 태그 prey) 는 마나 50
             hud.ts      클라이언트 시스템 eco.mana.hud — 토끼 위에 마나 값, "스크립트" 칸에 토끼 수 · 마나가 가득한 수
행동       eco.energize "기운 주기" (R) — 커서 둘레 3 안의 토끼 3 마리까지, 마나 30 이상이면 마나 −30 · 기운 +30 (Rule 조건 ·
           효과가 스크립트 컴포넌트의 필드를 그대로 쓴다)
```

## 해 보기

```text
SandboxClient --world samples/mana_regen      싱글플레이 — 토끼 위 숫자 · "스크립트" 칸, R 로 기운 주기, F1 패널의 "스크립트
                                              프로파일" (서버 · 클라이언트 시스템의 호출 · 연료 · ms), 인스펙터의 eco.mana
SandboxServer --world samples/mana_regen      서버 (끝날 때 스크립트 시스템마다 호출 · 연료 · 시간 한 줄)
sbx check samples/mana_regen                  검사 — 스크립트 요약 (컴포넌트 1 · 시스템 3)
code samples/mana_regen                       sdk/sandbox.d.ts · tsconfig.json — IDE 자동 완성 · tsc --noEmit 검사
```

## 자동 플레이 (사람 없이 끝까지)

```text
SandboxServer --world samples/mana_regen --script samples/mana_regen/playthrough.json --port 0
SandboxClient --headless --world samples/mana_regen --script samples/mana_regen/playthrough.json
```

8 배속으로 처음 자리의 토끼 열 마리에 기운 주기, 30 초 뒤 다음 열 마리 (빗나가면 거절 — 결정적), 60 초 (1800 틱) 에서 끝.
둘 다 마지막에 같은 한 줄 (헤드리스 서버 = 로컬 플레이 · 게임 패키지도 — CTest `sample_mana_regen`):

```text
게임 결과: 게임 없음 · tick 1801 · hash 0x51ddbf1f7d1ab7e9
```

`expect` 와 다르면 종료 코드 1. 해시는 툴체인별 (ADR-0012) — 스크립트 · 콘텐츠 · 시뮬레이션 규칙을 일부러 바꾸면 다시 적는다.
같은 프로세스의 시험 (unit_core `sample mana_regen`) 은 세이브 → 로드 → 이어 돌린 해시, 복제 월드의 마나 = 서버 값, 인스펙터
편집도 본다.
