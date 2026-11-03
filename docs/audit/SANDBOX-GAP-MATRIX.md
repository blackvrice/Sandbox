# Sandbox Gap Matrix (Studio 감사, 2026-11-03)

> **감사 문서** (master a1307fc). 벤치마크 ([ENGINE-BENCHMARK](ENGINE-BENCHMARK.md)) 와 실제 소스를 잇는 표. 상태:
> **Implemented** (코드 + 자동 시험) · **Partial** · **Designed** (상세 설계, 코드 없음) · **Planned** (계획 줄만) · **Missing** · **Unverified**.
> 특별 표시: **⟂접근** = 기능 (코드) 은 있으나 사용자 접근 경로 (UI) 가 없다 · **⟂낡음** = 코드 · 문서의 계획 표시가 시점이 지났다 ·
> **⟂불일치** = 설계와 구현 · 데이터 모델이 어긋난다. "실행해 확인" 은 [VERIFICATION-RESULTS](VERIFICATION-RESULTS.md) 에 있는 것만.
> 근거 표기: U · G · E · R = 벤치마크 출처, [일반] = 공식 문서 미확인.

---

## 1. Creator Studio (Track B)

| ID | 기능 | 경쟁 근거 | Sandbox 파일 | 상태 | 부족한 부분 | 영향 | 개선안 | P | 버전 | 선행 | 테스트 기준 |
|----|------|-----------|--------------|------|-------------|------|--------|---|------|------|-------------|
| GM-01 | 프로젝트 홈 · 생성 · 열기 · 최근 | [일반] Unity Hub · Godot 관리자 | core/project/GameProject.hpp:149 createProject, tools/sbx | Partial **⟂접근** | 클라이언트 UI 없음 — sbx new (CLI) 만 | UX: 알파 G1 실패 | Studio Home → ProjectService → createProject (25 5장 ① ②) | P0 | v0.5 (M6-T1) | — | CTest studio_new_project |
| GM-02 | GUI 빌드 · 패키징 · 누락 검사 | [일반] Build Settings · Export | core/package/GamePackage.hpp:83 buildPackage | Partial **⟂접근** | UI 없음 (sbx build), 누락 검사 없음 | UX: G8 실패 | BuildService + "빌드" 화면 (25 5장 ⑩) | P0 | v0.5 (M6-T3 확장) | GM-03 | CTest studio_build (빈 캐시에서 열기) |
| GM-03 | 패키지에 에셋 포함 · 프로젝트 에셋 루트 | [일반] 모든 엔진 내보내기 | core/package/GamePackage.cpp (assets 없음), apps/client/main.cpp:189 (전역 에셋 루트) | Missing **⟂불일치** | 프로젝트 에셋 폴더 · 패키지 실림 · 클라이언트의 프로젝트별 에셋 루트 없음 | v0.5 C1 (A → B) 차단 — A 의 PNG 가 B 에 없다 | 프로젝트 assets/ + .sbpkg 형식 3 (assets/ 항목 · digest) + 클라이언트 루트 차례 (패키지 → 프로젝트 → 엔진) (26 2장) | P0 | v0.5 (M6-T9 신규) | GM-04 | unit (패키지 왕복 · digest), M7-T3 E2E |
| GM-04 | 안정 에셋 id (.meta) | U1 · G1 · E2 | render/asset/AssetId.hpp ("[계획] .meta GUID") | Planned | 경로 해시 — 이름 바꾸기 = 참조 끊김 | 확장성 · 데이터 손실 | .meta 사이드카 128 비트 id + 색인 (ADR-0090) | P0 | v0.5 최소 · v0.6 | — | unit: 가져오기 → 옮기기 → 참조 그대로 |
| GM-05 | Asset Browser · 가져오기 | R5 Toolbox · [일반] | 없음 | Missing | 브라우저 · 끌어다 놓기 · 썸네일 | UX: G2 | 25 5장 ⑥, 26 2장 | P1 | v0.5 최소 · v0.6 | GM-04 | 수동 QA + unit (가져오기 Job) |
| GM-06 | 에셋 캐시 분리 · 다시 가져오기 · Hot Reload | U1 | render/asset/AssetManager.hpp ("8A 는 해제 · 핫 리로드가 없다") | Planned | 캐시 · 파일 감시 · 아틀라스 다시 쓰기 | 생산성 | 26 1장 A4 | P2 | v0.6 | GM-04 | unit · 렌더 시험 |
| GM-07 | 참조 찾기 · 안전 삭제 | E2 redirector | 콘텐츠 V2 (JSON 참조만) | Missing | 에셋 역참조 · 지우기 경고 | 데이터 손실 | 에셋 DB 역참조 (26 5장) | P1 | v0.6 | GM-04 | unit |
| GM-08 | 레이아웃 저장 · 이름 붙인 레이아웃 | G3 | apps/client/ui/ImGuiLayer.cpp:198 (IniFilename = nullptr, "[계획] 레이아웃 저장 (Phase 12)") | Missing **⟂낡음** (Phase 12 는 끝났다) | 저장 · 프리셋 | UX | 사용자 폴더 studio/layouts/ (25 4장) | P1 | v0.5 (M6-T6 신규) | — | unit (경로 · 깨진 파일 옮기기) |
| GM-09 | 명령 팔레트 · 단축키 표 | [일반] | 단축키 몇 개 (Application · Editor) | Missing | 한 표 · 충돌 진단 · 팔레트 | UX · 발견성 | CommandService (25 3장) | P2 | v0.6 | GM-28 | unit (충돌 진단) |
| GM-10 | 자동 저장 · 크래시 복구 | R1 (5 분 자동 저장) | apps/server/ServerOptions.hpp:7 ("[계획] --autosave"), 충돌 보고 M1 | Planned | 주기 저장 · 복구 제안 | 데이터 손실 | AutosaveService (25 7장) | P0 | v0.5 (M6-T7 신규) | — | CTest studio_recovery |
| GM-11 | 되돌리기 — 월드 + 파일 문서 | [일반] | editor/Undo.hpp (월드 명령만) | Partial | 팩 파일 편집 · 문서 되돌리기 없음 | 복구 불가 편집 | UndoService 한 줄 (25 3장) | P1 | v0.6 | GM-28 | unit |
| GM-12 | 스크립트 자동 완성 | R2 | script/lang/LanguageService (프로토타입 — ctx. · 도메인) | Partial | 타입 기반 · 컴포넌트 · import · 편집기 팝업 | 생산성 · 오류 | 27 3장 | P0 | v0.5 (M6-T8 신규) | — | unit (후보 표) + 응답 시간 측정 |
| GM-13 | hover · 서명 도움말 | R2 | HostFunction.doc · 서명 (script/api/HostApi.hpp:51) | Partial (데이터만) | 편집기 표시 | 생산성 | 27 2장 | P1 | v0.5 (M6-T8) | GM-12 | unit |
| GM-14 | 정의 · 참조 · 이름 바꾸기 · 빠른 수정 | G2 (LSP) | Program.symbols · modules (script/lang/Checker.hpp:197) | Missing | 위치 → 기호 색인 | 생산성 | 27 2 · 4장 | P2 | v0.6 | GM-12 | unit |
| GM-15 | 소스 줄 표 (.sbc) | [일반] | script/vm/Bytecode.hpp (줄 표 없음) | Planned (M6-T4) | pc → 줄 | 오류 위치 · 디버거 · 프로파일 | .sbc 형식 10 (27 6장) | P0 | v0.5 (M6-T4) | — | unit (왕복 · 트랩 줄) |
| GM-16 | 디버거 (중단점 · 단계 · 스택 · 지역 · 감시) | G2 (DAP) · [일반] | 없음 | Missing | 전부 | 생산성 | 재생 위 디버깅 (27 6장 B) | P1 | v0.6 | GM-15 | unit (재생 중단 · 같은 해시) · 훅 비용 측정 |
| GM-17 | 외부 IDE — LSP | G2 · R1 Script Sync | sbx sdk (d.ts — ADR-0065) | Partial | 의미 (부분집합 · 도메인) 진단 | 생산성 | sbx lsp (27 5장) | P2 | v0.7 | GM-12 · GM-14 | LSP 왕복 시험 |
| GM-18 | Prefab 인스턴스 오버라이드 · 변형 | U2 · R5 | core/components/core/Tags.hpp (core.prefab = 출처 id 만) | Missing | 오버라이드 · 전파 · 변형 | 생산성 · 확장성 | 25 7장 (설계 ADR 필요 — 결정론 측정) | P1 | v0.6 | — | det 시험 (전파 순서 · 해시) |
| GM-19 | 그룹 · 부모-자식 | [일반] | 없음 (평평한 ECS) | Missing | 그룹 · 계층 | 대규모 장면 | 편집기 그룹 (v0.6) · 변환 계층은 3D 결정 (25 7장) | P2 | v0.6 · v1.x | — | unit |
| GM-20 | Scene/Game 뷰 분리 · 다중 모니터 | [일반] · G3 | 한 뷰 | Missing | 두 카메라 · 떼어 낸 창 | UX | 25 4 · 5장 | P3 | v0.6 | — | 수동 QA |
| GM-21 | 게임 UI 런타임 | U4 | ui/ 없음 | Designed (ADR-0079, 20 2장) | 전부 | G5 | 20 M3 그대로 | P0 | v0.5 (M3) | M2-T5 ✅ | 20 M3 시험 |
| GM-22 | 게임 UI 시각 디자이너 | U4 UI Builder | 없음 | Planned (21 4장 v0.6) | 전부 | G5 (v0.6) | 25 8장 — 같은 문서 편집 | P1 | v0.6 | GM-21 | 수동 QA · 문서 왕복 unit |
| GM-23 | 서버 + N 클라이언트 테스트 단추 | R3 · E1 · U3 | network LocalServerHost, platform/process (SandboxProcess) | Partial | 단추 · 프로세스 관리 · 한 번에 끝내기 | G6 멀티 | TestService (25 3장) | P1 | v0.6 | — | CTest (서버 + 2 헤드리스) |
| GM-24 | 지연 · 손실 모의 (사용자용) | R3 · E1 | network/transport/SimulatedTransport.hpp (시험 전용) | Partial **⟂접근** | 옵션 · UI | 네트워크 버그 발견 | 클라이언트 · 서버 옵션 + Test 화면 | P2 | v0.6 | GM-23 | net 시험 |
| GM-25 | 데이터 편집 GUI (키/값 · 배열 · 객체) | [일반] | editor/ui/InspectorPanel (FieldMeta), TriggerForm | Partial | 콘텐츠 JSON 일반 폼 | 초보자 | 26 6장 | P2 | v0.6 | — | unit (스키마 → 폼 왕복) |
| GM-26 | 시각 스크립팅 캔버스 | [일반] | 트리거 폼 (ADR-0052) | Planned (v0.6) | 노드 그래프 | 노코드 | 기존 계획 | P2 | v0.6 | — | 수동 QA |
| GM-27 | 협업 — 스크립트 초안 · 문서 병합 | R1 Drafts | 서버 권위 명령 편집 · edit_session | Partial | 파일 문서 동시 편집 · 초안 | 협업 | 25 7장 (D-17) | P2 | v0.7 | GM-28 | 시험 |
| GM-28 | Editor Services 층 (UI 분리) | [일반] · G3 (EditorDock 플러그인) | editor/ (Editor.cpp — 툴 로직은 ImGui 없음, editor/ui 는 ImGui), apps/client/Application.cpp (808 줄) · NetworkSession.cpp (1,087 줄) | Partial | 서비스 경계 · check_includes 규칙 | 유지보수 · UI 교체 | editor/services (ADR-0089) | P1 | v0.5 (새 기능부터) · v0.6 | — | check_includes 규칙 시험 |
| GM-29 | 콘텐츠 편집의 저장 위치 (오버레이 ↔ 팩 파일) | [일반] | editor/ui/ContentPanel.cpp:529 ("팩 원본 (적용하면 오버레이로)") | Partial **⟂불일치** | Studio 편집이 월드 오버레이에만 — 프로젝트 소스 (content/) 에 쓰는 길이 없다 | 두 진실원 · 버전 관리 밖 편집 | DocumentService [저장 (팩 파일)] vs [적용 (오버레이)] 구분 (25 3장) | P1 | v0.5 (M6-T8 와 같이) | — | unit (저장 → 다시 열기 = 같은 내용, 오버레이 비움) |
| GM-30 | 플러그인 | R2 (ScriptEditorService — 플러그인 권한) | 없음 | Missing | 전부 | 확장성 | 25 9장 (스크립트 · Capability) | P3 | v0.8+ | GM-28 | 24 P5 · P6 |

## 2. Engine Runtime (Track A) — Studio 관점에서 본 것만 (나머지는 22 · 23)

| ID | 기능 | Sandbox 파일 | 상태 | 부족 · 개선 | P | 버전 |
|----|------|--------------|------|-------------|---|------|
| GM-31 | Edit/Play (원본 보존) | ADR-0053, unit_network · unit_client | Implemented | — | — | ✅ |
| GM-32 | Inspector 자동 생성 (리플렉션) | editor/ui/InspectorPanel, 10 E4 | Implemented | 오버라이드 표시 (GM-18) | — | ✅ |
| GM-33 | 스크립트 Hot Reload (오버레이) | ADR-0066, test_script_overlay | Implemented | 디버거와의 충돌 규칙 (27 6장) | — | ✅ |
| GM-34 | 충돌 보고 · 진단 (sbx doctor) | foundation/diag, platform/crash, CTest crash_report_* · project_doctor | Implemented | 클라이언트 안의 진단 화면 (20 7.2 [계획]) | P2 | v0.6 |
| GM-35 | UI 이벤트 위조 방지 (버튼 → 서버 검증) | PlayerSignal (v0.5 M2-T5 (3)), test_player_signal | Implemented (신호 경로) | UI 런타임 연결 (M3-T5) | — | v0.5 |
| GM-36 | 다국어 · 접근성 기반 | ADR-0083 | Designed | M4 그대로 | P1 | v0.5 |
| GM-37 | 메모리 · GPU 프로파일 | 14 7.8 (렌더 GPU 시간) | Partial | 메모리 · 에셋 예산 (22 11장) | P2 | v0.8 |
| GM-38 | 권장 사양 자동 분석 | 22 11장 | Planned | — | P1 | v0.9 |
| GM-39 | 2D 타일 그림 | 지형 칠하기 (머티리얼 칸) | Partial | 스프라이트 타일 · 타일셋 | P2 | v0.6 |
| GM-40 | 3D | 없음 | Missing | 별도 결정 D-16 (비용 · 의존 · 근거) | P3 | v1.x |

## 3. Platform Services (Track C) — 변경 없음, 상태만

| ID | 기능 | 상태 | 버전 (ADR-0082 · 0088) |
|----|------|------|-------------------------|
| GM-41 | Hub (계정 · 레지스트리 · 게시) | Planned | v0.6 (D-06 먼저) |
| GM-42 | 인증 멀티 · 방 · 무료 세션 | Planned | v0.7 (D-07) |
| GM-43 | 마켓 · 호스팅 · 결제 (테스트 모드) | Planned | v0.8 |
| GM-44 | 팀 ACL · CI/CD · 서명 업데이트 | Planned | v0.9 |

## 4. 요약

```text
P0 (v0.5) 8 개   GM-01 · 02 · 03 · 04 · 10 · 12 · 15 · 21 (+ 기존 M3 · M7)  — 셋은 "코드는 있고 접근 경로가 없다" (01 · 02 · 24)
⟂접근 3 · ⟂낡음 1 · ⟂불일치 2 (GM-03 에셋 · GM-29 오버레이)
문서상 구현인데 코드가 없는 항목은 찾지 못했다 (표본 — 10 · 18 · 19 · 20 · 21 의 ✅ 를 소스 · 시험 이름으로 대조, VERIFICATION-RESULTS 2장)
```
