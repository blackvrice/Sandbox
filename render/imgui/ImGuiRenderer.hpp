#pragma once
// Dear ImGui 렌더러 — RHI 위 한 벌 (공식 imgui_impl_* 백엔드를 쓰지 않는다). docs/06-RENDERING.md 10장, ADR-0008 ·
// ADR-0023.
//
//   텍스처 (1.92 프로토콜, ImGuiBackendFlags_RendererHasTextures)
//     ImDrawData::Textures 의 요청을 처리한다: WantCreate → RGBA8 텍스처 + 바인드 그룹을 만들고 전체 업로드,
//     WantUpdates → 바뀐 사각형만 업로드, WantDestroy → 파괴(지연 해제). ImTextureID = 우리 표의 칸 번호 + 1 (0 =
//     없음). 폰트 글리프는 처음 쓰일 때 아틀라스에 굽힌다 — 한글 11,172 자를 미리 굽지 않는다.
//   그리기 (UIPass)
//     렌더 패스 하나 (LoadOp::Load — 이미 그린 월드 위에), 정점 · 인덱스는 이번 프레임 업로드 링 구간, 파이프라인 하나,
//     명령마다 scissor(클립 사각형 × FramebufferScale) + drawIndexed(vertexOffset — RendererHasVtxOffset).
// 업로드(복사)는 렌더 패스 밖이어야 하므로 record 가 둘 다 한다: 텍스처 → 패스.
//
// imgui.h 를 이 헤더에서 include 하지 않는다 (SandboxRender 를 쓰는 쪽이 ImGui 를 몰라도 되게).

#include <vector>

#include "foundation/types/Error.hpp"
#include "render/rhi/RenderDevice.hpp"

struct ImDrawData;
struct ImGuiIO;
struct ImGuiPlatformIO;

namespace sbx::render {

struct ImGuiRenderStats {
    u32 drawLists = 0;
    u32 draws = 0;
    u32 vertices = 0;
    u32 indices = 0;
    u32 textures = 0;        // 살아 있는 ImGui 텍스처
    u32 texturesCreated = 0; // 이번 프레임
    u32 textureUpdates = 0;  // 이번 프레임 (사각형 수)
    u64 uploadedBytes = 0;   // 이번 프레임 (텍스처 + 정점 + 인덱스)
    u32 skippedCommands = 0; // 텍스처가 아직 없거나(업로드 링 가득) 클립이 비어 그리지 않은 명령
};

class ImGuiRenderer {
public:
    explicit ImGuiRenderer(rhi::IRenderDevice& device) : m_dev(device) {}
    ~ImGuiRenderer();
    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

    // 파이프라인 · 샘플러. io 에 백엔드 플래그(RendererHasTextures · RendererHasVtxOffset)와 텍스처 상한을 적는다
    [[nodiscard]] Expected<void> init(rhi::Format targetFormat, ImGuiIO& io, ImGuiPlatformIO& platformIo);
    [[nodiscard]] bool ready() const noexcept { return m_pipeline.valid(); }

    // 텍스처 요청 처리(패스 밖) → UI 패스. target 은 RenderTarget 상태. drawData 가 null 이거나 비면 텍스처만
    void record(rhi::ICommandList& cl, rhi::RhiTexture target, ImDrawData* drawData);

    // ImGui 컨텍스트를 지우기 전에: 우리가 만든 텍스처를 모두 파괴하고 ImGui 쪽 상태를 Destroyed 로
    void shutdown(ImGuiPlatformIO& platformIo);

    [[nodiscard]] const ImGuiRenderStats& stats() const noexcept { return m_stats; }

private:
    struct Slot {
        rhi::RhiTexture texture;
        rhi::RhiBindGroup group;
        u32 width = 0, height = 0;
        rhi::ResourceState state = rhi::ResourceState::Undefined;
        bool used = false;
    };
    void updateTextures(rhi::ICommandList& cl, ImDrawData& drawData);
    u32 allocateSlot();
    void releaseSlot(u32 index);

    rhi::IRenderDevice& m_dev;
    rhi::RhiBindGroupLayout m_layout;
    rhi::RhiSampler m_sampler;
    rhi::RhiPipeline m_pipeline;
    [[maybe_unused]] u32 m_groupSlot = 0; // 셰이더가 없는 빌드에서는 쓰지 않는다
    std::vector<Slot> m_slots;
    std::vector<u32> m_free;
    ImGuiRenderStats m_stats;
};

} // namespace sbx::render
