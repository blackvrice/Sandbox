# 08. 네트워크 — Transport · Protocol · Replication · Interest

> **규범 문서.** 서버 권한 네트워킹 전체를 정합니다. 상태: **Phase 9 구현** — Transport(ENet · Loopback · Simulated) ·
> 비트스트림 · 메시지(핸드셰이크 · 명령 · 결과 · 통계 · 끊기) · ServerHost · CommandValidator · ClientSession (12장).
> **Phase 10A 구현** — 복제(6 · 7장: ReplicationWriter · ClientWorld · Snapshot · SnapshotAck · TerrainChunk, 프로토콜 2).
> 보간 · SandboxClient 연결(9장)은 `[계획 Phase 10B]`, Interest · 재접속(8장)은 `[계획 Phase 11]`.
> 결정 근거: [ADR-0003](adr/0003-server-authoritative.md), [ADR-0010](adr/0010-enet-transport.md), [ADR-0024](adr/0024-network-foundation-enet-serverhost-two-halves.md),
> [ADR-0025](adr/0025-replication-change-stamp-records-epochs.md).

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

class INetworkTransport {   // network/transport/Transport.hpp — 한 객체 = 한 스레드, ConnectionId 재사용 없음
public:
    virtual ~INetworkTransport() = default;
    virtual Expected<void> listen(const Endpoint&, u32 maxConnections) = 0;
    virtual u16 boundPort() const noexcept = 0;               // listen 에서 port 0 이면 OS 가 고른 포트
    virtual Expected<ConnectionId> connect(const Endpoint&) = 0;
    virtual void send(ConnectionId, Channel, std::span<const std::byte>) = 0;   // 복사, 끊긴 id 면 버림
    virtual void flush() = 0;
    virtual void poll(std::vector<TransportEvent>& out) = 0;   // Connected / Disconnected{reason} / Received
    virtual void wait(u32 maxMs);                              // Net IO 스레드가 이벤트를 기다린다
    virtual void disconnect(ConnectionId, DisconnectReason) = 0; // 쌓인 송신 뒤에 끊는다. 상대만 Disconnected 를 받는다
    virtual void drain(u32 maxMs);                             // 끊는 중인 연결이 상대에게 닿을 시간 (서버 종료)
    virtual TransportStats stats(ConnectionId) const = 0;      // rtt, rttVar, loss, bytes · packets in/out
};
```

| 구현                    | 용도              | 비고                                                                                                                                                         |
|-------------------------|-------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `EnetTransport`         | 실제 네트워크     | ENet 1.3.18 채널 3개 매핑. Snapshot = `ENET_PACKET_FLAG_UNSEQUENCED` 아님, **unreliable sequenced**(오래된 것 폐기). 끊기 = `disconnect_later`, 무응답 15 초 |
| `LoopbackTransport`     | 싱글플레이·테스트 | 같은 프로세스, 큐 복사. 직렬화는 그대로 거친다 (경로 동일성)                                                                                                 |
| `SimulatedTransport`    | 테스트            | 다른 Transport를 감싸 지연·지터·손실·재정렬·대역폭 제한 주입. 시드 고정                                                                                      |
| `GnsTransport` `[후속]` | 암호화·NAT·릴레이 | ADR로 도입                                                                                                                                                   |

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

Phase 10A 는 Subscribe · EntityBaseline · Ready 없이 간다 (ADR-0025 결정 6): Welcome 직후 서버가 epoch 1 Snapshot(기준 없음 =
모두 spawn)과 TerrainChunk 를 보내고, 그 뒤로는 ack 된 기준 위의 차분. 관련 엔티티 = 모든 엔티티 (Interest 는 Phase 11).

```text
타임아웃   핸드셰이크 10초, 무응답 연결 15초
Reconnect  Auth{sessionToken} 이 60초 내 유효하면 같은 clientId/role. 상태는 위 Bulk 부터 다시. [계획 Phase 11.4 — 지금은 토큰 발급만]
Kick/Ban   Control: Disconnect{reason} 후 연결 종료
```

Phase 9 구현 (ServerHost · ClientSession — Subscribe 부터는 Phase 10 · 11):

```text
거절      버전 · 콘텐츠(팩 목록 + contentHash 를 실어 준다) · 가득 참(maxClients — Transport 는 4 개 더 받아 Reject 로 말한다) ·
          순서(Hello 전에 Auth 등) · nonce · 이름(1 ~ 32 글자, 제어 문자 없음) · 시간 초과(10 초). Reject 뒤 서버가 끊는다.
          buildId 가 다르면 경고만 (프로토콜이 같으면 접속).
콘텐츠    Reject{ContentMismatch} 를 받은 클라이언트는 packs 를 자기 content 루트에서 읽어 해시가 맞으면 다시 접속한다
          (sbx_net_probe). 콘텐츠를 보내 주는 것은 ContentOverlay [계획 Phase 12].
Welcome 뒤 Command → CommandResult, 1 초마다 ServerStats, 서버 종료 시 Disconnect{ServerShutdown}. 그 밖의 메시지 → ProtocolError 로 끊기.
          Phase 10A: Welcome 뒤 Snapshot(15 Hz) · TerrainChunk 를 받고 SnapshotAck 를 보낸다.
역할      Welcome.role = 서버 --default-role (기본 editor). 역할 바꾸기(RoleChanged)는 [계획 Phase 12].
```

---

## 5. 메시지 카탈로그 (`kProtocolVersion = 2`)

| id | 이름           | 방향   | 채널     | 필드                                                                                                                                                                  |
|----|----------------|--------|----------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 1  | Hello          | C→S    | Control  | protocolVersion u32, buildId u64, caps u32                                                                                                                            |
| 2  | Challenge      | S→C    | Control  | nonce u64                                                                                                                                                             |
| 3  | Auth           | C→S    | Control  | token bytes(≤32, [계획 11.4] 지금은 비움), displayName str(32 글자 · 64 바이트, 제어 문자 없음), contentHash u64, nonce u64                                           |
| 4  | Welcome        | S→C    | Control  | clientId u16, role u8, epoch u8, worldMeta{name, 청크 경계 4 × i32, paused, speed}, serverTick u64, tickRate u8, snapshotRate u8, sessionToken bytes(16)              |
| 5  | Reject         | S→C    | Control  | reason u8 (1 버전 · 2 콘텐츠 · 3 가득 참 · 4 핸드셰이크 · 5 이름 · 6 종료 중), detail str(256), serverProtocolVersion u32, contentHash u64, packs[] str(64) × ≤16     |
| 10 | Subscribe      | C→S    | Control  | chunks[] (ChunkCoord varint) `[계획 Phase 11]`                                                                                                                        |
| 11 | Ready          | C→S    | Control  | baselineTick u64 `[계획 — 쓰지 않는다: Welcome 뒤 Snapshot 이 바로 시작, ADR-0025]`                                                                                   |
| 20 | Command        | C→S    | Control  | sequence u32 (1 부터 단조 증가), payload (5.1)                                                                                                                        |
| 21 | CommandResult  | S→C    | Control  | sequence u32, accepted bool, reason u8 (ErrorCode), detail str(256), appliedTick u64 (틱 전 거절이면 0), created[] NetEntityId                                        |
| 30 | Snapshot       | S→C    | Snapshot | snapshotId · epoch · baselineId · serverTick varint, paused, speed f32, complete, entities[], despawns[] NetEntityId (6.2)                                            |
| 31 | SnapshotAck    | C→S    | Snapshot | epoch varint, snapshotId varint (적용한 가장 최근 것)                                                                                                                 |
| 40 | TerrainChunk   | S→C    | Bulk     | x · y zigzag, revision varint, runs[] {count, material} varint (길이 부호화, 머티리얼 번호 kTilesPerChunk 개)                                                         |
| 41 | EntityBaseline | S→C    | Bulk     | baselineTick, entities[] (Spawn 형식) `[계획 — Snapshot(새 epoch) 이 대신한다, ADR-0025]`                                                                             |
| 42 | ContentOverlay | S→C    | Bulk     | rules/behaviors/prefabs JSON (월드 오버레이분) `[계획 Phase 12]`                                                                                                      |
| 50 | ServerStats    | S→C    | Control  | serverTick u64, entities u32, tickMs avg · max f32 (최근 1 초), ticksPerSecond f32, paused, speed f32, clients u8 (1 Hz). [계획] systemTimes[] · pathQueue · jobQueue |
| 60 | Chat           | 양방향 | Control  | text str(256) `[계획 Phase 12]`                                                                                                                                       |
| 61 | RoleChanged    | S→C    | Control  | clientId, role `[계획 Phase 12]`                                                                                                                                      |
| 62 | Disconnect     | 양방향 | Control  | reason u8 (DisconnectReason — Transport 의 disconnect 이유와 같은 값)                                                                                                 |

메시지 헤더: `id: varint`. 필드 인코딩은 [09](09-SERIALIZATION.md) 5장. 메시지를 추가·변경하면 `kProtocolVersion`을 올리고 이 표를 갱신합니다.
(Phase 9 에서 3 · 4 · 5 · 21 · 50 의 필드를 구현에 맞췄다 — 아직 내보낸 적 없는 프로토콜이라 버전은 1 그대로, ADR-0024.)
프로토콜 2 (Phase 10A): Welcome 끝에 replicated[] (복제 컴포넌트 stableId u64 × ≤ 64 — Snapshot 의 컴포넌트 번호 = 이 표의 칸) ·
Snapshot · SnapshotAck · TerrainChunk. Snapshot · TerrainChunk 는 16 MB 까지, 그 밖은 64 KB.
메시지 하나 = Transport 패킷 하나. 예약된 id(`[계획]`)를 받거나, 남는 바이트 · 상한 초과 · 잘못된 열거 값이면 형식 오류 → 연결을
끊는다 (ProtocolError). 구현: `network/protocol/Messages.{hpp,cpp}`.

### 5.1 Command 페이로드 (`network/protocol/CommandCodec`)

```text
종류 태그 u8 (값 고정): 1 CreateEntity · 2 DeleteEntity · 3 MoveEntity · 4 AddComponent · 5 RemoveComponent · 6 ChangeComponent ·
                       7 PaintTerrain · 20 Pause · 21 Resume · 22 Step · 23 SetSimulationSpeed
엔티티 참조  NetEntityId varint          좌표 · 속도  f32 (유한해야 한다)          타일  zigzag varint (i32 범위)
컴포넌트 값 · 패치  JSON 텍스트 (≤ 16 KB, 파싱되어야 한다 — 리플렉션 JsonReader 가 적용)   prefab · material id  str(64)
개수 상한   대상 4096 · 컴포넌트 64 · 칠할 타일 4096 · 브러시 반지름 31 · 한 틱 3600 (03 4.1 · SimulationWorld 상수)
```

---

## 6. Replication

### 6.1 원칙

```text
- 서버는 클라이언트별로 마지막으로 ack 된 스냅샷(baseline) 을 기억한다 (최근 32개 송신 기록 유지).
- 스냅샷 = baseline 이후 변경분: Spawn, Despawn, 변경된 Replicated 컴포넌트, 이번 구간 Event.
- 손실 스냅샷은 재전송하지 않는다. 다음 Delta 가 더 오래된 baseline 에서 계산되어 자연 복구된다.
- baseline 이 32개 송신 기록보다 오래되면 해당 클라이언트에 Bulk EntityBaseline 으로 재동기화.
```

Phase 10A 구현 (`network/replication/ReplicationWriter`, ADR-0025) — 위 원칙을 이렇게 푼다:

```text
"바뀜"      changedAt > 기록의 stamp. stamp = SimulationWorld::changeStamp — tick() 마다 max(이전 + 1, 틱) 로 오르는 순번이라
            일시정지 편집 단계(같은 틱)도 잡힌다 (02 8장). 일시정지가 없으면 틱 번호와 같다.
기록        클라이언트마다 보낸 스냅숏 32 개. 기록 = netId 오름차순 {netId, mask, stamp, pendingSpawn}.
            B = 가장 최근 ack 된 기록, inflight = B 이후 보낸 기록들 (받는 쪽이 무엇을 적용했든 맞도록 합을 본다)
spawn       B 에 없음 (또는 pendingSpawn) → 복제 컴포넌트 전부 + Opaque(render.* JSON 텍스트, spawn 때만)
update      바뀐 컴포넌트 (B 에 없던 것 · changedAt > stamp), 또는 mask 가 B · inflight(OR) 와 다르면 mask 만이라도
despawn     (B ∪ inflight) − 지금. ack 될 때까지 매번
예산        bytesPerSnapshot 를 넘으면 나머지 spawn · update 는 미룬다 (기록은 B 의 것을 이어 받는다 — B 에 없고 inflight 에만
            있으면 pendingSpawn). 시작 위치를 돌려 가며 (round-robin). despawn 은 예산 밖
다시 맞추기 ack 된 기준이 기록 밖으로 밀려났고 epoch 의 첫 스냅숏도 밀려났으면 epoch++ 하고 기준 없이 처음부터 (EntityBaseline 대신)
지형        청크 revision 이 이 클라이언트에 마지막으로 보낸 것과 다르면 TerrainChunk (Bulk · 신뢰 — 보낸 것으로 친다), 스냅숏당 64 청크
Event       [계획] (6.2 events[])
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

Phase 10A 와이어 (`network/protocol/Messages`) — 위 설계와 다른 점: spawns · updates 를 한 목록으로 합쳤고(spawn 비트),
컴포넌트 번호는 stableId 대신 Welcome.replicated 표의 칸 (u8), prefabId · version · ackedCommandSeq · events 는 없다
(명령 결과는 CommandResult 가 따로 준다).

```text
Snapshot {
  snapshotId varint (epoch 안에서 1 부터) · epoch varint · baselineId varint (0 = 없음) · serverTick varint
  paused bool · speed f32 · complete bool (예산 때문에 미룬 엔티티가 없다)
  entities[] { netId varint, spawn bool, mask varint (u64: bit i = 표의 칸 i — 받는 쪽은 mask 에 없는 것을 뗀다),
               components[] { index varint, bytes (길이 + BinaryCodec 바이트, 09 5장 — 모르는 칸은 건너뛴다) },
               opaque str (spawn 일 때만, ≤ 16 KB) }
  despawns[] netId varint
}
상한  엔티티 2^20 · 컴포넌트 64 · 컴포넌트 값 64 KB · 메시지 16 MB
```

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

Phase 10A: Interest 없이 모든 엔티티 (netId 오름차순), 우선순위 대신 round-robin `[계획 Phase 11]`. ServerHost 의 Sim 절반이
Simulation 단계 2 번마다 (일시정지 중에도 — 편집이 보이게) `ReplicationWriter::build` → 메시지를 Net 절반으로. 예산 =
SandboxServer `--snapshot-kbps` (기본 256, 0 = 제한 없음) × 1024 × 간격 ÷ 30.

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

Phase 10A 구현 (`network/client/ClientWorld`, ClientSession 이 쓴다):

```text
버림    epoch 이 작음 · snapshotId ≤ 마지막 적용 (늦은 옛 패킷 — tombstone 대신: 적용이 단조라 despawn 된 엔티티가 되살아나지 않는다)
새 epoch 모두 지우고 시작
적용    despawns → 파괴. entities → 없으면 만든다 (spawn 비트가 없어도 — 방어), mask 에 없는 것을 떼고 받은 값을 쓴다 (멱등)
EntityRef netId → 로컬 EntityId, 모르면 null [계획 — 대기 목록]
표본    core.transform 이 바뀔 때마다 (serverTick, 위치, 회전) 둘 (직전 · 지금) — 보간 [계획 10B]
지형    TerrainChunk → WorldGrid 에 쓰고 로컬 revision 을 올린다. Welcome 보다 먼저 온 것은 모아 두었다가 적용
ack     적용한 스냅숏마다 SnapshotAck{epoch, snapshotId} (비트마스크 없음 — 서버는 가장 최근 ack 만 기준으로 쓴다)
Event   [계획]
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
          Phase 10A: ClientWorld 안의 표 (ClientWorld::find · entities)
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

## 9. 클라이언트 보간 `[계획 Phase 10B]`

Phase 10A 는 표본만 남긴다 (ClientWorld::transformTrack — 직전 · 지금). SnapshotBuffer · renderTick 은 10B.

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
 → Server [Net IO] 역직렬화 + 형식 검증 → CommandValidator (순번 · 속도 제한 · 권한 — 월드를 보지 않는다, ADR-0024)
     거절 → CommandResult{Rejected, appliedTick 0}      통과 → Simulation inbox (배치 swap)
 → [Sim] executeTick = currentTick + 1 → CommandQueue → [Stage 2] 적용 (대상 · 콘텐츠 · 값 검사 — 거절도 CommandResult)
 → ReplayRecorder [계획 — 서버 --record-replay]

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
- BitWriter/Reader 왕복, 경계 초과 시 error                                         ✅ Phase 9 (test_bitstream · test_messages)
- 핸드셰이크: 버전 불일치, contentHash 불일치, 재접속 토큰                          ✅ 재접속 토큰 빼고 (test_server_host)
- 명령: 다음 틱 스탬프 · 결과 · 월드 거절 · 권한 · 속도 제한 · 종료 틱                ✅ Phase 9
- Transport: Loopback · Simulated(지연 · 손실 · 순서, seed) · ENet(실제 UDP, 3 채널)    ✅ Phase 9 (test_transport · test_enet)
- 서버 + 클라이언트 두 프로세스 (SandboxServer + sbx_net_probe, 실제 UDP)           ✅ Phase 9 (CTest net_server_probe_smoke)
- Loopback 수렴: 서버 틱 T 상태 == 클라 복제본 (Replicated 필드, 양자화 오차 허용)   ✅ Phase 10A (net_convergence — 바이트 비교)
- SimulatedTransport(100 ms, 지터 20 ms, 손실 5%, 재정렬) 에서 수렴                   ✅ Phase 10A (+ 64 KB/s 예산)
- Despawn 손실, 늦은 스냅샷 tombstone, EntityRef 대기 목록                           ✅ 대기 목록 빼고 (test_replication)
- Interest: 카메라 이동 시 Spawn/Despawn 수 상한, 히스테리시스
- 대역폭: 50k 월드에서 클라이언트당 바이트가 가시 엔티티 수에 비례 (sbx_bench net.snapshot)
```

---

## 13. 구현 (Phase 9 · 10A)

```text
network/transport/   Transport.hpp (INetworkTransport · Endpoint · DisconnectReason) · LoopbackTransport(+ LoopbackNetwork 허브)
                     · SimulatedTransport · EnetTransport (enet.h 는 이 .cpp 에만)
network/protocol/    BitStream · Messages (카탈로그 · encode/decode · Role · RejectReason) · CommandCodec (5.1)
network/server/      ServerHost (Net 절반 + Sim 절반, Inline | Threaded) · CommandValidator
network/client/      ClientSession (핸드셰이크 · 명령 · 결과 · 통계 · 복제 적용 · ack) · ClientWorld (복제 월드, 6.4)
network/replication/ ReplicationWriter (6.1 · 6.3 — Simulation 스레드)
core/serialization/  BinaryCodec (컴포넌트 값 바이트, 09 5장)
apps/server          SandboxServer --world <시나리오 | 세이브 폴더> … (15-BUILD 7장)
tools/net_probe      sbx_net_probe — 접속 · 명령 · 통계 확인 도구
```

ServerHost 스레드 (01 5장, ADR-0024):

```text
Net IO 스레드     transport.wait(2 ms) → poll → 핸드셰이크 · 검사 → inbox · 결과 송신 · outbox 비우기 → flush
Simulation 스레드 30 TPS × speed 로 틱. inbox → executeTick 스탬프 → ScenarioRunner.step → 네트워크 명령의 결과만 outbox
                  (시나리오가 넣은 명령은 issuer 가 같아도 가리지 않는다 — (issuer, sequence) 집합). 1 초마다 ServerStats.
                  3 틱 넘게 밀리면 기준점을 다시 잡는다 (overruns, 03 3장)
                  단계 2 번마다 ReplicationWriter::build (roster · ack 는 Net 절반이 큐로 넘긴다) → outbox (Phase 10A)
멈추기            Sim 멈춤 → Net 멈춤 → 모두에게 Disconnect{ServerShutdown} + 끊기 → drain(300 ms)
```
