# Phase 1 & Phase 2 — Graphics SDK 의존성 및 RHI 어댑터 구현 보고서 (`PHASE1_ADAPTER.md`)

본 문서는 Phase 1(`R0.3`~`R0.11`) 및 Phase 2(`R0.12`~`R0.15`)에서 구현·검증된 Graphics SDK CMake 연동과 `render/graphics_sdk/` 어댑터 계층의 상세 구현 내용을 기록합니다.

---

## 1. CMake 패키지 연동 및 공존 구조 (`R0.3` ~ `R0.4`)

1. **`cmake/SbxOptions.cmake`**:
   ```cmake
   option(SBX_USE_GRAPHICS_SDK "Use Graphics SDK rendering backend" OFF)
   ```
   기본값을 `OFF`로 유지하여 Legacy 백엔드 빌드에 전혀 영향을 주지 않습니다.
2. **`render/CMakeLists.txt`**:
   - `SBX_USE_GRAPHICS_SDK=ON`일 때 `find_package(GraphicsSDK CONFIG REQUIRED)`를 호출하고 `render/graphics_sdk/*.cpp`를 컴파일합니다.
   - `SandboxRender`에 `SBX_HAS_GRAPHICS_SDK=1`(`PUBLIC`)을 정의하고 `GraphicsSDK::Core`를 `PRIVATE`으로만 링크하여, 상위 모듈(`SandboxClient`, `SandboxEditor`, `SandboxTests`)로 SDK 헤더가 누출되지 않도록 격리했습니다.
   - `SBX_USE_GRAPHICS_SDK=OFF`일 때는 기존 `render/dx12/`, `render/vulkan/`, `render/metal/` 소스를 그대로 컴파일합니다.
3. **`tools/check_includes.py`**:
   - `RESTRICTED` 목록에 `("render/graphics_sdk/", set())`을 추가하여 어댑터 내부에서 네이티브 그래픽/OS 헤더(`d3d12.h`, `vulkan.h`, `Metal.h`, `windows.h`, `imgui.h`)를 직접 include 하는 것을 금지하고 오직 `<graphics/Graphics.hpp>` 공개 API만 사용하도록 강제했습니다.

---

## 2. 어댑터(`render/graphics_sdk/`) 클래스별 구현 내역 (`R0.5` ~ `R0.15`)

| 파일 | 구현 내용 |
|---|---|
| `GraphicsSdkTypes.{hpp,cpp}` | `BackendType`, `Format`(`gfx::Format` 및 `gfx::VertexFormat`), `MemoryType`, `ResourceState`, `LoadOp`/`StoreOp`, `Filter`/`AddressMode`/`CompareOp`, `NativeWindowHandle` → `gfx::NativeSurfaceDesc`, `gfx::Error` → `sbx::Error` 변환 및 `verifyAdapterTypeMappings()` 자가 검증 함수 |
| `GraphicsSdkResource.hpp` | `GraphicsSdkBuffer`(`gfx::BufferPtr`, `cpuShadow`, `internal`, `cpuMapped`), `GraphicsSdkTexture`(`gfx::TexturePtr`, `trackedState`, `initialized`, `external`), `GraphicsSdkSampler`, `GraphicsSdkShader`, `GraphicsSdkGarbage` |
| `GraphicsSdkPipeline.{hpp,cpp}` | `GraphicsSdkBindGroupLayout`, `GraphicsSdkBindGroup`, `GraphicsSdkPipeline` 및 4개 바인드 그룹 슬롯 `(group, binding)` → 평탄화된 SDK binding index 매핑 테이블(`buildSdkBindingLayout`) |
| `GraphicsSdkCommandQueue.{hpp,cpp}` | `ICommandQueue` 구현. `prepareForSubmit` 호출, 타임라인 펜스 제출, 5000ms 유한 타임아웃 순차 대기(`wait`), 드라이버 펜스 레이스 시 `waitIdle()` 폴백, Readback 버퍼 자동 동기화(`syncCompletedReadbacks`) |
| `GraphicsSdkSwapChain.{hpp,cpp}` | `ISwapChain` 구현. `gfx::SwapChain` 래핑, `currentBackbuffer()`를 외부 `RhiTexture` 핸들로 등록하고 `present()`/`resize()` 전에 즉시 회수 |
| `GraphicsSdkDevice.{hpp,cpp}` | `IRenderDevice` 구현. 어댑터 열거 및 하드웨어/WARP 선택, Validation 폴백, `HandlePool`, `DeferredDestructionQueue`, `FrameSlot`별 `CommandPool`·`CommandList`·SDK Upload 링 버퍼 관리, `collectSlotTimestamps()` |
| `GraphicsSdkCommandList.{hpp,cpp}` | `ICommandList` 구현. `RecordedOp` 지연 재생으로 Stage-Before-Record 계약 준수, RenderPass(단일 및 MRT), `copyBuffer`, `copyBufferToTexture`/`copyTextureToBuffer`(256B/512B 정렬 검증 및 임의 부분 영역 복사), `writeTimestamp`/`resolveTimestamps` → `beginTimestampScope`/`endTimestampScope` 변환 |
| `GraphicsSdkFactory.cpp` | `sbx::rhi::graphics_sdk::createDevice(const DeviceDesc&)` 팩토리 엔트리 포인트 |

---

## 3. 검증 요약

- `SandboxTests --test-suite=render`: **33/33 PASS**
- `sbx_render_tests` (D3D12 WARP, D3D12 GTX 1660 SUPER, Vulkan GTX 1660 SUPER): **10/10 코어 RHI 테스트 케이스 PASS**, 리소스 누수 `0`, Debug Layer / VVL 경고 `0`
- 전체 `ctest` (`build/windows-msvc-gfxsdk`): **99/99 실행 테스트 100% PASS**

---

## 4. 관련 문서
- [../ARCHITECTURE.md](../ARCHITECTURE.md)
- [../API_COMPATIBILITY.md](../API_COMPATIBILITY.md)
- [../TEST_REPORT.md](../TEST_REPORT.md)
