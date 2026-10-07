#pragma once
// 텍스처 에셋 → 스프라이트 아틀라스. docs/06-RENDERING.md 7.2 ~ 7.4, ADR-0020.
//
//   requestSprite("ecosystem/rabbit.png") → SpriteId (바로. 준비 전에는 흰색 자리 — 색만 보인다)
//      Worker:  파일 읽기 + PNG 디코드 (JobSystem 이 없으면 그 자리에서)
//      Render:  update(cl) — 디코드가 끝난 것을 아틀라스 층(page)에 선반 패킹하고, 프레임 예산(기본 8 MB) 안에서
//               업로드 링 → copyBufferToTexture 로 그 영역만 복사. 같은 커맨드 리스트의 그리기보다 먼저 기록되므로
//               그 프레임부터 바로 보인다.
//   regions()[id] → (page, uv). SpriteBatcher 가 그대로 쓴다.
//
// 아틀라스 = Texture2DArray 하나 (RGBA8Unorm, atlasSize² × atlasPages, 기본 1024² × 4 = 16 MB). 크기를 바꾸지 않는다.
// 이미지마다 가장자리를 1 텍셀 늘린 테두리(padding)를 둔다 — 선형 필터가 이웃 스프라이트를 섞지 않게.
// 실패(파일 없음 · 디코드 오류 · 아틀라스 가득)는 마젠타 자리 + 경고 한 번.
// 스레드: requestSprite · update · regions 는 Render 스레드(지금은 메인)만. Worker 는 결과 칸에만 쓴다.
// 8A 는 해제 · 핫 리로드가 없다 ([계획] refcount · 파일 감시).

#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "foundation/job/JobSystem.hpp"
#include "foundation/types/Error.hpp"
#include "render/asset/AssetId.hpp"
#include "render/asset/Image.hpp"
#include "render/asset/ShelfPacker.hpp"
#include "render/renderer/SpriteBatcher.hpp"
#include "render/rhi/RenderDevice.hpp"

namespace sbx::render {

enum class AssetState : u8 { Queued = 0, Decoded, Ready, Failed };

struct AssetManagerDesc {
    std::filesystem::path root;         // 에셋 루트 (requestSprite 의 경로가 이 아래)
    u32 atlasSize = 1024;               // 층 한 변 (텍셀)
    u32 atlasPages = 4;                 // 층 수
    u32 padding = 1;                    // 이미지 둘레 테두리 (텍셀)
    u64 uploadBudgetBytes = 8ull << 20; // 프레임당 업로드 상한 (06 4.2)
};

struct AssetStats {
    u32 sprites = 0; // 흰색 포함
    u32 queued = 0;  // 디코드 중 · 업로드 대기
    u32 ready = 0;
    u32 failed = 0;
    u32 pagesUsed = 0;
    u64 uploadedBytesLastFrame = 0;
    u64 uploadedBytesTotal = 0;
    u32 framesOverBudget = 0; // 예산 · 업로드 링 때문에 다음 프레임으로 미룬 횟수
};

class AssetManager {
public:
    // jobs 가 nullptr 이면 requestSprite 가 그 자리에서 디코드한다 (테스트 — 결과 순서가 고정된다)
    AssetManager(rhi::IRenderDevice& device, JobSystem* jobs, AssetManagerDesc desc);
    ~AssetManager();
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // 아틀라스 텍스처를 만든다. 실패하면 오류 (이후 호출은 모두 흰색 자리)
    [[nodiscard]] Expected<void> init();

    // 같은 경로(정규화 후)는 같은 id
    [[nodiscard]] SpriteId requestSprite(std::string_view path);
    // 메모리의 이미지 (절차적 텍스처 · 테스트). name 이 같으면 같은 id (두 번째 이미지는 무시)
    [[nodiscard]] SpriteId addSprite(std::string_view name, Image image);

    // Render 스레드, 이번 프레임의 그리기를 기록하기 전에. 디코드가 끝난 것을 패킹 · 업로드하고 아틀라스를 ShaderRead
    // 로
    void update(rhi::ICommandList& cl);
    // 모든 디코드 Job 이 끝날 때까지 (테스트 · 로딩 화면). 업로드는 다음 update 에서
    void waitDecodes();

    [[nodiscard]] std::span<const SpriteRegion> regions() const noexcept { return m_regions; }
    [[nodiscard]] AssetState state(SpriteId id) const noexcept;
    [[nodiscard]] rhi::RhiTexture atlas() const noexcept { return m_atlas; }
    [[nodiscard]] const AssetStats& stats() const noexcept { return m_stats; }
    [[nodiscard]] const AssetManagerDesc& desc() const noexcept { return m_desc; }

private:
    struct Entry {
        std::string name; // 정규화한 경로 또는 addSprite 이름
        AssetState state = AssetState::Queued;
        SpriteRegion final{}; // Ready 가 되면 regions 에 들어갈 자리
    };
    struct Decoded {
        SpriteId id;
        Expected<Image> image;
    };
    struct Upload {
        SpriteId id;
        u32 page, x, y;
        Image padded;
    };

    SpriteId newEntry(std::string name);
    void place(SpriteId id, Image image); // 패킹 → 업로드 대기열
    void fail(SpriteId id, std::string_view why);
    [[nodiscard]] static Image addPadding(const Image& src, u32 pad);

    rhi::IRenderDevice& m_dev;
    JobSystem* m_jobs;
    AssetManagerDesc m_desc;
    rhi::RhiTexture m_atlas;
    rhi::ResourceState m_atlasState = rhi::ResourceState::Undefined;

    std::vector<Entry> m_entries;
    std::vector<SpriteRegion> m_regions; // 지금 쓸 자리 (준비 전 = 흰색, 실패 = 마젠타)
    std::unordered_map<AssetId, SpriteId> m_byId;
    std::vector<ShelfPacker> m_pages;
    SpriteRegion m_white{};
    SpriteRegion m_magenta{};

    std::vector<Upload> m_uploads; // 패킹했지만 아직 복사하지 않은 것 (요청 순)
    JobGroup m_group;
    std::mutex m_doneMutex;
    std::vector<Decoded> m_done; // Worker 가 채운다
    AssetStats m_stats;
};

} // namespace sbx::render
