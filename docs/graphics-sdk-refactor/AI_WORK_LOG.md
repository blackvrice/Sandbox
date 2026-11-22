# AI 작업 로그 (`AI_WORK_LOG.md`)

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
