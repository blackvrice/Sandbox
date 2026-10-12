# ADR-0031. 세이브 · 로드 · 리플레이는 서버 Sim 절반이 틱 사이에 하고, 월드를 바꾸면 generation 을 올려 클라이언트가 새 복제본으로 갈아탄다

- 상태: **Accepted** · 날짜: 2026-10-12
- 관련: [10-EDITOR](../10-EDITOR.md) 2 · 7 · 9장, [08-NETWORK](../08-NETWORK.md) 4 · 5장(프로토콜 7),
  [09-SERIALIZATION](../09-SERIALIZATION.md) 3 · 4장, [04-DETERMINISM](../04-DETERMINISM.md) D3,
  [ADR-0024](0024-network-foundation-enet-serverhost-two-halves.md), [ADR-0025](0025-replication-change-stamp-records-epochs.md),
  [ADR-0030](0030-roles-player-list-content-overlay.md), [16-ROADMAP](../16-ROADMAP.md) Phase 12D

## 맥락

12D = 12.6 저장 · 불러오기 · 리플레이 UI + Phase 12 완료 기준 (edit_session 시나리오 D3, 두 클라이언트 동시 편집, 역할 × 명령
권한 표). 세이브(persist::saveWorld · loadWorld)와 리플레이(ReplayRecorder · playReplay)는 Phase 4 부터 Core 에 있지만 도구
(sim_check)만 썼다. 클라이언트는 언제나 서버의 복제본을 그린다 (ADR-0026) — 싱글플레이도 같은 프로세스의 서버에 접속한다.
정할 것:

```text
1. 누가 저장 · 불러오기 · 기록 · 재생을 하나 (클라이언트의 파일인가 서버의 파일인가), 권한
2. 실행 중인 서버의 월드를 통째로 바꾸는 법 (복제 · 오버레이 · 기다리던 명령 · 기록 중인 리플레이)
3. 클라이언트가 바뀐 월드를 아는 법 — 메시지 채널이 달라 스냅숏과 순서가 바뀔 수 있다
4. 리플레이 재생을 실시간으로 보여 주는 법 (편집 · 실행 제어는 무엇을 하나, 끝나면)
5. 에디터의 되돌리기 기록 · 기다리던 편집
```

## 결정

```text
1. 서버의 일이다. WorldOp(65){op, name} — List · Save · Load · RecordStart · RecordStop · Play. 이름 = [A-Za-z0-9_-]{1,48}
   (경로가 아니다 — <savesRoot>/<name>). 서버 Net 절반이 거른다: List = 모든 역할, 나머지 = 실행 제어와 같은 열 (admin 부터,
   10-EDITOR 7장). 결과는 보낸 사람에게 WorldOpResult(66){op, name, accepted, detail}, List 는 WorldList(67){이름 순
   {name, replay, tick, entities}} (world.json 만 읽는다). 저장 폴더는 SandboxServer --saves (기본 saves), 싱글플레이는
   SandboxClient --saves (같은 기본). 세이브 폴더 = world.json 이 있는 곳, 리플레이 폴더 = replay.sbxr + start/ (시작 세이브).
   같은 이름 세이브는 덮어쓴다 (saveWorld 가 폴더째 바꿔 넣는다). 리플레이 이름으로는 저장 · 불러오기를, 있는 이름으로는
   기록 시작을 거절한다.
2. Sim 절반이 틱 사이에 (inbox 를 넣기 전에) 한다 — 세이브는 틱 경계에서 부른다는 09 3장 그대로. 불러오기 = 서버가 이미 읽은
   팩 콘텐츠로만 (scenario::openSave — world.json 의 contentHash 가 다르면 거절: 접속한 클라이언트가 그 콘텐츠를 가지고 있다).
   월드 바꾸기(replaceWorld): 기록 중이면 먼저 끝내 쓴다 → Job 풀을 옮기고 runner 를 바꾼다 → generation++ → 선택 상세 · 결과
   기다리는 명령을 비운다 → ReplicationWriter::resetWorld (클라이언트마다 새 epoch · 지형 판 · 지연 해제 · 늘 관련을 비운다) →
   모두에게 WorldChanged(68){generation, 새 WorldMeta, reason(1 Load · 2 Replay), name} 와 ContentOverlay{reset} 전체. 그 배치에
   들어온 명령은 옛 월드의 것이라 적용하지 않고 거절한다 ("월드가 바뀌어 적용하지 않았습니다").
3. generation 은 서버 월드가 바뀐 횟수 — Welcome.generation(끝에) · Snapshot.generation(처음에) 에 싣는다. WorldChanged 는 Bulk
   (TerrainChunk · ContentOverlay 와 같은 신뢰 · 순서 채널 — 새 월드의 지형이 언제나 그 뒤에 온다). Snapshot 채널의 새
   generation 스냅숏이 먼저 오면 ClientSession 이 쥐고 있다가(64 개까지) WorldChanged 를 받으면 새 경계로 ClientWorld 를 다시
   만들고 적용 · ack 한다. 옛 generation 스냅숏은 버린다 (ack 없음). Welcome 보다 먼저 온 WorldChanged 는 그 월드로 시작한다.
   선택 상세는 비운다 (서버도 비웠다).
4. 리플레이 기록 = 시작 세이브(start/) + ReplayRecorder (tick() 마다 afterTick — 일시정지 편집 단계도 call 하나). 재생 = 시작
   세이브를 불러와 월드를 바꾸고 core 의 ReplayPlayer 가 Sim 단계마다 call 하나 (기록한 명령을 그 call 에 넣고 tick() — 해시
   레코드마다 비교, 불일치는 처음 것을 기억하고 끝까지). 재생 중 편집 명령은 거절 ("리플레이 재생 중"), 실행 제어는 재생을
   다룬다: Pause = 재생 멈춤, Step n = 멈춘 채 n call, Resume, Speed = 재생 배율 (단계 간격 = 월드 틱 간격 ÷ 배율 — 기록된
   속도 변경도 그대로 따른다). 끝나면 모두에게 WorldOpResult{Play, 요약 — "해시 n개 일치" 또는 첫 불일치 call · tick} 을 보내고
   그 월드가 보통 월드가 되어 일시정지 (살펴보고 이어서 편집). ServerStats 에 mode(0 보통 · 1 기록 · 2 재생) · 이름 · call ·
   전체 · 비교한 해시 · 불일치 · 재생 멈춤 · 배율. 서버를 멈추면 기록 중인 리플레이를 쓴다.
5. IEditorHost::worldGeneration() — 바뀌면 에디터가 되돌리기 · 다시 하기 · 기다리던 편집 · EditPreview · 끌던 것을 비운다
   (옛 월드의 netId 다). "월드" 패널 (apps/client/ui/WorldPanel): 이름 · 저장 · 기록 시작/끝 · 새로고침, 목록 → 불러오기 ·
   재생, 상태 한 줄, 마지막 결과. Ctrl+S (editor.save) = 그 이름으로 저장. 접속하면 목록을 한 번, 받아들여진 결과마다 다시.
   재생 중 Space · . · = / - 는 서버 통계의 재생 상태로 판단해 재생을 다룬다. 프로토콜 7.
```

## 근거

```text
- 클라이언트는 월드를 갖지 않는다 (ADR-0026) — 저장할 월드는 서버에만 있다. 원격 서버의 파일을 클라이언트가 고르는 것은
  "서버 폴더 안의 이름" 으로 제한해야 안전하다 (경로 · .. 금지).
- 월드를 통째로 바꾸는 것은 epoch 재동기화(ADR-0025)와 같은 길이다 — 새 길을 만들지 않는다. 다만 경계 · 지형 크기가 달라질 수
  있어 ClientWorld 를 다시 만든다. epoch 만으로는 "다른 월드" 와 "같은 월드 다시 맞추기" 를 가릴 수 없다 — generation 을 따로 둔다.
- 채널 사이 순서는 보장되지 않는다 (ENet 채널마다 따로). 스냅숏을 쥐는 쪽이 "옛 스냅숏을 새 월드에 적용" 하는 경우를 원천 차단한다.
- 재생을 별도 프로세스 · 도구가 아니라 서버 월드로 하면 모든 클라이언트가 같은 화면을 보고 그대로 이어 편집할 수 있다.
  call 단위 재생은 기록 단위(tick() 호출)와 같아 일시정지 편집 단계까지 똑같다 (D3).
- 되돌리기 기록은 netId 로 대상을 가리킨다 (ADR-0029) — 다른 월드에서는 엉뚱한 개체를 바꾼다. 비우는 것이 안전하다.
```

## 결과

- 얻는 것: 실행 중 저장 · 불러오기 · 기록 · 재생 (모든 클라이언트가 함께), 역할 표 끝까지 (명령 12 종 + 월드 요청),
  edit_session 시나리오 (편집 명령 전 종류 · 일시정지 편집 · 오버레이 · 거절 — D1 · D2 · D3 · D5), 두 클라이언트 동시 편집 수렴.
  시험: core(ReplayPlayer · edit_session · openSave), network(메시지 · 저장 · 불러오기 · 같은 배치 거절 · 늦게 들어옴 · 콘텐츠
  다름 · 기록 → 재생 · 재생 제어 · 멈출 때 쓰기 · 스냅숏 쥐기 · 이른 WorldChanged · 권한 표 · 동시 편집), editor(월드가 바뀌면
  기록 비우기), client(로컬 저장 · 불러오기 · 오버레이 복원 · 재생 멈춤 · 패널), 옵션(--saves).
- 포기하는 것 / [계획]: 세이브 지우기 · 이름 바꾸기, 썸네일 · 설명, 자동 저장, 클라이언트로 세이브 내려받기 · 올리기, 재생 되감기
  (앞으로만 — 되감기는 시작부터 다시), 재생 중 시점 고르기(시크바), 저장을 Worker 로 (지금은 Sim 스레드에서 동기 — 큰 월드는 그
  틱이 길다), 다른 팩으로 만든 세이브를 팩을 바꿔 불러오기 (서버 재시작으로).
- 위험: 저장 · 불러오기가 그 틱을 늦춘다 (ecosystem 수 ms ~ 수십 ms). 쥔 스냅숏 64 개를 넘으면 버린다 (서버가 ack 없는 것을
  다시 담는다). 세이브 폴더가 서버 프로세스 권한으로 쓰인다 — 이름 규칙으로 폴더 밖은 막았다.

## 대안

| 대안                                             | 기각 사유                                                                        |
|--------------------------------------------------|----------------------------------------------------------------------------------|
| 클라이언트가 복제본을 저장                       | 서버 전용 컴포넌트 · AI 상태가 없다 — 불러올 수 있는 세이브가 아니다             |
| 저장 · 불러오기를 SimCommand 로                  | 결정론 상태가 아니다 (파일 입출력) — 리플레이에 섞이면 재생이 파일을 다시 쓴다   |
| 불러오기 = 서버 다시 시작 · 클라이언트 다시 접속 | 접속 · 역할 · 토큰이 끊긴다, 싱글플레이는 프로세스 안 서버라 더 번거롭다         |
| epoch 만 올리기 (generation 없이)                | 경계가 바뀌는 것 · 오버레이 리셋 시점을 알 수 없다, 옛 스냅숏이 새 월드에 붙는다 |
| 재생을 클라이언트 혼자 (sim_check 처럼)          | 다른 사람이 볼 수 없고 그대로 이어 편집할 수 없다 — 클라이언트에는 Core 가 없다  |
| 재생 중에도 편집 허용 (갈라지기)                 | 재생이 기록과 달라져 해시 비교가 뜻을 잃는다 — 끝난 뒤(일시정지)에 편집          |

## 재검토 조건

세이브가 커져 저장이 틱을 눈에 띄게 늦출 때 (Worker 직렬화 · 증분), 원격 서버의 세이브를 클라이언트가 내려받고 싶을 때
(파일 전송 채널), 리플레이 시크 · 되감기 (주기적 스냅숏 세이브), 인터넷에 여는 서버 (폴더 쿼터 · 감사 기록).
