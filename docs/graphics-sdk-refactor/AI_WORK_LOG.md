# AI 작업 로그 (`AI_WORK_LOG.md`)

---

## 세션 2 — 2026-11-23 (`R0.16` ~ `R0.24`: Phase 3 셰이더·파이프라인, Phase 4 상위 렌더 패스, Phase 5 ImGui 및 31/31 전체 GPU 검증)

### 1. 수행한 작업
1. **Phase 3 셰이더 컴파일 및 파이프라인 전환 (`R0.16` ~ `R0.18`)**:
   - `tools/shader/gen_gfxsdk_spirv.py`를 작성하여 7종 Sandbox 내장 셰이더(14개 스테이지: `basic_color`, `basic_texture`, `sprite`, `terrain`, `grid`, `lines`, `imgui`)를 GraphicsSDK Vulkan 규약(SPIR-V 1.3, `entry="main"`, `DescriptorSet 0`, `binding=i`/`32+j`, `GLSL.std.450` 미사용, `OpTypeImage` Depth `2→0` 정규화)으로 컴파일하고 `render/graphics_sdk/GraphicsSdkSpirvData.hpp`에 내장했습니다.
   - `render/graphics_sdk/GraphicsSdkShaders.{hpp,cpp}`를 구현하여 D3D12(`gfx::compileHlsl()` SM5.1 프로세스 캐시), Vulkan(`spirv_data::k*`), Metal(MSL) 백엔드별 셰이더 변환을 완성했습니다.
   - `Format::R16Uint` → `gfx::Format::R16UNorm`(`terrain.ps`에서 `(uint)(raw * 65535.0 + 0.5)` 복원), 정점 `Format::R32Uint` → `gfx::VertexFormat::Float`(`sprite.vs`에서 `asuint()` 복원)으로 매핑했습니다.
   - 바인딩된 `RhiSampler` `(filter, address)` 조합별 PSO 변형 캐시(`resolvePipelineVariant`)와 슬롯별 전용 `pushBuffer`(256 KB)를 구현했습니다.
   - 공용 6-인덱스 쿼드 스트립 버퍼(`m_quadStripIndexBuffer`) + 동적 스트립→리스트 변환 및 2D CPU 와인딩 컬링(`tryDrawWithCpuCulling`)을 구현했습니다.
2. **Phase 4 ~ Phase 5 상위 렌더러 패스, ImGui 및 실기 클라이언트 검증 (`R0.19` ~ `R0.24`)**:
   - `prepareAndReplay()`에 렌더 패스 진입 전 Vertex/Index/Constant 버퍼 상태 사전 전이 스캔을 추가했습니다.
   - 1-layer 스프라이트 아틀라스(`"sprite atlas"`)를 `TextureDimension::Texture2DArray`로 생성하고, Vulkan 부분 업로드 전 1회 `fullSlice` 제로 초기화를 수행하도록 수정했습니다.
   - GraphicsSDK D3D12/Vulkan의 `DestBlendAlpha = ZERO` 불일치(`SDK-REQ-009`)에 대해 불투명 RT Readback 알파 불변량 보정(`applyOpaqueAlphaFixup`)을 추가했습니다.
   - 정적 매핑 Upload 버퍼의 `lastUploadedShadow` 변경 감지를 추가하여 `SandboxClient`의 `Cannot overwrite a buffer in flight` 오류를 해소했습니다.
   - `tests/render/CMakeLists.txt`에서 `SBX_RENDER_TEST_FILTER`를 완전 제거하여 전체 31개 GPU 테스트가 기본 CTest에 포함되도록 전환했습니다.
3. **벤치마크 비교 및 문서 갱신**:
   - `RelWithDebInfo` 빌드에서 `sbx_render_tests` 31/31(D3D12 WARP, D3D12 HW, Vulkan HW), `SandboxClient` 1920×1080 `--vsync off` (`ecosystem_small`, `ecosystem_10k`) 성능을 실측하여 `BENCHMARK_COMPARISON.md`, `TEST_REPORT.md`, `DEVELOPMENT_DASHBOARD.md`, `REFACTORING_ROADMAP.md`, `SDK_REQUIREMENTS.md`, `VERSION_HISTORY.md`, `AI_HANDOFF.md`, `docs/17-SOURCE-MAP.md`, `DEVELOPMENT_LOG.md`에 반영했습니다.

### 2. 변경 및 추가된 파일
- `tools/shader/gen_gfxsdk_spirv.py` (신규)
- `render/graphics_sdk/GraphicsSdkSpirvData.hpp` (신규)
- `render/graphics_sdk/GraphicsSdkShaders.hpp`, `render/graphics_sdk/GraphicsSdkShaders.cpp` (신규)
- `render/CMakeLists.txt`
- `render/graphics_sdk/GraphicsSdkTypes.cpp`
- `render/graphics_sdk/GraphicsSdkPipeline.hpp`
- `render/graphics_sdk/GraphicsSdkResource.hpp`
- `render/graphics_sdk/GraphicsSdkDevice.hpp`, `render/graphics_sdk/GraphicsSdkDevice.cpp`
- `render/graphics_sdk/GraphicsSdkCommandList.cpp`
- `tests/render/CMakeLists.txt`
- `docs/17-SOURCE-MAP.md`, `DEVELOPMENT_LOG.md`
- `docs/graphics-sdk-refactor/{DEVELOPMENT_DASHBOARD,REFACTORING_ROADMAP,SDK_REQUIREMENTS,TEST_REPORT,BENCHMARK_COMPARISON,VERSION_HISTORY,AI_HANDOFF,AI_WORK_LOG}.md`

### 3. 실행한 테스트 명령 및 결과
- `sbx_render_tests.exe --rhi dx12 --warp --debug`: **31/31 PASS (527 assertions)**, 누수 0, Debug Layer 경고 0, 예상 오류 8/8
- `sbx_render_tests.exe --rhi dx12 --debug`: **31/31 PASS (527 assertions)**, 누수 0, Debug Layer 경고 0, 예상 오류 8/8
- `sbx_render_tests.exe --rhi vulkan --debug` (+ VVL 1.3.290): **31/31 PASS (527 assertions)**, 누수 0, VVL 경고 0, 예상 오류 8/8
- `SandboxClient.exe --world ecosystem_small --frames 300 --vsync off --rhi dx12 --rhi-debug --safe-mode --no-audio`: Debug Layer 경고 0 · 오류 0
- `ctest --test-dir build/windows-msvc-gfxsdk -C RelWithDebInfo -j 4`: **99/99 실행 테스트 100% PASS** (`studio_gui_smoke` 1건 Skipped)
- `check_includes.py` / `check_docs.py` / `gen_roadmap.py --check`: 모두 **0 errors**

### 4. 남은 작업 (다음 세션)
- Phase 6 (`R0.25` ~ `R0.27`): 멀티스레드 커맨드 기록 · CPU 오버헤드 절감, Dedicated Copy Queue 활용, GPU 메모리/UploadRing 최적화

---

## 세션 1 — 2026-11-22 (`R0.1` ~ `R0.15`: Phase 0 분석·벤치마크, Phase 1 어댑터 구현, Phase 2 수명·동기화 구현)

### 1. 수행한 작업
1. **저장소 및 문서 조사**:
   - `D:/Game/Sandbox` (`master` @ `189ac37`), `D:/source/GraphicsSDK` (`main` @ `9345c181`), `D:/source/GraphicsUI`의 설계 문서, 최신 커밋, GitHub Actions 상태, RHI 및 SDK 공개 헤더를 전수 대조했습니다.
2. **병렬 개발 Worktree 구축 (`R0.3`)**:
   - 기존 `D:/Game/Sandbox`와 `.claude/worktrees/ecstatic-mestorf-5f72fc`를 전혀 건드리지 않고, `git worktree add ../Sandbox-GraphicsSDK -b refactor/graphics-sdk origin/master`로 `D:/Game/Sandbox-GraphicsSDK`를 분리 구축했습니다.
   - `D:/source/GraphicsSDK`(루트에 미커밋 `tests/VulkanAllocationBenchmarkTests.cpp` 존재)를 수정하지 않고, `D:/Game/Sandbox-GraphicsSDK/build/gfx-sdk-build-mc`에서 out-of-tree Multi-Config(`Debug`/`RelWithDebInfo`/`Release`, `CMAKE_DEBUG_POSTFIX=d`)로 빌드하여 `build/gfx-sdk-install`에 설치했습니다.
3. **Phase 0 기존 시스템 분석 및 기준선 실측 (`R0.1` ~ `R0.2`)**:
   - `build/windows-msvc` (`SBX_USE_GRAPHICS_SDK=OFF`, `SBX_ENABLE_VULKAN=ON`, `SBX_BUILD_BENCH=ON`)를 빌드하고 D3D12 WARP, D3D12(GTX 1660 SUPER), Vulkan(GTX 1660 SUPER + VVL 1.3.290)에서 `sbx_render_tests`(31/31 PASS), `sbx_bench --only render`, `SandboxClient` 1920×1080 `--vsync off` (`ecosystem_small`, `ecosystem_10k`) 기준선을 실측했습니다.
4. **Phase 1 & Phase 2 Graphics SDK 어댑터 구현 (`R0.4` ~ `R0.15`)**:
   - `SBX_USE_GRAPHICS_SDK` 옵션(기본값 `OFF`)과 `find_package(GraphicsSDK CONFIG REQUIRED)`(`SandboxRender` `PRIVATE` 링크)를 구성했습니다.
   - `render/graphics_sdk/` 아래에 `GraphicsSdkTypes`, `GraphicsSdkResource`, `GraphicsSdkPipeline`, `GraphicsSdkCommandQueue`, `GraphicsSdkSwapChain`, `GraphicsSdkDevice`, `GraphicsSdkCommandList`, `GraphicsSdkFactory`를 구현했습니다.
   - Stage-Before-Record 지연 커맨드 재생, 슬롯별 SDK Upload 링 버퍼, `FrameSlot`별 `CommandPool`/`CommandList` 소유권 분리, 5000ms 분할 펜스 대기 및 드라이버 레이스 방어, `beginTimestampScope` 기반 GPU 타임스탬프 변환을 구현했습니다.
5. **문서 체계 작성**:
   - `docs/graphics-sdk-refactor/` 아래 18개 문서 및 `docs/17-SOURCE-MAP.md`, `DEVELOPMENT_LOG.md`를 갱신했습니다.

### 2. 변경 및 추가된 파일
- `cmake/SbxOptions.cmake`
- `render/CMakeLists.txt`
- `render/rhi/Backends.hpp`
- `render/rhi/RenderDeviceFactory.cpp`
- `render/graphics_sdk/GraphicsSdkTypes.hpp`, `render/graphics_sdk/GraphicsSdkTypes.cpp`
- `render/graphics_sdk/GraphicsSdkResource.hpp`
- `render/graphics_sdk/GraphicsSdkPipeline.hpp`, `render/graphics_sdk/GraphicsSdkPipeline.cpp`
- `render/graphics_sdk/GraphicsSdkCommandQueue.hpp`, `render/graphics_sdk/GraphicsSdkCommandQueue.cpp`
- `render/graphics_sdk/GraphicsSdkSwapChain.hpp`, `render/graphics_sdk/GraphicsSdkSwapChain.cpp`
- `render/graphics_sdk/GraphicsSdkDevice.hpp`, `render/graphics_sdk/GraphicsSdkDevice.cpp`
- `render/graphics_sdk/GraphicsSdkCommandList.hpp`, `render/graphics_sdk/GraphicsSdkCommandList.cpp`
- `render/graphics_sdk/GraphicsSdkFactory.cpp`
- `tests/render/CMakeLists.txt`
- `tests/unit/render/test_rhi_helpers.cpp`
- `tools/check_includes.py`
- `docs/17-SOURCE-MAP.md`
- `DEVELOPMENT_LOG.md`
- `docs/graphics-sdk-refactor/*` (18개 문서)

### 3. 실행한 테스트 명령 및 결과
- `SandboxTests.exe --test-suite=render`: **33/33 PASS** (340,535 assertions)
- `sbx_render_tests.exe --rhi dx12 --warp --debug` (Phase 1·2 10개 케이스): **10/10 PASS**, 누수 0, 경고 0
- `sbx_render_tests.exe --rhi dx12 --debug` (GTX 1660 SUPER, Phase 1·2 10개 케이스): **10/10 PASS**, 누수 0, 경고 0
- `sbx_render_tests.exe --rhi vulkan --debug` (GTX 1660 SUPER + VVL 1.3.290, Phase 1·2 10개 케이스): **10/10 PASS**, 누수 0, 경고 0
- `ctest --test-dir build/windows-msvc-gfxsdk -C RelWithDebInfo -j 4`: **99/99 실행 테스트 100% PASS** (`studio_gui_smoke` 1건 Skipped)

### 4. 남은 작업 (다음 세션)
- Phase 3 (`R0.16` ~ `R0.18`): `SDK-REQ-001`~`006` 해소 및 셰이더 드로우 파이프라인(`test_rhi_draw.cpp`) 활성화
- Phase 4 ~ 5 (`R0.19` ~ `R0.24`): `Renderer`, `SpriteBatcher`, `TerrainPass`, `GridPass`, `LinePass`, `ImGuiRenderer` 전환 및 31/31 전체 골든 이미지 통과
