#include "network/protocol/Messages.hpp"

#include <cmath>
#include <format>
#include <limits>
#include <type_traits>

#include "core/world/ChunkCoord.hpp"
#include "foundation/BuildInfo.hpp"
#include "foundation/hash/Fnv1a.hpp"
#include "foundation/text/Utf8.hpp"
#include "network/protocol/BitStream.hpp"
#include "network/protocol/CommandCodec.hpp"

namespace sbx::net {

std::string_view roleName(Role r) noexcept {
    switch (r) {
    case Role::Observer:
        return "observer";
    case Role::Player:
        return "player";
    case Role::Editor:
        return "editor";
    case Role::Admin:
        return "admin";
    case Role::Owner:
        return "owner";
    }
    return "?";
}

Expected<Role> parseRole(std::string_view text) {
    for (u8 i = 0; i <= static_cast<u8>(Role::Owner); ++i) {
        if (roleName(static_cast<Role>(i)) == text) {
            return static_cast<Role>(i);
        }
    }
    return makeError(ErrorCode::InvalidArgument, "역할은 observer · player · editor · admin · owner 중 하나입니다",
                     std::string(text));
}

std::string_view rejectReasonName(RejectReason r) noexcept {
    switch (r) {
    case RejectReason::VersionMismatch:
        return "프로토콜 버전이 다름";
    case RejectReason::ContentMismatch:
        return "콘텐츠가 다름";
    case RejectReason::ServerFull:
        return "서버가 가득 참";
    case RejectReason::BadHandshake:
        return "핸드셰이크 오류";
    case RejectReason::InvalidName:
        return "이름이 잘못됨";
    case RejectReason::ShuttingDown:
        return "서버 종료 중";
    }
    return "알 수 없음";
}

MessageId messageId(const Message& m) noexcept {
    return std::visit(
        [](const auto& msg) {
            using T = std::decay_t<decltype(msg)>;
            if constexpr (std::is_same_v<T, Hello>) {
                return MessageId::Hello;
            } else if constexpr (std::is_same_v<T, Challenge>) {
                return MessageId::Challenge;
            } else if constexpr (std::is_same_v<T, Auth>) {
                return MessageId::Auth;
            } else if constexpr (std::is_same_v<T, Welcome>) {
                return MessageId::Welcome;
            } else if constexpr (std::is_same_v<T, Reject>) {
                return MessageId::Reject;
            } else if constexpr (std::is_same_v<T, CommandMsg>) {
                return MessageId::Command;
            } else if constexpr (std::is_same_v<T, CommandResultMsg>) {
                return MessageId::CommandResult;
            } else if constexpr (std::is_same_v<T, ServerStats>) {
                return MessageId::ServerStats;
            } else if constexpr (std::is_same_v<T, DisconnectMsg>) {
                return MessageId::Disconnect;
            } else if constexpr (std::is_same_v<T, Snapshot>) {
                return MessageId::Snapshot;
            } else if constexpr (std::is_same_v<T, SnapshotAck>) {
                return MessageId::SnapshotAck;
            } else if constexpr (std::is_same_v<T, InspectRequest>) {
                return MessageId::Inspect;
            } else if constexpr (std::is_same_v<T, InspectResult>) {
                return MessageId::InspectResult;
            } else if constexpr (std::is_same_v<T, Subscribe>) {
                return MessageId::Subscribe;
            } else {
                static_assert(std::is_same_v<T, TerrainChunk>);
                return MessageId::TerrainChunk;
            }
        },
        m);
}

std::string_view messageName(MessageId id) noexcept {
    switch (id) {
    case MessageId::Hello:
        return "Hello";
    case MessageId::Challenge:
        return "Challenge";
    case MessageId::Auth:
        return "Auth";
    case MessageId::Welcome:
        return "Welcome";
    case MessageId::Reject:
        return "Reject";
    case MessageId::Subscribe:
        return "Subscribe";
    case MessageId::Ready:
        return "Ready";
    case MessageId::Command:
        return "Command";
    case MessageId::CommandResult:
        return "CommandResult";
    case MessageId::Snapshot:
        return "Snapshot";
    case MessageId::SnapshotAck:
        return "SnapshotAck";
    case MessageId::TerrainChunk:
        return "TerrainChunk";
    case MessageId::EntityBaseline:
        return "EntityBaseline";
    case MessageId::ContentOverlay:
        return "ContentOverlay";
    case MessageId::ServerStats:
        return "ServerStats";
    case MessageId::Inspect:
        return "Inspect";
    case MessageId::InspectResult:
        return "InspectResult";
    case MessageId::Chat:
        return "Chat";
    case MessageId::RoleChanged:
        return "RoleChanged";
    case MessageId::Disconnect:
        return "Disconnect";
    }
    return "?";
}

Channel channelOf(const Message& m) noexcept {
    if (std::holds_alternative<Snapshot>(m) || std::holds_alternative<SnapshotAck>(m) ||
        std::holds_alternative<InspectResult>(m)) {
        return Channel::Snapshot;
    }
    if (std::holds_alternative<TerrainChunk>(m)) {
        return Channel::Bulk;
    }
    return Channel::Control;
}

namespace {

void writeBytesFixed(BitWriter& w, std::span<const std::byte> b) {
    w.writeBytes(b);
}

} // namespace

u64 localBuildId() {
    static const u64 id = fnv1a64(std::format("{} {}", build::kVersionString, build::kGitCommit));
    return id;
}

bool isValidDisplayName(std::string_view s) {
    if (s.empty() || utf8::length(s) > kMaxDisplayNameChars) {
        return false;
    }
    usize pos = 0;
    while (pos < s.size()) {
        const char32_t cp = utf8::decodeNext(s, pos);
        if (cp < 0x20 || cp == 0x7F) {
            return false;
        }
    }
    return true;
}

namespace {

f32 readFiniteF32(BitReader& r) {
    const f32 v = r.readF32();
    if (!std::isfinite(v)) {
        r.fail();
        return 0;
    }
    return v;
}

i32 readI32(BitReader& r) {
    const i64 v = r.readVarI();
    if (v < std::numeric_limits<i32>::min() || v > std::numeric_limits<i32>::max()) {
        r.fail();
        return 0;
    }
    return static_cast<i32>(v);
}

void encodeBody(BitWriter& w, const Hello& m) {
    w.writeVarU(m.protocolVersion);
    w.writeU64(m.buildId);
    w.writeVarU(m.caps);
}
void encodeBody(BitWriter& w, const Challenge& m) {
    w.writeU64(m.nonce);
}
void encodeBody(BitWriter& w, const Auth& m) {
    writeBytesFixed(w, m.token);
    w.writeString(m.displayName);
    w.writeU64(m.contentHash);
    w.writeU64(m.nonce);
}
void encodeBody(BitWriter& w, const Welcome& m) {
    w.writeVarU(m.clientId);
    w.writeU8(static_cast<u8>(m.role));
    w.writeU8(m.epoch);
    w.writeString(m.world.name);
    w.writeVarI(m.world.minChunkX);
    w.writeVarI(m.world.minChunkY);
    w.writeVarI(m.world.maxChunkX);
    w.writeVarI(m.world.maxChunkY);
    w.writeBool(m.world.paused);
    w.writeF32(m.world.speed);
    w.writeVarU(m.serverTick);
    w.writeU8(m.tickRate);
    w.writeU8(m.snapshotRate);
    writeBytesFixed(w, m.sessionToken);
    w.writeVarU(m.replicated.size());
    for (const u64 id : m.replicated) {
        w.writeU64(id);
    }
}
void encodeBody(BitWriter& w, const Reject& m) {
    w.writeU8(static_cast<u8>(m.reason));
    w.writeString(m.detail);
    w.writeVarU(m.serverProtocolVersion);
    w.writeU64(m.contentHash);
    w.writeVarU(m.packs.size());
    for (const auto& p : m.packs) {
        w.writeString(p);
    }
}
void encodeBody(BitWriter& w, const CommandMsg& m) {
    w.writeVarU(m.sequence);
    writePayload(w, m.payload);
}
void encodeBody(BitWriter& w, const CommandResultMsg& m) {
    w.writeVarU(m.sequence);
    w.writeBool(m.accepted);
    w.writeU8(static_cast<u8>(m.reason));
    w.writeString(m.detail);
    w.writeVarU(m.appliedTick);
    w.writeVarU(m.created.size());
    for (const NetEntityId id : m.created) {
        w.writeVarU(id);
    }
}
void encodeBody(BitWriter& w, const ServerStats& m) {
    w.writeVarU(m.serverTick);
    w.writeVarU(m.entities);
    w.writeF32(m.tickMsAvg);
    w.writeF32(m.tickMsMax);
    w.writeF32(m.ticksPerSecond);
    w.writeBool(m.paused);
    w.writeF32(m.speed);
    w.writeU8(m.clients);
}
void encodeBody(BitWriter& w, const DisconnectMsg& m) {
    w.writeU8(static_cast<u8>(m.reason));
}
void encodeBody(BitWriter& w, const Snapshot& m) {
    w.writeVarU(m.snapshotId);
    w.writeVarU(m.epoch);
    w.writeVarU(m.baselineId);
    w.writeVarU(m.serverTick);
    w.writeBool(m.paused);
    w.writeF32(m.speed);
    w.writeBool(m.complete);
    w.writeVarU(m.entities.size());
    for (const EntityState& e : m.entities) {
        w.writeVarU(e.netId);
        w.writeBool(e.spawn);
        w.writeVarU(e.mask);
        w.writeVarU(e.components.size());
        for (const ReplicatedComponent& c : e.components) {
            w.writeVarU(c.index);
            w.writeVarU(c.bytes.size());
            for (const u8 b : c.bytes) {
                w.writeU8(b);
            }
        }
        if (e.spawn) {
            w.writeString(e.opaque);
        }
    }
    w.writeVarU(m.despawns.size());
    for (const NetEntityId id : m.despawns) {
        w.writeVarU(id);
    }
}
void encodeBody(BitWriter& w, const SnapshotAck& m) {
    w.writeVarU(m.epoch);
    w.writeVarU(m.snapshotId);
}
void encodeBody(BitWriter& w, const TerrainChunk& m) {
    w.writeVarI(m.x);
    w.writeVarI(m.y);
    w.writeVarU(m.revision);
    // 길이 부호화 (run, 머티리얼) — 한 머티리얼로 채운 청크는 몇 바이트
    std::vector<std::pair<u64, u16>> runs;
    for (const u16 v : m.materials) {
        if (!runs.empty() && runs.back().second == v) {
            ++runs.back().first;
        } else {
            runs.emplace_back(1, v);
        }
    }
    w.writeVarU(runs.size());
    for (const auto& [n, v] : runs) {
        w.writeVarU(n);
        w.writeVarU(v);
    }
}

void encodeBody(BitWriter& w, const Subscribe& m) {
    w.writeBool(m.all);
    if (!m.all) {
        w.writeVarI(m.minChunkX);
        w.writeVarI(m.minChunkY);
        w.writeVarI(m.maxChunkX);
        w.writeVarI(m.maxChunkY);
    }
}
void encodeBody(BitWriter& w, const InspectRequest& m) {
    w.writeVarU(m.ids.size());
    for (const NetEntityId id : m.ids) {
        w.writeVarU(id);
    }
}
void encodeBody(BitWriter& w, const InspectResult& m) {
    w.writeVarU(m.serverTick);
    w.writeVarU(m.entries.size());
    for (const InspectEntry& e : m.entries) {
        w.writeVarU(e.netId);
        w.writeString(e.state);
        w.writeF32(e.sensorRadius);
        w.writeVarU(e.path.size());
        for (const Vec2 p : e.path) {
            w.writeF32(p.x);
            w.writeF32(p.y);
        }
        w.writeBool(e.goal.has_value());
        if (e.goal) {
            w.writeF32(e.goal->x);
            w.writeF32(e.goal->y);
        }
        w.writeVarU(e.target);
    }
}

Hello decodeHello(BitReader& r) {
    Hello m;
    m.protocolVersion = static_cast<u32>(r.readVarU(0xFFFF'FFFFull));
    m.buildId = r.readU64();
    m.caps = static_cast<u32>(r.readVarU(0xFFFF'FFFFull));
    return m;
}
Challenge decodeChallenge(BitReader& r) {
    return Challenge{r.readU64()};
}
Auth decodeAuth(BitReader& r) {
    Auth m;
    m.token = r.readBytes(kMaxTokenBytes);
    m.displayName = r.readString(kMaxDisplayNameBytes);
    m.contentHash = r.readU64();
    m.nonce = r.readU64();
    return m;
}
Welcome decodeWelcome(BitReader& r) {
    Welcome m;
    m.clientId = static_cast<u16>(r.readVarU(0xFFFF));
    const u8 role = r.readU8();
    if (role > static_cast<u8>(Role::Owner)) {
        r.fail();
    }
    m.role = static_cast<Role>(role);
    m.epoch = r.readU8();
    m.world.name = r.readString(kMaxWorldNameBytes);
    m.world.minChunkX = readI32(r);
    m.world.minChunkY = readI32(r);
    m.world.maxChunkX = readI32(r);
    m.world.maxChunkY = readI32(r);
    m.world.paused = r.readBool();
    m.world.speed = readFiniteF32(r);
    m.serverTick = r.readVarU();
    m.tickRate = r.readU8();
    m.snapshotRate = r.readU8();
    m.sessionToken = r.readBytes(kMaxTokenBytes);
    const u64 n = r.readVarU(kMaxReplicatedComponents);
    for (u64 i = 0; i < n && !r.error(); ++i) {
        m.replicated.push_back(r.readU64());
    }
    return m;
}
Reject decodeReject(BitReader& r) {
    Reject m;
    const u8 reason = r.readU8();
    if (reason < static_cast<u8>(RejectReason::VersionMismatch) ||
        reason > static_cast<u8>(RejectReason::ShuttingDown)) {
        r.fail();
    }
    m.reason = static_cast<RejectReason>(reason);
    m.detail = r.readString(kMaxDetailBytes);
    m.serverProtocolVersion = static_cast<u32>(r.readVarU(0xFFFF'FFFFull));
    m.contentHash = r.readU64();
    const u64 n = r.readVarU(kMaxPacks);
    for (u64 i = 0; i < n && !r.error(); ++i) {
        m.packs.push_back(r.readString(kMaxPackIdBytes));
    }
    return m;
}
CommandMsg decodeCommand(BitReader& r) {
    CommandMsg m;
    m.sequence = static_cast<u32>(r.readVarU(0xFFFF'FFFFull));
    if (!r.error()) {
        (void)readPayload(r, m.payload);
    }
    return m;
}
CommandResultMsg decodeCommandResult(BitReader& r) {
    CommandResultMsg m;
    m.sequence = static_cast<u32>(r.readVarU(0xFFFF'FFFFull));
    m.accepted = r.readBool();
    const u8 reason = r.readU8();
    if (reason > static_cast<u8>(kLastErrorCode)) {
        r.fail();
    }
    m.reason = static_cast<ErrorCode>(reason);
    m.detail = r.readString(kMaxDetailBytes);
    m.appliedTick = r.readVarU();
    const u64 n = r.readVarU(4096);
    if (r.remainingBits() < n * 8) {
        r.fail();
    }
    for (u64 i = 0; i < n && !r.error(); ++i) {
        m.created.push_back(static_cast<NetEntityId>(r.readVarU(0xFFFF'FFFFull)));
    }
    return m;
}
ServerStats decodeServerStats(BitReader& r) {
    ServerStats m;
    m.serverTick = r.readVarU();
    m.entities = static_cast<u32>(r.readVarU(0xFFFF'FFFFull));
    m.tickMsAvg = readFiniteF32(r);
    m.tickMsMax = readFiniteF32(r);
    m.ticksPerSecond = readFiniteF32(r);
    m.paused = r.readBool();
    m.speed = readFiniteF32(r);
    m.clients = r.readU8();
    return m;
}
Snapshot decodeSnapshot(BitReader& r) {
    Snapshot m;
    m.snapshotId = static_cast<u32>(r.readVarU(0xFFFF'FFFFull));
    m.epoch = static_cast<u32>(r.readVarU(0xFFFF'FFFFull));
    m.baselineId = static_cast<u32>(r.readVarU(0xFFFF'FFFFull));
    m.serverTick = r.readVarU();
    m.paused = r.readBool();
    m.speed = readFiniteF32(r);
    m.complete = r.readBool();
    const u64 n = r.readVarU(kMaxSnapshotEntities);
    if (r.remainingBits() < n * 8) { // 엔티티마다 최소 몇 바이트 — 큰 개수로 메모리를 먼저 잡지 않게
        r.fail();
    }
    m.entities.reserve(r.error() ? 0 : static_cast<usize>(n));
    for (u64 i = 0; i < n && !r.error(); ++i) {
        EntityState e;
        e.netId = static_cast<NetEntityId>(r.readVarU(0xFFFF'FFFFull));
        e.spawn = r.readBool();
        e.mask = r.readVarU();
        const u64 nc = r.readVarU(kMaxReplicatedComponents);
        for (u64 c = 0; c < nc && !r.error(); ++c) {
            ReplicatedComponent rc;
            rc.index = static_cast<u8>(r.readVarU(kMaxReplicatedComponents - 1));
            const u64 len = r.readVarU(kMaxComponentBytes);
            if (r.remainingBits() < len * 8) {
                r.fail();
                break;
            }
            rc.bytes.resize(static_cast<usize>(len));
            for (auto& b : rc.bytes) {
                b = r.readU8();
            }
            e.components.push_back(std::move(rc));
        }
        if (e.spawn) {
            e.opaque = r.readString(kMaxOpaqueBytes);
        }
        m.entities.push_back(std::move(e));
    }
    const u64 nd = r.readVarU(kMaxSnapshotEntities);
    if (r.remainingBits() < nd * 8) {
        r.fail();
    }
    for (u64 i = 0; i < nd && !r.error(); ++i) {
        m.despawns.push_back(static_cast<NetEntityId>(r.readVarU(0xFFFF'FFFFull)));
    }
    return m;
}
SnapshotAck decodeSnapshotAck(BitReader& r) {
    SnapshotAck m;
    m.epoch = static_cast<u32>(r.readVarU(0xFFFF'FFFFull));
    m.snapshotId = static_cast<u32>(r.readVarU(0xFFFF'FFFFull));
    return m;
}
TerrainChunk decodeTerrainChunk(BitReader& r) {
    TerrainChunk m;
    m.x = readI32(r);
    m.y = readI32(r);
    m.revision = r.readVarU();
    const u64 runs = r.readVarU(static_cast<u64>(world::kTilesPerChunk));
    m.materials.reserve(static_cast<usize>(world::kTilesPerChunk));
    for (u64 i = 0; i < runs && !r.error(); ++i) {
        const u64 n = r.readVarU(static_cast<u64>(world::kTilesPerChunk));
        const u16 v = static_cast<u16>(r.readVarU(0xFFFF));
        if (n == 0 || m.materials.size() + n > static_cast<usize>(world::kTilesPerChunk)) {
            r.fail();
            break;
        }
        m.materials.insert(m.materials.end(), static_cast<usize>(n), v);
    }
    if (m.materials.size() != static_cast<usize>(world::kTilesPerChunk)) {
        r.fail();
    }
    return m;
}
Subscribe decodeSubscribe(BitReader& r) {
    Subscribe m;
    m.all = r.readBool();
    if (!m.all) {
        m.minChunkX = readI32(r);
        m.minChunkY = readI32(r);
        m.maxChunkX = readI32(r);
        m.maxChunkY = readI32(r);
        // 뒤집힌 사각형 · 월드 최대 크기(축마다 64 청크)보다 큰 것은 형식 오류
        const i64 w = i64{m.maxChunkX} - m.minChunkX;
        const i64 h = i64{m.maxChunkY} - m.minChunkY;
        if (w < 0 || h < 0 || w >= world::kMaxWorldChunks * 2 || h >= world::kMaxWorldChunks * 2) {
            r.fail();
        }
    }
    return m;
}
InspectRequest decodeInspect(BitReader& r) {
    InspectRequest m;
    const u64 n = r.readVarU(kMaxInspect);
    for (u64 i = 0; i < n && !r.error(); ++i) {
        m.ids.push_back(static_cast<NetEntityId>(r.readVarU(0xFFFF'FFFFull)));
    }
    return m;
}
InspectResult decodeInspectResult(BitReader& r) {
    InspectResult m;
    m.serverTick = r.readVarU();
    const u64 n = r.readVarU(kMaxInspect);
    for (u64 i = 0; i < n && !r.error(); ++i) {
        InspectEntry e;
        e.netId = static_cast<NetEntityId>(r.readVarU(0xFFFF'FFFFull));
        e.state = r.readString(kMaxStateBytes);
        e.sensorRadius = readFiniteF32(r);
        const u64 np = r.readVarU(kMaxInspectPath);
        for (u64 k = 0; k < np && !r.error(); ++k) {
            const f32 x = readFiniteF32(r);
            const f32 y = readFiniteF32(r);
            e.path.push_back({x, y});
        }
        if (r.readBool()) {
            const f32 x = readFiniteF32(r);
            const f32 y = readFiniteF32(r);
            e.goal = Vec2{x, y};
        }
        e.target = static_cast<NetEntityId>(r.readVarU(0xFFFF'FFFFull));
        m.entries.push_back(std::move(e));
    }
    return m;
}
DisconnectMsg decodeDisconnect(BitReader& r) {

    const u8 reason = r.readU8();
    if (reason > static_cast<u8>(DisconnectReason::ConnectFailed)) {
        r.fail();
    }
    return DisconnectMsg{static_cast<DisconnectReason>(reason)};
}

} // namespace

std::vector<std::byte> encodeMessage(const Message& m) {
    BitWriter w;
    w.writeVarU(static_cast<u8>(messageId(m)));
    std::visit([&](const auto& msg) { encodeBody(w, msg); }, m);
    return std::move(w).take();
}

Expected<Message> decodeMessage(std::span<const std::byte> data) {
    BitReader r(data);
    const u64 rawId = r.readVarU(255);
    if (r.error()) {
        return makeError(ErrorCode::ParseError, "메시지 id 를 읽지 못했습니다");
    }
    const auto id = static_cast<MessageId>(rawId);
    const bool stream = id == MessageId::Snapshot || id == MessageId::TerrainChunk;
    if (data.size() > (stream ? kMaxStreamMessageBytes : kMaxControlMessageBytes)) {
        return makeError(ErrorCode::ParseError, std::format("메시지가 너무 큽니다 ({} 바이트)", data.size()));
    }
    Message out;
    switch (id) {
    case MessageId::Hello:
        out = decodeHello(r);
        break;
    case MessageId::Challenge:
        out = decodeChallenge(r);
        break;
    case MessageId::Auth:
        out = decodeAuth(r);
        break; // 이름 규칙은 거절 사유 (InvalidName) — 서버가 본다
    case MessageId::Welcome:
        out = decodeWelcome(r);
        break;
    case MessageId::Reject:
        out = decodeReject(r);
        break;
    case MessageId::Command:
        out = decodeCommand(r);
        break;
    case MessageId::CommandResult:
        out = decodeCommandResult(r);
        break;
    case MessageId::ServerStats:
        out = decodeServerStats(r);
        break;
    case MessageId::Snapshot:
        out = decodeSnapshot(r);
        break;
    case MessageId::SnapshotAck:
        out = decodeSnapshotAck(r);
        break;
    case MessageId::TerrainChunk:
        out = decodeTerrainChunk(r);
        break;
    case MessageId::Inspect:
        out = decodeInspect(r);
        break;
    case MessageId::Subscribe:
        out = decodeSubscribe(r);
        break;
    case MessageId::InspectResult:
        out = decodeInspectResult(r);
        break;
    case MessageId::Disconnect:
        out = decodeDisconnect(r);
        break;
    default:
        return makeError(ErrorCode::ParseError, std::format("아직 없는 메시지 {} ({})", rawId, messageName(id)));
    }
    if (r.error()) {
        return makeError(ErrorCode::ParseError, std::format("{} 메시지 형식 오류", messageName(id)));
    }
    if (!r.atEnd()) {
        return makeError(ErrorCode::ParseError, std::format("{} 메시지 뒤에 남는 바이트", messageName(id)));
    }
    return out;
}

} // namespace sbx::net
