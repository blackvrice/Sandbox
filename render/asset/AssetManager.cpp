#include "render/asset/AssetManager.hpp"

#include <algorithm>
#include <cstring>
#include <format>

#include "foundation/log/Log.hpp"

namespace sbx::render {

AssetManager::AssetManager(rhi::IRenderDevice& device, JobSystem* jobs, AssetManagerDesc desc)
    : m_dev(device), m_jobs(jobs), m_desc(std::move(desc)) {}

AssetManager::~AssetManager() {
    m_group.wait(); // Worker 가 m_done 에 쓰기 전에 사라지지 않게
    m_dev.destroy(m_atlas);
}

Expected<void> AssetManager::init() {
    rhi::TextureDesc td;
    td.width = m_desc.atlasSize;
    td.height = m_desc.atlasSize;
    td.layers = std::max(m_desc.atlasPages, 1u);
    td.format = rhi::Format::RGBA8Unorm;
    td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
    td.debugName = "sprite atlas";
    m_atlas = m_dev.createTexture(td);
    if (!m_atlas.valid()) {
        return makeError(ErrorCode::Unsupported,
                         std::format("스프라이트 아틀라스 {}²×{} 를 만들 수 없습니다", m_desc.atlasSize, td.layers));
    }
    m_pages.assign(td.layers, ShelfPacker(m_desc.atlasSize, m_desc.atlasSize));

    // 0번 = 흰색 (색만 있는 사각형 · 준비 전 자리). 마젠타 = 실패 자리 (id 없음)
    const SpriteId white = addSprite("<white>", Image::filled(4, 4, 255, 255, 255));
    m_white = m_entries[white].final;
    const SpriteId magenta = newEntry("<magenta>");
    place(magenta, Image::filled(4, 4, 255, 0, 255));
    m_magenta = m_entries[magenta].final;
    for (SpriteRegion& r : m_regions) {
        r = m_white;
    }
    m_regions[magenta] = m_magenta;
    return {};
}

SpriteId AssetManager::newEntry(std::string name) {
    const auto id = static_cast<SpriteId>(m_entries.size());
    m_entries.push_back({std::move(name), AssetState::Queued, {}});
    m_regions.push_back(m_white);
    ++m_stats.sprites;
    ++m_stats.queued;
    return id;
}

SpriteId AssetManager::requestSprite(std::string_view path) {
    std::string norm = normalizeAssetPath(path);
    const AssetId aid = assetIdOf(norm);
    if (const auto it = m_byId.find(aid); it != m_byId.end()) {
        return it->second;
    }
    const SpriteId id = newEntry(norm);
    m_byId.emplace(aid, id);
    if (!m_atlas.valid()) {
        fail(id, "아틀라스가 없다");
        return id;
    }
    const std::filesystem::path file = m_desc.root / std::filesystem::path(norm);
    if (m_jobs == nullptr) {
        m_done.push_back({id, loadPng(file)});
        return id;
    }
    m_jobs->submit(m_group, [this, id, file] {
        Expected<Image> img = loadPng(file); // Worker: 파일 읽기 + 디코드만
        const std::scoped_lock lock(m_doneMutex);
        m_done.push_back({id, std::move(img)});
    });
    return id;
}

SpriteId AssetManager::addSprite(std::string_view name, Image image) {
    const AssetId aid = assetIdOf(std::string("mem:") + std::string(name));
    if (const auto it = m_byId.find(aid); it != m_byId.end()) {
        return it->second;
    }
    const SpriteId id = newEntry(std::string(name));
    m_byId.emplace(aid, id);
    if (m_pages.empty()) {
        fail(id, "아틀라스가 없다");
        return id;
    }
    place(id, std::move(image));
    return id;
}

void AssetManager::waitDecodes() {
    m_group.wait();
}

AssetState AssetManager::state(SpriteId id) const noexcept {
    return id < m_entries.size() ? m_entries[id].state : AssetState::Failed;
}

Image AssetManager::addPadding(const Image& src, u32 pad) {
    Image out;
    out.width = src.width + pad * 2;
    out.height = src.height + pad * 2;
    out.rgba.resize(static_cast<usize>(out.width) * out.height * 4);
    for (u32 y = 0; y < out.height; ++y) {
        // 테두리는 가장 가까운 가장자리 텍셀을 늘린다
        const u32 sy = std::min(y >= pad ? y - pad : 0u, src.height - 1);
        for (u32 x = 0; x < out.width; ++x) {
            const u32 sx = std::min(x >= pad ? x - pad : 0u, src.width - 1);
            std::memcpy(out.pixel(x, y), src.pixel(sx, sy), 4);
        }
    }
    return out;
}

void AssetManager::place(SpriteId id, Image image) {
    Entry& e = m_entries[id];
    if (image.width == 0 || image.height == 0) {
        fail(id, "빈 이미지");
        return;
    }
    const u32 pad = m_desc.padding;
    const u32 w = image.width + pad * 2;
    const u32 h = image.height + pad * 2;
    for (u32 page = 0; page < m_pages.size(); ++page) {
        if (const auto r = m_pages[page].insert(w, h)) {
            const f32 s = static_cast<f32>(m_desc.atlasSize);
            e.final.atlas = 0;
            e.final.page = page;
            e.final.uv = {static_cast<f32>(r->x + pad) / s, static_cast<f32>(r->y + pad) / s,
                          static_cast<f32>(r->x + pad + image.width) / s,
                          static_cast<f32>(r->y + pad + image.height) / s};
            e.state = AssetState::Decoded;
            m_stats.pagesUsed = std::max(m_stats.pagesUsed, page + 1);
            m_uploads.push_back({id, page, r->x, r->y, addPadding(image, pad)});
            return;
        }
    }
    fail(id, std::format("아틀라스가 가득 찼다 ({}×{}, 층 {}개 · {}²)", image.width, image.height, m_pages.size(),
                         m_desc.atlasSize));
}

void AssetManager::fail(SpriteId id, std::string_view why) {
    Entry& e = m_entries[id];
    if (e.state == AssetState::Failed) {
        return;
    }
    if (e.state != AssetState::Ready) {
        --m_stats.queued;
    }
    e.state = AssetState::Failed;
    ++m_stats.failed;
    m_regions[id] = m_magenta;
    log::warn("render", "스프라이트 '{}' 실패: {}", e.name, why);
}

void AssetManager::update(rhi::ICommandList& cl) {
    m_stats.uploadedBytesLastFrame = 0;
    if (!m_atlas.valid()) {
        return;
    }
    // 1) Worker 결과 → 패킹 (요청 순으로 정렬해 같은 입력이면 같은 배치가 되게 — 완료 순서는 의미가 없다)
    std::vector<Decoded> done;
    {
        const std::scoped_lock lock(m_doneMutex);
        done.swap(m_done);
    }
    std::ranges::sort(done, {}, &Decoded::id);
    for (Decoded& d : done) {
        if (!d.image) {
            fail(d.id, d.image.error().message);
        } else {
            place(d.id, std::move(*d.image));
        }
    }

    // 2) 업로드 (예산 · 업로드 링 안에서). 처음에는 Undefined → CopyDst
    const rhi::DeviceCaps& caps = m_dev.caps();
    const auto toCopy = [&] {
        if (m_atlasState != rhi::ResourceState::CopyDst) {
            const rhi::ResourceBarrier b{m_atlas, m_atlasState, rhi::ResourceState::CopyDst};
            cl.barrier({&b, 1});
            m_atlasState = rhi::ResourceState::CopyDst;
        }
    };
    if (m_atlasState == rhi::ResourceState::Undefined) {
        toCopy();
    }
    usize consumed = 0;
    for (; consumed < m_uploads.size(); ++consumed) {
        const Upload& u = m_uploads[consumed];
        const u32 rowBytes = u.padded.width * 4;
        const u32 pitch = static_cast<u32>(rhi::alignUp(rowBytes, caps.textureCopyRowAlignment));
        const u64 bytes = static_cast<u64>(pitch) * u.padded.height;
        if (m_stats.uploadedBytesLastFrame > 0 && m_stats.uploadedBytesLastFrame + bytes > m_desc.uploadBudgetBytes) {
            ++m_stats.framesOverBudget; // 다음 프레임에 (한 프레임에 최소 하나는 보낸다)
            break;
        }
        const rhi::UploadAllocation a = m_dev.allocateUpload(bytes, caps.textureCopyOffsetAlignment);
        if (!a.valid()) {
            ++m_stats.framesOverBudget;
            break;
        }
        for (u32 y = 0; y < u.padded.height; ++y) {
            std::memcpy(a.cpu + static_cast<u64>(y) * pitch, u.padded.pixel(0, y), rowBytes);
        }
        toCopy();
        rhi::BufferTextureCopy c;
        c.buffer = a.buffer;
        c.bufferOffset = a.offset;
        c.bufferRowPitch = pitch;
        c.texture = m_atlas;
        c.layer = u.page;
        c.x = u.x;
        c.y = u.y;
        c.width = u.padded.width;
        c.height = u.padded.height;
        cl.copyBufferToTexture(c);
        m_stats.uploadedBytesLastFrame += bytes;

        Entry& e = m_entries[u.id];
        if (e.state == AssetState::Decoded) {
            e.state = AssetState::Ready;
            --m_stats.queued;
            ++m_stats.ready;
            m_regions[u.id] = e.final; // 이 복사가 같은 리스트의 그리기보다 앞이라 이번 프레임부터 쓸 수 있다
        }
    }
    m_uploads.erase(m_uploads.begin(), m_uploads.begin() + static_cast<std::ptrdiff_t>(consumed));
    m_stats.uploadedBytesTotal += m_stats.uploadedBytesLastFrame;

    if (m_atlasState != rhi::ResourceState::ShaderRead) {
        const rhi::ResourceBarrier b{m_atlas, m_atlasState, rhi::ResourceState::ShaderRead};
        cl.barrier({&b, 1});
        m_atlasState = rhi::ResourceState::ShaderRead;
    }
}

} // namespace sbx::render
