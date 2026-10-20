#pragma once
// 와이어 메시지 카탈로그. docs/08-NETWORK.md 4 · 5장 (표가 정본 — 메시지를 바꾸면 표와 kProtocolVersion 을 같이).
//
// 메시지 = id(varint) + 필드 (BitStream, 09 5장). 메시지 하나 = Transport 패킷 하나.
// Phase 9 구현: Hello · Challenge · Auth · Welcome · Reject · Command · CommandResult · ServerStats · Disconnect.
// Phase 10: Snapshot · SnapshotAck · TerrainChunk (복제). 10B: Inspect · InspectResult (선택한 개체의 서버 전용 상태).
// Phase 11: Subscribe (관심 영역 — 08 8장), Auth.token 으로 다시 접속 (11.4).
// Phase 12C: PlayerList · SetRole · SetRoleResult (접속자 · 역할), ContentOverlay (월드 오버레이), Auth.ownerKey.
// Phase 12D: WorldOp · WorldOpResult · WorldList (세이브 · 로드 · 리플레이 기록 · 재생), WorldChanged (월드가 바뀜 — generation).
// [계획] Ready · EntityBaseline (Snapshot 이 대신한다 — ADR-0025) · Chat
//        — id 는 예약돼 있고, 지금 받으면 "아직 없는 메시지" 로 형식 오류다.
//
// 상한 (11장): 모든 길이 · 개수에 상한이 있다. 넘으면 decodeMessage 가 오류 → 받는 쪽은 연결을 끊는다.

#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "core/command/SimCommand.hpp"
#include "core/project/GameProject.hpp"
#include "network/transport/Transport.hpp"

namespace sbx::net {

// 2: 복제 (Snapshot · SnapshotAck · TerrainChunk, Welcome.replicated) — Phase 10A. 3: Inspect · InspectResult — 10B.
// 4: Subscribe · 다시 접속 토큰 — Phase 11. 5: InspectResult 의 컴포넌트 값 — 12B. 6: EditContent 명령 — 12C.
// 7: 세이브 · 로드 · 리플레이 (WorldOp · WorldList · WorldChanged, Welcome · Snapshot 의 generation, ServerStats 의 모드) — 12D
// 8: 게임 정체성 (Welcome · Reject 의 GameIdentity) — v0.2 M1-T3
// 9: 게임 진행 — 명령 GameControl (시작 · 중단), WorldOp Restart · WorldChanged 의 Restart — v0.2 M2-T3
// 10: 명령 PlayerAction (콘텐츠 행동 — 역할 player 부터) — v0.2 M3-T1
inline constexpr u32 kProtocolVersion = 10;
inline constexpr usize kMaxControlMessageBytes = 64 * 1024; // 03 장 표: Control 메시지 64 KB
inline constexpr usize kMaxDisplayNameBytes = 64;           // UTF-8 바이트 (글자 수 상한은 32)
inline constexpr usize kMaxDisplayNameChars = 32;
inline constexpr usize kMaxDetailBytes = 256;
inline constexpr usize kMaxTokenBytes = 32;
inline constexpr usize kSessionTokenBytes = 16;
inline constexpr usize kMaxWorldNameBytes = 64;
inline constexpr usize kMaxPacks = 16;
inline constexpr usize kMaxPackIdBytes = 64;
inline constexpr usize kMaxGameIdBytes = 64;       // 매니페스트 gameId 와 같다 (18 5.2)
inline constexpr usize kMaxGameNameBytes = 256;    // 매니페스트 name 64 글자의 UTF-8 상한
inline constexpr usize kMaxGameVersionBytes = 128; // SemVer 문자열 (프리릴리스 · 빌드 포함)
inline constexpr usize kMaxStreamMessageBytes =
    16 * 1024 * 1024;                                 // Snapshot · Bulk (조각 — 예산은 ServerHost 가 정한다)
inline constexpr usize kMaxReplicatedComponents = 64; // EntityState.mask 가 u64
inline constexpr usize kMaxSnapshotEntities = 1u << 20;
inline constexpr usize kMaxComponentBytes = 64 * 1024;
inline constexpr usize kMaxOpaqueBytes = 16 * 1024;
inline constexpr usize kMaxInspect = 32;            // 한 번에 볼 수 있는 개체 (선택 상세)
inline constexpr usize kMaxInspectPath = 64;        // 남은 경로 점
inline constexpr usize kMaxStateBytes = 64;         // 행동 상태 id
inline constexpr usize kMaxInspectComponents = 16;  // 선택 상세의 서버 컴포넌트 (12B)
inline constexpr usize kMaxInspectJsonBytes = 4096; // 그 JSON 하나
inline constexpr usize kMaxOwnerKeyBytes = 64;      // Auth.ownerKey (12C)
inline constexpr usize kMaxPlayers = 256;           // PlayerList (12C)
inline constexpr usize kMaxSaveNameBytes = 48;      // 세이브 · 리플레이 이름 (12D — isValidSaveName)
inline constexpr usize kMaxSavedWorlds = 256;       // WorldList

enum class MessageId : u8 {
    Hello = 1,
    Challenge = 2,
    Auth = 3,
    Welcome = 4,
    Reject = 5,
    Subscribe = 10, // Phase 11
    Ready = 11,     // [계획 — Snapshot 이 바로 시작한다, ADR-0025]
    Command = 20,
    CommandResult = 21,
    Snapshot = 30,
    SnapshotAck = 31,
    TerrainChunk = 40,
    EntityBaseline = 41, // [계획 — Snapshot(epoch) 이 대신한다]
    ContentOverlay = 42, // 12C

    ServerStats = 50,
    Inspect = 51,       // 10B
    InspectResult = 52, // 10B
    Chat = 60,          // [계획]
    PlayerList = 61,    // 12C (설계 초안의 RoleChanged 를 대신한다 — 접속 · 나감 · 역할이 모두 이 표로)
    Disconnect = 62,
    SetRole = 63,       // 12C
    SetRoleResult = 64, // 12C
    WorldOp = 65,       // 12D
    WorldOpResult = 66, // 12D
    WorldList = 67,     // 12D
    WorldChanged = 68,  // 12D
};

// 10-EDITOR 7장. 값 순서 = 권한 크기 (Observer < … < Owner)
enum class Role : u8 { Observer = 0, Player = 1, Editor = 2, Admin = 3, Owner = 4 };
[[nodiscard]] std::string_view roleName(Role r) noexcept;
[[nodiscard]] Expected<Role> parseRole(std::string_view text); // "observer" … "owner"

enum class RejectReason : u8 {
    VersionMismatch = 1, // protocolVersion 이 다르다
    ContentMismatch = 2, // contentHash 가 다르다 → packs · contentHash 로 무엇을 읽어야 하는지 알려 준다
    ServerFull = 3,
    BadHandshake = 4, // 순서가 틀렸다 · nonce 가 다르다 · 시간 초과
    InvalidName = 5,  // displayName 이 비었거나 깁니다 · 제어 문자
    ShuttingDown = 6,
};
[[nodiscard]] std::string_view rejectReasonName(RejectReason r) noexcept;
// 이 실행 파일의 buildId (Hello) = fnv1a64("<버전> <git 커밋>"). 서버와 다르면 경고만 한다
[[nodiscard]] u64 localBuildId();
// 표시 이름 규칙: 비지 않고, 32 글자 이하, 제어 문자 없음 (UTF-8 은 BitReader 가 이미 검사)
[[nodiscard]] bool isValidDisplayName(std::string_view name);

struct Hello {
    u32 protocolVersion = kProtocolVersion;
    u64 buildId = 0; // 다르면 경고만 (같은 프로토콜이면 접속은 된다)
    u32 caps = 0;    // 기능 비트 (지금은 0)
};
struct Challenge {
    u64 nonce = 0;
};
struct Auth {
    std::vector<std::byte> token; // 다시 접속 토큰 (Welcome.sessionToken — 11.4). 처음 접속이면 빈 것
    std::string displayName;
    u64 contentHash = 0;
    u64 nonce = 0; // Challenge 의 값을 그대로
    // 프로토콜 6 (12C): 서버의 --owner-key 와 같으면 Owner 로 들어온다. 비면 없음 (다르면 Reject{BadHandshake})
    std::string ownerKey;
};
// 서버가 돌리는 게임 프로젝트의 정체성 (프로토콜 8, v0.2 M1-T3 — 18 5장). 시나리오 · 세이브 폴더면 모두 빈 문자열
struct GameIdentity {
    std::string gameId;  // "studio.arena"
    std::string name;    // 매니페스트 name (WorldMeta.name 은 64 바이트로 잘릴 수 있다 — 이것은 잘리지 않는다)
    std::string version; // SemVer 문자열 "0.1.0"

    [[nodiscard]] bool empty() const noexcept { return gameId.empty(); }
    bool operator==(const GameIdentity&) const = default;
};
// 매니페스트 → 와이어의 게임 정체성 (서버 --world <프로젝트> · LocalServerHost)
[[nodiscard]] GameIdentity gameIdentityOf(const project::GameManifest& manifest);
struct WorldMeta {
    std::string name;
    i32 minChunkX = 0, minChunkY = 0, maxChunkX = 0, maxChunkY = 0; // 양끝 포함
    bool paused = false;
    f32 speed = 1.0f;
};
struct Welcome {
    u16 clientId = 0;
    Role role = Role::Observer;
    u8 epoch = 0; // NetEntityId 세션 epoch (07장) — 지금은 0
    WorldMeta world;
    u64 serverTick = 0;
    u8 tickRate = 30;
    u8 snapshotRate = 15;
    std::vector<std::byte> sessionToken; // kSessionTokenBytes
    // 복제되는 컴포넌트 표 (stableId, 서버 순서). Snapshot 의 컴포넌트 번호 = 이 표의 칸. 클라이언트가 모르는 것은
    // 건너뛴다
    std::vector<u64> replicated;
    u32 generation = 0; // 프로토콜 7: 서버 월드가 바뀐 횟수 (로드 · 리플레이 재생 — WorldChanged)
    GameIdentity game;  // 프로토콜 8
};
struct Reject {
    RejectReason reason = RejectReason::BadHandshake;
    std::string detail;
    u32 serverProtocolVersion = kProtocolVersion;
    u64 contentHash = 0;            // 서버 콘텐츠 (ContentMismatch 일 때 쓸모)
    std::vector<std::string> packs; // 서버가 읽은 콘텐츠 팩 id (비면 내장 콘텐츠)
    GameIdentity game;              // 프로토콜 8: 게임 프로젝트면 그 정체성 (ContentMismatch 때 무엇의 팩인지)
};
struct CommandMsg {
    u32 sequence = 0; // 클라이언트별 1 부터 단조 증가
    cmd::CommandPayload payload;
};
struct CommandResultMsg {
    u32 sequence = 0;
    bool accepted = false;
    ErrorCode reason = ErrorCode::Unknown; // 거절 사유 (accepted 면 무시)
    std::string detail;
    u64 appliedTick = 0;              // 적용된 틱 (거절이면 0 — 형식 · 권한 · 속도 제한은 틱 전에 거절된다)
    std::vector<NetEntityId> created; // CreateEntity 가 만든 엔티티
};
struct ServerStats {
    u64 serverTick = 0;
    u32 entities = 0;
    f32 tickMsAvg = 0; // 최근 1 초
    f32 tickMsMax = 0; // 최근 1 초
    f32 ticksPerSecond = 0;
    bool paused = false;
    f32 speed = 1.0f;
    u8 clients = 0;
    // 프로토콜 7 (12D): 월드 모드 — 0 보통 · 1 리플레이 기록 중 · 2 리플레이 재생 중
    u8 mode = 0;
    std::string modeName;  // 기록 · 재생 중인 이름 (str 48)
    u64 replayCall = 0;    // 기록: 기록한 tick() 수 · 재생: 재생한 수
    u64 replayCalls = 0;   // 재생: 전체
    u32 hashesChecked = 0; // 재생: 비교한 해시
    bool mismatch = false; // 재생: 기록과 다른 해시가 나왔다
    bool replayPaused = false;
    f32 replaySpeed = 1.0f;
};
struct DisconnectMsg {
    DisconnectReason reason = DisconnectReason::None;
};

// --- 복제 (Phase 10, 08 6장 · ADR-0025) ----------------------------------------------------------------------
struct ReplicatedComponent {
    u8 index = 0;          // Welcome.replicated 의 칸
    std::vector<u8> bytes; // 컴포넌트 값 (core/serialization/BinaryCodec — EntityRef 는 NetEntityId)
};
struct EntityState {
    NetEntityId netId = kInvalidNetEntityId;
    bool spawn = false; // 처음 보내거나(받는 쪽에 없을 수 있다) 다시 보냄 — 받는 쪽은 없으면 만든다
    u64 mask = 0;       // 지금 붙어 있는 복제 컴포넌트 (bit i = 표의 칸 i). 받는 쪽은 mask 에 없는 것을 뗀다
    std::vector<ReplicatedComponent> components; // 바뀐 것만 (spawn 이면 전부)
    std::string opaque; // spawn 때만: 서버가 해석하지 않는 컴포넌트 (render.* 등) JSON 텍스트, 없으면 ""
};
struct Snapshot {
    u32 generation = 0; // 프로토콜 7: 이 스냅숏의 서버 월드 (WorldChanged 를 받기 전에 온 새 월드 것은 미뤄 둔다)
    u32 snapshotId = 0; // epoch 안에서 1 부터 단조 증가
    u32 epoch = 0;      // 다시 맞추기(resync)마다 1 증가 — 받는 쪽은 새 epoch 면 모두 지우고 시작
    u32 baselineId = 0; // 이 차분의 기준 (0 = 없음)
    u64 serverTick = 0;
    bool paused = false;
    f32 speed = 1.0f;
    bool complete = true; // 예산 때문에 미룬 엔티티가 없다
    std::vector<EntityState> entities;
    std::vector<NetEntityId> despawns;
};
struct SnapshotAck {
    u32 epoch = 0;
    u32 snapshotId = 0; // 적용한 가장 최근 스냅숏
};
struct TerrainChunk {
    i32 x = 0, y = 0; // 청크 좌표
    u64 revision = 0;
    std::vector<u16>
        materials; // 머티리얼 번호 kTilesPerChunk 개 (와이어는 길이 부호화 — 콘텐츠 해시가 같으니 번호가 같다)
};

// --- 관심 영역 (Phase 11, 08 8장 · ADR-0027) -------------------------------------------------------------------------
// 클라이언트가 보는 청크 사각형 (양끝 포함 — 카메라 + 1 청크 여유). all 이면 월드 전체 (구독 전 기본값과 같다).
// 서버는 월드 경계로 자르고, 그 청크 안의 엔티티 · 지형만 보낸다 (+ 선택한 개체 · 위치 없는 개체). 빠진 청크는 잠시
// 남겨 둔다 (히스테리시스)
struct Subscribe {
    bool all = true;
    i32 minChunkX = 0, minChunkY = 0, maxChunkX = 0, maxChunkY = 0;
    friend bool operator==(const Subscribe&, const Subscribe&) = default;
};

// --- 선택 상세 (Phase 10B, ADR-0026) — 서버에만 있는 컴포넌트(ai.*)를 선택한 개체에 한해 본다 ------------------------
// 클라이언트가 볼 개체 집합을 보내면(빈 목록 = 그만) 서버가 스냅숏마다 InspectResult 를 보낸다 (Snapshot 채널 — 잃어도
// 다음 것이 온다). 읽기 전용이라 모든 역할이 쓸 수 있다.
struct InspectRequest {
    std::vector<NetEntityId> ids; // ≤ kMaxInspect
};
// 복제되지 않는 EditorVisible 컴포넌트의 값 (Phase 12B — Inspector 가 서버 전용 값을 보고 고친다)
struct InspectComponent {
    u64 stableId = 0;
    std::string json; // 리플렉션 JSON (≤ kMaxInspectJsonBytes)
    friend bool operator==(const InspectComponent&, const InspectComponent&) = default;
};
struct InspectEntry {
    NetEntityId netId = kInvalidNetEntityId;
    std::string state;                        // ai.behavior 의 지금 상태 id (없으면 "")
    f32 sensorRadius = 0;                     // ai.sensor (없으면 0)
    std::vector<Vec2> path;                   // 남은 경유점 (따라가는 중일 때)
    std::optional<Vec2> goal;                 // ai.path 의 목표 (따라가는 중 · 대기 중)
    NetEntityId target = kInvalidNetEntityId; // ai.behavior.target (살아 있으면)
    std::vector<InspectComponent> components; // 프로토콜 5 (≤ kMaxInspectComponents, stableId 오름차순)
};
struct InspectResult {
    u64 serverTick = 0;
    std::vector<InspectEntry> entries; // 살아 있는 것만, 요청 순서
};

// --- 접속자 · 역할 (Phase 12C, ADR-0030) -------------------------------------------------------------------------
// 서버가 접속자 표가 바뀔 때마다 (들어옴 · 나감 · 역할) 모두에게 표 전체를 보낸다. 역할 바꾸기는 SetRole → 서버가
// 10-EDITOR 7장 표대로 판단 (Owner = 누구든 무엇이든, Admin = Admin 아래인 사람을 Editor 이하로, 자기 역할은 못 바꾼다)
// → 요청한 사람에게 SetRoleResult, 바뀌었으면 모두에게 PlayerList.
struct PlayerEntry {
    u16 clientId = 0;
    Role role = Role::Observer;
    std::string name;
    friend bool operator==(const PlayerEntry&, const PlayerEntry&) = default;
};
struct PlayerList {
    std::vector<PlayerEntry> players; // clientId 오름차순, ≤ kMaxPlayers
};
struct SetRoleRequest {
    u16 clientId = 0;
    Role role = Role::Observer;
};
struct SetRoleResultMsg {
    u16 clientId = 0;
    Role role = Role::Observer;
    bool accepted = false;
    std::string detail; // 거절 사유
};

// --- 월드 오버레이 (Phase 12C, ADR-0030) -------------------------------------------------------------------------
// 서버 월드의 콘텐츠 오버레이 (EditContent 가 바꾼다). 들어오면 reset 으로 전체, 그 뒤로는 바뀐 항목만 (json 이 "" =
// 지움). Bulk 채널 (신뢰 · 순서), Snapshot 처럼 16 MB 까지.
struct ContentOverlayItem {
    content::ContentKind kind = content::ContentKind::Rule;
    std::string id;
    std::string json; // compact JSON 객체 (≤ content::kMaxOverlayItemBytes), "" = 오버레이에서 뺐다
    friend bool operator==(const ContentOverlayItem&, const ContentOverlayItem&) = default;
};
struct ContentOverlayMsg {
    bool reset = false;                    // 받는 쪽은 지금 오버레이를 비우고 시작
    std::vector<ContentOverlayItem> items; // ≤ content::kMaxOverlayItems
};

// --- 세이브 · 로드 · 리플레이 (Phase 12D, ADR-0031) -----------------------------------------------------------------
// 요청 하나 = WorldOp (이름은 isValidSaveName). 목록은 모든 역할, 나머지는 admin 부터 (10-EDITOR 7장 실행 제어 열).
// 결과는 보낸 사람에게 WorldOpResult (List 는 WorldList). 로드 · 재생이 월드를 바꾸면 모두에게 WorldChanged (Bulk).
enum class WorldOpKind : u8 {
    List = 1,        // 서버 저장 폴더의 세이브 · 리플레이
    Save = 2,        // 지금 월드를 <저장 폴더>/<이름>
    Load = 3,        // 그 세이브로 월드를 바꾼다
    RecordStart = 4, // <이름>/start 에 시작 세이브, 이후 적용된 명령을 기록
    RecordStop = 5,  // <이름>/replay.sbxr 로 쓰고 끝
    Play = 6,        // 리플레이를 재생 (시작 세이브를 불러와 한 call 씩 — 편집 명령은 거절, 실행 제어는 재생을 다룬다)
    Restart = 7,     // 게임 재시작 (프로토콜 9): 게임 프로젝트의 시작 월드를 다시 열고 세션을 Lobby 로 — 이름은 무시
};
[[nodiscard]] std::string_view worldOpName(WorldOpKind op) noexcept;
// 세이브 이름: 영문 · 숫자 · '_' · '-' 1 ~ 48 글자 (경로 문자 · 점 · 공백 없음 — 저장 폴더 아래 한 단계)
[[nodiscard]] bool isValidSaveName(std::string_view name) noexcept;

struct WorldOpRequest {
    WorldOpKind op = WorldOpKind::List;
    std::string name; // List 는 무시
};
struct WorldOpResultMsg {
    WorldOpKind op = WorldOpKind::List;
    std::string name;
    bool accepted = false;
    std::string detail; // 거절 사유 · 결과 요약
};
struct SavedWorld {
    std::string name;
    bool replay = false; // 리플레이 폴더 (재생) — 아니면 세이브 (불러오기)
    u64 tick = 0;        // 세이브의 틱 (리플레이는 시작 틱)
    u64 entities = 0;
    friend bool operator==(const SavedWorld&, const SavedWorld&) = default;
};
struct WorldList {
    std::vector<SavedWorld> worlds; // 이름 순, ≤ kMaxSavedWorlds
};
enum class WorldChangeReason : u8 { Load = 1, Replay = 2, Restart = 3 }; // Restart: 프로토콜 9
struct WorldChanged {
    u32 generation = 0; // 새 월드 (이 번호의 Snapshot 부터 그린다)
    WorldMeta world;    // 새 월드의 이름 · 경계 · 일시정지 · 속도
    WorldChangeReason reason = WorldChangeReason::Load;
    std::string name; // 세이브 · 리플레이 이름
};

using Message = std::variant<Hello, Challenge, Auth, Welcome, Reject, CommandMsg, CommandResultMsg, ServerStats,
                             DisconnectMsg, Snapshot, SnapshotAck, TerrainChunk, InspectRequest, InspectResult,
                             Subscribe, PlayerList, SetRoleRequest, SetRoleResultMsg, ContentOverlayMsg, WorldOpRequest,
                             WorldOpResultMsg, WorldList, WorldChanged>;

[[nodiscard]] MessageId messageId(const Message& m) noexcept;
[[nodiscard]] std::string_view messageName(MessageId id) noexcept;
// 이 메시지가 가는 채널: Snapshot · SnapshotAck · InspectResult = Snapshot, TerrainChunk · ContentOverlay · WorldChanged =
// Bulk, 나머지 = Control
[[nodiscard]] Channel channelOf(const Message& m) noexcept;

[[nodiscard]] std::vector<std::byte> encodeMessage(const Message& m);
// 형식 오류 · 모르는 id · 남는 바이트 · 상한 초과 → ParseError
[[nodiscard]] Expected<Message> decodeMessage(std::span<const std::byte> data);

} // namespace sbx::net
