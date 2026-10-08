#pragma once
// 와이어 메시지 카탈로그. docs/08-NETWORK.md 4 · 5장 (표가 정본 — 메시지를 바꾸면 표와 kProtocolVersion 을 같이).
//
// 메시지 = id(varint) + 필드 (BitStream, 09 5장). 메시지 하나 = Transport 패킷 하나.
// Phase 9 구현: Hello · Challenge · Auth · Welcome · Reject · Command · CommandResult · ServerStats · Disconnect.
// Phase 10: Snapshot · SnapshotAck · TerrainChunk (복제).
// [계획] Subscribe (11) · Ready · EntityBaseline (Snapshot 이 대신한다 — ADR-0025) · ContentOverlay · Chat ·
// RoleChanged (12)
//        — id 는 예약돼 있고, 지금 받으면 "아직 없는 메시지" 로 형식 오류다.
//
// 상한 (11장): 모든 길이 · 개수에 상한이 있다. 넘으면 decodeMessage 가 오류 → 받는 쪽은 연결을 끊는다.

#include <span>
#include <string>
#include <variant>
#include <vector>

#include "core/command/SimCommand.hpp"
#include "network/transport/Transport.hpp"

namespace sbx::net {

inline constexpr u32 kProtocolVersion =
    2; // 2: 복제 (Snapshot · SnapshotAck · TerrainChunk, Welcome.replicated) — Phase 10
inline constexpr usize kMaxControlMessageBytes = 64 * 1024; // 03 장 표: Control 메시지 64 KB
inline constexpr usize kMaxDisplayNameBytes = 64;           // UTF-8 바이트 (글자 수 상한은 32)
inline constexpr usize kMaxDisplayNameChars = 32;
inline constexpr usize kMaxDetailBytes = 256;
inline constexpr usize kMaxTokenBytes = 32;
inline constexpr usize kSessionTokenBytes = 16;
inline constexpr usize kMaxWorldNameBytes = 64;
inline constexpr usize kMaxPacks = 16;
inline constexpr usize kMaxPackIdBytes = 64;
inline constexpr usize kMaxStreamMessageBytes =
    16 * 1024 * 1024;                                 // Snapshot · Bulk (조각 — 예산은 ServerHost 가 정한다)
inline constexpr usize kMaxReplicatedComponents = 64; // EntityState.mask 가 u64
inline constexpr usize kMaxSnapshotEntities = 1u << 20;
inline constexpr usize kMaxComponentBytes = 64 * 1024;
inline constexpr usize kMaxOpaqueBytes = 16 * 1024;

enum class MessageId : u8 {
    Hello = 1,
    Challenge = 2,
    Auth = 3,
    Welcome = 4,
    Reject = 5,
    Subscribe = 10, // [계획 Phase 11]
    Ready = 11,     // [계획 — Snapshot 이 바로 시작한다, ADR-0025]
    Command = 20,
    CommandResult = 21,
    Snapshot = 30,
    SnapshotAck = 31,
    TerrainChunk = 40,
    EntityBaseline = 41, // [계획 — Snapshot(epoch) 이 대신한다]
    ContentOverlay = 42, // [계획 Phase 12]
    ServerStats = 50,
    Chat = 60,        // [계획 Phase 12]
    RoleChanged = 61, // [계획 Phase 12]
    Disconnect = 62,
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
    std::vector<std::byte> token; // 재접속 토큰 [계획 Phase 11.4 — 지금은 비운다]
    std::string displayName;
    u64 contentHash = 0;
    u64 nonce = 0; // Challenge 의 값을 그대로
};
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
};
struct Reject {
    RejectReason reason = RejectReason::BadHandshake;
    std::string detail;
    u32 serverProtocolVersion = kProtocolVersion;
    u64 contentHash = 0;            // 서버 콘텐츠 (ContentMismatch 일 때 쓸모)
    std::vector<std::string> packs; // 서버가 읽은 콘텐츠 팩 id (비면 내장 콘텐츠)
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

using Message = std::variant<Hello, Challenge, Auth, Welcome, Reject, CommandMsg, CommandResultMsg, ServerStats,
                             DisconnectMsg, Snapshot, SnapshotAck, TerrainChunk>;

[[nodiscard]] MessageId messageId(const Message& m) noexcept;
[[nodiscard]] std::string_view messageName(MessageId id) noexcept;
// 이 메시지가 가는 채널: Snapshot · SnapshotAck = Snapshot, TerrainChunk = Bulk, 나머지 = Control
[[nodiscard]] Channel channelOf(const Message& m) noexcept;

[[nodiscard]] std::vector<std::byte> encodeMessage(const Message& m);
// 형식 오류 · 모르는 id · 남는 바이트 · 상한 초과 → ParseError
[[nodiscard]] Expected<Message> decodeMessage(std::span<const std::byte> data);

} // namespace sbx::net
