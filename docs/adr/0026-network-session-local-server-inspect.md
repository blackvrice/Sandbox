# ADR-0026. 클라이언트는 언제나 서버(같은 프로세스의 LocalServerHost 또는 원격)에 접속해 복제본을 그리고, 서버 전용 상태는 Inspect 로 보며, 보간은 서버 틱 추정 시계로 한다

- 상태: **Accepted** · 날짜: 2026-10-08
- 관련: [08-NETWORK](../08-NETWORK.md) 4 · 5 · 6.3 · 9장, [06-RENDERING](../06-RENDERING.md) 9장, [01-ARCHITECTURE](../01-ARCHITECTURE.md) 4 · 5장,
  [ADR-0021](0021-direct-sim-simulation-thread-snapshot.md)(대체), [ADR-0025](0025-replication-change-stamp-records-epochs.md),
  [16-ROADMAP](../16-ROADMAP.md) Phase 10B

## 맥락

10A 로 서버 월드가 ClientWorld 로 복제된다. 10B 의 일은 SandboxClient 를 그 위로 옮기고 `--direct-sim`(클라이언트가 Core 를
직접 돌리는 임시 경로, ADR-0020 · 0021)을 지우는 것이다 (16 2장: "Phase 10 완료 기준에서 삭제를 강제한다"). 정할 것:

```text
1. 싱글플레이는 어떻게 도나 — 같은 프로세스의 서버? 클라이언트가 월드를 직접?
2. 선택한 개체의 자세한 상태(행동 상태 · 감지 반경 · 경로 · 대상) — ai.* 는 ServerOnly 라 복제되지 않는다
3. 보간 — 스냅숏(15 Hz)과 렌더 프레임(60+ Hz) 사이, 08 9장의 SnapshotBuffer 32 개?
4. 속도 배율 ×8 에서 "Simulation 단계 2 번마다" 스냅숏이면 120 Hz — 예산 · 클라이언트 적용 비용이 속도에 비례한다
5. 일시정지 · 한 틱 · 속도 — 8A 는 클라이언트 쪽 진행만 바꿨다
6. 앱 상태기계의 Connecting
```

## 결정

```text
1. LocalServerHost (network/server): ServerHost + LoopbackTransport 둘 + 월드(openWorldSource — 시나리오 · 세이브 폴더,
   SandboxServer --world 와 같은 core/scenarios/WorldSource). 역할 Owner, 예산 없음. 창 실행은 Threaded (Simulation ·
   Net IO 스레드 — ADR-0021 의 "틱이 느려도 화면은 제 속도" 가 그대로), 헤드리스 · 시험은 Inline (update(dt) 안에서 —
   같은 dt 열이면 같은 틱 수). 클라이언트는 원격과 똑같이 ClientSession 으로 접속한다 — 길이 하나다.
   SandboxClient --world <시나리오|세이브> (로컬) · --connect host:port (원격) · --name. --direct-sim · DirectSim 은 지운다.
2. Inspect (51, C→S, Control): 볼 netId 집합 (≤ 32, 빈 목록 = 그만, 바뀔 때만 보낸다). 서버는 클라이언트별 집합을 두고
   스냅숏을 만들 때마다 InspectResult (52, S→C, Snapshot 채널 — 잃어도 다음 것): {netId, 행동 상태 id, 감지 반경, 남은 경로,
   목표, 대상 netId}. 읽기 전용이라 모든 역할. 복제되는 값(위치 · 속도 · 프리팹 · 에너지 · 체력)은 넣지 않는다. 프로토콜 3.
3. 보간 = InterpolationClock + 엔티티별 표본 둘 (ClientWorld::TransformTrack — 직전 · 지금, 서버 틱).
   시계: 스냅숏의 serverTick 과 받은 시각으로 est(now) = base + (now − t) × 30 × speed, 스냅숏마다 오차의 0.1 만 당기고
   0.5 초 넘게 어긋나면 다시 맞춘다. renderTick = est − 0.1 초 몫 (스냅숏 간격 1.5 배), 앞으로만. 일시정지면 마지막
   serverTick (편집이 바로 보이게), 재개하면 멈춘 자리에서 이어 간다. 표본: 직전 표본의 틱은 적어도 직전 스냅숏의 틱
   (멈췄다 움직인 개체의 움직임이 긴 구간에 퍼지지 않게), 같은 틱이거나 8 칸 넘게 움직였으면 지금 값 (순간이동), 외삽 없음.
4. 스냅숏 간격은 실제 시간 (snapshotIntervalSteps ÷ 30 초 = 15 Hz) — 속도 배율과 무관. 단계 시각이 흔들려도 간격의 1/4
   까지는 당겨 보낸다.
5. 일시정지 · 한 틱 · 속도는 서버로 가는 명령 (Pause/Resume/Step/SetSimulationSpeed). 결과를 기다리지 않고 "보낸 값" 에서
   다음을 정한다 (속도 칸 연타). 거절(원격 editor 역할 등)은 로그 · 네트워크 패널에.
6. IWorldSession::ready() · failure(): Application 은 Connecting 에서 update 를 부르며 ready (접속 + 첫 스냅숏) 를 기다리고,
   failure (거절 · 끊김 · 시간 초과) 면 끝낸다 (종료 코드 1). 메뉴 UI 가 생기면 MainMenu 로 [계획].
```

그 밖에:

```text
- SpriteExtraction 은 ClientWorld 를 Main 스레드에서 프레임마다 읽는다 (적용과 같은 스레드 — 불변 스냅숏 복사본이 필요
  없다). 키 = NetEntityId (선택 · 캐시). 지형은 ClientWorld 가 올리는 로컬 revision 으로 바뀐 청크만 복사한다.
- 원격: 콘텐츠가 다르다고 거절되면 서버가 알려 준 팩을 --content 에서 읽어 한 번 다시 접속 (sbx_net_probe 와 같다).
- 네트워크 패널: 서버 · 역할 · RTT · 스냅숏(epoch · 다시 맞춤) · 받은 KB/s · 적용 ms · 보간 지연 · 거절된 명령.
```

## 근거

```text
- 싱글플레이가 같은 길을 타야 "로컬에서는 되는데 서버에서는 안 된다" 가 없다 (01 4장의 단일 코드 경로). Loopback 은 복사
  비용만 있고, Inline 모드로 헤드리스 · 시험이 결정적이다.
- Inspect 를 복제와 따로 두면 ServerOnly 를 지키면서 (모든 클라이언트에 ai.* 를 보내지 않는다) 선택한 몇 개만 본다.
  Snapshot 채널이라 오래된 값이 쌓이지 않는다.
- 표본 둘 + 추정 시계는 08 9장의 SnapshotBuffer 32 보다 단순하고, 엔티티마다 바뀐 스냅숏이 달라도(예산 · 변경 없음) 맞다.
- 스냅숏을 실제 시간으로 고정하면 ×8 에서도 클라이언트 적용 비용 · 대역폭이 같다 (단계마다면 8 배).
```

## 결과

- 얻는 것: `--direct-sim` 코드 0줄 (DirectSim · 그 시험 · 옵션 삭제). SandboxClient 가 같은 코드로 로컬 · 원격 서버를
  그린다. 선택 상세 · 디버그 선(감지 반경 · 경로 · 대상)이 원격에서도 보인다.
- 측정 (Release, 헤드리스 ecosystem_10k 11.5k 개체, 서버 Inline): 추출 1.9 ms/프레임, 받기 · 적용 평균 2.1 ms/프레임
  (스냅숏 하나 약 8 ms — 15 Hz), 서버 스냅숏 만들기 7.4 ms (14 7.10).
- 포기하는 것 / [계획]: 스냅숏 적용이 Main 스레드에서 돈다 — 10k 에서 66 ms 마다 수 ms 가 끼어든다 (Net 스레드에서
  디코드 · 이중 버퍼 [계획 15]). 외삽 · 순간이동 판정은 단순하다. 메뉴 · 다시 접속 UI 없음 (실패하면 끝낸다).
  Inspect 는 서버 전용 컴포넌트 몇 개만 — 인스펙터(12)는 컴포넌트 전체.
- 위험: 서버 틱이 느리면(Debug 10k) 스냅숏이 늦게 오고 보간 시계가 따라 느려진다 — 화면은 매끄럽지만 늦다.

## 대안

| 대안                                                     | 기각 사유                                                                                 |
|----------------------------------------------------------|-------------------------------------------------------------------------------------------|
| 싱글플레이는 --direct-sim 을 남긴다                      | 두 길 — 명령 · 권한 · 복제를 싱글플레이에서 시험하지 못한다 (16 2장이 삭제를 강제)        |
| ai.* 를 Replicated 로 바꾼다                             | 모든 클라이언트 · 모든 개체에 큰 값(경로 16 점)을 보낸다, ServerOnly 의 뜻이 깨진다        |
| 선택 상세를 명령(요청 → CommandResult)으로               | 명령 큐 · 틱을 거치고 결과가 한 번뿐 — 계속 보려면 매번 요청해야 한다                      |
| SnapshotBuffer 32 (08 9장 원안)                          | 엔티티마다 스냅숏에 들어간 시점이 달라 버퍼 전체 보간이 맞지 않는다 (예산 · 변경 없음)    |
| 스냅숏을 Simulation 단계 수로 (10A 그대로)               | ×8 에서 120 Hz — 대역폭 · 적용 비용이 속도에 비례                                         |

## 재검토 조건

Phase 11 Interest (보낼 집합이 화면 근처로 줄면 적용 비용도 준다), 적용이 프레임 예산을 위협할 때 (Net 스레드 디코드),
Phase 12 인스펙터 (Inspect 를 컴포넌트 전체로 넓힐지), 다시 접속 UI (11.4).
