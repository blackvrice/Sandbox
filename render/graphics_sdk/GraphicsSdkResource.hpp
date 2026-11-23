#pragma once
// Graphics SDK 어댑터의 GPU 리소스 · 지연 해제 항목 표현.

#include <graphics/Graphics.hpp>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "render/rhi/RhiTypes.hpp"

namespace sbx::rhi::graphics_sdk {

struct GraphicsSdkBuffer {
    gfx::BufferPtr buffer;
    BufferDesc desc;
    // Upload · Readback 의 map() 호출과 UploadRing 의 cpu 포인터를 위한 호스트 그림자 버퍼.
    // Stage-Before-Record 계약에 따라 커맨드 기록 직전 또는 readback 완료 직후 동기화한다.
    std::vector<std::byte> cpuShadow;
    std::vector<std::byte> lastUploadedShadow;
    bool cpuMapped = false;
    bool internal = false; // 업로드 링 등 내부 전용 버퍼
    // SDK-REQ-009 임시 보정: 불투명 클리어 타깃 + BlendMode::Alpha 이후 Readback 시 Porter-Duff Over 알파 불변식(1.0) 복원
    bool fixupOpaqueAlpha = false;
    u64 fixupOffset = 0;
    u32 fixupPitch = 0;
    u32 fixupWidth = 0;
    u32 fixupHeight = 0;
};

struct GraphicsSdkTexture {
    gfx::TexturePtr texture;
    TextureDesc desc;
    gfx::Format sdkFormat = gfx::Format::RGBA8UNorm;
    ResourceState trackedState = ResourceState::Undefined;
    bool initialized = false;        // LoadOp::Load 전에 Clear 또는 Copy 가 있었는가
    bool external = false;           // 스왑체인 백버퍼
    bool opaqueAlphaCleared = false; // clear.a == 1.0 으로 초기화된 타깃
    bool alphaBlendDrawn = false;    // BlendMode::Alpha 드로우가 실행됨
};

// 지연 해제 큐 항목: GPU 작업이 완료된 펜스 이후에 SDK shared_ptr 참조를 해제하고 디스크립터 수 등을 정리한다.
class GraphicsSdkGarbage {
public:
    GraphicsSdkGarbage() = default;
    GraphicsSdkGarbage(gfx::BufferPtr buf, gfx::TexturePtr tex, gfx::PipelinePtr pipe,
                       std::function<void()> onRelease = {})
        : m_buffer(std::move(buf)), m_texture(std::move(tex)), m_pipeline(std::move(pipe)),
          m_onRelease(std::move(onRelease)) {}

    GraphicsSdkGarbage(GraphicsSdkGarbage&& other) noexcept
        : m_buffer(std::move(other.m_buffer)), m_texture(std::move(other.m_texture)),
          m_pipeline(std::move(other.m_pipeline)), m_onRelease(std::move(other.m_onRelease)) {
        other.m_onRelease = nullptr;
    }

    GraphicsSdkGarbage& operator=(GraphicsSdkGarbage&&) = delete;
    GraphicsSdkGarbage(const GraphicsSdkGarbage&) = delete;
    GraphicsSdkGarbage& operator=(const GraphicsSdkGarbage&) = delete;

    ~GraphicsSdkGarbage() {
        m_buffer.reset();
        m_texture.reset();
        m_pipeline.reset();
        if (m_onRelease) {
            m_onRelease();
        }
    }

private:
    gfx::BufferPtr m_buffer;
    gfx::TexturePtr m_texture;
    gfx::PipelinePtr m_pipeline;
    std::function<void()> m_onRelease;
};

} // namespace sbx::rhi::graphics_sdk
