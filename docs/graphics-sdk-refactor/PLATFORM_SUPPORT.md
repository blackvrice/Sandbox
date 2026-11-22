# 플랫폼별 지원 및 실측 현황 (`PLATFORM_SUPPORT.md`)

본 문서는 Windows, Linux, macOS 3대 운영체제와 D3D12, Vulkan, Metal 백엔드에 대한 Graphics SDK 어댑터(`render/graphics_sdk/`)의 구현 및 실제 검증 상태를 기록합니다.

---

## 1. 플랫폼 × 백엔드 지원 매트릭스

| 운영체제 | 그래픽 API | 창(Surface) 백엔드 | 어댑터 코드 구현 | 로컬 실기 검증 상태 | 검증 장치 / 드라이버 |
|---|---|---|---|---|---|
| **Windows 11 x64** | **D3D12 (Hardware)** | Win32 (`HWND`, `HINSTANCE`) | ✅ 완료 | **✅ 완료** (`sbx_render_tests` 10/10 PASS + Debug Layer 0 경고) | NVIDIA GeForce GTX 1660 SUPER |
| **Windows 11 x64** | **D3D12 (WARP)** | Win32 / Headless | ✅ 완료 | **✅ 완료** (`sbx_render_tests` 10/10 PASS + CTest 99/99 PASS) | Microsoft Basic Render Driver |
| **Windows 11 x64** | **Vulkan (Hardware)** | Win32 (`VK_KHR_win32_surface`) | ✅ 완료 | **✅ 완료** (`sbx_render_tests` 10/10 PASS + VVL 1.3.290 0 경고) | NVIDIA GeForce GTX 1660 SUPER |
| **Linux x64 / ARM64** | **Vulkan (Lavapipe)** | Headless | ✅ 완료 | **⚠️ 환경 부족** (현재 로컬 머신이 Windows 전용; Linux CI에서 검증 예정) | Mesa `lavapipe` (`lvp_icd`) |
| **Linux x64 / ARM64** | **Vulkan (Hardware)** | X11 (Xlib / XCB) · Wayland | 🟡 부분 완료 | **⚠️ 환경 부족** (`SDK-REQ-008`: Xlib/Wayland Surface 확장 필요) | Linux 물리 GPU |
| **macOS (Apple Silicon / Intel)** | **Metal (Metal 3)** | Cocoa (`CAMetalLayer*`) | ✅ 완료 | **⚠️ 환경 부족** (어댑터 `Cocoa` → `gfx::SurfaceKind::MetalLayer` 매핑 및 단위 테스트 완료, macOS 실기 필요) | Apple Silicon / Metal 3 GPU |

---

## 2. 플랫폼별 세부 사항

### 2.1 Windows (D3D12 및 Vulkan)
- `SBX_USE_GRAPHICS_SDK=ON`일 때 `SBX_ENABLE_D3D12=ON`과 `SBX_ENABLE_VULKAN=ON`이 동시에 활성화되며, 런타임 `--rhi auto|dx12|vulkan` 및 `--warp`, `--debug`, `--gpu-validation` CLI 플래그가 기존과 100% 동일하게 동작합니다.
- `BackendType::Auto`는 Windows에서 `D3D12`를 기본 선택합니다.

### 2.2 Linux (Vulkan)
- `BackendType::Auto`는 Linux에서 `Vulkan`을 기본 선택합니다.
- 오프스크린 및 헤드리스 렌더링(`sbx_render_tests`)은 창 Surface 없이 `gfx::createDevice`만으로 동작합니다.
- 창 모드(`SandboxClient`)의 경우 Sandbox의 `platform/linux/`는 `NativeWindowHandle::Backend::Xlib`(`Display*`, `Window`) 또는 `Backend::Wayland`(`wl_display*`, `wl_surface*`)를 반환합니다. 현재 `GraphicsSdkTypes::mapNativeSurface`는 `Xlib`의 `xDisplay`를 `Xcb` connection으로 브리지하거나 Phase 7(`SDK-REQ-008`)에서 `gfx::SurfaceKind::Xlib` / `Wayland`를 정식 지원하도록 계획되어 있습니다.

### 2.3 macOS (Metal)
- `BackendType::Auto`는 macOS에서 `Metal`을 기본 선택합니다.
- Sandbox의 `platform/macos/`가 채우는 `NativeWindowHandle::Backend::Cocoa`(`metalLayer` = `CAMetalLayer*`)는 `GraphicsSdkTypes::mapNativeSurface`에서 `gfx::SurfaceKind::MetalLayer`로 1:1 매핑됩니다 (`verifyAdapterTypeMappings` 단위 테스트 검증 완료).

---

## 3. 서버 전용 빌드(`SandboxServer`) 격리
- 모든 플랫폼(Windows, Linux, macOS)에서 `SandboxServer`는 `SandboxRender`, `SandboxPlatform`, `GraphicsSDK::Core` 중 어느 것도 링크하지 않습니다 (`arch_link_boundary_selftest`로 매 빌드 자동 검사).
