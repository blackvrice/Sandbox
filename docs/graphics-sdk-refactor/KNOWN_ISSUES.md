# 알려진 이슈 및 제약사항 (`KNOWN_ISSUES.md`)

본 문서는 Graphics SDK 기반 RHI 리팩토링(현재 Phase 1·2 완료, `R0.1`~`R0.15`) 진행 중 확인된 기술적 특이사항, 어댑터에서 해결한 이슈, 그리고 Phase 3 이후 해소 예정인 제약사항을 기록합니다.

---

## 1. Phase 1·2 어댑터에서 해결 완료된 이슈

### 1.1 `GraphicsSDK` Stage-Before-Record (`observedVersion`) 계약과 Sandbox 지속 매핑(`map`) 간의 충돌
- **현상**: Sandbox의 `allocateUpload()`와 `map()`은 지속적인 CPU 포인터를 반환하여 호출자가 커맨드 기록 이후에도 제출 전까지 CPU 메모리를 수정할 수 있는 반면, Graphics SDK는 커맨드 기록 이후 `gfx::Buffer::write()` 호출 시 `Resource changed after recording` 오류로 제출을 거부합니다.
- **해결 (✅ 완료)**: `GraphicsSdkCommandList`에 지연 재생(`prepareAndReplay`)을 도입하여 `submit()` 시점에 모든 더티 Upload 범위를 먼저 `gfx::Buffer::write()`로 flush한 뒤 SDK 커맨드 리스트에 명령을 기록하도록 해결했습니다.

### 1.2 Vulkan 멀티 프레임 UploadRing 래핑 시 `Upload buffer busy` 및 펜스 타임아웃 제약
- **현상**:
  1. 단일 `gfx::Buffer`를 멀티 프레임 UploadRing으로 공유하면 다른 영역을 쓰더라도 SDK가 버퍼 전체의 `executionFence`를 검사하여 `Upload buffer busy`를 반환합니다.
  2. `gfx::CommandQueue::wait(fence, timeout)`의 타임아웃 상한이 `4,294,967,294 ns`(~4.29초)는 아니지만 Vulkan 백엔드가 `timeout.count() > 4294967294ll`(밀리초 단위 검사 후 1,000,000을 곱함) 및 FIFO `qSlot.pending` 큐 순차 은퇴 방식을 사용합니다.
- **해결 (✅ 완료)**:
  1. `FrameSlot`(`m_framesInFlight = 2|3`)마다 독립적인 `gfx::Buffer` 업로드 링 인스턴스를 두고, 해당 슬롯이 재사용될 때 슬롯의 펜스를 먼저 대기합니다.
  2. `GraphicsSdkCommandQueue::wait()`가 `cur + 1`부터 목표 펜스까지 5000ms 타임아웃으로 순차 대기하도록 구현하여 12프레임 링 감기 테스트(`test_rhi_basic`)를 D3D12와 Vulkan 모두에서 오류 0건으로 통과시켰습니다.

### 1.3 Windows 콘솔 코드페이지(CP949)에서 CTest 한글 출력 매칭 실패
- **현상**: Windows 기본 CP949 콘솔에서 `ctest`를 바로 실행하면 실행 파일의 UTF-8 한글 로그(`충돌 보고: `)를 CMake 스크립트(`crash_report.cmake`)가 매칭하지 못합니다.
- **우회 / 해결 (✅ 완료)**: `cmd /c "chcp 65001 >nul && ctest ..."`로 UTF-8 코드페이지를 활성화하면 100개 CTest가 100% 통과합니다.

---

## 2. 미해결 이슈 및 다음 단계(Phase 3~7) 해소 항목

| ID | 내용 | 영향 | 해소 예정 단계 |
|---|---|---|---|
| `ISSUE-001` | 오프라인 리플렉션 수용 및 커스텀 엔트리포인트(`VSMain`/`PSMain`)/다중 Descriptor Set 미지원 ([SDK-REQ-001](SDK_REQUIREMENTS.md)) | 셰이더 드로우 파이프라인(`test_rhi_draw`, `Renderer` 패스) 생성 대기 | Phase 3 (`R0.16`) |
| `ISSUE-002` | `Format::R16Uint` 텍스처 및 `Format::R32Uint` 정점 속성 미지원 ([SDK-REQ-002](SDK_REQUIREMENTS.md)) | `TerrainPass` 타일맵 및 `SpriteBatcher` 인스턴스 속성 | Phase 4 (`R0.20`~`R0.21`) |
| `ISSUE-003` | `PrimitiveTopology::TriangleStrip` / `LineList` 및 `CullMode` 설정 미지원 ([SDK-REQ-003](SDK_REQUIREMENTS.md), [SDK-REQ-004](SDK_REQUIREMENTS.md)) | 쿼드 스트립 기반 패스 및 컬링 테스트 | Phase 3 (`R0.18`) |
| `ISSUE-004` | Linux Xlib / Wayland Surface 생성 미지원 ([SDK-REQ-008](SDK_REQUIREMENTS.md)) | Linux 창 모드 스왑체인 (헤드리스/오프스크린은 영향 없음) | Phase 7 (`R0.28`) |
