#include "core/persist/ChunkFile.hpp"

#include <format>

namespace sbx::persist {
namespace {

void putU8(std::string& out, u8 v) {
    out.push_back(static_cast<char>(v));
}
void putU16(std::string& out, u16 v) {
    putU8(out, static_cast<u8>(v & 0xFFu));
    putU8(out, static_cast<u8>(v >> 8));
}
void putU32(std::string& out, u32 v) {
    for (int i = 0; i < 4; ++i) {
        putU8(out, static_cast<u8>(v >> (8 * i)));
    }
}
void putU64(std::string& out, u64 v) {
    for (int i = 0; i < 8; ++i) {
        putU8(out, static_cast<u8>(v >> (8 * i)));
    }
}

struct Reader {
    std::string_view bytes;
    usize pos = 0;
    u8 u8v() { return static_cast<u8>(bytes[pos++]); }
    u16 u16v() {
        const u16 lo = u8v();
        const u16 hi = u8v();
        return static_cast<u16>(lo | static_cast<u16>(hi << 8));
    }
    u32 u32v() {
        u32 v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= static_cast<u32>(u8v()) << (8 * i);
        }
        return v;
    }
    u64 u64v() {
        u64 v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<u64>(u8v()) << (8 * i);
        }
        return v;
    }
};

} // namespace

std::string chunkFileName(world::ChunkCoord c) {
    return std::format("{}_{}.chunk", c.x, c.y);
}

std::string encodeChunk(const world::Chunk& chunk) {
    std::string out;
    out.reserve(kChunkFileSize);
    out += "SBXC";
    putU16(out, kChunkFileVersion);
    putU32(out, static_cast<u32>(chunk.coord().x));
    putU32(out, static_cast<u32>(chunk.coord().y));
    putU64(out, chunk.terrainRevision());
    putU8(out, kAllLayers);
    putU8(out, 0); // 무압축
    const world::TerrainLayers& L = chunk.layers();
    for (const auto m : L.material) {
        putU16(out, m);
    }
    for (const auto f : L.flags) {
        putU8(out, f);
    }
    for (const auto c : L.moveCost) {
        putU8(out, c);
    }
    for (const auto h : L.height) {
        putU16(out, static_cast<u16>(h));
    }
    return out;
}

Expected<DecodedChunk> decodeChunk(std::string_view bytes, std::string_view context) {
    const std::string ctx(context);
    if (bytes.size() < kChunkHeaderSize || bytes.substr(0, 4) != "SBXC") {
        return makeError(ErrorCode::ParseError, "청크 파일이 아니다 (magic)", ctx);
    }
    Reader r{bytes, 4};
    const u16 version = r.u16v();
    if (version != kChunkFileVersion) {
        return makeError(ErrorCode::VersionMismatch,
                         std::format("청크 파일 버전 {} (지원: {})", version, kChunkFileVersion), ctx);
    }
    DecodedChunk d;
    d.coord.x = static_cast<i32>(r.u32v());
    d.coord.y = static_cast<i32>(r.u32v());
    d.revision = r.u64v();
    const u8 mask = r.u8v();
    const u8 compression = r.u8v();
    if (compression != 0) {
        return makeError(ErrorCode::Unsupported, std::format("압축 방식 {} 은 아직 지원하지 않는다", compression), ctx);
    }
    if (mask != kAllLayers) {
        return makeError(ErrorCode::Unsupported, std::format("레이어 마스크 0x{:02x} (지원: 0x0f)", mask), ctx);
    }
    if (bytes.size() != kChunkFileSize) {
        return makeError(ErrorCode::ParseError, std::format("크기 {} (기대 {})", bytes.size(), kChunkFileSize), ctx);
    }
    for (auto& m : d.layers.material) {
        m = r.u16v();
    }
    for (auto& f : d.layers.flags) {
        f = r.u8v();
    }
    for (auto& c : d.layers.moveCost) {
        c = r.u8v();
    }
    for (auto& h : d.layers.height) {
        h = static_cast<i16>(r.u16v());
    }
    return d;
}

} // namespace sbx::persist
