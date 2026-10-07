#pragma once
// sbx_render_tests 공용: 실행 옵션, 공유 디바이스, 텍스처 읽기, 기준 이미지 비교. docs/06-RENDERING.md 14장,
// 13-TESTING.

#include <filesystem>
#include <memory>
#include <string>

#include "render/asset/Image.hpp"
#include "render/rhi/RenderDevice.hpp"

namespace sbx::rendertest {

struct Options {
    bool warp = false;
    bool debugLayer = false;
    bool gpuValidation = false;
    bool allowFeatureLevel11 = false;   // --fl11: Wine/vkd3d 시험 전용 (DeviceDesc 참고)
    bool updateReferences = false;      // 기준 이미지를 지금 결과로 덮어쓴다 (검토 후 커밋)
    std::filesystem::path referenceDir; // tests/render/references
    std::filesystem::path outputDir;    // 실패 시 actual · diff PNG
};

[[nodiscard]] Options& options();
[[nodiscard]] rhi::DeviceDesc deviceDesc(); // 옵션을 반영한 기본 DeviceDesc
// 모든 테스트가 같이 쓰는 디바이스 (main 이 만들고 끝에 누수를 검사한다)
[[nodiscard]] rhi::IRenderDevice& device();

// 텍스처(현재 상태 state)를 읽어 RGBA8 이미지로. BGRA 는 RGBA 로 바꾼다. 상태는 원래대로 돌려놓는다.
[[nodiscard]] render::Image readback(rhi::IRenderDevice& dev, rhi::RhiTexture texture, rhi::ResourceState state);

// 일부러 RHI 를 잘못 쓰는 테스트가 부른다 — 끝의 "검증 오류 0" 검사에서 뺀다
void expectValidationErrors(u64 n);

// 한 번 기록해 제출하고 끝날 때까지 기다린다 (beginFrame … endFrame 포함)
template <class F>
void submitAndWait(rhi::IRenderDevice& dev, F&& record) {
    auto list = dev.createCommandList(rhi::QueueType::Graphics);
    dev.beginFrame();
    list->begin();
    record(*list);
    list->end();
    rhi::ICommandList* lists[] = {list.get()};
    const rhi::FenceValue v = dev.queue(rhi::QueueType::Graphics).submit(lists);
    dev.endFrame();
    dev.queue(rhi::QueueType::Graphics).wait(v);
}

// 기준 이미지 references/<name>.png 와 비교. 다르면 outputDir 에 <name>.actual.png · <name>.diff.png 를 남긴다.
// --update-references 면 기준을 덮어쓰고 통과. 돌려준 문자열이 비면 통과, 아니면 실패 이유.
[[nodiscard]] std::string matchReference(const std::string& name, const render::Image& actual, u32 tolerance,
                                         double maxBadPixelFraction = 0.0);

} // namespace sbx::rendertest
