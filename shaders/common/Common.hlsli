// 공통 HLSL. docs/06-RENDERING.md 6.2.
//   - 행렬은 column_major, mul(M, v). 상수 버퍼의 16 바이트 정렬은 직접 맞춘다 (생성 헤더의 static_assert 가 검사)
//   - 좌표는 엔진 규약(06 12장): NDC Y 위가 +1, 텍스처 원점 좌상단, CCW = 앞면. 백엔드 분기(#ifdef VULKAN) 금지
//   - 바인딩: register(<b|t|s|u>N, spaceG) — G = BindGroup(0~3). push constant 는 SBX_PUSH_CONSTANT + (b0, space7)
#ifndef SBX_COMMON_HLSLI
#define SBX_COMMON_HLSLI

// push constant 선언 앞에 붙인다 (b0, space7). SPIR-V (DXC -spirv → Vulkan · MSL) 에서만 [[vk::push_constant]] — DXIL 은 그
// 속성을 무시하고, FXC (Graphics SDK 의 D3D12 런타임 컴파일 SM 5.1 — ADR-0171) 는 그 문법을 모른다
#if defined(__spirv__)
#define SBX_PUSH_CONSTANT [[vk::push_constant]]
#else
#define SBX_PUSH_CONSTANT
#endif

// 0xAABBGGRR (RGBA8 리틀 엔디언) → float4
float4 unpackRgba8(uint c) {
    return float4(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, c >> 24) / 255.0;
}

float2 rotate2d(float2 p, float radians) {
    float s, c;
    sincos(radians, s, c);
    return float2(p.x * c - p.y * s, p.x * s + p.y * c);
}

#endif
