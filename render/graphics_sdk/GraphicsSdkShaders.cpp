#include "render/graphics_sdk/GraphicsSdkShaders.hpp"

#include <cstring>
#include <format>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include "render/generated/GraphicsSdkHlslSources.hpp"
#include "render/graphics_sdk/GraphicsSdkSpirvData.hpp"
#include "render/graphics_sdk/GraphicsSdkTypes.hpp"

namespace sbx::rhi::graphics_sdk {
namespace {

// 내장 셰이더의 D3D12 원본 = shaders/*.hlsl 그대로 (include 를 푼 글 — cmake/SbxEmbedHlsl.cmake). 손으로 고친 사본을 두지 않는다
// (ADR-0171). Graphics SDK 는 DXC 없이 빌드돼 (GFX_HAS_DXC 아님) DXIL 리플렉션이 없으므로 FXC (SM 5.1) 로 런타임 컴파일한다
using namespace hlsl_sources;

struct BuiltinHlslEntry {
    std::string_view name;
    std::string_view hlsl;
    std::string_view entry;
    gfx::ShaderStage stage;
};

constexpr BuiltinHlslEntry kBuiltinHlsl[] = {
    {"basic_color.vs", kBasicColorHlsl, "VSMain", gfx::ShaderStage::Vertex},
    {"basic_color.ps", kBasicColorHlsl, "PSMain", gfx::ShaderStage::Fragment},
    {"basic_texture.vs", kBasicTextureHlsl, "VSMain", gfx::ShaderStage::Vertex},
    {"basic_texture.ps", kBasicTextureHlsl, "PSMain", gfx::ShaderStage::Fragment},
    {"sprite.vs", kSpriteHlsl, "VSMain", gfx::ShaderStage::Vertex},
    {"sprite.ps", kSpriteHlsl, "PSMain", gfx::ShaderStage::Fragment},
    {"terrain.vs", kTerrainHlsl, "VSMain", gfx::ShaderStage::Vertex},
    {"terrain.ps", kTerrainHlsl, "PSMain", gfx::ShaderStage::Fragment},
    {"grid.vs", kGridHlsl, "VSMain", gfx::ShaderStage::Vertex},
    {"grid.ps", kGridHlsl, "PSMain", gfx::ShaderStage::Fragment},
    {"lines.vs", kLinesHlsl, "VSMain", gfx::ShaderStage::Vertex},
    {"lines.ps", kLinesHlsl, "PSMain", gfx::ShaderStage::Fragment},
    {"imgui.vs", kImguiHlsl, "VSMain", gfx::ShaderStage::Vertex},
    {"imgui.ps", kImguiHlsl, "PSMain", gfx::ShaderStage::Fragment},
};

template <std::size_t N>
gfx::ShaderBinary makeSpirvBinary(gfx::ShaderStage stage, const std::array<std::uint32_t, N>& words) {
    const auto raw = std::as_bytes(std::span{words});
    return {stage, std::vector<std::byte>(raw.begin(), raw.end()), gfx::ShaderFormat::SPIRV};
}

Expected<gfx::ShaderBinary> resolveD3D12Shader(const GraphicsSdkShader& shader) {
    static std::mutex cacheMutex;
    static std::unordered_map<std::string, gfx::ShaderBinary> dxbcCache;

    {
        std::scoped_lock lock(cacheMutex);
        if (const auto it = dxbcCache.find(shader.name); it != dxbcCache.end()) {
            return it->second;
        }
    }

    for (const BuiltinHlslEntry& item : kBuiltinHlsl) {
        if (item.name == shader.name) {
            auto compiled = gfx::compileHlsl(item.hlsl, item.entry, item.stage);
            if (!compiled) {
                return std::unexpected(mapError(compiled.error(), std::format("gfx::compileHlsl({})", shader.name)));
            }
            std::scoped_lock lock(cacheMutex);
            dxbcCache.emplace(shader.name, *compiled);
            return *compiled;
        }
    }

    // 내장 셰이더가 아닌 경우 원본 DXIL 바이트코드를 그대로 전달한다.
    const gfx::ShaderStage stage =
        shader.stage == ShaderStage::Vertex ? gfx::ShaderStage::Vertex : gfx::ShaderStage::Fragment;
    return gfx::ShaderBinary{stage, shader.dxil, gfx::ShaderFormat::DXIL};
}

Expected<gfx::ShaderBinary> resolveVulkanShader(const GraphicsSdkShader& shader) {
    using namespace spirv_data;
    if (shader.name == "basic_color.vs") {
        return makeSpirvBinary(gfx::ShaderStage::Vertex, kBasicColorVs);
    }
    if (shader.name == "basic_color.ps") {
        return makeSpirvBinary(gfx::ShaderStage::Fragment, kBasicColorPs);
    }
    if (shader.name == "basic_texture.vs") {
        return makeSpirvBinary(gfx::ShaderStage::Vertex, kBasicTextureVs);
    }
    if (shader.name == "basic_texture.ps") {
        return makeSpirvBinary(gfx::ShaderStage::Fragment, kBasicTexturePs);
    }
    if (shader.name == "sprite.vs") {
        return makeSpirvBinary(gfx::ShaderStage::Vertex, kSpriteVs);
    }
    if (shader.name == "sprite.ps") {
        return makeSpirvBinary(gfx::ShaderStage::Fragment, kSpritePs);
    }
    if (shader.name == "terrain.vs") {
        return makeSpirvBinary(gfx::ShaderStage::Vertex, kTerrainVs);
    }
    if (shader.name == "terrain.ps") {
        return makeSpirvBinary(gfx::ShaderStage::Fragment, kTerrainPs);
    }
    if (shader.name == "grid.vs") {
        return makeSpirvBinary(gfx::ShaderStage::Vertex, kGridVs);
    }
    if (shader.name == "grid.ps") {
        return makeSpirvBinary(gfx::ShaderStage::Fragment, kGridPs);
    }
    if (shader.name == "lines.vs") {
        return makeSpirvBinary(gfx::ShaderStage::Vertex, kLinesVs);
    }
    if (shader.name == "lines.ps") {
        return makeSpirvBinary(gfx::ShaderStage::Fragment, kLinesPs);
    }
    if (shader.name == "imgui.vs") {
        return makeSpirvBinary(gfx::ShaderStage::Vertex, kImguiVs);
    }
    if (shader.name == "imgui.ps") {
        return makeSpirvBinary(gfx::ShaderStage::Fragment, kImguiPs);
    }

    const gfx::ShaderStage stage =
        shader.stage == ShaderStage::Vertex ? gfx::ShaderStage::Vertex : gfx::ShaderStage::Fragment;
    return gfx::ShaderBinary{stage, shader.spirv, gfx::ShaderFormat::SPIRV};
}

} // namespace

Expected<gfx::ShaderBinary> resolveSdkShader(BackendType backend, const GraphicsSdkShader& shader) {
    if (backend == BackendType::D3D12) {
        return resolveD3D12Shader(shader);
    }
    if (backend == BackendType::Vulkan) {
        return resolveVulkanShader(shader);
    }
    const gfx::ShaderStage stage =
        shader.stage == ShaderStage::Vertex ? gfx::ShaderStage::Vertex : gfx::ShaderStage::Fragment;
    std::vector<std::byte> bytes(shader.msl.size());
    if (!shader.msl.empty()) {
        std::memcpy(bytes.data(), shader.msl.data(), shader.msl.size());
    }
    return gfx::ShaderBinary{stage, std::move(bytes), gfx::ShaderFormat::MSL};
}

Expected<void> verifyVulkanShaderSources() {
    // Vulkan 생성 SPIR-V (tools/shader/gen_gfxsdk_spirv.py — SDK 계약: set 0 하나 · main · GLSL.std.450 없음, SDK-REQ-001) 는
    // shaders/*.hlsl 를 손으로 옮긴 변형에서 만든다. 생성할 때 원본의 SHA-256 을 기록하므로, 원본이 바뀌었는데 다시 만들지
    // 않았으면 여기서 실패한다 — 조용히 다른 셰이더로 그리지 않는다
    struct Pair {
        std::string_view name;
        std::string_view now;
        std::string_view generatedFrom;
    };
    using namespace spirv_data;
    using namespace hlsl_sources;
    const Pair pairs[] = {
        {"basic_color", kBasicColorHlslSha256, kBasicColorSourceSha256},
        {"basic_texture", kBasicTextureHlslSha256, kBasicTextureSourceSha256},
        {"sprite", kSpriteHlslSha256, kSpriteSourceSha256},
        {"terrain", kTerrainHlslSha256, kTerrainSourceSha256},
        {"grid", kGridHlslSha256, kGridSourceSha256},
        {"lines", kLinesHlslSha256, kLinesSourceSha256},
        {"imgui", kImguiHlslSha256, kImguiSourceSha256},
    };
    std::string stale;
    for (const Pair& p : pairs) {
        if (p.now != p.generatedFrom) {
            stale += std::format("{}{}", stale.empty() ? "" : ", ", p.name);
        }
    }
    if (!stale.empty()) {
        return makeError(ErrorCode::ValidationFailed,
                         std::format("Vulkan SPIR-V 가 지금 shaders/*.hlsl 에서 만든 것이 아니다: {} — 바뀐 내용을 "
                                     "tools/shader/gen_gfxsdk_spirv.py 의 변형에 옮기고 다시 생성한다 (ADR-0171)",
                                     stale));
    }
    return {};
}

} // namespace sbx::rhi::graphics_sdk
