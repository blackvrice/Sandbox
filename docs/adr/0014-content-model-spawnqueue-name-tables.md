# ADR-0014. 콘텐츠는 이름으로 묶고, Prefab 생성은 SpawnQueue 로, 로드 해시는 Opaque 이름 집합으로 판단한다

- 상태: **Accepted** · 날짜: 2026-10-05
- 관련: [11-CONTENT-SCHEMA](../11-CONTENT-SCHEMA.md) 0장, [03-SIMULATION](../03-SIMULATION.md) Lifecycle·SpawnQueue,
  [09-SERIALIZATION](../09-SERIALIZATION.md) Phase 5A, [ADR-0013](0013-save-folder-swap-and-load-hash.md) 2번 (일부 대체)

## 맥락

Phase 5A 에서 콘텐츠 팩(Prefab·Tag·Rule·BehaviorGraph)과 생명 주기(번식으로 Prefab 이 생긴다)를 넣었다. 세 가지를 정해야 했다.

```text
1. System 이 틱 도중 Prefab 을 만들어야 한다 (번식, 5B 의 Rule spawn 효과). ECB 는 "컴포넌트 값 묶음"만 안다.
   Prefab 생성은 병합(Prefab + 덮어쓰기) · 검증 · 경계 검사 · saveId/netId 부여 · Opaque 등록까지 SimulationWorld 의 일이다.
2. 태그는 콘텐츠가 이름으로 선언하고 엔티티는 비트(TagSet)로 들고 다닌다. 비트 번호는 콘텐츠 구성에 따라 바뀐다.
3. ADR-0013 은 "Opaque 가 있으면 로드 해시 비교를 건너뛴다" 였다. ecosystem 의 모든 동물·풀은 render.sprite(Opaque)를
   갖는다 → 생태계 세이브는 D2 검증을 영영 받지 못한다.
```

## 결정

```text
1. Prefab 생성 요청은 ECB 가 아니라 SpawnQueue 로 보낸다: SpawnRequest{parent saveId, seq, Prefab*, position, uniqueTile}.
   ECB 적용 뒤 (parent, seq) 순으로 정렬해 SimulationWorld::instantiate 로 만든다. CreateEntity{prefab} 명령도 같은 함수를 쓴다.
   uniqueTile 요청은 같은 틱에 같은 타일·같은 Prefab 요청이 먼저 처리됐으면 버린다 (이미 있는 개체는 요청한 System 이 본다).
2. 콘텐츠 참조는 이름(id)으로 저장·해석한다. 태그 비트 = 전체 태그 이름 정렬 순. 세이브는 태그 이름 표를 남기고
   로드는 이름으로 비트를 재매핑한다 (머티리얼 표와 같은 방식, ADR-0013 3번).
3. 로드 해시 비교는 "저장한 프로세스가 몰랐던 컴포넌트 이름 집합 == 이 프로세스가 모르는 이름 집합"일 때 한다.
   world.json 에 opaqueComponents[이름] 을 남긴다. 그 밖에 건너뛰는 경우: simVersion 다름 · 마이그레이션 · 태그 표가 다름.
   (ADR-0013 2번의 "이번 로드에 Opaque 가 있음 · 저장한 프로세스에 Opaque 가 있었음" 조건을 대체한다.)
4. 내장 "core" 팩(core.grass/rock/sand/water)은 팩 선택과 무관하게 항상 있다. 팩 id "core" 는 예약.
```

## 근거

```text
- SpawnQueue 는 생성 순서를 System 실행 순서가 아닌 (부모 saveId, seq) 로 고정한다. 5B 에서 System 을 Job 으로 나눠도
  (D5) 생성 순서 → saveId 할당이 바뀌지 않는다. instantiate 하나로 모으면 명령·번식·Rule 이 같은 검증을 지난다.
- 이름 표 재매핑은 "팩에 태그 하나 추가"가 기존 세이브를 깨지 않게 한다. 비트로 해시하므로 표가 같을 때만 비교가 의미 있다.
- Opaque 컴포넌트는 값이 JSON 그대로 왕복하고 해시에도 그대로 들어간다. 비교가 틀어지는 원인은 "어떤 컴포넌트가 Opaque
  였는가"가 두 프로세스에서 다른 것뿐이다 → 그 집합만 같으면 비교가 성립한다. 같은 바이너리·같은 팩의 일상적인 왕복은
  모두 검증된다 (eco_lifecycle 900틱 세이브 왕복으로 확인).
- 빈 월드·테스트가 팩 없이도 지형을 칠할 수 있어야 한다.
```

## 결과

- 얻는 것: 생성 순서 결정론이 스케줄과 분리된다. 콘텐츠 변경에 세이브가 강하다. 생태계 세이브도 매 로드 D2 검증.
- 포기하는 것: Prefab 생성이 같은 틱의 다른 System 에 보이지 않는다 (다음 틱부터 — 생성은 Stage 17 의 ECB 적용 직후).
  태그 표가 바뀐 세이브는 해시 비교를 건너뛴다 (재매핑 결과는 경고로 알린다).
- 위험: SpawnQueue 는 상한이 없다. 번식 폭주는 Reproduce 의 cooldown·minEnergy 와 uniqueTile 로만 막힌다 → 5C 밸런스·
  성능 측정에서 상한(틱당 생성 수)을 다시 본다.

## 대안

| 대안                                             | 기각 사유                                                                                                              |
|--------------------------------------------------|------------------------------------------------------------------------------------------------------------------------|
| ECB 에 "Prefab 생성" 명령 추가                   | ECB 가 콘텐츠·검증·정체성 부여를 알게 된다. System 별 ECB 적용 순서가 곧 saveId 순서가 된다                            |
| 태그를 문자열 집합 컴포넌트로                    | 매칭(TagExpr)이 틱마다 문자열 비교. 256 비트 AND 로 충분하다                                                           |
| 태그 비트를 해시에서 이름으로 먹이기             | 해시 비용이 태그 수만큼 늘고, 지금 필요한 경우(표가 다른 로드)는 경고로 충분하다. 필요해지면 simVersion 을 올려 바꾼다 |
| Opaque 가 있으면 비교 건너뛰기 (ADR-0013 그대로) | 생태계 세이브 전부가 검증 밖으로 빠진다                                                                                |

## 재검토 조건

5C 에서 번식 폭주가 틱 예산을 위협할 때 (SpawnQueue 상한), 또는 모드가 같은 이름의 태그를 다른 뜻으로 쓰는 충돌이 생길 때
(지금은 여러 팩이 같은 태그를 선언하면 하나로 합친다).
