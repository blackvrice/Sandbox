// Metal 번호 · MSL (Phase 14A, ADR-0035): 생성기가 매긴 ReflectedBinding::metalIndex 와, SBX_SHADER_MSL 빌드면 내장한
// MSL 소스가 그 번호를 쓰는지. MSL 을 Metal 로 컴파일하는 것은 macOS 에서만 (Metal 백엔드 — [계획] 14B).
// docs/06-RENDERING.md 6장.
#include <doctest/doctest.h>

#include <format>
#include <string>
#include <string_view>

#include "render/rhi/ShaderTypes.hpp"

#ifdef SBX_HAS_SHADERS
#include "render/generated/BasicColorShader.hpp"
#include "render/generated/BasicTextureShader.hpp"
#include "render/generated/GridShader.hpp"
#include "render/generated/ImguiShader.hpp"
#include "render/generated/LinesShader.hpp"
#include "render/generated/SpriteShader.hpp"
#include "render/generated/TerrainShader.hpp"
#endif

using namespace sbx;
using namespace sbx::rhi;

namespace {

#ifdef SBX_HAS_SHADERS
struct Program {
    std::string_view name;
    const ShaderReflection& reflection;
    ShaderBytecode vs;
    ShaderBytecode ps;
};

// 같은 종류(버퍼 · 텍스처 · 샘플러) 안에서 (group, binding) 순서로 0 부터 — 버퍼는 push constant 다음 1 부터
void checkNumbering(const ShaderReflection& r) {
    u32 buffers = kMetalFirstResourceBuffer;
    u32 textures = 0;
    u32 samplers = 0;
    u32 lastKey = 0;
    bool first = true;
    for (const ReflectedBinding& b : r.bindings) {
        CAPTURE(b.name);
        const u32 key = b.group * 1000 + b.binding;
        CHECK((first || key > lastKey)); // 생성기가 (group, binding) 순서로 내놓는다
        first = false;
        lastKey = key;
        switch (b.type) {
        case BindingType::ConstantBuffer:
        case BindingType::StorageBuffer:
        case BindingType::StorageBufferRW:
            CHECK(b.metalIndex == buffers++);
            CHECK(b.metalIndex < metalVertexBufferIndex(kMetalMaxVertexBuffers - 1)); // 정점 버퍼와 겹치지 않는다
            break;
        case BindingType::Texture:
        case BindingType::StorageTexture:
            CHECK(b.metalIndex == textures++);
            CHECK(b.metalIndex < kMetalMaxTextures);
            break;
        case BindingType::Sampler:
            CHECK(b.metalIndex == samplers++);
            CHECK(b.metalIndex < kMetalMaxSamplers);
            break;
        }
    }
}

#ifdef SBX_HAS_MSL
std::string attributeFor(const ReflectedBinding& b) {
    switch (b.type) {
    case BindingType::Texture:
    case BindingType::StorageTexture:
        return std::format("[[texture({})]]", b.metalIndex);
    case BindingType::Sampler:
        return std::format("[[sampler({})]]", b.metalIndex);
    default:
        return std::format("[[buffer({})]]", b.metalIndex);
    }
}

void checkMsl(const Program& p, const ShaderBytecode& code, std::string_view qualifier) {
    CAPTURE(code.debugName);
    REQUIRE_FALSE(code.msl.empty());
    CHECK(code.msl.find("#include <metal_stdlib>") != std::string_view::npos);
    REQUIRE_FALSE(code.mslEntry.empty());
    // "vertex VSMain_out VSMain(" — 진입점 함수가 그 이름 · 단계로 있다
    CHECK(code.msl.find(std::string(qualifier) + " ") != std::string_view::npos);
    CHECK(code.msl.find(std::string(code.mslEntry) + "(") != std::string_view::npos);
    const auto stageBit = static_cast<ShaderStageMask>(code.stage);
    for (const ReflectedBinding& b : p.reflection.bindings) {
        if ((b.stages & stageBit) == 0) {
            continue;
        }
        CAPTURE(b.name);
        // "<이름> [[texture(0)]]" — 리소스가 생성기가 정한 번호로 묶였다
        const std::string expect = std::string(b.name) + " " + attributeFor(b);
        CHECK(code.msl.find(expect) != std::string_view::npos);
    }
    if (p.reflection.pushConstantBytes > 0 && code.msl.find("push") != std::string_view::npos) {
        CHECK(code.msl.find(std::format("push [[buffer({})]]", kMetalPushConstantBuffer)) != std::string_view::npos);
    }
}
#endif
#endif

} // namespace

TEST_SUITE("render") {
    TEST_CASE("metal numbering constants: push constant, resource buffers and vertex buffers do not overlap") {
        CHECK(kMetalPushConstantBuffer < kMetalFirstResourceBuffer);
        CHECK(metalVertexBufferIndex(0) == kMetalVertexBufferTop);
        CHECK(metalVertexBufferIndex(kMetalMaxVertexBuffers - 1) == 27u);
        CHECK(kMetalVertexBufferTop <= 30u); // Metal 은 단계마다 버퍼 31 개 (0 ~ 30)
        CHECK(kMetalMaxSamplers <= 16u);
    }

#ifdef SBX_HAS_SHADERS
    TEST_CASE("generated shaders: metal indices follow (group, binding) order per kind") {
        namespace bc = render::shaders::basic_color;
        namespace bt = render::shaders::basic_texture;
        namespace sp = render::shaders::sprite;
        namespace te = render::shaders::terrain;
        const Program programs[] = {
            {"basic_color", bc::reflection(), bc::vs(), bc::ps()},
            {"basic_texture", bt::reflection(), bt::vs(), bt::ps()},
            {"sprite", sp::reflection(), sp::vs(), sp::ps()},
            {"terrain", te::reflection(), te::vs(), te::ps()},
            {"grid", render::shaders::grid::reflection(), render::shaders::grid::vs(), render::shaders::grid::ps()},
            {"lines", render::shaders::lines::reflection(), render::shaders::lines::vs(), render::shaders::lines::ps()},
            {"imgui", render::shaders::imgui::reflection(), render::shaders::imgui::vs(), render::shaders::imgui::ps()},
        };
        for (const Program& p : programs) {
            CAPTURE(p.name);
            checkNumbering(p.reflection);
#ifdef SBX_HAS_MSL
            checkMsl(p, p.vs, "vertex");
            checkMsl(p, p.ps, "fragment");
#else
            CHECK(p.vs.msl.empty()); // MSL 이 없는 빌드 (SBX_SHADER_MSL=OFF)
#endif
        }
        // 손으로 확인한 값: cbuffer Frame = buffer 1, 아틀라스 · 샘플러 = 0 · 0, 지형 타일 · 팔레트 = texture 0 · 1
        REQUIRE(bc::reflection().bindings.size() == 1);
        CHECK(bc::reflection().bindings[0].metalIndex == 1u);
        REQUIRE(sp::reflection().bindings.size() == 2);
        CHECK(sp::reflection().bindings[0].metalIndex == 0u);
        CHECK(sp::reflection().bindings[1].metalIndex == 0u);
        REQUIRE(te::reflection().bindings.size() == 2);
        CHECK(te::reflection().bindings[0].metalIndex == 0u);
        CHECK(te::reflection().bindings[1].metalIndex == 1u);
    }
#endif
}
