# 기능별 지원 및 검증 매트릭스 (`FEATURE_MATRIX.md`)

본 문서는 Sandbox 그래픽 시스템의 각 세부 기능이 Legacy 백엔드(`SBX_USE_GRAPHICS_SDK=OFF`)와 Graphics SDK 어댑터(`SBX_USE_GRAPHICS_SDK=ON`)에서 어느 수준까지 구현·검증되었는지를 나타냅니다.

---

## 1. RHI 및 렌더러 기능 매트릭스

| 기능 영역 | 세부 기능 | Legacy D3D12 | Legacy Vulkan | Legacy Metal | Graphics SDK D3D12 | Graphics SDK Vulkan | Graphics SDK Metal | 관련 요구사항 / 비고 |
|---|---|---|---|---|---|---|---|---|
| **Device & Adapter** | 하드웨어/WARP 어댑터 선택 및 Caps 조회 | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | `GraphicsSdkDevice::init()` |
| **Debug / Validation** | Debug Layer / VVL 활성화 및 미설치 시 폴백 | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | `validationErrorCount()` 통합 |
| **Frame Context** | `framesInFlight` (2~3) 슬롯 회전 및 CommandPool 리셋 | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | `FrameSlot` (Phase 2 `R0.14`) |
| **Handle & Lifetime** | 세대형 핸들(`HandlePool`) 및 지연 해제(`DeferredDestructionQueue`) | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | Phase 2 (`R0.12`~`R0.13`) |
| **Buffer** | `GpuOnly` / `Upload` / `Readback` 생성 및 `map()` | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | Stage-Before-Record 섀도 동기화 |
| **Upload Ring** | 프레임 링 버퍼 할당, 래핑(wrap-around), 이월 집계 | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | 슬롯별 SDK Upload 버퍼 (`R0.15`) |
| **Texture** | 2D / 2DArray / DepthStencil / RenderTarget 생성 | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | `R16Uint`는 `SDK-REQ-002` 대기 |
| **Transfer / Copy** | `copyBuffer`, `copyBufferToTexture`, `copyTextureToBuffer` (전체/부분 영역) | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | 256B/512B 정렬 검증 포함 |
| **Render Pass** | 단일 및 MRT(최대 4개) Clear / Load / Store | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | `StoreOp::DontCare`는 `SDK-REQ-007` |
| **SwapChain** | Win32 / Cocoa / X11 스왑체인 생성, Acquire, Present, Resize | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | Linux Xlib/Wayland는 `SDK-REQ-008` |
| **GPU Timestamps** | `writeTimestamp` / `resolveTimestamps` / `completedTimestamps` | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | `beginTimestampScope` 변환 (`R0.9`) |
| **Pipeline Validation** | 오프라인 리플렉션 대비 Layout / BindGroup / Vertex 입력 검증 | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | `validatePipelineAgainstReflection` (`R0.11`) |
| **Shader & PSO** | 오프라인 리플렉션 기반 PSO 생성 (`basic_color`, `basic_texture`) | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | ⬜ 대기 | ⬜ 대기 | ⬜ 대기 | Phase 3 (`SDK-REQ-001`~`006`) |
| **SpriteBatcher** | 2DArray 아틀라스 + 인스턴싱(`R32Uint`) + Z-sort 배치 | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | ⬜ 대기 | ⬜ 대기 | ⬜ 대기 | Phase 4 (`SDK-REQ-002`, `003`) |
| **Terrain / Grid / Lines** | 풀스크린 쿼드 `TriangleStrip` + `R16Uint` 타일맵 + 두께 선분 | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | ⬜ 대기 | ⬜ 대기 | ⬜ 대기 | Phase 4 (`SDK-REQ-002`, `003`) |
| **ImGuiRenderer** | 시저 클리핑 + 폰트 아틀라스 + 멀티 드로우 리스트 | ✅ 완료 | ✅ 완료 | ⚠️ 환경 부족 | ⬜ 대기 | ⬜ 대기 | ⬜ 대기 | Phase 5 (`R0.23`) |
| **Graphics UI** | `GraphicsSDK::Core` 직접 연동 모던 UI | 🚫 미지원 | 🚫 미지원 | 🚫 미지원 | ⬜ 대기 | ⬜ 대기 | ⬜ 대기 | Phase 8 (`R0.31`~`R0.32`) |
