# ADR-0168: Sandbox RHI의 Graphics SDK 단일 백엔드 전환 및 자체 백엔드 제거

- **상태**: Accepted
- **날짜**: 2026-11-23
- **관련 문서**: [docs/graphics-sdk-refactor/ARCHITECTURE.md](../graphics-sdk-refactor/ARCHITECTURE.md), [docs/graphics-sdk-refactor/TEST_REPORT.md](../graphics-sdk-refactor/TEST_REPORT.md), [docs/graphics-sdk-refactor/BENCHMARK_COMPARISON.md](../graphics-sdk-refactor/BENCHMARK_COMPARISON.md)

## 배경

Sandbox는 그동안 `render/dx12/`, `render/vulkan/`, `render/metal/`에 D3D12 · Vulkan · Metal 백엔드를 자체 구현해 유지해 왔다. 독립적으로 개발 중인 `GraphicsSDK` (`GraphicsSDK::Core`, 고정 커밋 `9345c1816879c63f8c26f13fc876382b0c2ed054`)가 3대 그래픽 API 추상화, 타임라인 펜스, 다중 Queue(`Graphics`/`Copy`/`Compute`), 스왑체인 Tearing/리사이즈, 프로파일링 및 `GraphicsUI` 공통 기반을 제공함에 따라, 중복 구현을 제거하고 단일 그래픽 기반으로 통합할 필요가 생겼다.

## 결정

1. **자체 백엔드 제거**: `render/dx12/`, `render/vulkan/`, `render/metal/` (19개 파일)을 제거하고, `render/graphics_sdk/` 어댑터(`GraphicsSDK::Core` `PRIVATE` 링크)를 Sandbox RHI의 유일한 백엔드로 사용한다.
2. **기존 RHI 인터페이스 및 CLI 보존**: `IRenderDevice`, `ICommandList`, `ICommandQueue`, `ISwapChain`과 `--rhi auto|dx12|vulkan|metal`, `--warp`, `--debug` CLI 계약은 그대로 유지한다. `QueueType`은 `Graphics · Copy · Compute`를 지원한다.
3. **경계 규칙 유지**: `render/graphics_sdk/`는 `<graphics/Graphics.hpp>` 공개 API만 사용하며 OS/그래픽 네이티브 헤더를 직접 포함하지 않는다 (`tools/check_includes.py` 0건). `SandboxServer`는 `SandboxRender` 및 `GraphicsSDK::Core`를 링크하지 않는다.
4. **Graphics UI 공존 브리지**: `GraphicsSdkUiBridge`를 통해 `GraphicsUI`가 순환 의존성 없이 `GraphicsSDK::Core` 공개 API로 직접 렌더링하며 Dear ImGui와 동일 커맨드 리스트에서 공존한다.

## 결과

- 3대 그래픽 API의 플랫폼별 중복 구현(19개 파일) 제거 및 장기 유지보수 비용 절감.
- Windows D3D12 (WARP/HW) 및 Vulkan (HW + VVL 1.3.290)에서 35/35 GPU 테스트(591 assertions, 누수 0, 경고 0) 및 전체 CTest(99/99 실행 테스트, 결정론 16/16) 통과.
- 빌드 시 `GraphicsSDK` 패키지가 `CMAKE_PREFIX_PATH`를 통해 제공되어야 한다.
