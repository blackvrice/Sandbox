# 알려진 이슈 및 제약사항 (`KNOWN_ISSUES.md`)

본 문서는 Graphics SDK 기반 RHI 리팩토링(Phase 0 ~ Phase 9, `R0.1`~`R0.34`) 진행 중 확인·해결된 기술적 이슈와 현재 남아 있는 외부 환경/SDK 제약사항을 기록합니다.

---

## 1. 어댑터에서 해결 완료된 이슈

| ID | 이슈 요약 | 해결 방식 | 상태 |
|---|---|---|---|
| `ISSUE-001` | Stage-Before-Record (`observedVersion`) vs Sandbox 지속 매핑(`map`) 충돌 | `GraphicsSdkCommandList::prepareAndReplay()` 지연 재생 + `flushPendingUploads()` 4KB 패딩 병합 | ✅ 해결 |
| `ISSUE-002` | Vulkan 멀티 프레임 UploadRing 래핑 시 `Upload buffer busy` 및 펜스 타임아웃 상한 | `FrameSlot`별 독립 UploadRing 버퍼 + `GraphicsSdkCommandQueue::wait()` 5000ms 분할 대기 | ✅ 해결 |
| `ISSUE-003` | 오프라인 리플렉션 · 다중 Descriptor Set · `VSMain`/`PSMain` 엔트리포인트 (`SDK-REQ-001`) | `GraphicsSdkShaders` (D3D12 SM5.1 캐시 + Vulkan 단일 셋 SPIR-V 1.3 생성기 `gen_gfxsdk_spirv.py` + Metal MSL) | ✅ 해결 |
| `ISSUE-004` | `Format::R16Uint` / `Format::R32Uint` 정점 속성 미지원 (`SDK-REQ-002`) | `R16Uint→R16UNorm`(`(uint)(raw*65535+0.5)`), `R32Uint→VertexFormat::Float`(`asuint()`) 매핑 | ✅ 해결 |
| `ISSUE-005` | `TriangleStrip` / `CullMode` / PushConstant / 동적 Sampler (`SDK-REQ-003`~`006`) | 공용 6-인덱스 쿼드 스트립 IB, CPU 2D 와인딩 컬링, 슬롯별 `pushBuffer`, 샘플러 변형 PSO 캐시 | ✅ 해결 |
| `ISSUE-006` | Vulkan 부분 업로드 시 `Sampled texture must have initialized contents` | 최초 부분 업로드 전 1회 `fullSlice` 제로 초기화 | ✅ 해결 |
| `ISSUE-007` | Vulkan `SwapChain::resize()` OS 창 크기 불일치 오류 | `surfaceMetrics()`로 실제 클라이언트 크기 정합 및 `InvalidState` 1회 `syncExtent()` 자동 복구 | ✅ 해결 |
| `ISSUE-008` | 정적 매핑 Upload 버퍼의 `Cannot overwrite a buffer in flight` | `GraphicsSdkBuffer::lastUploadedShadow` 변경 감지 후 실제 변경 시에만 `Buffer::write()` 호출 | ✅ 해결 |
| `ISSUE-009` | Windows CP949 콘솔에서 CTest UTF-8 한글 출력 매칭 실패 | `chcp 65001 >nul` 환경에서 CTest 실행 (100/100 통과) | ✅ 해결 |

---

## 2. 남아 있는 SDK / 외부 환경 제약사항

| ID | 내용 | 현재 대응 및 영향 | 향후 조치 |
|---|---|---|---|
| `OPEN-001` | **Linux Xlib / Wayland Surface 미지원 (`SDK-REQ-008`)** | `GraphicsSdkTypes::mapSurface()`가 `Xcb`는 지원하고 `Xlib`/`Wayland`는 명시적 `Unsupported` 반환 (오프스크린/헤드리스 `sbx_render_tests`는 영향 없음) | `GraphicsSDK`에 Xlib/Wayland Surface 추가 또는 Sandbox Linux 창에서 XCB 연결 노출 |
| `OPEN-002` | **D3D12/Vulkan `DestBlendAlpha = ZERO` (`SDK-REQ-009`)** | 불투명 클리어 타깃(`clear.a >= 0.999`)의 CPU Readback 시 `applyOpaqueAlphaFixup()`으로 Porter-Duff Over 알파 불변량 보정 (전 35/35 골든 이미지 일치) | `GraphicsSDK` D3D12/Vulkan 백엔드의 `DestBlendAlpha`를 Metal과 동일하게 `INV_SRC_ALPHA`로 통일 후 어댑터 보정 제거 |
| `OPEN-003` | **Linux / macOS 실기 실행 검증 미수행** | 로컬 환경이 Windows 11 전용. 어댑터 코드 및 6종 Surface 단위 매핑 검증 완료 | Linux(Lavapipe/GPU) 및 macOS(Apple Silicon Metal 3) CI 러너에서 `sbx_render_tests` 실기 실행 |
| `OPEN-004` | **GraphicsSDK D3D12/Vulkan CPU `render ms` 추가 개선 여지** | Phase 6~7 최적화로 D3D12 1% Low(173.6 vs 169.7 FPS) · 0.1% Low(123.0 vs 121.9 FPS), Vulkan 0.1% Low(430.3 vs 382.3 FPS, `10k` 24.3 vs 22.3 FPS)에서 Legacy 초과 달성. 다만 평균 FPS는 `Buffer::write()` 이중 복사로 Legacy 대비 소폭 낮음 | 향후 `GraphicsSDK`에 Persistent Mapped Upload Buffer API 도입 시 이중 복사 완전 제거 가능 |
