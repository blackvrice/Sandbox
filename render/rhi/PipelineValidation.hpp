#pragma once
// 백엔드 독립 검사: 파이프라인 · 바인드 그룹이 셰이더 리플렉션 · 레이아웃과 맞는가. ADR-0019.
// 백엔드가 그래픽 API 를 부르기 전에 부른다 — D3D12 런타임 오류(또는 조용한 잘못된 바인딩) 대신 원인을 말하는 메시지.
// 모두 "빈 문자열 = 통과, 아니면 첫 문제의 설명" 을 돌려준다.

#include <array>
#include <span>
#include <string>

#include "render/rhi/RhiTypes.hpp"

namespace sbx::rhi {

using LayoutSet = std::array<const BindGroupLayoutDesc*, kMaxBindGroups>; // 없는 슬롯은 nullptr

[[nodiscard]] std::string validateLayout(const BindGroupLayoutDesc& layout);

// 셰이더가 쓰는 모든 바인딩이 같은 group · binding · 종류로 레이아웃에 있는가, push constant 크기,
// VS 정점 입력이 모두 속성으로 공급되는가
[[nodiscard]] std::string validateAgainstReflection(std::span<const ShaderReflection* const> shaders,
                                                    const LayoutSet& layouts, u32 pushConstantBytes,
                                                    std::span<const VertexAttribute> attributes);

// 그룹의 항목이 레이아웃의 각 binding 을 정확히 한 번, 맞는 리소스 종류로 채우는가.
// isBuffer · isTexture · isSampler: 핸들이 살아 있는 리소스인지 (백엔드가 넘긴다)
struct BindGroupResourceCheck {
    bool (*bufferAlive)(const void* ctx, RhiBuffer) = nullptr;
    bool (*textureAlive)(const void* ctx, RhiTexture) = nullptr;
    bool (*samplerAlive)(const void* ctx, RhiSampler) = nullptr;
    const void* ctx = nullptr;
};
[[nodiscard]] std::string validateBindGroup(const BindGroupLayoutDesc& layout, const BindGroupDesc& group,
                                            const BindGroupResourceCheck& alive);

// 레이아웃의 모양을 문자열로 (호환 비교 · 루트 시그니처 캐시 키)
[[nodiscard]] std::string layoutSignature(const BindGroupLayoutDesc& layout);

} // namespace sbx::rhi
