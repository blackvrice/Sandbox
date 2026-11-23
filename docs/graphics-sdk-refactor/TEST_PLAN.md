# 테스트 계획서 (`TEST_PLAN.md`)

본 문서는 Graphics SDK 기반 RHI 리팩토링의 모든 단계에서 기존 Sandbox 기능 보존, 렌더링 정확도, 메모리 안전성 및 시뮬레이션 결정론을 검증하기 위한 테스트 계층과 기준을 정의합니다.

---

## 1. 테스트 계층

### 1.1 아키텍처 및 경계 정적 검사 (`arch`, `docs`)
- **목적**: `render/graphics_sdk/`가 네이티브 그래픽/OS 헤더(`d3d12.h`, `vulkan.h`, `Metal.h`, `windows.h`, `imgui.h`)를 직접 include 하지 않고, `SandboxServer`가 `SandboxRender` 및 `GraphicsSDK::Core`에 의존하지 않음을 보장합니다.
- **실행 명령**:
  ```powershell
  python tools/check_includes.py --root .
  python tools/check_docs.py --root .
  python tools/gen_roadmap.py --root . --check
  ctest -R "arch_|docs_" --output-on-failure
  ```

### 1.2 RHI 헬퍼 및 어댑터 단위 테스트 (`SandboxTests --test-suite=render`)
- **목적**: GPU 디바이스 생성 없이도 포맷 정보(`formatInfo`), 정렬 계산, `UploadRing` 오프셋 회수, `HandlePool` 세대 검증, `DeferredDestructionQueue` 펜스 순서, 오프라인 리플렉션 검증(`PipelineValidation`), 그리고 `GraphicsSdkTypes`의 모든 열거형/서피스/오류 코드 매핑(`verifyAdapterTypeMappings`)을 검증합니다.
- **실행 명령**:
  ```powershell
  .\build\windows-msvc-gfxsdk\bin\RelWithDebInfo\SandboxTests.exe --test-suite=render
  ```

### 1.3 GPU RHI & 골든 이미지 테스트 (`sbx_render_tests`)
- **목적**: 실제 D3D12(WARP 및 물리 GPU), Vulkan(물리 GPU 및 Lavapipe), Metal 디바이스 위에서 리소스 수명, 렌더 패스, 복사, 타임스탬프, 파이프라인 검증 및 골든 레퍼런스 PNG(`tests/render/references/`) 픽셀 일치를 검증합니다.
- **합격 기준**:
  - 종료 시 `liveBuffers == 0`, `liveTextures == 0`, `livePipelines == 0`, `descriptorsUsed == 0`, `pendingDestructions == 0`
  - Debug Layer / Vulkan Validation Layer 경고 `0`건, 예상되지 않은 검증 오류 `0`건 (`expectValidationErrors(N)`과 정확히 일치)
  - 출력 로그 마지막 줄에 `render tests OK` 출력

### 1.4 시뮬레이션 결정론 및 전체 시스템 회귀 테스트 (`ctest`)
- **목적**: 그래픽 백엔드 교체가 시뮬레이션(`kSimVersion`, 골든 `WorldHash`), 리플레이, 네트워크 수렴, 서버, 스크립트 VM, 콘텐츠 파이프라인에 어떠한 부작용도 일으키지 않음을 검증합니다.
- **실행 명령**:
  ```powershell
  cmd /c "chcp 65001 >nul && ctest --test-dir build/windows-msvc-gfxsdk -C RelWithDebInfo -j 4 --output-on-failure"
  ```

---

## 2. Phase별 GPU 테스트 활성화 범위

| 단계 | `sbx_render_tests` 검증 범위 | 목표 통과 수 |
|---|---|---|
| **Phase 1 ~ 2 (`R0.3`~`R0.15`)** | Phase 7A 코어 RHI (Device Caps, Clear Single/MRT, Upload Orientation, Sub-region Copy, 12-Frame Ring Wrap-around, Handle Lifetime, Unaligned Copy Misuse) + Phase 8B GPU Timestamps + Phase 7B Pipeline/BindGroup Layout Validation | **10 / 10 test cases (101 assertions)** |
| **Phase 3 (`R0.16`~`R0.18`)** | 위 10개 + Phase 7B Draw (`triangle`, `coord_convention`, `culling`, `texture nearest/linear`, `push constant`, `indexed draw`, `pipeline lifetime`) | **18 / 18 test cases** |
| **Phase 4 (`R0.19`~`R0.22`)** | 위 18개 + Phase 8A (`SpriteBatcher`, `TextureAtlas`) + Phase 8B (`TerrainPass`, `GridPass`, `LinePass`) | **28 / 28 test cases** |
| **Phase 5 (`R0.23`~`R0.24`)** | 전체 31개 테스트 케이스 (+ Phase 8C `ImGuiRenderer` 클립·폰트·드로우) | **31 / 31 test cases (527 assertions)** |
