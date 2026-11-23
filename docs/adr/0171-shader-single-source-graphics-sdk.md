# ADR-0171. 셰이더 원본은 shaders/*.hlsl 하나 — Graphics SDK 의 D3D12 는 그대로 컴파일, Vulkan 생성 SPIR-V 는 출처를 검사

- 상태: **Accepted** · 날짜: 2026-11-23
- 관련: [ADR-0168](0168-graphics-sdk-rhi-backend.md) (Graphics SDK 단일 백엔드), [ADR-0019](0019-dxc-nuget-pin-own-spirv-reflector-root-signature-layout.md)
  (HLSL · DXC · 내장), [06-RENDERING](../06-RENDERING.md) 6장

## 맥락

ADR-0168 의 어댑터는 빌드가 `shaders/*.hlsl` 로 만든 바이트코드 (sbx_add_shader — DXIL · SPIR-V · MSL) 를 D3D12 · Vulkan 에서
**쓰지 않고**, 디버그 이름 ("sprite.vs" …) 으로 맞춰 코드에 박은 사본을 썼다.

```text
D3D12   GraphicsSdkShaders.cpp 안의 HLSL 사본 7 개 → 런타임 FXC (SM 5.1 DXBC). SDK 가 DXC 없이 빌드돼 DXIL 리플렉션이 없다
Vulkan  tools/shader/gen_gfxsdk_spirv.py 안의 또 다른 사본 → DXC → GraphicsSdkSpirvData.hpp (SDK 계약: set 0 하나 · main ·
        GLSL.std.450 없음 — SDK-REQ-001, 그래서 push constant 를 cbuffer 로 · 바인딩을 어댑터 순서로 · 내장 함수를 손으로)
Metal   빌드의 MSL (shaders/*.hlsl 에서) — 그대로
```

`shaders/*.hlsl` 을 고쳐도 D3D12 · Vulkan 화면은 바뀌지 않았고 아무것도 알리지 않았다. 사본에는 SDK 의 형식 공백 (정수 정점 ·
정수 텍스처 형식 없음, SDK-REQ-002) 을 메우는 바뀐 뜻 (sprite page · flags = Float + asuint, terrain tiles = R16UNorm × 65535) 도
있어서, 그 뜻을 모르는 Metal 의 MSL (원본의 uint) 은 어댑터의 형식 (Float · R16UNorm) 과 어긋났다.

## 결정

1. **원본은 shaders/*.hlsl 하나.** SDK 형식 공백을 메우는 뜻을 원본에 옮긴다 (sprite `float page/flags` + `asuint`, terrain
   `Texture2D<float>` + `× 65535`) — 모든 백엔드가 같은 뜻 (어댑터의 형식 대응과 같다).
2. **D3D12 = 원본 그대로.** `cmake/SbxEmbedHlsl.cmake` 가 구성 때 원본을 (include 를 풀어) `render/generated/
   GraphicsSdkHlslSources.hpp` 에 넣고 어댑터가 FXC 로 컴파일한다. 손 사본을 지운다. FXC 가 모르는 `[[vk::push_constant]]` 는
   `SBX_PUSH_CONSTANT` (Common.hlsli — `__spirv__` 일 때만 그 속성) 로 바꾼다 (DXIL 은 원래 무시하던 속성).
3. **Vulkan = 생성 SPIR-V + 출처 검사.** SDK 계약 때문에 변형은 생성기 안에 남는다. 생성기가 원본 (include 를 푼 글, 줄 끝 LF)
   의 SHA-256 을 헤더에 적고 (`k<이름>SourceSha256`), 어댑터 `verifyVulkanShaderSources` 가 구성 때 계산한 지금 값과 견준다 —
   단위 시험 (test_rhi_helpers) 이 실패해 원본과 SPIR-V 가 조용히 갈라지지 않는다. 변형이 원본과 같은 뜻임을 확인한 뒤에만
   `--stamp-sources` 로 기록을 새로 쓴다.
4. SDK 가 SPIR-V 의 다중 set · push constant · GLSL.std.450 을 받게 되면 (SDK-REQ-001) 빌드의 SPIR-V 를 그대로 쓰고 생성기를
   지운다 — 이 ADR 을 Superseded 로.

## 결과

- D3D12 · Metal 은 원본 하나에서, Vulkan 은 원본과 어긋나면 시험이 멈춘다.
- D3D12 는 여전히 첫 사용 때 런타임 컴파일 (FXC — Windows 의 d3dcompiler) 이다. 빌드의 DXIL 은 SDK 가 DXC 리플렉션을 갖출 때까지
  쓰이지 않는다.
