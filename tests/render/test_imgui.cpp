// Phase 8C GPU 테스트: ImGuiRenderer — 1.92 텍스처 프로토콜(만들기 · 부분 갱신 · 파괴), 클립(scissor), 기준 이미지
// imgui_basic. docs/06-RENDERING.md 10 · 14장, ADR-0023.
#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>

#include <imgui.h>

#include "render/imgui/ImGuiRenderer.hpp"
#include "tests/render/RenderTestEnv.hpp"

using namespace sbx;
using namespace sbx::render;
using sbx::rendertest::device;
using sbx::rendertest::matchReference;
using sbx::rendertest::readback;
using sbx::rendertest::submitAndWait;

namespace {

constexpr u32 kW = 320, kH = 200;

struct UiRig {
    rhi::IRenderDevice& dev;
    ImGuiContext* ctx = nullptr;
    ImGuiRenderer renderer;
    rhi::RhiTexture target;

    UiRig() : dev(device()), renderer(dev) {
        ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; // 파일을 쓰지 않는다 — 같은 입력이면 같은 그림
        io.DisplaySize = {static_cast<float>(kW), static_cast<float>(kH)};
        io.DeltaTime = 1.f / 60.f;
        io.Fonts->AddFontDefaultBitmap(); // 13 px 비트맵 — 구현마다 같은 글자
        ImGui::StyleColorsDark();
        ImGui::GetStyle().AntiAliasedLines = false; // 선 테두리 반투명 픽셀을 줄여 구현 차이를 작게
        REQUIRE(renderer.init(rhi::Format::RGBA8Unorm, io, ImGui::GetPlatformIO()));
        rhi::TextureDesc td;
        td.width = kW;
        td.height = kH;
        td.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySrc;
        td.debugName = "imgui target";
        target = dev.createTexture(td);
        REQUIRE(target.valid());
    }
    ~UiRig() {
        renderer.shutdown(ImGui::GetPlatformIO());
        ImGui::DestroyContext(ctx);
        dev.destroy(target);
    }

    // 지운 대상 위에 ImGui 한 프레임
    template <class F>
    Image frame(F&& ui) {
        ImGui::NewFrame();
        ui();
        ImGui::Render();
        submitAndWait(dev, [&](rhi::ICommandList& cl) {
            const rhi::ResourceBarrier toRt{target, rhi::ResourceState::Undefined, rhi::ResourceState::RenderTarget};
            cl.barrier({&toRt, 1});
            rhi::RenderPassDesc clear;
            clear.colorCount = 1;
            clear.colors[0] = {target, rhi::LoadOp::Clear, rhi::StoreOp::Store, {0.1f, 0.2f, 0.3f, 1.f}};
            cl.beginRenderPass(clear);
            cl.endRenderPass();
            renderer.record(cl, target, ImGui::GetDrawData());
            const rhi::ResourceBarrier toCopy{target, rhi::ResourceState::RenderTarget, rhi::ResourceState::CopySrc};
            cl.barrier({&toCopy, 1});
        });
        return readback(dev, target, rhi::ResourceState::CopySrc);
    }
};

void basicWindow() {
    ImGui::SetNextWindowPos({10, 10});
    ImGui::SetNextWindowSize({220, 150});
    ImGui::Begin("Sandbox 8C", nullptr, ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted("tick 1234 / 30 TPS");
    static bool check = true;
    ImGui::Checkbox("grid", &check);
    ImGui::Button("Step");
    ImGui::SameLine();
    ImGui::Button("Pause");
    ImGui::ProgressBar(0.62f, {-1, 0});
    const float values[] = {1, 3, 2, 5, 4, 6, 3, 7, 5, 8};
    ImGui::PlotLines("##frame", values, 10, 0, nullptr, 0.f, 8.f, {-1, 40});
    ImGui::End();
}

bool isClear(const u8* p) {
    // 지운 색 (0.1, 0.2, 0.3) → (26, 51, 77) ± 1
    return std::abs(p[0] - 26) <= 1 && std::abs(p[1] - 51) <= 1 && std::abs(p[2] - 77) <= 1;
}

} // namespace

TEST_SUITE("render.gpu") {

    TEST_CASE("imgui: font texture created on demand, window drawn with scissor, reference imgui_basic") {
        UiRig rig;
        const Image img = rig.frame(basicWindow);
        const ImGuiRenderStats& st = rig.renderer.stats();
        CHECK(st.texturesCreated >= 1); // 폰트 아틀라스
        CHECK(st.textures >= 1);
        CHECK(st.draws >= 1);
        CHECK(st.vertices > 0);
        CHECK(st.skippedCommands == 0);
        // 창 밖은 지운 색 그대로 (scissor · 정점 범위)
        u32 outside = 0;
        for (u32 y = 0; y < kH; y += 3) {
            for (u32 x = 0; x < kW; x += 3) {
                const bool inWindow = x >= 10 && x < 230 && y >= 10 && y < 160;
                if (!inWindow && !isClear(img.pixel(x, y))) {
                    ++outside;
                }
            }
        }
        CHECK(outside == 0);
        CHECK_FALSE(isClear(img.pixel(100, 80))); // 창 안
        CHECK(matchReference("imgui_basic", img, 8, 0.02).empty());

        // 같은 UI 를 다시: 새 텍스처도 갱신도 없다
        (void)rig.frame(basicWindow);
        CHECK(rig.renderer.stats().texturesCreated == 0);

        // 큰 글자: 1.92 동적 폰트 — 처음 쓰는 크기의 글리프가 굽혀져 아틀라스가 갱신(또는 다시 생성)된다
        (void)rig.frame([] {
            basicWindow();
            ImGui::SetNextWindowPos({240, 120});
            ImGui::Begin("big", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::PushFont(nullptr, 40.f);
            ImGui::TextUnformatted("Aa");
            ImGui::PopFont();
            ImGui::End();
        });
        const ImGuiRenderStats& s3 = rig.renderer.stats();
        CHECK(s3.textureUpdates + s3.texturesCreated > 0);
        CHECK(s3.skippedCommands == 0);
    }

    TEST_CASE("imgui: a draw command clipped to nothing is skipped, textures released on shutdown") {
        const u64 before = device().stats().liveTextures;
        {
            UiRig rig;
            (void)rig.frame([] {
                ImGui::SetNextWindowPos({400, 400}); // 화면 밖
                ImGui::Begin("off", nullptr, ImGuiWindowFlags_NoSavedSettings);
                ImGui::TextUnformatted("hidden");
                ImGui::End();
            });
            CHECK(rig.renderer.stats().draws == 0);
            CHECK(rig.renderer.stats().textures >= 1);
        }
        // 대상 + ImGui 텍스처가 모두 파괴됐다 (실제 해제는 지연 — 핸들 수는 바로 준다)
        CHECK(device().stats().liveTextures == before);
    }
}
