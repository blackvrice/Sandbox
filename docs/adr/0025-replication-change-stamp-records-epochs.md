# ADR-0025. 복제는 클라이언트별 스냅숏 기록(변경 순번)으로 차분을 만들고, 기준을 잃으면 epoch 으로 다시 맞추며, 클라이언트는 시뮬레이션하지 않는 ClientWorld 에 적용한다

- 상태: **Accepted** · 날짜: 2026-10-08
- 관련: [08-NETWORK](../08-NETWORK.md) 6 · 7 · 9장, [02-ECS](../02-ECS.md) 8장, [09-SERIALIZATION](../09-SERIALIZATION.md) 5장,
  [ADR-0024](0024-network-foundation-enet-serverhost-two-halves.md), [16-ROADMAP](../16-ROADMAP.md) Phase 10

## 맥락

Phase 10 은 복제다: 10.1 NetEntityMap · 10.2 Spawn/Update/Despawn · ack · baseline 32 · 10.3 양자화 · EntityRef · tombstone ·
10.4 SnapshotBuffer · 보간 · 10.5 LocalServerHost · --direct-sim 삭제 · 10.6 지표 · 오버레이. 커서 둘로 나눈다 — **10A** 복제 자체
(서버 쓰개 · 클라이언트 복제 월드 · 수렴 시험), **10B** SandboxClient 연결(NetworkSession · LocalServerHost · 그리기 · 보간 ·
--direct-sim 삭제). 이 ADR 은 10A 의 결정이다. 정할 것:

```text
1. "기준(baseline) 이후 바뀐 것" 을 무엇으로 아나 — 레지스트리의 changed 는 틱 번호였는데, 일시정지 편집 단계는 틱이
   그대로라서 같은 틱 안의 편집을 놓친다 (스냅숏도 같은 틱에 찍힌다)
2. 손실 · 순서 바뀜 · 예산(일부만 보냄) 아래에서 despawn · 컴포넌트 떼기를 놓치지 않으려면 서버가 무엇을 기억하나
3. 클라이언트가 오래 ack 하지 못하면
4. 컴포넌트 값의 와이어 형식, 서버가 모르는 render.* (Opaque) 는
5. 클라이언트 쪽 월드는 무엇인가 (SimulationWorld 를 다시 쓸까)
6. 08 4장의 Subscribe → Bulk EntityBaseline → Ready 흐름
```

## 결정

```text
1. SimulationWorld::changeStamp — tick() 마다 max(이전 + 1, 틱 번호) 로 오르는 변경 순번 (편집 단계 포함). 레지스트리의
   currentTick 에 이 값을 넣어 changed/added 가 순번을 기록한다. 일시정지가 없으면 틱 번호와 같다 (기존 테스트 · 해시와 무관 —
   변경 기록은 해시 · 세이브에 들어가지 않는다). 복제는 changedAt > 기록의 stamp 로 고른다.
2. ReplicationWriter (Simulation 스레드): 클라이언트마다 보낸 스냅숏 기록 32 개. 기록 = netId 오름차순 {netId, mask, stamp,
   pendingSpawn} — "이 스냅숏을 적용한 클라이언트가 가진 것". B = 가장 최근 ack 된 기록, inflight = B 이후 보낸 기록들.
     spawn  = B 에 없음(또는 pendingSpawn) → 복제 컴포넌트 전부 + Opaque
     update = 바뀐 컴포넌트(B 에 없던 것 · changedAt > stamp) 또는 mask 가 B · inflight(OR) 와 다름 → 바뀐 것 + 지금 mask
     despawn = (B ∪ inflight) − 지금 — ack 될 때까지 매번
   예산(bytesPerSnapshot)을 넘으면 나머지는 미룬다: 기록에는 B 의 항목을 그대로 (B 에 없고 inflight 에 있으면 pendingSpawn
   항목 — 받은 쪽에 있을 수 있으니 despawn 을 놓치지 않게). 시작 위치를 돌려 가며(round-robin). despawn 은 예산 밖.
3. epoch: ack 된 기준이 기록 밖으로 밀려나고 epoch 의 첫 스냅숏도 밀려났으면 epoch++ 하고 기준 없이 처음부터. 받는 쪽은
   새 epoch 이면 모두 지우고 시작한다 (08 의 "Bulk EntityBaseline 으로 재동기화" 를 같은 Snapshot 으로).
4. 컴포넌트 값 = core/serialization/BinaryCodec (리플렉션 바이트: LEB128 · zigzag · f32 IEEE 비트 그대로 · EntityRef =
   NetEntityId) — 카탈로그에 writeBinary/readBinary. 와이어에서는 길이 붙은 바이트열이라 모르는 컴포넌트는 건너뛴다.
   컴포넌트 번호 = Welcome.replicated 표(stableId, net.identity · ServerOnly 제외)의 칸, mask 는 u64. Opaque 는 spawn 때
   JSON 텍스트 한 번 (16 KB 상한). 양자화는 [계획 — 측정 뒤]: 지금은 서버 값과 비트 단위로 같다.
5. ClientWorld (network/client): Registry(복제 컴포넌트 풀) + WorldGrid(지형) + NetEntityMap(조회) + Opaque + transform 표본
   (직전 · 지금, 서버 틱). 시뮬레이션 · System · tick() 없음 — SimulationWorld 는 정체성 부여 · 명령 · 시계가 얽혀 복제본에
   맞지 않는다. 적용: epoch 이 작거나 snapshotId 가 이미 적용한 것 이하면 버린다, despawn → entities (없으면 만든다, mask 에
   없는 것을 떼고 값을 쓴다). 지형 청크는 로컬 revision 을 올린다 (그리는 쪽 캐시).
6. Ready · EntityBaseline 은 쓰지 않는다 [계획 — Interest(Phase 11) 와 함께 다시 본다]: Welcome 직후 서버가 epoch 1 스냅숏
   (기준 없음 = 모두 spawn) 과 TerrainChunk(Bulk, 길이 부호화, 스냅숏당 64 청크)를 보낸다. Welcome 보다 먼저 온 TerrainChunk 는
   클라이언트가 모아 두었다가 적용한다 (채널이 달라 순서가 바뀔 수 있다 — 신뢰 채널이라 버릴 수 없다). Snapshot 은 버려도 된다.
```

그 밖에:

```text
- 프로토콜 2 (Welcome.replicated · Snapshot · SnapshotAck · TerrainChunk). Snapshot · TerrainChunk 는 16 MB 까지 (Control 64 KB).
- ServerHost: 스냅숏은 Simulation 단계 2 번마다 (15 Hz, 일시정지 중에도 — 편집이 보이게), 클라이언트당 예산 = 초당 바이트
  (기본 256 KB/s, SandboxServer --snapshot-kbps, 0 = 제한 없음) × 간격 ÷ 30. Net 절반 → Sim 절반: roster(Welcome · 나감) ·
  SnapshotAck. Welcome.snapshotRate = 30 ÷ 간격.
- ClientSession: catalog · content 를 주면 Welcome 뒤 ClientWorld 를 만들고 적용 · ack. 없으면 ack 만 (서버가 같은 상태를
  계속 다시 보내지 않게).
```

## 근거

```text
- 변경 순번은 "틱 안의 편집" 을 구조로 잡는다. 편집 순번(editSequence)을 기록에 함께 두는 방법은 같은 틱에 움직인 모든
  엔티티를 편집마다 다시 보냈다 (시험에서 2 개 대신 29 개).
- 기록 + inflight 합은 Quake 3 방식의 차분을 "받는 쪽이 B 이후 무엇을 적용했든" 맞게 만든다 — 클라이언트는 상태 사본을
  여러 개 들 필요가 없고, 적용은 멱등(값 전체를 쓴다)이다.
- epoch 은 "기준 없음" 을 명시해 받는 쪽이 지울지 말지를 추측하지 않게 한다.
- 비트 단위로 같은 값이라 수렴 시험이 바이트 비교로 끝난다 (08 12장 L3′).
```

## 결과

- 얻는 것: 손실 · 지연 · 순서 바뀜 · 예산에서도 서버 월드와 같아지는 복제 (시험: 100 ms ± 20, 5 % 손실 양방향, 64 KB/s).
  probe 가 복제 개체 수를 보인다. 10k 월드(Release, 제한 없음) 스냅숏 만들기 5.4 ms (2 틱마다), 256 KB/s 예산이면 1.5 ms.
- 포기하는 것 / [계획]: Interest(모든 엔티티를 보낸다 — 10k 원격은 예산 때문에 늦게 따라온다, Phase 11), 우선순위, 양자화 ·
  필드 마스크, Event 복제, EntityRef 대기 목록, 여러 클라이언트의 바이트 재사용, Opaque 갱신(spawn 때만).
- 위험: 클라이언트당 기록 메모리 = 32 × 엔티티 수 × 24 B (10k → 7.7 MB). 스냅숏 만들기가 클라이언트 수에 비례.

## 대안

| 대안                                                  | 기각 사유                                                                 |
|-------------------------------------------------------|---------------------------------------------------------------------------|
| 컴포넌트마다 더러움 비트 (보내면 지움)                | 클라이언트마다 ack 기준이 달라 비트 하나로 안 된다 (02 8장)               |
| 기록에 편집 순번을 함께 두고 changed == stamp 도 비교 | 일시정지 편집마다 같은 틱에 움직인 엔티티 전부를 다시 보냄                |
| 클라이언트가 스냅숏별 상태 사본을 들고 기준 위에 적용 | 메모리 · 복사 비용, 서버 쪽 합집합 계산이 더 싸다                         |
| 복제본도 SimulationWorld                              | 정체성 부여 · 명령 큐 · 시계 · System 이 복제본과 맞지 않다 (saveId 없음) |
| 와이어 JSON                                           | 10k 엔티티 15 Hz 에 너무 크고 느리다                                      |

## 재검토 조건

Interest(Phase 11 — 관련 엔티티 집합이 클라이언트마다 달라진다), 클라이언트 4 명 이상에서 스냅숏 만들기가 틱 예산을 위협할 때
(바이트 재사용 · Net 쪽으로 인코딩 옮기기), 양자화 도입, 10B 의 보간이 표본 둘로 부족할 때.
