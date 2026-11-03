# 검증 결과 (Studio 감사, 2026-11-03)

> **감사 문서**. 실제로 실행한 것만 "통과" 로 적는다. 실행하지 않은 것은 이유와 명령을 적는다. 로컬 (이 개발 PC) 과 CI 를 나눈다.

---

## 1. 환경 · 기준

```text
저장소      master a1307fc (감사 시작) — 감사 커밋은 이 위에 (DEVELOPMENT_LOG 의 해시)
로컬        Windows 11 Pro 10.0.26220 · i7-14700K · MSVC 19.44 (VS 2022 Community) · 프리셋 windows-msvc-debug
CI          GitHub Actions 다섯 잡 (Linux GCC 13 · Clang 19 · Clang 19 ASan · Windows MSVC · macOS) — 결과는 gh run 으로 읽음
```

## 2. 코드 경로 추적 (읽어서 확인 — 실행 아님)

| 경로 | 확인한 것 | 근거 |
|------|-----------|------|
| CMake 타깃 | SandboxFoundation · Script · Core · Network · Platform · Render · Crash · Process · Editor (라이브러리), SandboxClient · SandboxServer · sbx · sbx_bench · sbx_sim_check · sbx_net_probe · sbx_render_tests · sbx_msl · SandboxTests (실행 파일). SandboxClient 가 SandboxEditor 를 링크 | */CMakeLists.txt, apps/client/CMakeLists.txt:43 |
| 에디터 기능 | 툴 5 · EditPreview · Undo (월드 명령) · 권한 · Inspector · 목록 · 콘텐츠 창 (Prefab · Rule · Behavior · 트리거 폼 · 스크립트) · 월드 패널 (세이브 · 리플레이) | editor/Editor.hpp · editor/ui/* · apps/client/ui/* |
| 스크립트 편집 → 컴파일 → Hot Reload | 콘텐츠 창 여러 줄 입력 → 팩 다시 컴파일 (진단 = 팩 파일 자리) → EditContent 오버레이 → 다음 틱 어댑터 다시 만들기 | editor/ui/ContentPanel.cpp:291 · :310 · :584, ADR-0066 |
| 에셋 로딩 · 참조 | requestSprite(경로) → FNV id → Worker 디코드 → 아틀라스. 에셋 루트 = --assets 또는 저장소 assets/ (전역 하나) | render/asset/*, apps/client/main.cpp:189 |
| 프로젝트 생성 → 패키징 | sbx new → core::createProject, sbx build → core::buildPackage (팩 파일 · 월드 · digest — assets 없음) | core/project/GameProject.hpp:149, core/package/GamePackage.hpp:83, GamePackage.cpp (grep "assets" 0 건) |
| 보안 · 검증 | 바이트코드 검증기 · 연료 · 콘텐츠 V1 ~ V13 · 패키지 digest · 명령 검사 (역할 · 속도 · PlayerSignal 선언 · 범위 · perTick) | 24 1장의 "현행" 행 |
| 문서 ✅ ↔ 코드 표본 대조 | 18 6.6 · 20 10장의 ✅ 행 (M1-T1 ~ T4, M2-T1 ~ T5 (3), M8-T1 ~ T9) 을 소스 경로 · 시험 이름으로 대조 — 코드 없는 ✅ 는 찾지 못함 | 각 행의 파일 · 시험 |

## 3. 실행한 것

| 무엇 | 결과 | 명령 |
|------|------|------|
| 빌드 (프로토타입 포함) | 통과 | `cmake --build --preset windows-msvc-debug` |
| 단위 시험 (unit_*) — 새 test_language_service 포함 | 14 / 14 통과 | `ctest --preset windows-msvc-debug -R "^unit_"` |
| 전체 ctest (감사 커밋 직전 — render_tests_warp 포함) | 74 / 74 통과 | `ctest --preset windows-msvc-debug` |
| check_includes | 562 파일 · 0 위반 (감사 커밋 직전) | `python tools/check_includes.py --root .` |
| CI (a1307fc — 감사 직전 커밋) | 다섯 잡 통과 (run 41454897972) | `gh run view 41454897972 --repo blackvrice/Sandbox` |

## 4. 실행하지 않은 것 (이유 · 재현 명령)

| 무엇 | 이유 | 재현 |
|------|------|------|
| Studio UI 프로토타입 (ImGui 화면) | 이번 감사는 화면 설계 (25 5장 와이어프레임) 와 서비스 경계까지 — ImGui 화면 코드는 M6-T1 에서 (기존 엔진 동작을 건드리지 않기 위해 이번에는 언어 서비스 프로토타입만 넣었다) | — |
| 다른 PC 에서 패키지 실행 (A → B) | 두 번째 PC 없음 — 그리고 R1 (에셋 미포함) 때문에 지금은 자기 그림을 쓴 게임이 통과하지 못할 것으로 예상 (코드 근거, 실행 아님) | M7-T3 계획 |
| Linux · macOS 실기 (창 · GPU) | 장비 없음 — CI 는 헤드리스 | 16 · MANUAL-QA 14B · 14C |
| 외부 사용자 사용성 시험 | 참가자 없음 — 계획 · 양식만 (28 2장) | — |
| 성능 측정 (자동 완성 응답 · 대규모 Hierarchy · 자동 저장) | 측정할 기능이 아직 없다 (프로토타입은 기능 시험만) | 25 10장 · 27 7장의 [계획] 시나리오 |
