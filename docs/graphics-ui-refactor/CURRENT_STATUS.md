# GraphicsUI 통합 — 현황

> 마지막 갱신: 2026-11-27 · 브랜치 `refactor/graphics-ui-integration` (master `0f97fbc` 에서). 정본 상태는 [TASK_REGISTRY](TASK_REGISTRY.md).
> 검증한 것만 통과로 적는다 — 못 한 것은 5장.

## 1. 요약

| 항목 | 상태 |
|---|---|
| 단계 | UI-R0 ✅ (창 FPS 기준선만 실기 대기) · UI-R1 🟡 9 / 12 (남음: 창 실행 실기 · 렌더러 공유 · 장치 손실) · UI-R2 🟡 7 / 10 (R2-01 화면 자리 · R2-02 GXML AOT · R2-03 테마 토큰 · R2-05 Dock 작업 공간 · R2-07 배치 저장 · R2-09 홈 · R2-10 상태 · 알림 · 진행 ✅, R2-04 상태 표시줄 · R2-08 단축키 · 키 패널 일부 — ADR-0189 · 0190 · 0194) · UI-R3 🟡 3 / 12 (R3-06 콘솔 · R3-09 진단 · R3-12 Undo · 부분 갱신 ✅ — ADR-0191, R3-02 목록 · R3-03 인스펙터 · R3-05 콘텐츠 · R3-07 빌드 · R3-08 테스트 일부) · UI-R4 🟡 R4-03 장면 칸 편집 도구 · 모드 띠 (ADR-0193) · R4-04 입력 나누기 ✅ (ADR-0187) · R4-01 과도기 (통과 장면 — ADR-0179) · R4-06 월드 패널 (테스트 플레이 — ADR-0185) · UI-R9 🟡 R9-03 대량 목록 (목록 패널 10 만 개체 29.6 → 4.7 ms — ADR-0188) · UI-R5 ~ R8 · R10 ⬜ (R7 · R10 🔴) |
| 작업 진행 | 81 중 32 Done (40 %) — **작업 수 비율이지 기능 완료가 아니다** |
| 화면 이전 | Studio 화면 26 중 1 (홈 — ImGui 홈의 행동 전부) + 새 GXML 화면 1 (Studio 설정 — 테마 · 간격 · 개발자 창) + Dock 작업 공간 미리보기 1 (정보 · 단축키 · 콘솔 패널 — 분할 · 탭 · 끌기). 기본 UI 는 여전히 ImGui (`--ui graphicsui` 로 고른다) |
| GraphicsUI 고정 | Core 만 `fa1e371` (main 최신, 2026-11-26) |
| GPU 렌더 | 과도기 — DisplayList → ImDrawList → 기존 ImGui 렌더러 (GraphicsSDK 세 백엔드). GraphicsUI 렌더러는 GUI-UP-01 대기 |
| 실행 | `SandboxClient --ui graphicsui` (헤드리스 검증 ✅ · Linux X11 창은 CI 시험 등록 · Windows · macOS 창은 사람 QA) |

## 2. 완료한 세부 작업

UI-R0-01 ~ 08 · 10 ~ 12 (분석 · 대응표 · upstream 요구 · 기준선 · 위험 · 계획 · 브랜치), UI-R1-01 (Core 벤더링 · CMake) · 02 (응용
호스트) · 03 (입력 라우팅) · 04 (과도기 렌더) · 05 (`--ui` 선택 실행 · GraphicsUI 홈) · 06 (경계 검사) · 07 (작업자 → UI 스레드) ·
08 (글자 입력 · IME 자리 · 휠 · 클립보드) · 09 (UI 배율 · DPI — ImGui 와 같은 크기), UI-R2-09 (홈 — 검색 · 가상 목록 · 빼기 · 복구 표시 · 새 프로젝트 · 폴더 열기 · 예제 복사 ·
간격), UI-R2-01 · 02 · 03 (editor/gui · GXML AOT · 토큰 표 — ADR-0175 · 0176), UI-R2-05 (Dock 작업 공간 — ADR-0177: DockLayout + 패널마다
UiHost, 탭 · 분할선 · 끌기 · Esc 취소 · Ctrl+PageUp/Down · 저장 바이트, 첫 패널 정보 · 단축키 (R2-08 일부) · 콘솔 (R3-06 일부)),
UI-R2-07 (배치 저장 — ADR-0178: 레이아웃 이름별 .guidock, ini 는 변환 없이 프리셋, 놓은 뒤 저장 · 다음 실행 복원),
UI-R3-06 (콘솔 — 수준 거르기 · 지우기 · 복사까지 ImGui 콘솔과 같은 기능),
UI-R4-01 과도기 (ADR-0179 — 프로젝트 안에서 작업 공간이 ImGui 셸 대신: 통과 장면 패널 · 장면 칸 입력은 게임으로 · "기존 UI 로").

## 3. 막힌 것

| 무엇 | 왜 | 푸는 것 |
|---|---|---|
| GraphicsUI GPU 렌더러 · 장치 손실 · Viewport · 썸네일 · ImGui 제거 | GraphicsUI SdkRenderer 가 GraphicsSDK 9ae01bb7 에 고정 (Sandbox 95967d2e) · D3D12 만 | GUI-UP-01 (· 02 · 07) |
| 메뉴 · 툴바 · 맥락 메뉴 · Project Explorer | Core 에 메뉴 · 팝업 없음 | GUI-UP-03 |
| Script IDE | 여러 줄 코드 편집 컨트롤 없음 | GUI-UP-04 |
| 게임 런타임 UI | GraphicsUI P9-07 미진행 | GUI-UP-11 |
| 분리 창 · 반복 resize | Sandbox 다중 OS 창 없음 (S-01), GraphicsUI P9-06 물리 GPU resize 메모리 증가 (진행 중) | S-01 · GUI-UP-10 |

## 4. 다음 작업 (순서)

1. **편집 창을 작업 공간으로** — 목록 (R3-02) · 인스펙터 (R3-03 — ADR-0180) · 편집 (툴 — ADR-0181) · 콘텐츠 (R3-05 — ADR-0182, Prefab
   폼 · 나머지는 읽기만) 는 들어왔다. 여러 줄 JSON · 트리거 폼 · 스크립트 편집기는 여러 줄 글 칸 (업스트림) 뒤 — 그 동안 "기존 UI 로". 진단 창 다섯 (통계 · 네트워크 · 실행 · 접속자 · 스크립트 프로파일 — R3-09 ✅, ADR-0183 · 0184) 도 모두 들어왔다.
   떠 있는 명령 팔레트 (R2-08) · 메뉴 (R2-04) 는 오버레이 (GUI-UP-03) 뒤.
2. Linux · macOS CI 확인 — 사용자 요청으로 리팩터링을 마친 뒤 (PR 을 열어 CI).
3. 사람 QA — Q-00 (ImGui 기준선) · Q-01 (GraphicsUI 홈 — Windows 창) · Q-02 (Dock 작업 공간 — 끌기 · 분할선 · 탭).
4. upstream — GUI-UP-01 · 03 · 04 를 GraphicsUI 작업으로 넘긴다 (이 저장소에서 GraphicsUI 를 고치지 않는다).

## 5. 검증 기록

| 무엇 | 결과 | 날짜 · 명령 |
|---|---|---|
| 구성 · 빌드 (Debug) | 통과 — 구성 로그 "GraphicsUI Core 0.0.1 — external/graphics-ui (fa1e371…)" | 2026-11-26 `cmake --preset windows-msvc` · `cmake --build build/windows-msvc --config Debug` |
| 경고 = 오류 빌드 (/WX) | 통과 (벤더링 Core 의 C4834 경고 1 — 우리 경고 정책 밖, GUI-UP-13) | `cmake --build build/ci-werror --config Debug` |
| 전체 CTest (Debug, project_doctor 제외) | **99 / 99 통과** (문서를 마친 최종 상태) | `ctest --preset windows-msvc-debug -E project_doctor -j 6` |
| 새 시험 | `unit_uihost` 7 케이스 · `unit_client` gui 3 케이스 · `client_graphicsui_home_headless` · `client_ui_rejects_unknown` · `arch_include_lint_selftest` (18) 통과 | 같은 CTest |
| 골든 WorldHash | det_* 16 개 통과 (시뮬레이션 코드를 고치지 않았다) | 같은 CTest |
| 서버 무의존 | 구성 단계 링크 검사 통과 (SandboxServer ↛ SandboxUiHost · GraphicsUICore) | 구성 |
| UI-R1-08 · R2-09 (2026-11-26 2 차) | /WX 빌드 · 전체 CTest 99 / 99, 새 시험 uihost 4 · client gui 5 + app 1 (실제 만들기 · 예제 복사 · 빼기 · IME 켜고 끄기) | 같은 명령 |
| UI-R2-05 (2026-11-27) | /WX 빌드 · 전체 CTest (project_doctor 제외) — 전체 CTest 100 / 100, 새 시험 `unit_client` dock 3 + app 1 (266 단언), check_includes 813 파일 0 위반 | 같은 명령 |
| UI-R2-07 (2026-11-27) | /WX 빌드 · 전체 CTest 100 / 100 (project_doctor 제외), 새 시험 `unit_editor` .guidock 1 · `unit_client` 프리셋 1 + app 1 | 같은 명령 |
| UI-R3-06 · 07 (2026-11-27) | /WX 빌드 · 전체 CTest 100 / 100 (project_doctor 제외), `unit_client` dock 7 케이스 (489 단언 — 콘솔 수준 · 지우기 · 복사, 빌드 패널 새 1) | 같은 명령 |
| UI-R3-08 · 09 (2026-11-27) | /WX 빌드 · 전체 CTest 100 / 100 (project_doctor 제외), `unit_client` dock 9 케이스 (618 단언 — 테스트 · 통계 패널 새 2) | 같은 명령 |
| UI-R4-01 과도기 (2026-11-27) | /WX 빌드 · 전체 CTest 101 / 101 (project_doctor 제외), `unit_client` dock 11 케이스 (752 단언 — 장면 통과 · 앱 local server), `client_graphicsui_world_headless` | 같은 명령 |
| UI-R3-02 (2026-11-27) | /WX 빌드 · 전체 CTest 101 / 101 (project_doctor 제외), `unit_client` dock 12 케이스 (887 단언 — 목록 패널 · 앱에서 누르면 실제 선택) | 같은 명령 |
| UI-R3-03 (2026-11-27) | /WX 빌드 · 전체 CTest 101 / 101 (project_doctor 제외), `unit_client` dock 14 케이스 (1246 단언 — 인스펙터 줄 · 편집 · 단추 · 앱에서 토끼의 maxSpeed 를 바꾸고 Ctrl+Z 로 되돌림) | 같은 명령 |
| 편집 (툴) 패널 (2026-11-27, ADR-0181) | /WX 빌드 · 전체 CTest 101 / 101 (project_doctor 제외), `unit_client` dock 15 케이스 (1730 단언 — 툴 단추 · 묶음 · 거르기 · − + 자르기 · 켜고 끄기 · 되돌리기, 앱에서 배치 툴 · Prefab · 간격 · 격자가 Editor 에) | 같은 명령 |
| 콘텐츠 패널 (2026-11-27, ADR-0182) | /WX 빌드 · 전체 CTest 101 / 101 (project_doctor 제외), `unit_client` dock 16 케이스 (2097 단언 — 목록 · 폼 편집 · 검사 · 적용 · 다시 불러오기 · 새 항목 · 읽기만, 앱에서 eco.rabbit 을 고쳐 적용 → 서버 오버레이 → 팩 값으로) | 같은 명령 |
| 실행 · 접속자 패널 (2026-11-28, ADR-0183) | /WX 빌드 · 전체 CTest 101 / 101 (project_doctor 제외), `unit_client` dock 17 케이스 (2320 단언 — 실행 단추 · 역할로 끄기 · 접속자 역할 단추 (서버 규칙) · 나간 사람, 앱에서 패널로 일시정지 · 되돌리기 · 격자 · 접속자에 나) | 같은 명령 |
| 스크립트 프로파일 (2026-11-28, ADR-0184) | /WX 빌드 · 전체 CTest 101 / 101 (project_doctor 제외), `unit_client` dock 18 케이스 (2388 단언 — 진단 창과 같은 표 · 자릿수 · 열 자르기 · 줄 복사 · 같은 모양 갱신, 앱에서 서버의 엔진 시스템 표) | 같은 명령 |
| 월드 패널 (2026-11-28, ADR-0185) | /WX 빌드 · 전체 CTest 101 / 101 (project_doctor 제외), `unit_client` dock 19 케이스 (2717 단언 — 저장 · 기록 · 이름 · 목록 · 불러오기 · 재생 · 탐색 자르기 · 역할, 앱에서 패널로 테스트 플레이 → 멈추기) | 같은 명령 |
| 게임 패널 (2026-11-28, ADR-0186) | /WX 빌드 · 전체 CTest 101 / 101 (project_doctor 제외), `unit_client` dock 20 케이스 (2858 단언 — 상태 줄 · 점수 · 상태 · 역할별 단추 · 행동 쥐기 · 놓기 · 키보드, 앱에서 ecosystem 의 행동을 패널로 쥐고 놓음) | 같은 명령 |
| 입력 나누기 (2026-11-28, ADR-0187) | /WX 빌드 · 전체 CTest 101 / 101 (project_doctor 제외), `unit_client` dock 21 케이스 (2950 단언 — 누른 자리 주인 두 방향 · Esc 로 장면 · 글자 칸 · 수정자 · 가려진 장면, 앱에서 장면 끌기가 패널 위로 · Esc → 게임 키) | 같은 명령 |
| 부분 갱신 (2026-11-28, ADR-0191) | /WX 빌드 · 전체 CTest, `unit_client` dock 24 케이스 (3124 단언 — 보이는 · 숨은 패널, 탭을 누른 feed 뒤 세기, 알림 · 프리셋, 앱에서 숨은 편집 탭은 모델 없음 → 누른 frame 에 생김), 벤치 shown=false | 같은 명령 · `sbx_bench --only gui.` |
| 진행 표시 (2026-11-28, ADR-0190) | /WX 빌드 · 전체 CTest, `unit_client` dock 23 케이스 (3081 단언 — 빌드 · 테스트 글 · 누르면 그 패널 · 다 끝나면 칸 0) | 같은 명령 |
| 상태 표시줄 · 알림 (2026-11-28, ADR-0189) | /WX 빌드 · 전체 CTest, `unit_client` dock 23 케이스 (띠 자리 · ImGui 와 같은 글 · 일시정지 · 배속 · 알림 처음 한 번 · 단추 → 알림 탭 · 포커스 그대로 · 감싸기 · 앱 월드의 모델), 헤드리스 "그룹 4 · 패널 17" | 같은 명령 |
| 키 패널 (2026-11-28, ADR-0194) | /WX 빌드 · 전체 CTest, `unit_client` dock 25 케이스 (keys panel — 줄 · 겹침 · 의도 · 꺼짐 규칙, 앱에서 패널로 F8 다시 묶기 · 패널 위 클릭 무시 · 기본 · Esc 취소), 헤드리스 "그룹 4 · 패널 18" | 같은 명령 |
| 장면 칸 편집 도구 · 모드 띠 (2026-11-28, ADR-0193) | /WX 빌드 · 전체 CTest, `unit_client` dock 24 케이스 (scene pane 의 띠 · 앱에서 배치 툴로 장면 칸 → 새 개체 · Ctrl+Z → 사라짐 · 테스트 플레이 띠) | 같은 명령 |
| 나머지 대량 목록 (2026-11-28, ADR-0192) | 벤치 `gui.workspace_list` · `gui.workspace_hierarchy` query (RelWithDebInfo, A/B 3 회 — 콘텐츠 패널 쪽 10 만 18.7 → 3.6 ms), /WX 빌드 · 전체 CTest, `unit_client` dock 24 케이스 | `sbx_bench --only gui.` |
| 대량 목록 (2026-11-28, ADR-0188) | 벤치 `gui.workspace_hierarchy` (RelWithDebInfo) 10 만 개체 29.6 → 4.7 ms, /WX 빌드 · 전체 CTest, `unit_client` 2 만 줄 굴린 뒤 누르기 | `sbx_bench --only gui.` · [PERFORMANCE_BASELINE](PERFORMANCE_BASELINE.md) 3.2 |
| 실제 실행 (헤드리스) | `SandboxClient --headless --ui graphicsui` — 컴포넌트 13 · 그리기 명령 32 · 정상 상태 배치 0.008 ms (Release) | [PERFORMANCE_BASELINE](PERFORMANCE_BASELINE.md) 3장 |
| 잘못된 값 | `--ui qt` → 종료 코드 2 · 오류 줄 | 직접 실행 |

검증하지 못한 것:

| 무엇 | 이유 |
|---|---|
| 실제 창에서 GraphicsUI 홈 (Windows D3D12 · macOS Metal) | 이 세션은 사용자 화면에 창을 띄우지 않는다 — QA Q-01 |
| Linux X11 창 (`client_x11_graphicsui_home`) | CI 에서만 돈다 — 이 브랜치는 PR 을 열어야 CI 가 돈다 (CI 는 master push · PR 에서) |
| 커밋마다 빌드 | 구현 커밋 하나에 묶었다 (vendoring · CMake · uihost · client 를 한 번에 빌드 · 시험) |
| ImGui 판과 같은 조건 성능 비교 | 창 · GPU 실행 필요 (UI-R9) |

## 6. 커밋

| 커밋 | 내용 |
|---|---|
| `da274e9` | external: GraphicsUI Core 를 고정 커밋 fa1e371 로 (UI-R1-01 의 벤더링) |
| `75a71fd` | uihost · client · docs: UI-R0 분석 · 설계 (ADR-0174 · 이 폴더), UI-R1-01 ~ 07 구현 · 시험 |
