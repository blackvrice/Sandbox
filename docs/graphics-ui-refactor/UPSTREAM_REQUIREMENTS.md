# GraphicsUI · GraphicsSDK 개선 요구 (upstream)

> Sandbox 리팩터링을 위해 **GraphicsUI 저장소를 직접 고치지 않는다** (ADR-0174). 범용 기능이 없으면 여기에 적고, GraphicsUI 의
> 작업으로 넘긴다. 각 요구는 Sandbox 의 어느 작업을 막는지와 재현 근거를 함께 적는다. GraphicsUI 의 기존 요구 목록
> (GRAPHICSSDK_REQUIREMENTS SDK-01 ~ 09) 과 겹치면 그 ID 를 적는다. 기준: GraphicsUI `fa1e371`, GraphicsSDK `95967d2e`.

| ID | 요구 | 왜 (근거) | 막는 작업 | 우선 | 상태 |
|---|---|---|---|---|---|
| GUI-UP-01 | **소비자가 준 GraphicsSDK 로 그리는 렌더러** — `GraphicsUI::SdkRenderer` 가 (a) 자기 스냅숏 대신 소비자의 `GraphicsSDK::Core` 타깃 (같은 프로세스의 한 SDK) 과 빌드되고, (b) 소비자가 만든 `gfx::Device` · 큐 · 스왑체인/렌더 타깃에 그리며, (c) D3D12 외 Vulkan · Metal 을 지원 | GraphicsUI `cmake/GraphicsSDKDependency.cmake` 는 스냅숏 9ae01bb7 만 받고 `GUI_SDK_COMPATIBILITY` 를 제품 어댑터에서 거부한다. Sandbox 는 95967d2e — 두 SDK 를 링크하면 gfx:: 심볼 두 벌 (ODR) · 장치 두 개 | UI-R1-10 · R1-11 · R3-04 · R4-01 · R7-01 · R10-02 | P0 | 열림 |
| GUI-UP-02 | **이미지 · 외부 텍스처 그리기 명령** — DisplayList 에 이미지 (텍스처 핸들 · UV · 틴트) 명령, 소비자 텍스처 (썸네일 · Viewport) 의 수명 · 동기 계약 | `render.hpp` 의 DrawCommand = FillRect · DrawText · LinearGradient · DropShadow 뿐. Viewport 합성은 SdkRenderer 의 Windows 전용 API (SDK-05) | UI-R3-04 · R4-01 | P1 | 열림 (SDK-05 와 관련) |
| GUI-UP-03 | **메뉴 · 맥락 메뉴 · 팝업 · 툴팁 컨트롤** (열림 · 닫힘 · 키보드 탐색 · 바깥 클릭 · 겹침 순서) | Core 컨트롤 = Button · Toggle · Slider · ComboBox · TextInput · ScrollView · ListView · TableView · TreeView. ComboBox 의 목록 외에 팝업 계층이 없다 | UI-R2-04 · R3-01 · R3-11 | P1 | 열림 |
| GUI-UP-04 | **여러 줄 코드 편집 컨트롤** (줄 버퍼 · 큰 문서 · 구문 색 구간 · 접기 · 여러 커서 선택 · 줄 번호 칸 · 장식 (진단 밑줄 · 중단점)) — 언어 서비스는 소비자 것 | TextInput 은 단일행 (`commit` 의 단일행 규칙) · 실행 취소 32 단계. Script IDE 를 단순 TextInput 으로 대신하지 않는다 | UI-R5-01 ~ 05 | P1 | 열림 |
| GUI-UP-05 | **끌어다 놓기 (DnD) 계약** — Core 안 (패널 사이) payload · 놓을 자리 판정, 호스트 OS 드롭 (파일) 연결 | Core 는 포인터 캡처만. OLE 드롭은 Win32 Settings 예제 (P7-06) | UI-R3-01 · R3-10 | P2 | 열림 |
| GUI-UP-06 | **노드 그래프 컨트롤** (가시 영역 질의 · 연결 · 줌 · 팬) 을 예제에서 라이브러리로 | game-editor 예제의 Node Graph (P9-05) 는 Core 가 아니다 | 트리거 캔버스 (FEATURE_MATRIX 2장 13) | P3 | 열림 — Sandbox Custom Control 로도 가능 |
| GUI-UP-07 | **플랫폼 독립 글자 배치 · 래스터** (Linux · macOS 글꼴, 대체 글꼴 사슬, 한글) | 글자 배치는 DirectWrite (Windows) 만. Sandbox 는 ImGui 아틀라스 (과도기) | UI-R1-10 · R8-02 · R8-03 | P1 | 열림 (SDK-02 관련) |
| GUI-UP-08 | **라이브러리 호스트 (또는 호스트 계약 문서)** — OS 창 · IME · 클립보드 · DPI 이벤트를 Core 로 넘기는 표준 길 | Win32 · GTK4 · AppKit 호스트는 예제에만. Sandbox 는 자체 platform/ 이 있어 uihost 로 해결 — 요구는 "계약 문서 · 키 코드 표의 정본" | UI-R1-03 · R1-08 | P2 | 열림 — 지금은 controls.hpp 의 Windows 가상 키 번호를 정본으로 본다 |
| GUI-UP-09 | **다중 선택** (ListView · TreeView 의 범위 · 추가 선택) | `select()` 가 키 하나 | UI-R3-02 | P2 | 열림 |
| GUI-UP-10 | **P9-06 물리 GPU surface resize 메모리 증가 해결** | GraphicsUI ROADMAP_STATUS P9-06: GTX validation-on +161.99 MiB · Intel UHD 770 +634.55 MiB (SDK 단독 swapchain resize 400 회) | UI-R2-06 · R4-05 · R8-05 | P0 (출시) | GraphicsUI 진행 중 — 검증 기준을 낮추지 않는다 |
| GUI-UP-11 | **P9-07 게임 런타임 UI (HUD · 월드 UI · 게임패드 · 권한 · 쿼터)** | ROADMAP_STATUS: P9-07 미진행 | UI-R7 전부 | P1 | 미진행 — 있는 것으로 가정하지 않는다 |
| GUI-UP-12 | **안정 API · 버전 정책 · 설치 패키지** (P10-04 · 05) | 0.0.1 개발 스냅숏, 안정 API · deprecation 정책 없음 | 제품 출시 판정 (ROADMAP 4장) | P1 | 미진행 |
| GUI-UP-13 | `src/core/layout.cpp:590` 의 `[[nodiscard]]` 반환값 버림 (MSVC C4834) | Sandbox 빌드 로그 (2026-11-26, MSVC 14.44 — 벤더링 사본은 우리 경고 정책 밖) | — | P3 | 열림 |

## Sandbox 쪽 선행 (GraphicsUI 아님)

| ID | 필요 | 막는 작업 |
|---|---|---|
| S-01 | 다중 OS 창 — platform 이 창을 둘 이상 만들고 (창마다 이벤트 · DPI), 렌더러가 창마다 스왑체인 | UI-R2-06 · R4-05 |
| S-02 | 렌더러의 외부 렌더 타깃 (Scene 을 텍스처로) — 지금은 스왑체인에 바로 그린다 | UI-R4-01 |
| S-03 | ImGui 레이아웃 ini → DockLayout 의 이름 대응 (프리셋 이름 · 창 id) | UI-R2-07 |
| S-04 | IME 조합 이벤트 (조합 글자 · 커서 · 확정 · 취소) 를 PlatformEvent 로 — 지금은 확정 글자만 온다 (07 4장 · ADR-0017). GraphicsUI TextInput 의 조합 미리보기 (P7-03 · 04) 를 글자 칸 안에 그리려면 필요 | UI-R1-08 (칸 안 미리보기) · R5-05 |
