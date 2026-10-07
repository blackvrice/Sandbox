// 7B 의 백엔드 독립 로직: 디스크립터 구간 할당기 · 리플렉션 → 레이아웃 · 파이프라인/바인드 그룹 검사 · 생성 셰이더.
// docs/06-RENDERING.md 3.4·5.1·6장, ADR-0019.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <random>
#include <vector>

#include "render/rhi/PipelineValidation.hpp"
#include "render/rhi/RangeAllocator.hpp"

#ifdef SBX_HAS_SHADERS
#include "render/generated/BasicColorShader.hpp"
#include "render/generated/BasicTextureShader.hpp"
#endif

using namespace sbx;
using namespace sbx::rhi;

namespace {

constexpr ShaderStageMask kVS = static_cast<ShaderStageMask>(ShaderStage::Vertex);
constexpr ShaderStageMask kPS = static_cast<ShaderStageMask>(ShaderStage::Pixel);

// basic_texture 와 같은 모양의 손으로 만든 리플렉션
const std::array<ReflectedBinding, 3> kPsBindings{{
    {0, 0, BindingType::ConstantBuffer, kPS, 16, TextureDim::None, "Frame"},
    {2, 0, BindingType::Texture, kPS, 0, TextureDim::Tex2D, "colorTexture"},
    {2, 1, BindingType::Sampler, kPS, 0, TextureDim::None, "colorSampler"},
}};
const std::array<ReflectedBinding, 1> kVsBindings{{
    {0, 0, BindingType::ConstantBuffer, kVS, 16, TextureDim::None, "Frame"},
}};
const std::array<ReflectedVertexInput, 2> kVsInputs{{
    {0, "POSITION", 0, 2},
    {1, "TEXCOORD", 0, 2},
}};
const ShaderReflection kVsRefl{kVsBindings, kVsInputs, 16};
const ShaderReflection kPsRefl{kPsBindings, {}, 0};

std::vector<VertexAttribute> goodAttributes() {
    return {{"POSITION", 0, 0, Format::RG32Float, 0, 0}, {"TEXCOORD", 0, 1, Format::RG32Float, 8, 0}};
}

// 가짜 백엔드: 유효한 핸들이면 살아 있다
bool aliveBuffer(const void*, RhiBuffer h) {
    return h.valid();
}
bool aliveTexture(const void*, RhiTexture h) {
    return h.valid();
}
bool aliveSampler(const void*, RhiSampler h) {
    return h.valid();
}
constexpr BindGroupResourceCheck kAlive{aliveBuffer, aliveTexture, aliveSampler, nullptr};

} // namespace

TEST_SUITE("render") {

    TEST_CASE("range allocator: first fit, coalescing on release, exhaustion") {
        RangeAllocator a(100);
        CHECK(a.capacity() == 100);
        CHECK_FALSE(a.allocate(0).has_value());
        const auto r0 = a.allocate(10);
        const auto r1 = a.allocate(20);
        const auto r2 = a.allocate(30);
        REQUIRE(r0);
        REQUIRE(r1);
        REQUIRE(r2);
        CHECK(*r0 == 0);
        CHECK(*r1 == 10);
        CHECK(*r2 == 30);
        CHECK(a.used() == 60);
        CHECK_FALSE(a.allocate(41).has_value());

        a.release(*r1, 20); // [10, 30) 이 빈다 — 끝의 [60, 100) 과는 떨어져 있다
        CHECK(a.freeRanges() == 2);
        const auto r3 = a.allocate(15); // first fit → 10
        REQUIRE(r3);
        CHECK(*r3 == 10);
        a.release(*r3, 15);
        a.release(*r0, 10); // [0, 30) 로 합쳐진다
        CHECK(a.freeRanges() == 2);
        a.release(*r2, 30); // 모두 합쳐 하나
        CHECK(a.freeRanges() == 1);
        CHECK(a.used() == 0);
        const auto all = a.allocate(100);
        REQUIRE(all);
        CHECK(*all == 0);
        CHECK(a.freeRanges() == 0);
    }

    TEST_CASE("range allocator: random allocate/release never overlaps and returns to one range") {
        std::mt19937 rng(11);
        RangeAllocator a(4096);
        struct Live {
            u32 start, count;
        };
        std::vector<Live> live;
        for (int step = 0; step < 5000; ++step) {
            if (live.empty() || rng() % 3 != 0) {
                const u32 n = 1 + rng() % 64;
                const auto s = a.allocate(n);
                if (!s) {
                    continue;
                }
                CHECK(*s + n <= a.capacity());
                for (const Live& l : live) {
                    CHECK_FALSE((*s < l.start + l.count && l.start < *s + n));
                }
                live.push_back({*s, n});
            } else {
                const usize i = rng() % live.size();
                a.release(live[i].start, live[i].count);
                live.erase(live.begin() + static_cast<std::ptrdiff_t>(i));
            }
            u32 sum = 0;
            for (const Live& l : live) {
                sum += l.count;
            }
            REQUIRE(a.used() == sum);
        }
        for (const Live& l : live) {
            a.release(l.start, l.count);
        }
        CHECK(a.used() == 0);
        CHECK(a.freeRanges() == 1);
    }

    TEST_CASE("layoutFromReflection merges stages across shaders and sorts by binding") {
        const std::array<const ShaderReflection*, 2> both{&kVsRefl, &kPsRefl};
        const BindGroupLayoutDesc g0 = layoutFromReflection(both, 0);
        REQUIRE(g0.entries.size() == 1);
        CHECK(g0.entries[0].type == BindingType::ConstantBuffer);
        CHECK(g0.entries[0].stages == (kVS | kPS));

        const BindGroupLayoutDesc g2 = layoutFromReflection(both, 2);
        REQUIRE(g2.entries.size() == 2);
        CHECK(g2.entries[0].binding == 0);
        CHECK(g2.entries[0].type == BindingType::Texture);
        CHECK(g2.entries[0].dim == TextureDim::Tex2D);
        CHECK(g2.entries[1].type == BindingType::Sampler);
        CHECK(layoutFromReflection(both, 1).entries.empty());
        CHECK(validateLayout(g0).empty());
        CHECK(validateLayout(g2).empty());
    }

    TEST_CASE("validateLayout: binding numbers are unique per group regardless of type, stages non-empty") {
        BindGroupLayoutDesc d{
            {{0, BindingType::Texture, kPS, TextureDim::Tex2D}, {0, BindingType::Sampler, kPS, TextureDim::Tex2D}},
            "dup"};
        CHECK(validateLayout(d).find("두 번") != std::string::npos);
        d.entries[1].binding = 1;
        CHECK(validateLayout(d).empty());
        d.entries[1].stages = 0;
        CHECK(validateLayout(d).find("stages") != std::string::npos);
    }

    TEST_CASE("validateAgainstReflection reports the first mismatch") {
        const std::array<const ShaderReflection*, 2> shaders{&kVsRefl, &kPsRefl};
        BindGroupLayoutDesc g0 = layoutFromReflection(shaders, 0);
        BindGroupLayoutDesc g2 = layoutFromReflection(shaders, 2);
        LayoutSet set{&g0, nullptr, &g2, nullptr};
        const auto attrs = goodAttributes();
        CHECK(validateAgainstReflection(shaders, set, 16, attrs).empty());
        CHECK(validateAgainstReflection(shaders, set, 32, attrs).empty()); // 파이프라인이 더 커도 된다

        SUBCASE("missing layout slot") {
            set[2] = nullptr;
            CHECK(validateAgainstReflection(shaders, set, 16, attrs).find("슬롯 2") != std::string::npos);
        }
        SUBCASE("binding missing from layout") {
            g2.entries.pop_back();
            CHECK(validateAgainstReflection(shaders, set, 16, attrs).find("colorSampler") != std::string::npos);
        }
        SUBCASE("type mismatch") {
            g2.entries[0].type = BindingType::StorageBuffer;
            const std::string e = validateAgainstReflection(shaders, set, 16, attrs);
            CHECK(e.find("Texture") != std::string::npos);
            CHECK(e.find("StorageBuffer") != std::string::npos);
        }
        SUBCASE("stage missing") {
            g0.entries[0].stages = kPS;
            CHECK(validateAgainstReflection(shaders, set, 16, attrs).find("stages") != std::string::npos);
        }
        SUBCASE("push constants") {
            CHECK(validateAgainstReflection(shaders, set, 8, attrs).find("push constant") != std::string::npos);
            CHECK_FALSE(validateAgainstReflection(shaders, set, 18, attrs).empty());  // 4 의 배수
            CHECK_FALSE(validateAgainstReflection(shaders, set, 132, attrs).empty()); // > 128
        }
        SUBCASE("vertex input not supplied") {
            std::vector<VertexAttribute> a = attrs;
            a[1].semantic = "COLOR";
            CHECK(validateAgainstReflection(shaders, set, 16, a).find("TEXCOORD0") != std::string::npos);
        }
    }

    TEST_CASE("validateBindGroup: exact coverage, live resources, storage stride") {
        const BindGroupLayoutDesc layout{{{0, BindingType::Texture, kPS, TextureDim::Tex2D},
                                          {1, BindingType::Sampler, kPS, TextureDim::Tex2D},
                                          {2, BindingType::StorageBuffer, kVS, TextureDim::Tex2D}},
                                         "material"};
        BindGroupDesc g;
        g.debugName = "g";
        g.entries.resize(3);
        g.entries[0].binding = 0;
        g.entries[0].texture = RhiTexture{1, 1};
        g.entries[1].binding = 1;
        g.entries[1].sampler = RhiSampler{1, 1};
        g.entries[2].binding = 2;
        g.entries[2].buffer = RhiBuffer{1, 1};
        g.entries[2].stride = 16;
        CHECK(validateBindGroup(layout, g, kAlive).empty());
        CHECK(validateBindGroup(layout, g, {}).empty()); // 살아 있음 검사 생략

        SUBCASE("entry count") {
            g.entries.pop_back();
            CHECK(validateBindGroup(layout, g, kAlive).find("항목 2 개") != std::string::npos);
        }
        SUBCASE("binding twice, another missing") {
            g.entries[1].binding = 0;
            CHECK(validateBindGroup(layout, g, kAlive).find("binding 0 항목이 2 개") != std::string::npos);
        }
        SUBCASE("dead texture") {
            g.entries[0].texture = RhiTexture{};
            CHECK(validateBindGroup(layout, g, kAlive).find("텍스처") != std::string::npos);
        }
        SUBCASE("dead sampler") {
            g.entries[1].sampler = RhiSampler{};
            CHECK(validateBindGroup(layout, g, kAlive).find("샘플러") != std::string::npos);
        }
        SUBCASE("storage stride 0") {
            g.entries[2].stride = 0;
            CHECK(validateBindGroup(layout, g, kAlive).find("stride") != std::string::npos);
        }
    }

    TEST_CASE("layoutSignature: same shape → same key, any difference → different key") {
        const BindGroupLayoutDesc a{{{0, BindingType::ConstantBuffer, kVS | kPS, TextureDim::Tex2D}}, "a"};
        BindGroupLayoutDesc b = a;
        b.debugName = "b"; // 이름은 키에 들어가지 않는다
        CHECK(layoutSignature(a) == layoutSignature(b));
        b.entries[0].stages = kVS;
        CHECK(layoutSignature(a) != layoutSignature(b));
        b = a;
        b.entries[0].binding = 1;
        CHECK(layoutSignature(a) != layoutSignature(b));
        CHECK(layoutSignature(BindGroupLayoutDesc{}).empty());
    }

#ifdef SBX_HAS_SHADERS
    TEST_CASE("generated shaders: embedded bytecode and reflection match the HLSL") {
        namespace bc = render::shaders::basic_color;
        const ShaderReflection& r = bc::reflection();
        CHECK(r.pushConstantBytes == bc::kPushConstantBytes);
        CHECK(sizeof(bc::Push) == bc::kPushConstantBytes);
        REQUIRE(r.bindings.size() == 1);
        CHECK(r.bindings[0].group == bc::kFrameGroup);
        CHECK(r.bindings[0].binding == bc::kFrameBinding);
        CHECK(r.bindings[0].type == BindingType::ConstantBuffer);
        CHECK(r.bindings[0].size == sizeof(bc::Frame));
        REQUIRE(r.vertexInputs.size() == 2);
        CHECK(r.vertexInputs[0].semantic == "POSITION");
        CHECK(r.vertexInputs[1].semantic == "COLOR");
        CHECK(r.vertexInputs[1].components == 4);

        for (const ShaderBytecode& code : {bc::vs(), bc::ps()}) {
            REQUIRE(code.dxil.size() > 4);
            CHECK(std::memcmp(code.dxil.data(), "DXBC", 4) == 0); // DXIL 컨테이너
            REQUIRE(code.spirv.size() > 4);
            u32 magic = 0;
            std::memcpy(&magic, code.spirv.data(), 4);
            CHECK(magic == 0x07230203u);
        }
        CHECK(bc::vs().stage == ShaderStage::Vertex);
        CHECK(bc::ps().stage == ShaderStage::Pixel);

        namespace bt = render::shaders::basic_texture;
        const std::array<const ShaderReflection*, 1> tex{&bt::reflection()};
        const BindGroupLayoutDesc g2 = layoutFromReflection(tex, 2);
        REQUIRE(g2.entries.size() == 2);
        CHECK(g2.entries[0].type == BindingType::Texture);
        CHECK(g2.entries[1].type == BindingType::Sampler);
        CHECK(g2.entries[0].stages == kPS);
    }
#endif
}
