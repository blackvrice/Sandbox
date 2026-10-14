#pragma once
// 셰이더 바이트코드 · 리플렉션 값 타입. docs/06-RENDERING.md 3.4·6장, ADR-0019.
// 생성 헤더(render/generated/*Shader.hpp — tools/shader/sbx_shader_gen.py)가 이 타입으로 바이트코드와 리플렉션을
// 내놓는다.
//
// 바인딩 규칙 (06 3.4): HLSL register(<b|t|s|u>N, spaceG) 에서 G = BindGroup 번호(0~3), N = 그룹 안 binding.
//   한 그룹 안에서 binding 번호는 레지스터 종류를 가리지 않고 고유해야 한다 (t0 · s1 — t0 · s0 은 안 된다).
//   push constant 는 cbuffer 하나: [[vk::push_constant]] + register(b0, space7) (D3D12 루트 상수).
//
// Metal 번호 (ADR-0035): Metal 에는 그룹이 없고 단계마다 버퍼 · 텍스처 · 샘플러 표가 따로 있다. 생성기가 프로그램(모든
//   단계)의 바인딩을 (group, binding) 순서로 종류마다 번호를 매겨 ReflectedBinding::metalIndex 에 적고, 같은 번호로 MSL 을
//   만든다. push constant = buffer 0 (setBytes), 리소스 버퍼 = 1.., 정점 버퍼 slot s = buffer 30 - s.

#include <array>
#include <span>
#include <string_view>

#include "foundation/types/Types.hpp"

namespace sbx::rhi {

enum class ShaderStage : u8 { Vertex = 1u << 0, Pixel = 1u << 1, Compute = 1u << 2 };
using ShaderStageMask = u8;
inline constexpr ShaderStageMask kAllGraphicsStages =
    static_cast<ShaderStageMask>(static_cast<u8>(ShaderStage::Vertex) | static_cast<u8>(ShaderStage::Pixel));

enum class BindingType : u8 {
    ConstantBuffer = 0, // cbuffer / ConstantBuffer<T>          (b)
    Texture,            // Texture2D · Texture2DArray            (t)
    StorageBuffer,      // StructuredBuffer<T> (읽기)            (t)
    StorageBufferRW,    // RWStructuredBuffer<T>                 (u)
    StorageTexture,     // RWTexture2D                           (u)
    Sampler,            // SamplerState                          (s)
};
[[nodiscard]] std::string_view bindingTypeName(BindingType t) noexcept;

enum class TextureDim : u8 { None = 0, Tex2D, Tex2DArray, Tex3D, Cube };

inline constexpr u32 kMaxBindGroups = 4;
inline constexpr u32 kPushConstantSpace = 7;
inline constexpr u32 kMaxPushConstantBytes = 128;

// Metal 번호 (tools/shader/sbx_shader_gen.py 의 METAL_* 와 같아야 한다 — ADR-0035)
inline constexpr u32 kMetalPushConstantBuffer = 0;
inline constexpr u32 kMetalFirstResourceBuffer = 1;
inline constexpr u32 kMetalVertexBufferTop = 30; // slot s → buffer 30 - s
inline constexpr u32 kMetalMaxVertexBuffers = 4;
inline constexpr u32 kMetalMaxTextures = 31;
inline constexpr u32 kMetalMaxSamplers = 16;
[[nodiscard]] constexpr u32 metalVertexBufferIndex(u32 slot) noexcept {
    return kMetalVertexBufferTop - slot;
}

struct ReflectedBinding {
    u32 group = 0;
    u32 binding = 0;
    BindingType type = BindingType::ConstantBuffer;
    ShaderStageMask stages = 0;
    u32 size = 0; // ConstantBuffer: 바이트 (16 의 배수). StorageBuffer: 요소 stride
    TextureDim dim = TextureDim::None;
    std::string_view name;
    u32 metalIndex = 0; // 종류(버퍼 · 텍스처 · 샘플러) 안의 Metal 번호 — 버퍼는 1 부터 (ADR-0035)
};

// 정점 입력 (SV_* 시스템 값은 제외). D3D12 입력 레이아웃은 semantic 이름·번호로, Vulkan 은 location 으로 묶는다.
struct ReflectedVertexInput {
    u32 location = 0;
    std::string_view semantic; // "POSITION", "COLOR", "TEXCOORD"
    u32 semanticIndex = 0;
    u32 components = 0; // float 1~4
};

struct ShaderReflection {
    std::span<const ReflectedBinding> bindings;
    std::span<const ReflectedVertexInput> vertexInputs; // VS 의 입력
    u32 pushConstantBytes = 0;
};

// 한 단계의 바이트코드. 백엔드는 자기 형식(D3D12: dxil, Vulkan: spirv, Metal: msl)만 쓴다.
struct ShaderBytecode {
    ShaderStage stage = ShaderStage::Vertex;
    std::string_view entry;
    std::span<const std::byte> dxil;
    std::span<const std::byte> spirv;
    std::string_view debugName;
    std::string_view msl;      // MSL 소스 (SBX_SHADER_MSL 빌드만 — 아니면 빈다). Metal 이 실행 중에 컴파일한다
    std::string_view mslEntry; // MSL 진입점 이름 (SPIRV-Cross 가 예약어를 피해 바꿀 수 있다)
};

} // namespace sbx::rhi
