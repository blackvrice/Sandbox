# 08. 네트워크 — Transport · Protocol · Replication · Interest

> **규범 문서.** 서버 권한 네트워킹 전체를 정합니다. 상태: 전부 `[계획]` — Phase 9~11.
> 결정 근거: [ADR-0003](adr/0003-server-authoritative.md), [ADR-0010](adr/0010-enet-transport.md).

---

## 1. 결정 표

| 항목                 | 결정                                                                              | 이유                                                             |
|----------------------|-----------------------------------------------------------------------------------|------------------------------------------------------------------|
| Server Authority     | 서버만 진짜 `SimulationWorld`. 클라이언트는 명령을 보내고 결과를 받는다           | 50k 엔티티, 동시 편집, Late Join. 신뢰 경계가 하나               |
| Lockstep             | 기본 방식으로 쓰지 않음                                                           | 전 클라이언트 전체 계산, Late Join 비용, 최저 사양 클라에 맞춰짐 |
| 싱글플레이           | in-process Server + Loopback                                                      | 코드 경로 단일화                                                 |
| Transport            | `INetworkTransport`. 1차 ENet, Loopback, Simulated. 후속 GameNetworkingSockets    | 2장                                                              |
| Channel              | Control(신뢰·순서) / Snapshot(비신뢰·순차) / Bulk(신뢰·저우선)                    | 3장                                                              |
| 직렬화               | 자체 비트스트림 + 리플렉션 Visitor, 위치 양자화                                   | [09](09-SERIALIZATION.md) 5장                                    |
| NetEntityId          | 서버 부여 u32, 세션 내 재사용 금지, epoch                                         | 7장                                                              |
| Snapshot Rate        | 기본 15 Hz = 2틱마다. 허용값 10/15/30 (틱레이트 약수)                             | 간격 일정 → 보간 지터 없음                                       |
| Delta                | 클라이언트별 ack baseline 대비 변경 컴포넌트                                      | 6장                                                              |
| Interest             | 청크 구독 ∩ Spatial 버킷 + alwaysRelevant, 히스테리시스                           | 8장                                                              |
| Late Join            | 핸드셰이크 → Bulk(관련 청크 지형 + 엔티티 baseline) → Ready → Delta               | 4장                                                              |
| Reconnect            | 세션 토큰으로 60초 내 재접속 시 같은 ClientId·역할. 상태는 Late Join으로 재동기화 | 단순성                                                           |
| Protocol Version     | `kProtocolVersion` 정확 일치 + capabilities 비트                                  | 불일치 즉시 거절 + 사유                                          |
| Content Hash         | 로드된 Content 매니페스트(경로 정렬 + 바이트)의 해시                              | 같은 Prefab/Rule 정의 확인                                       |
| Client Interpolation | renderTime = 추정 서버 시각 − 보간 지연(2 스냅샷 간격 + 지터 여유 ≈ 150 ms)       | 패킷 하나 손실 흡수                                              |
| 예측                 | 게임플레이 예측 없음. 에디터 EditPreview만                                        | [10](10-EDITOR.md) 6장                                           |

---

## 2. Transport

```cpp
enum class Channel : std::uint8_t { Control = 0, Snapshot = 1, Bulk = 2 };

class INetworkTransport {
public:
    virtual ~INetworkTransport() = default;
    virtual bool listen(const Endpoint&, std::uint32_t maxConnections) = 0;
    virtual ConnectionId connect(const Endpoint&) = 0;
    virtual void send(ConnectionId, Channel, std::span<const std::byte>) = 0;
    virtual void flush() = 0;
    virtual void poll(std::vector<TransportEvent>& out) = 0;   // Connected / Disconnected / Received
    virtual void disconnect(ConnectionId, DisconnectReason) = 0;
    virtual TransportStats stats(ConnectionId) const = 0;      // rtt, rttVar, loss, bytesIn/Out, queued
};
```

| 구현                    | 용도              | 비고                                                                                                         |
|-------------------------|-------------------|--------------------------------------------------------------------------------------------------------------|
| `EnetTransport`         | 실제 네트워크     | ENet 채널 3개 매핑. Snapshot = `ENET_PACKET_FLAG_UNSEQUENCED` 아님, **unreliable sequenced**(오래된 것 폐기) |
| `LoopbackTransport`     | 싱글플레이·테스트 | 같은 프로세스, 큐 복사. 직렬화는 그대로 거친다 (경로 동일성)                                                 |
| `SimulatedTransport`    | 테스트            | 다른 Transport를 감싸 지연·지터·손실·재정렬·대역폭 제한 주입. 시드 고정                                      |
| `GnsTransport` `[후속]` | 암호화·NAT·릴레이 | ADR로 도입                                                                                                   |

ENet은 암호화가 없습니다. 공개 인터넷 서버를 운영하기 전에 GNS로 교체하거나 DTLS 계층을 추가해야 합니다 ([16-ROADMAP](16-ROADMAP.md) 위험 R6).

---

## 3. 채널

| 채널     | 보장                       | 내용                                                               | 크기 상한                                 |
|----------|----------------------------|--------------------------------------------------------------------|-------------------------------------------|
| Control  | 신뢰 + 순서                | 핸드셰이크, 명령(C→S), CommandResult, 권한 변경, 채팅, ServerStats | 메시지 64 KB                              |
| Snapshot | 비신뢰 + 순차              | Delta Snapshot                                                     | 패킷 1,200 B 목표(MTU), 클라이언트별 예산 |
| Bulk     | 신뢰 + 순서, 낮은 우선순위 | 청크 지형, 엔티티 baseline, 콘텐츠 오버레이                        | 조각화, 청크 단위                         |

---

## 4. 연결 흐름

```text
Client                                     Server
  │─ Hello{protocolVersion, buildId, caps} ─▶│ 버전 불일치 → Reject{VersionMismatch, serverVersion}
  │◀──────────────── Challenge{nonce} ───────│
  │─ Auth{sessionToken | displayName,        │
  │       contentHash, nonce} ──────────────▶│ contentHash 불일치 → Reject{ContentMismatch, manifest}
  │◀─ Welcome{clientId, role, epoch,          │
  │     worldMeta, serverTick, tickRate,      │
  │     snapshotRate, sessionToken} ──────────│
  │─ Subscribe{chunks[]} ───────────────────▶│
  │◀═ Bulk: TerrainChunk × k ════════════════│
  │◀═ Bulk: EntityBaseline{tick=Tb, …} ══════│ 관련 엔티티 전체 Replicated 상태
  │─ Ready{baselineTick=Tb} ────────────────▶│
  │◀── Snapshot(from Tb) … ──────────────────│ 정상 Delta
```

```text
타임아웃   핸드셰이크 10초, 무응답 연결 15초
Reconnect  Auth{sessionToken} 이 60초 내 유효하면 같은 clientId/role. 상태는 위 Bulk 부터 다시.
Kick/Ban   Control: Disconnect{reason} 후 연결 종료
```

---

## 5. 메시지 카탈로그 (`kProtocolVersion = 1`)

| id | 이름           | 방향   | 채널     | 필드                                                                                                         |
|----|----------------|--------|----------|--------------------------------------------------------------------------------------------------------------|
| 1  | Hello          | C→S    | Control  | protocolVersion u32, buildId u64, caps u32                                                                   |
| 2  | Challenge      | S→C    | Control  | nonce u64                                                                                                    |
| 3  | Auth           | C→S    | Control  | token bytes, displayName str(32), contentHash u64, nonce u64                                                 |
| 4  | Welcome        | S→C    | Control  | clientId u16, role u8, epoch u8, worldMeta, serverTick u64, tickRate u8, snapshotRate u8, sessionToken bytes |
| 5  | Reject         | S→C    | Control  | reason u8, detail str                                                                                        |
| 10 | Subscribe      | C→S    | Control  | chunks[] (ChunkCoord varint)                                                                                 |
| 11 | Ready          | C→S    | Control  | baselineTick u64                                                                                             |
| 20 | Command        | C→S    | Control  | sequence u32, payload (SimCommand 바이너리)                                                                  |
| 21 | CommandResult  | S→C    | Control  | sequence u32, status u8, reason u8, detail str                                                               |
| 30 | Snapshot       | S→C    | Snapshot | 6.2                                                                                                          |
| 31 | SnapshotAck    | C→S    | Snapshot | lastSnapshotId u32, receivedBitmask u32                                                                      |
| 40 | TerrainChunk   | S→C    | Bulk     | coord, revision, compressed layers                                                                           |
| 41 | EntityBaseline | S→C    | Bulk     | baselineTick, entities[] (Spawn 형식)                                                                        |
| 42 | ContentOverlay | S→C    | Bulk     | rules/behaviors/prefabs JSON (월드 오버레이분)                                                               |
| 50 | ServerStats    | S→C    | Control  | tickMs avg/p99, entityCount, systemTimes[], pathQueue, jobQueue (1 Hz)                                       |
| 60 | Chat           | 양방향 | Control  | text str(256)                                                                                                |
| 61 | RoleChanged    | S→C    | Control  | clientId, role                                                                                               |
| 62 | Disconnect     | 양방향 | Control  | reason u8                                                                                                    |

메시지 헤더: `id: varint`. 필드 인코딩은 [09](09-SERIALIZATION.md) 5장. 메시지를 추가·변경하면 `kProtocolVersion`을 올리고 이 표를 갱신합니다.

---

## 6. Replication

### 6.1 원칙

```text
- 서버는 클라이언트별로 마지막으로 ack 된 스냅샷(baseline) 을 기억한다 (최근 32개 송신 기록 유지).
- 스냅샷 = baseline 이후 변경분: Spawn, Despawn, 변경된 Replicated 컴포넌트, 이번 구간 Event.
- 손실 스냅샷은 재전송하지 않는다. 다음 Delta 가 더 오래된 baseline 에서 계산되어 자연 복구된다.
- baseline 이 32개 송신 기록보다 오래되면 해당 클라이언트에 Bulk EntityBaseline 으로 재동기화.
```

### 6.2 Snapshot 패킷

```text
Snapshot {
  snapshotId   u32
  serverTick   u64 (varint)
  baselineId   u32            (0 = 없음, 전체)
  ackedCommandSeq u32         (이 클라이언트 명령 중 적용 완료된 최대 seq)
  spawns[]     { netId, prefabId u64, components[] { stableId, version, fields(bitstream) } }
  updates[]    { netId, components[] { stableId, changedFieldMask?, fields } }
  despawns[]   { netId }
  events[]     { type, netIds[], payload }
}
```

`updates`의 컴포넌트는 처음에는 **컴포넌트 전체 필드**를 보냅니다. 필드 단위 마스크는 측정 후 최적화입니다.

### 6.3 서버 측 (Stage 18)

```text
for client in clients (clientId 오름차순):
  relevant = Interest.relevantEntities(client)
  for e in relevant (netId 오름차순):
     미스폰 → Spawn (Replicated 전체)
     else for T in Replicated (stableId 오름차순): pool<T>.changed[e] > client.ackedTick → Update
  (client.known − relevant) ∪ destroyedLog ∩ client.known → Despawn  (ack 될 때까지 반복 포함)
  우선순위 정렬: 선택·소유 엔티티 > 카메라 중심 거리 > 마지막 전송 후 경과
  바이트 예산(기본 256 KB/s ÷ snapshotRate) 초과분은 다음 스냅샷으로 이월
  직렬화 바이트 → Net IO 스레드
```

### 6.4 클라이언트 측

```text
수신 → snapshotId 가 최신보다 오래되면 폐기
Spawn  → ClientWorld.create + NetEntityMap 등록 (이미 있으면 덮어쓰기)
Update → NetEntityMap 조회 → 컴포넌트 기록 → SnapshotBuffer 에 Transform 기록
Despawn→ 파괴 + tombstone[netId] = snapshotId (baseline 보다 오래되면 정리)
EntityRef 필드 → netId 를 로컬 EntityId 로 변환, 모르면 대기 목록 (다음 Spawn 시 연결)
Event  → AudioExtraction / 이펙트
ack    → 다음 SnapshotAck 에 lastSnapshotId + 최근 32개 수신 비트마스크
```

### 6.5 대역폭 추정 (설계값, 측정 아님)

```text
가시 엔티티 2,000, 이동 중 50%, 위치 2×16bit + 회전 8bit + netId varint ≈ 9 B
→ 1,000 × 9 B × 15 Hz ≈ 135 KB/s (무압축). 상한 256 KB/s.
```

---

## 7. NetEntityId

```text
표현     u32 id (0 = invalid). 세션 epoch(u8)는 Welcome 에서 공유, 패킷에는 id 만.
부여     서버가 엔티티 생성 시 nextNetId++ (재사용 없음). 30 TPS 로 매 틱 1,000 개 생성해도 39시간.
         고갈 임박(2^32 − 2^24) 시 서버가 경고하고 다음 재시작에서 epoch++ 후 0부터.
클라      NetEntityMap: unordered_map<NetEntityId, EntityId> + 역방향 (조회 전용 → 04 4.2 위반 아님)
```

| 문제                                         | 해결                                                            |
|----------------------------------------------|-----------------------------------------------------------------|
| 슬롯 재사용으로 옛 참조가 새 엔티티를 가리킴 | NetEntityId는 EntityId 슬롯과 무관, 재사용 없음                 |
| Despawn 손실                                 | ack될 때까지 매 스냅샷에 포함                                   |
| 늦은 옛 스냅샷이 despawn된 엔티티를 되살림   | 순차 채널 + tombstone                                           |
| 컴포넌트 내 엔티티 참조                      | `Hint::EntityRef` → 전송 시 netId, 수신 시 로컬 EntityId        |
| 세이브                                       | NetEntityId 저장 안 함. saveId 사용 ([09](09-SERIALIZATION.md)) |

---

## 8. Interest Management

```text
Subscribe{chunks}: 클라이언트가 카메라 가시 영역 + 1청크 여유를 보낸다 (변경 시만, 최대 2 Hz)
서버:
  relevantChunks   = subscribed ∩ 존재 청크 (상한 256청크/클라이언트)
  relevantEntities = SpatialIndex.queryChunk(c) for c in relevantChunks
                   ∪ alwaysRelevant (월드 전역 엔티티, 해당 클라이언트 소유·선택 중인 엔티티)
히스테리시스: 구독 해제는 2초 지연 → 경계 왕복 시 Spawn/Despawn 폭주 방지
지형: 새로 구독한 청크는 Bulk TerrainChunk (클라이언트가 같은 revision 을 캐시하면 생략)
```

Chunk System과 Interest가 **같은 ChunkCoord 분할**을 씁니다. 추가 공간 자료구조가 없습니다 ([05](05-WORLD.md)).

---

## 9. 클라이언트 보간

```text
serverTimeEstimate: Snapshot.serverTick 수신 시각들의 지수 평활 (RTT/2 보정)
renderTick = serverTimeEstimate − interpDelay (분수 틱)
SnapshotBuffer: 최근 32개 (serverTick, netId → Transform)
pos = lerp(a.pos, b.pos, α), 회전은 최단 각도 보간
순간이동: 한 구간 이동 > maxSpeed × 구간 × 4 → 스냅 (RTS TickInterpolator 규칙)
버퍼 고갈: 최대 1 구간 외삽, 이후 정지
```

---

## 10. 데이터 흐름

```text
── Client → Server ───────────────────────────────────────────────
Editor/Input → SimCommand{seq} → ClientSession.outbox → [Net IO] BitWriter → Control
 → Server [Net IO] 역직렬화 + 형식 검증 → Simulation inbox (배치 swap)
 → [Stage 1] CommandValidator (권한·대상·콘텐츠·값·속도 제한)
     거절 → CommandResult{Rejected}      승인 → executeTick = tick + 1 → CommandQueue → ReplayRecorder
 → [Stage 2] 적용

── Server → Client ───────────────────────────────────────────────
[Stage 18] Replication → 클라이언트별 바이트 → [Net IO] Snapshot/Bulk
 → Client [Net IO] → Main inbox → ClientWorld 적용 · SnapshotBuffer · ack
 → Interpolation → Extraction → Render

── 주기 ──────────────────────────────────────────────────────────
Simulation 30 TPS ≠ Snapshot 15 Hz (2틱마다, 틱 경계 정렬) ≠ Render 60+ FPS
```

---

## 11. 보안·견고성 최소선

```text
- 모든 수신 길이·개수에 상한. 초과 → 연결 종료 (BitReader 는 경계 검사 실패 시 예외가 아니라 error 상태)
- 명령 속도 제한 (03 4.1)
- 서버는 클라이언트가 보낸 executeTick·netId 소유권을 신뢰하지 않는다
- 콘텐츠 JSON(ChangeRule 등) 은 크기 상한 + 스키마 검증 후 적용
- 암호화 없음 (ENet) — LAN/신뢰 환경 한정임을 문서·UI 에 명시
```

---

## 12. 필수 테스트

```text
- BitWriter/Reader 왕복, 경계 초과 시 error
- 핸드셰이크: 버전 불일치, contentHash 불일치, 재접속 토큰
- Loopback 수렴: 서버 틱 T 상태 == 클라 복제본 (Replicated 필드, 양자화 오차 허용)
- SimulatedTransport(100 ms, 지터 20 ms, 손실 5%, 재정렬) 에서 수렴
- Despawn 손실, 늦은 스냅샷 tombstone, EntityRef 대기 목록
- Interest: 카메라 이동 시 Spawn/Despawn 수 상한, 히스테리시스
- 대역폭: 50k 월드에서 클라이언트당 바이트가 가시 엔티티 수에 비례 (sbx_bench net.snapshot)
```
