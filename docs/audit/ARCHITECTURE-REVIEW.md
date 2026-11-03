# 아키텍처 검토 — 구조적 위험 10 · 재사용 · 새로 설계할 것 (Studio 감사, 2026-11-03)

> **감사 문서** (master a1307fc). 근거는 소스 경로 · 시험 이름 ([SANDBOX-GAP-MATRIX](SANDBOX-GAP-MATRIX.md) 의 ID). 대응 설계는 25 ~ 28 ·
> ADR-0089 ~ 0091, 작업은 23. 위험 순서 = 알파 목표 (CLI 없이 만들어 다른 PC 에서 실행) 에 대한 영향.

---

## 1. 구조적 위험 10

| # | 위험 | 종류 | 근거 | 영향 | 대응 (어디서) |
|---|------|------|------|------|---------------|
| R1 | **에셋이 패키지 밖** — .sbpkg 은 팩만 싣고 클라이언트는 전역 에셋 루트 하나 | 데이터 모델 불일치 | core/package/GamePackage.cpp (assets 없음), apps/client/main.cpp:189 | A 의 그림이 B 에서 마젠타 — v0.5 C1 실패 | 프로젝트 assets/ · .sbpkg 형식 3 · 루트 차례 (26, M6-T9 P0) |
| R2 | **에셋 id = 경로 해시** | ID · 참조 수명 불일치 | render/asset/AssetId.hpp | 이름 · 폴더 바꾸기가 조용히 참조를 끊는다 (마젠타 — 진단 없음) | .meta 안정 id (ADR-0090) |
| R3 | **기능은 있는데 접근 경로가 없다** — createProject · buildPackage · diagnose · SimulatedTransport | 접근 경로 | GameProject.hpp:149 · GamePackage.hpp:83 · Diagnostics · SimulatedTransport.hpp | 일반 제작자가 CLI 를 써야 한다 (G1 · G8) | Studio Home · Build (M6-T1 · T7) — 로직은 재사용 |
| R4 | **콘텐츠 편집의 두 진실원** — Studio 의 "적용" 은 월드 오버레이에만, 팩 파일 (프로젝트 소스) 에 쓰는 길이 없다 | 데이터 모델 불일치 | editor/ui/ContentPanel.cpp:529 | 버전 관리 · 다른 월드에 편집이 안 간다, 어느 것이 정본인지 혼란 | DocumentService 의 [저장 (팩)] / [적용 (오버레이)] 구분 · 오버레이 → 팩 승격 (25 3장, GM-29) |
| R5 | **되돌릴 수 없는 편집** — Undo 는 받아들여진 월드 명령만 | 복구 불가 동작 | editor/Undo.hpp | 파일 문서 편집 · 오버레이 삭제 실수가 되돌려지지 않는다 | UndoService 한 줄 (v0.6), 그 전에는 오버레이 삭제 확인 대화 |
| R6 | **자동 저장 · 복구 없음** + 레이아웃 미저장 | 복구 불가 · ⟂낡음 | ServerOptions.hpp:7 · ImGuiLayer.cpp:198 | 충돌 = 작업 손실 (충돌 보고는 남지만 — M1) | AutosaveService (M6-T7), 레이아웃 (M6-T6) |
| R7 | **앱 클래스 비대 · UI 결합** — Application 808 줄 · NetworkSession 1,087 줄이 루프 · 입력 · 패널 · 세션을 함께 | UI · 엔진 결합 · 책임 중복 | apps/client/Application.cpp · NetworkSession.cpp (wc) | Studio 기능이 여기 쌓이면 시험 · UI 교체가 어렵다 | editor/services 층 (ADR-0089) — 새 기능부터, 기존은 옮기면서 (대규모 리팩터 금지) |
| R8 | **디버그 정보 없는 바이트코드** | 버전 호환 · 접근 경로 | script/vm/Bytecode.hpp (줄 표 없음) | 오류 줄 · 디버거 · 프로파일 모두 막힘 | 줄 표 .sbc 10 (M6-T4) — 다른 형식 변경과 순서 조정 (23 7장) |
| R9 | **Prefab = 복사** — 출처 id 만 기록 | 데이터 모델 · 확장 | core/components/core/Tags.hpp | 오버라이드를 나중에 넣으면 세이브 · 해시 · 리플레이 의미가 바뀐다 — 늦을수록 비싸다 | v0.6 초에 설계 ADR (결정론 측정 포함) · v0.5 는 core.prefab 의 의미를 "출처" 로 동결 (25 7장) |
| R10 | **편집기 색인 · 검색의 스레드 소유** — AssetManager 는 Render 스레드 전용, Studio 의 가져오기 · 색인 · 분석은 아직 자리가 없다 | 스레드 소유권 | render/asset/AssetManager.hpp (스레드 주석) | 메인 스레드 I/O · 분석으로 프레임 · 틱 지연 | 25 S5 — Worker Job + 메인 큐 (22 T7 · T9 규칙 확장), 결과는 편집 번호로 거른다 (27 L3) |

## 2. 순환 의존 · 경계 점검 (이번에 본 것)

```text
- check_includes (tools/check_includes.py) 는 editor/ 의 허용 의존에 platform · render 를 둔다 — editor/services 를 만들면 그 폴더는
  {foundation, script, core} 만 (imgui · render · platform 금지) 으로 규칙을 더한다 (ADR-0089 3). 지금 위반 0 (VERIFICATION-RESULTS 3장)
- script/lang/LanguageService (프로토타입) 은 script/api 만 쓴다 — SandboxScript 는 Foundation 만 (ADR-0056) 규칙 그대로
- Asset DB 를 core/asset 에 두면 render 가 core 를 몰라야 하는 규칙 (SandboxRender 는 ECS 를 모른다) 과 충돌하지 않는다: render 는 id ·
  경로만 받는다 (지금처럼 문자열) — 색인 → 경로 풀기는 클라이언트 (presentation) 에서
```

## 3. 재사용할 것 · 새로 설계할 것

| 재사용 (고치지 않거나 조금) | 새로 설계 |
|-----------------------------|-----------|
| SimCommand · 서버 권위 편집 · 역할 · Undo (월드) · EditPreview (10) | Editor Services 층 · Command · Document · Undo 통합 (25) |
| Inspector 리플렉션 · FieldMeta | 콘텐츠 JSON 일반 폼 (26 6장) |
| 테스트 플레이 (ADR-0053) · LocalServerHost · SandboxProcess · SimulatedTransport | TestService (서버 + N · 지연 모의 UI) |
| createProject · buildPackage · diagnose · 충돌 보고 (M1) | Studio Home · Build 화면 · 자동 저장 · 복구 |
| 컴파일러 (Lexer · Parser · Checker 기호 표) · Host 표 · d.ts 생성 | 언어 서비스 (자동 완성 · 색인 · 이동), 줄 표 · 재생 디버거, LSP (27) |
| 선언형 게임 UI 문서 (ADR-0079 — v0.5 M3) | UI Designer (같은 문서 편집 — 25 8장) |
| 패키지 관리자 · 잠금 파일 · digest (v0.4) | 에셋 DB · .meta · 가져오기 · .sbpkg 형식 3 (26) |
| 사용자 설정 폴더 · 안전 모드 (M1-T3) | studio.json · 레이아웃 |

## 4. 기존 설계와의 충돌 · 우선순위

```text
- ADR-0079 (게임 UI 는 GraphicsUI 를 기다리지 않는다) 와 같은 방향 — Studio UI 도 ImGui 먼저 (ADR-0089). 충돌 없음
- 10-EDITOR E1 (명령만) 은 월드 편집의 규범으로 남는다. 파일 편집 (DocumentService) 은 E1 의 대상이 아니다 — ADR-0089 2 가 이 구분을 더한다
  (10 이 우선, 25 는 파일 편집만 더함)
- 06 7.2 의 "[계획] .meta GUID" 를 ADR-0090 이 구체화 — 06 의 경로 해시 id 는 대체 경로로 남는다 (06 이 런타임 규범, 26 이 제작 데이터)
- 20 의 v0.5 완료 기준 C1 (A → B) 은 R1 때문에 지금 계획만으로는 통과할 수 없다 — 23 · 20 에 M6-T9 (P0) 을 더했다
```
