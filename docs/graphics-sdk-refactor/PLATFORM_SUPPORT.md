# 플랫폼별 지원 및 실측 현황 (`PLATFORM_SUPPORT.md`)

본 문서는 Windows, Linux, macOS 3대 운영체제와 D3D12, Vulkan, Metal 백엔드에 대한 Graphics SDK 어댑터(`render/graphics_sdk/`)의 구현 및 실제 검증 상태를 기록합니다.

---

## 1. 플랫폼 × 백엔드 지원 매트릭스

| 운영체제 | 그래픽 API | 창(Surface) 백엔드 | 어댑터 코드 구현 | 로컬 실기 검증 상태 | 검증 장치 / 드라이버 |
|---|---|---|---|---|---|
| **Windows 11 x64** | **D3D12 (Hardware)** | Win32 (`HWND`, `HINSTANCE`, Tearing 지원) | ✅ 완료 | **✅ 완료** (`sbx_render_tests` **34/34 PASS**, SwapChain Resize/0×0/VSync, Debug Layer 0 경고) | NVIDIA GeForce GTX 1660 SUPER |
| **Windows 11 x64** | **D3D12 (WARP)** | Win32 / Headless | ✅ 완료 | **✅ 완료** (`sbx_render_tests` **34/34 PASS**, CTest 99/99 PASS) | Microsoft Basic Render Driver |
| **Windows 11 x64** | **Vulkan (Hardware)** | Win32 (`VK_KHR_win32_surface`, Tearing 지원) | ✅ 완료 | **✅ 완료** (`sbx_render_tests` **34/34 PASS**, SwapChain Resize/0×0/VSync, VVL 1.3.290 0 경고) | NVIDIA GeForce GTX 1660 SUPER |
| **Linux x64 / ARM64** | **Vulkan (Lavapipe)** | Headless | ✅ 완료 | **⚠️ 환경 부족** (로컬 머신이 Windows 전용; Linux CI `render_tests_lavapipe` 대상) | Mesa `lavapipe` (`lvp_icd`) |
| **Linux x64 / ARM64** | **Vulkan (Hardware)** | XCB (`gfx::SurfaceKind::Xcb`) / Xlib·Wayland(`SDK-REQ-008`) | 🟡 부분 완료 | **✅ 단위 매핑 검증 완료** (`verifyAdapterTypeMappings`: Xcb 매핑, Xlib/Wayland `Unsupported` 명시 거부), Linux 실기 필요 | Linux 물리 GPU |
| **macOS (Apple Silicon / Intel)** | **Metal (Metal 3)** | Cocoa (`CAMetalLayer*` → `gfx::SurfaceKind::MetalLayer`) | ✅ 완료 | **✅ 단위 매핑 검증 완료** (`verifyAdapterTypeMappings`: Cocoa→MetalLayer 1:1 매핑, MSL 셰이더 경로 준비), macOS 실기 필요 | Apple Silicon / Metal 3 GPU |

---

## 2. 플랫폼별 세부 사항

### 2.1 Windows (D3D12 및 Vulkan)
- `SBX_USE_GRAPHICS_SDK=ON`일 때 D3D12와 Vulkan이 동시에 활성화되며, `--rhi auto|dx12|vulkan`, `--warp`, `--debug`, `--gpu-validation` CLI 플래그가 기존과 100% 동일하게 동작합니다.
- `makeGraphicsSdkSwapChain()`은 `querySurfacePresentCapabilities()`를 조회해 VSync OFF 시 `PresentMode::Tearing`(미지원 시 `Immediate` → `Fifo`)을 자동 선택하고 `DeviceCaps::tearing`을 갱신합니다.
- `GraphicsSdkSwapChain::resize()`는 Vulkan에서 OS 창 실제 클라이언트 크기(`surfaceMetrics()`)와 정합하며, `0×0` 최소화 및 런타임 `setVsync()` 토글을 `sbx_render_tests` 34/34에서 자동 검증합니다.

### 2.2 Linux (Vulkan)
- `BackendType::Auto`는 Linux에서 `Vulkan`을 기본 선택합니다.
- 오프스크린 및 헤드리스 렌더링(`sbx_render_tests`)은 창 Surface 없이 `gfx::createDevice`만으로 동작합니다.
- `NativeWindowHandle::Kind::Xcb`는 `gfx::SurfaceKind::Xcb`로 매핑되며, `Xlib`/`Wayland`는 `SDK-REQ-008`에 따라 명시적 `ErrorCode::Unsupported`를 반환합니다 (`verifyAdapterTypeMappings` 검증 완료).

### 2.3 macOS (Metal)
- `BackendType::Auto`는 macOS에서 `Metal`을 기본 선택합니다.
- `NativeWindowHandle::Kind::Cocoa`(`CAMetalLayer*`)는 `gfx::SurfaceKind::MetalLayer`로 1:1 매핑됩니다 (`verifyAdapterTypeMappings` 검증 완료).

---

## 3. 서버 전용 빌드(`SandboxServer`) 격리
- 모든 플랫폼(Windows, Linux, macOS)에서 `SandboxServer`는 `SandboxRender`, `SandboxPlatform`, `GraphicsSDK::Core` 중 어느 것도 링크하지 않습니다 (`arch_link_boundary_selftest`로 매 빌드 자동 검사).
