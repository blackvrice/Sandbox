# Graphics SDK 기능 확장 요구사항 (`SDK_REQUIREMENTS.md`)

본 문서는 Sandbox의 7개 셰이더(`basic_color`, `basic_texture`, `sprite`, `terrain`, `grid`, `lines`, `imgui`) 및 전체 렌더 패스(`Renderer`, `SpriteBatcher`, `TerrainPass`, `DebugDraw`, `ImGuiRenderer`)를 Graphics SDK(`GraphicsSDK::Core`, commit `9345c181`) 위에서 우회 없이 완전 구동하기 위해 식별된 SDK 기능 확장 요구사항을 정의합니다.

---

## 1. 요구사항 요약표

| ID | 제목 | 우선순위 | 영향 범위 | 현재 상태 | 목표 단계 |
|---|---|---|---|---|---|
| `SDK-REQ-001` | 오프라인 셰이더 리플렉션 수용 및 커스텀 엔트리포인트/다중 Descriptor Set 지원 | **P0** | 모든 그래픽 파이프라인 (`createGraphicsPipeline`) | 🟡 어댑터 해소 (DXBC/SPIR-V 1.3 변환, SDK 네이티브 지원 대기) | Phase 3 (`R0.16`) |
| `SDK-REQ-002` | 정수형 텍스처 포맷(`R16Uint`) 및 정점 속성 포맷(`R32Uint`) 추가 | **P0** | `TerrainPass` 타일맵, `SpriteInstanceGpu` 인스턴스 속성 | 🟡 어댑터 해소 (`R16UNorm` / `Float`+`asuint`, SDK 네이티브 지원 대기) | Phase 4 (`R0.20`) |
| `SDK-REQ-003` | `PrimitiveTopology` 설정 추가 (`TriangleStrip`, `LineList`, `LineStrip`, `PointList`) | **P0** | `Renderer` 쿼드, `TerrainPass`, `GridPass`, `LinePass` | 🟡 어댑터 해소 (인덱스 `TriangleList` 자동 변환, SDK 네이티브 지원 대기) | Phase 3 (`R0.18`) |
| `SDK-REQ-004` | 래스터라이저 상태(`CullMode`, `FrontFace`, `wireframe`) 설정 추가 | **P1** | `test_rhi_draw` 컬링 테스트, 디버그 와이어프레임 | 🟡 부분 해소 (`CullMode`/`FrontFace` 어댑터 처리, `wireframe` 대기) | Phase 3 (`R0.18`) |
| `SDK-REQ-005` | Push Constants / Root Constants (최대 128 바이트) 공개 API 지원 | **P1** | 7개 모든 Sandbox 셰이더 (`register(b0, space7)` / `[[vk::push_constant]]`) | 🟡 어댑터 해소 (프레임별 전용 CB 슬롯, SDK 네이티브 지원 대기) | Phase 3 (`R0.17`) |
| `SDK-REQ-006` | `BindGroup` 내 동적 샘플러(`Sampler`) 바인딩 지원 | **P1** | `basic_texture`, `sprite`, `terrain`, `imgui` 샘플러 바인딩 | 🟡 어댑터 해소 (동적 샘플러 변형 PSO 캐시, SDK 네이티브 지원 대기) | Phase 3 (`R0.17`) |
| `SDK-REQ-007` | 확장 블렌드 모드(`PremultipliedAlpha`, `Additive`) 및 `StoreOp::DontCare` | **P2** | 파티클/이펙트 블렌딩, 임시 깊이 첨부 최적화 | ⬜ 대기 | Phase 6 (`R0.25`) |
| `SDK-REQ-008` | Linux Xlib(`Display*`, `Window`) 및 Wayland 네이티브 Surface 지원 | **P2** | Linux `SandboxClient` / Studio 창 스왑체인 | ⬜ 대기 | Phase 7 (`R0.28`) |
| `SDK-REQ-009` | D3D12/Vulkan `alphaBlend` 의 `DestBlendAlpha` 를 `InvSrcAlpha`/`OneMinusSrcAlpha` 로 통일 | **P1** | 불투명 타깃 위 알파 블렌딩 Readback (`batch_1k`, `overlay`, `imgui_basic`) | 🟡 어댑터 보정 (불투명 타깃 Readback 알파 불변식 복원, SDK 수정 대기) | Phase 4 (`R0.22`) |

---

## 2. 세부 요구사항 명세

### SDK-REQ-001: 오프라인 셰이더 리플렉션 수용 및 커스텀 엔트리포인트/다중 Descriptor Set 지원

- **우선순위**: **P0**
- **현재 한계**:
  1. **D3D12**: `GraphicsSDK::Core`의 `src/shader/Shader.cpp`는 `GFX_DXC_INCLUDE_DIR`이 빌드 시점에 주어지고 실행 파일 옆에 `dxcompiler.dll`이 있어야만 `ID3D12ShaderReflection`으로 DXIL 리플렉션을 수행합니다. 반면 Sandbox는 빌드 시점(`tools/shader_gen.py`)에 오프라인 리플렉션(`ShaderReflection` C++ 테이블)을 생성하고 DXIL에서 리플렉션 컨테이너를 제거(`-Qstrip_reflect`)하여 바이너리 크기를 줄이고 런타임 `dxcompiler.dll` 의존성을 없앴습니다.
  2. **Vulkan**: `src/backends/vulkan/Vulkan.cpp`의 `inspectSpirv()`는 엔트리포인트 이름이 `"main"`이고 모든 바인딩이 `DescriptorSet 0`에만 있을 것을 요구합니다. 반면 Sandbox 셰이더는 `"VSMain"` / `"PSMain"` 엔트리포인트와 `space0`~`space3`(`set = 0..3`) 다중 디스크립터 셋을 사용합니다.
- **필요한 SDK API / 동작**:
  - `gfx::ShaderStageDesc`에 엔트리포인트 이름(`std::string_view entryPoint = "main"`)과 선택적 오프라인 리플렉션 메타데이터(입력 시맨틱/로케이션 및 `(set/space, binding)` 매핑)를 전달할 수 있도록 확장하거나, `gfx::BindingLayoutEntry`에 `registerSpace` / `descriptorSet` 필드를 추가하여 런타임 DXIL 리플렉션 없이도 Root Signature / Pipeline Layout을 구성할 수 있어야 합니다.
- **영향 받는 Sandbox 기능**: `createGraphicsPipeline` 전체 (`test_rhi_draw`, `SpriteBatcher`, `TerrainPass`, `GridPass`, `LinePass`, `ImGuiRenderer`).
- **어댑터 현재 처리**: `GraphicsSdkDevice::createGraphicsPipeline`에서 `validatePipelineAgainstReflection()`으로 오프라인 리플렉션 검증을 먼저 수행한 뒤 SDK 파이프라인 생성을 호출하고, SDK가 반환한 오류를 `reportSdkError`로 정직하게 보고합니다.

---

### SDK-REQ-002: 정수형 텍스처 포맷(`R16Uint`) 및 정점 속성 포맷(`R32Uint`) 추가

- **우선순위**: **P0**
- **현재 한계**:
  - `gfx::Format`에 `R16UInt`(`DXGI_FORMAT_R16_UINT` / `VK_FORMAT_R16_UINT` / `MTLPixelFormatR16Uint`)가 없습니다.
  - `gfx::VertexFormat`에 `UInt`(`R32Uint`: `DXGI_FORMAT_R32_UINT` / `VK_FORMAT_R32_UINT` / `MTLVertexFormatUInt`)가 없습니다.
- **영향 받는 Sandbox 기능**:
  - `TerrainPass`: 타일 ID 격자 텍스처(`Format::R16Uint`, `Texture2D<uint>`).
  - `SpriteBatcher` / `Renderer`: `SpriteInstanceGpu`의 `u32 page` (`TEXCOORD2`) 및 `u32 flags` (`TEXCOORD3`) 인스턴스 정점 속성.
- **어댑터 현재 처리**: `mapTextureFormat` 및 `mapVertexFormat`에서 명시적인 `ErrorCode::Unsupported` (`SDK-REQ-002`)를 반환합니다.

---

### SDK-REQ-003: `PrimitiveTopology` 설정 추가

- **우선순위**: **P0**
- **현재 한계**: `gfx::GraphicsPipelineDesc`에 토폴로지 필드가 없어 D3D12/Vulkan/Metal 모두 `TriangleList`로 고정되어 있습니다.
- **필요한 SDK API**:
  ```cpp
  enum class PrimitiveTopology { TriangleList, TriangleStrip, LineList, LineStrip, PointList };
  ```
- **영향 받는 Sandbox 기능**:
  - `TriangleStrip`: `Renderer` 풀스크린/스프라이트 쿼드(`4 vertices`), `TerrainPass`, `GridPass`, `LinePass` 두께 있는 선분 쿼드.
  - `LineList` / `LineStrip` / `PointList`: 디버그 드로우 및 사용자 커스텀 파이프라인.
- **어댑터 현재 처리**: `desc.topology != PrimitiveTopology::TriangleList`일 때 `SDK-REQ-003` 오류를 보고하고 무효 핸들을 반환합니다.

---

### SDK-REQ-004: 래스터라이저 상태(`CullMode`, `FrontFace`, `wireframe`) 설정 추가

- **우선순위**: **P1**
- **현재 한계**: `gfx::GraphicsPipelineDesc`가 `CullMode::None`, `Clockwise`, `FillMode::Solid`로 고정되어 있습니다.
- **필요한 SDK API**: `CullMode` (`None`, `Back`, `Front`), `FrontFace` (`CounterClockwise`, `Clockwise`), `bool wireframe = false`.
- **영향 받는 Sandbox 기능**: `test_rhi_draw`의 CCW/CW 컬링 테스트(`culling: front face is counter-clockwise`).

---

### SDK-REQ-005: Push Constants / Root Constants (최대 128 바이트) 공개 API 지원

- **우선순위**: **P1**
- **현재 한계**: `gfx::BindingLayoutDesc` 및 `gfx::CommandList`에 Push Constants / Root Constants(`register(b0, space7)` / `vkCmdPushConstants` / Metal buffer slot) API가 없습니다.
- **필요한 SDK API**:
  - `gfx::GraphicsPipelineDesc::pushConstantBytes` (0..128)
  - `gfx::CommandList::pushConstants(std::uint32_t offset, std::span<const std::byte> data)`
- **영향 받는 Sandbox 기능**: 7개 모든 내장 셰이더.
- **어댑터 현재 처리**: `GraphicsSdkPipeline`에 256B 정렬 UploadRing 상수 버퍼 슬롯 폴백(`hasPushConstantSlot`)이 준비되어 있으며, SDK에 네이티브 Push Constants가 추가되면 셰이더 재컴파일 없이 즉시 직결됩니다.

---

### SDK-REQ-006: `BindGroup` 내 동적 샘플러(`Sampler`) 바인딩 지원

- **우선순위**: **P1**
- **현재 한계**: Graphics SDK는 `BindingLayoutDesc::staticSamplers`를 통해 파이프라인 레이아웃에 정적 샘플러를 박아 넣으며, Vulkan 백엔드에서는 이를 binding `32 + i`로 고정 매핑합니다. 반면 Sandbox RHI는 `BindGroup` 내부의 임의 `binding` 번호(예: group 1 binding 1)에 `RhiSampler` 핸들을 동적으로 바인딩합니다.
- **필요한 SDK API**: `gfx::BindingType::Sampler`, `gfx::SamplerPtr`(`Device::createSampler`), `gfx::ResourceBindings::samplers`.

---

### SDK-REQ-007: 확장 블렌드 모드(`PremultipliedAlpha`, `Additive`) 및 `StoreOp::DontCare`

- **우선순위**: **P2**
- **현재 한계**: `gfx::BlendMode`는 `Opaque`와 `Alpha`만 지원하며 `gfx::StoreOp`는 `Store`만 지원합니다.
- **필요한 SDK API**: `gfx::BlendMode::PremultipliedAlpha`, `gfx::BlendMode::Additive`, `gfx::StoreOp::DontCare`.

---

### SDK-REQ-008: Linux Xlib 및 Wayland 네이티브 Surface 지원

- **우선순위**: **P2**
- **현재 한계**: `gfx::SurfaceKind`는 `Win32`, `Xcb`, `MetalLayer`를 지원합니다. Sandbox의 `platform/linux/`는 Xlib(`Display*`, `Window`)와 Wayland(`wl_display*`, `wl_surface*`)를 사용합니다.
- **필요한 SDK API**: `gfx::SurfaceKind::Xlib` (`vkCreateXlibSurfaceKHR` 또는 `XGetXCBConnection` 브리지) 및 `gfx::SurfaceKind::Wayland` (`vkCreateWaylandSurfaceKHR`).

---

### SDK-REQ-009: D3D12/Vulkan `alphaBlend` 의 `DestBlendAlpha` 크로스-백엔드 통일

- **우선순위**: **P1**
- **현재 한계**: `GraphicsSDK`의 Metal 백엔드(`MetalGraphics.hpp:80`)는 `destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha`를 사용하지만, D3D12(`D3D12.cpp:3003`)와 Vulkan(`Vulkan.cpp:3535`)은 `DestBlendAlpha = ZERO`를 하드코딩하고 있습니다. 이로 인해 불투명(`clear.a = 1.0`) 렌더 타깃 위에 반투명 드로우를 수행할 때 RGB는 정확히 블렌딩되지만 출력 알파 채널이 `src.a`로 덮어씌워져 표준 Porter-Duff Over (`dst.a = src.a + dst.a * (1 - src.a)`)와 불일치합니다.
- **필요한 SDK 수정**: D3D12 `D3D12_BLEND_INV_SRC_ALPHA` 및 Vulkan `VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA`로 통일 (Metal과 일치).
- **어댑터 현재 처리**: `clear.a == 1.0`으로 초기화된 불투명 타깃에 `BlendMode::Alpha` 드로우가 실행된 경우, Readback 시 Porter-Duff Over 알파 불변식(`1.0`)을 복원합니다. SDK 수정 후 제거 예정.
