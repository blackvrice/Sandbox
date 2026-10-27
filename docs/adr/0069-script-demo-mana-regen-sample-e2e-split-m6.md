# ADR-0069. 스크립트 데모 samples/mana_regen — 스크립트가 든 콘텐츠 루트로 sbx new, E2E 는 CTest 와 같은 프로세스 시험, M6 를 셋으로

- 상태: **Accepted** · 날짜: 2026-10-27
- 관련: [ADR-0055](0055-sample-minigame-play-script-server-equals-local.md) (데모 · 자동 플레이 · 헤드리스 서버 = 로컬 플레이),
  [ADR-0059](0059-script-components-schema-dynamic-pool-name-order-catalog.md) ~ [ADR-0068](0068-script-profile-injected-diagnostic-clock-serverstats-protocol-14.md)
  (스크립트 컴포넌트 · 시스템 · 도구), [ADR-0012](0012-golden-hash-per-toolchain.md), [18-PLATFORM](../18-PLATFORM.md) 6.2 (v0.3 M6-T1),
  [13-TESTING](../13-TESTING.md) 5.1

## 맥락

v0.3 완료 기준의 첫 줄 "스크립트로 Mana Component · Regeneration System" 과, 계획의 M6-T1 "데모 samples/mana_regen: Inspector ·
세이브/로드 · 복제 · 로컬 = 원격 해시, 보호 시험, 10k 대상 벤치". 한 태스크에 성격이 다른 셋 (데모 E2E · 보호 시험 · 벤치) 이
묶여 있다. 정할 것: 데모를 어떻게 만들고 무엇을 보이나, 시작 월드의 개체가 스크립트 컴포넌트를 갖게 하는 길, E2E 를 무엇으로 보나.

## 결정

```text
1. M6 를 셋으로: T1 데모 + E2E (이 ADR), T2 보호 시험 (무한 루프 · 권한 위반 · 불법 바이트코드 · 핸들 위조) + 10k 대상 벤치 (sbx_bench
   스크립트 단계), T3 버전 종료 (릴리스 노트 · 0.3.0)
2. samples/mana_regen — 커밋한 게임 프로젝트 (게임 모드 없음 — 샌드박스). 콘텐츠 = 생태계 팩 복사본 + scripts/ (컴포넌트 eco.mana,
   server 시스템 eco.mana.regen (onTick 15 틱마다) · eco.mana.newborn (onEvent spawn), client 시스템 eco.mana.hud) + 토끼 Prefab 의
   eco.mana + 행동 eco.energize (조건 · 효과가 스크립트 컴포넌트 필드 — 마나 30 → 기운 30) + 키 R. 스크립트 기능 (컴포넌트 · 주기 ·
   이벤트 · client · Rule 연결) 을 한 게임에서 다 쓴다
3. 시작 월드는 그 콘텐츠를 넣은 루트에서 sbx new --from ecosystem_small --content-root <루트> 로 만든다 — 템플릿 시나리오가 Prefab
   으로 개체를 만들어 처음부터 마나가 있다 (나중에 Prefab 만 고치면 세이브된 개체에는 붙지 않는다)
4. 자동 플레이 playthrough.json: 8 배속, 기운 주기 20 번 (처음 자리의 토끼 — 빗나가면 거절, 결정적), maxTicks 1800, expect = tick ·
   툴체인별 해시. CTest sample_mana_regen = sample_playthrough.cmake (매개변수 RESULT_MATCH · SERVER_MATCH · CLIENT_MATCH 를 더했다):
   헤드리스 서버 = 로컬 플레이의 결과 줄 (프로젝트 · 패키지), 서버 끝 줄의 스크립트 시스템 · 트랩 0, 클라이언트 끝 줄의 client 시스템
5. 같은 프로세스 시험 (unit_core test_mana_regen_sample): 열기 (시스템 셋 · 토끼 75 · 마나 50), 자동 플레이 두 번 같다 · 기운 주기가
   받아들여진다, 세이브 → 로드 → 이어 돌린 해시 = 곧장, 로컬 세션 — 멈춘 뒤 복제 월드의 마나 바이트 = 서버, client 글자 수 = 마나
   개체 수, Inspector 값 · 편집 (ChangeComponent → 서버 값). "로컬 = 원격" 은 ServerHost 가 같다는 ADR-0055 의 판정을 따른다
   (헤드리스 서버 = 원격 서버의 실행 파일)
```

## 근거

```text
- 게임 모드 없이: rabbit_guard 가 게임 진행을 보였다 — 이 데모는 스크립트를 보인다. 끝은 maxTicks 로 충분하다
- 콘텐츠 루트로 sbx new: 지금 도구 그대로 (새 옵션 없음) 시작 월드가 콘텐츠와 맞는다
- 셋으로 나누기: 한 커밋에 한 작업 (AGENTS 5장). 보호 시험 · 벤치는 데모 없이도 서고, 데모는 그 둘 없이 E2E 를 마친다
```

## 결과

- 얻는 것: 스크립트 게임 하나가 편집 · 세이브 · 복제 · 패키지 · 두 호스트에서 같은 결과로 자동 검증된다. 사용자가 따라 할 예제.
- 포기하는 것: 데모 안의 게임 규칙 (승패) — rabbit_guard 와 겹친다.
- 위험: 해시가 툴체인 · 콘텐츠 · 스크립트에 묶인다 — 일부러 바꾸면 expect 를 다시 적는다 (골든과 같은 규칙). CTest 시간 약 40 초.

## 대안

| 대안                                          | 기각 사유                                                               |
|-----------------------------------------------|-------------------------------------------------------------------------|
| rabbit_guard 에 스크립트를 더하기             | v0.2 데모 · 해시 · 릴리스 노트의 기록이 바뀐다                           |
| Prefab 만 고치고 시작 월드 그대로             | 세이브된 토끼에는 마나가 없다 — 새로 태어난 토끼만                       |
| sbx new 에 스크립트 넣기 옵션                 | 콘텐츠 루트 옵션으로 이미 된다 — 도구를 늘릴 까닭이 없다                |
| M6-T1 을 한 커밋으로                           | 데모 · 보호 · 벤치가 한 커밋에 섞인다                                   |

## 재검토 조건

데모가 게임 규칙을 필요로 할 때 (스크립트 GameMode — v0.4 이후), 자동 플레이 시간이 CTest 예산을 넘을 때.
