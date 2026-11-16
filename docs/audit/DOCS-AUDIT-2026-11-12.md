# 문서 체계 감사 (2026-11-12) — 분류 · 설계와 구현의 차이 · 정리한 것

> **감사 문서 (이력).** 문서 전체를 소스 · 시험 · 빌드 구성과 대조한 기록이다. 기준 커밋 master `1cce764`. 이 문서는 그날의 판정이고
> 뒤에 고치지 않는다 — 지금 상태는 [overview/CURRENT_STATUS](../overview/CURRENT_STATUS.md), 열린 발견 사항의 처리는 태스크 등록부로 옮긴다.

## 목차

1. [조사 범위](#1-조사-범위)
2. [문서 분류](#2-문서-분류)
3. [구조 결정](#3-구조-결정)
4. [설계와 구현의 차이](#4-설계와-구현의-차이)
5. [이번에 바꾼 것](#5-이번에-바꾼-것)
6. [검증 결과](#6-검증-결과)
7. [권장 사항](#7-권장-사항)

## 1. 조사 범위

| 대상 | 수 · 방법 |
|---|---|
| Markdown 문서 | `docs/` 220 + 루트 3 (README · AGENTS · DEVELOPMENT_LOG) + `samples/` 4 = 227 (조사 시작 시점. `external/` 의 남의 문서와 시험 픽스처는 범위 밖) |
| 로드맵 · 태스크 | [task-registry](../roadmap/task-registry.md) 167 행 전부, [releases](../roadmap/releases.md) 버전 절 전부, [release-gates](../roadmap/release-gates.md) |
| 소스 | 최상위 모듈의 폴더 · CMake 타깃 · `target_link_libraries` · 호환 번호 상수 · 에디터 툴 목록 · 서비스 · 패널 파일 |
| 시험 | `tests/` 시험 파일 174 · `TEST_CASE` 약 1,000 · CTest 등록 이름, 등록부의 완료 기준이 가리키는 시험 파일 표본 대조 |
| 수동 QA | [MANUAL-QA](../qa/MANUAL-QA.md) 의 `[ ]` 217 · `[x]` 8 줄 |
| 개발 기록 | DEVELOPMENT_LOG 최근 항목, 커밋 262 개의 제목, CI 실행 목록 (`gh run list`). GitHub Issue · PR 은 기록이 없다 |

읽는 방법은 번호 문서의 머리 "상태" 줄 · 현황 표 · 호환 번호 · 완료 표시를 소스와 견주는 것이었다. 문서 본문 3 만 5 천 줄을 한 줄씩 다 대조하지는 않았다 —
대조하지 못한 범위는 6장에 적는다.

## 2. 문서 분류

| 분류 | 문서 | 판정 | 처리 |
|---|---|---|---|
| 최신 — 계획의 정본 | `roadmap/` README · versioning · task-registry · releases · release-gates · stabilization | 등록부 · releases 는 커밋마다 갱신되고 `check_docs.py` 가 검사한다. versioning 5장의 호환 번호 두 칸이 낡아 있었다 (G-01) | 유지 · 번호 수정 · 보는 문서 (MASTER_ROADMAP · versions/) 를 그 위에 생성 |
| 최신 — 구현 문서 (규범) | `00` ~ `12` · `14` · `15` · `19` · `22` · `24` ~ `27` · `29` | 본문은 태스크마다 고쳐 왔다. 머리 "상태" 줄이 Phase 순서로 길게 쌓여 처음 읽기 어렵다 | 유지. 입구는 [overview/MODULES](../overview/MODULES.md) 로 |
| 머리 · 일부 표가 낡은 문서 | `13` (CTest 68 개 — G-07) · `17` (머리 날짜) · `18` 2장 (G-05) · `20` 머리 (G-06) · `28` 1장 (G-04) · 루트 README · docs/README (G-03) | 본문 일부는 맞고 요약 줄 · 표가 뒤처졌다 | 틀린 줄을 고치거나 (18 · 28 · README) 안내를 붙임. 13 · 17 · 20 은 권장 사항으로 |
| 중복 — 현황 | `18` 2장 · `21` 1장 · audit/SANDBOX-GAP-MATRIX · audit/ALPHA-READINESS · `28` 의 "지금" 칸 | 같은 "구현 상태" 를 다섯 곳이 다른 날짜로 적어 서로 어긋났다 | 입구를 [overview/CURRENT_STATUS](../overview/CURRENT_STATUS.md) · [FEATURE_MATRIX](../overview/FEATURE_MATRIX.md) 하나로. 나머지는 날짜가 붙은 분석으로 남기고 안내를 붙임 |
| 중복 — 로드맵 | `16` (Phase) · `18` 1 · 5 · 6장 · `20` 10장 · `23` · `roadmap/` | 2026-11-03 재구성 (ADR-0096) 에서 정본을 `roadmap/` 로 정했고 옛 표기는 versioning 4장의 변환표로 읽는다 | 유지 — 이미 정리된 구조다. 태스크 카드 상세는 20 · 23 에 그대로 |
| 계획 문서 | `20` · `22` · `23` · `24` · `28` · `roadmap/stabilization` | 태스크 ID 로 등록부에 이어져 있다 | 버전 문서가 태스크 → 카드 문서를 잇는다 |
| 감사 (날짜가 붙은 분석) | `audit/` 8 · `21` | 그날의 판정 — 고치지 않는 것이 맞다 | 유지 |
| 이력 | `adr/` 137 + README · `releases/` 6 · DEVELOPMENT_LOG | 고치지 않는다 (AGENTS 6장) | 유지 |
| 동결 (보관 성격) | `design/` 1 · `platform/` 28 | 이미 "이력 (동결)" 로 표시돼 있고 ADR 15 개가 링크한다 | 유지 — `docs/archive/` 로 옮기지 않았다 (3장) |
| 절차 | `qa/` 2 | 맞다. 다만 확인된 줄이 8 / 225 다 (G-08) | 유지 |
| 불명확 | 없음 | 목적을 알 수 없는 문서는 찾지 못했다 | — |

## 3. 구조 결정

요청의 제안 구조 (`overview/` · `roadmap/` · `architecture/` · `modules/` · `testing/` · `development/` · `archive/`) 를 그대로 만들지 않았다.

| 결정 | 이유 |
|---|---|
| 번호 문서 `00` ~ `29` 를 옮기거나 이름을 바꾸지 않는다 | AGENTS · ADR 137 개 · DEVELOPMENT_LOG · 소스 주석이 "02 9.6" · "19 16장" 같은 번호로 가리킨다. ADR 과 로그는 고칠 수 없는 이력이라 옮기면 참조가 영영 깨진다 |
| `overview/` 를 새로 만든다 | 없던 층이다 — 현황 한 장 · 기능 매트릭스 · 모듈 입구 |
| `roadmap/` 은 그대로 두고 `MASTER_ROADMAP` · `versions/` 를 더한다 | 등록부가 이미 상태의 정본이다. 체크리스트를 손으로 또 쓰면 두 곳이 어긋나므로 생성한다 |
| `modules/` · `architecture/` · `testing/` 폴더를 만들지 않는다 | 그 내용이 번호 문서 (01 · 02 ~ 11 · 13 · 19 · 25 ~ 27) 에 이미 있다. 복사하면 중복이 는다 — [MODULES](../overview/MODULES.md) 가 모듈별 입구 역할을 한다 |
| `development/` 에는 문서 가이드만 둔다 | 빌드 안내 = [15-BUILD](../15-BUILD.md), 기여 규칙 = [AGENTS](../../AGENTS.md) · [12](../12-CODING-STANDARDS.md) 가 이미 있다 |
| `archive/` 를 만들지 않는다 | 보관할 문서가 없다 — 낡은 분석은 날짜와 안내를 붙여 제자리에 두었고, 동결 문서는 ADR 이 링크한다 |
| 번호 문서를 9 절 틀로 다시 쓰지 않는다 | 규범 문서다 — 순서를 바꾸다 뜻이 바뀌면 코드와의 계약이 흔들린다. 틀은 [문서 가이드](../development/DOCUMENTATION_GUIDE.md) 7장에 두고 새 문서 · 큰 개정 때 따른다 |

## 4. 설계와 구현의 차이

심각도: **높음** = 읽는 사람이 틀린 판단을 한다 · **중** = 혼란 · 추적 누락 · **낮음** = 표기.
처리: ✅ 이번에 고침 · 📝 기록만 (결정이 필요하거나 범위 밖).

| ID | 영역 | 발견 사항 | 심각도 | 권장 조치 | 처리 | 관련 파일 |
|---|---|---|---|---|---|---|
| G-01 | 호환 번호 | versioning 5장의 "지금" 값이 소스와 달랐다 — Bytecode 10 (소스 12) · Protocol 16 (소스 18) | 높음 | 표를 소스에 맞춤. 자동 대조 검사 제안 (문서 가이드 8장) | ✅ | `docs/roadmap/versioning.md` · `script/vm/Bytecode.hpp` · `network/protocol/Messages.hpp` |
| G-02 | 네트워크 | 08 5장 제목이 `kProtocolVersion = 16` (17 · 18 의 설명은 표 아래에 이미 있다) | 중 | 제목 수정 | ✅ | `docs/08-NETWORK.md` |
| G-03 | 입구 | 루트 README 의 상태 표가 2026-10-26 이고 v0.8 이후를 모두 `[계획]` 으로 적었다. docs/README 0장이 "다음 v0.8" | 높음 | 현황 대시보드로 연결 · 표 갱신 | ✅ | `README.md` · `docs/README.md` |
| G-04 | 알파 기준 | 28 1장의 "지금" 칸이 G1 (CLI 필요) · G2 (불가) · G5 (불가) · G8 (CLI 필요) 로 남아 있었다 — M6-T1 · M6-T9 · M3 · M6-T3 은 Done | 높음 | 다섯 칸을 등록부 · 시험에 맞춰 갱신 | ✅ | `docs/28-CREATOR-UX-GATES.md` |
| G-05 | 현황 | 18 2장이 게임 UI · 다국어 "Missing", 게임플레이 API "Partial", 실행 구조 v2 "Planned" | 높음 | 세 줄 수정 + 현황 대시보드 안내 | ✅ | `docs/18-PLATFORM.md` |
| G-06 | 현황 | 21 1장 (2026-11-02 분석) 이 게임 UI · 다국어 Missing · 프로토콜 15 · .sbpkg 형식 2 · ctest 74. 20 의 머리가 "여기의 모든 기능은 [계획]" (M1 ~ M4 · M8 은 Done) | 중 | 21 은 날짜 붙은 분석으로 남기고 안내. 20 의 머리 문장은 다음 개정 때 | ✅ (21 안내) · 📝 (20) | `docs/21-PLATFORM-FEATURES.md` · `docs/20-CREATOR-ALPHA.md` |
| G-07 | 시험 문서 | 13 의 머리가 "CTest 68 개 (2026-10-28)" — 지금 등록 수는 6장 | 낮음 | 머리 줄 갱신 | 📝 | `docs/13-TESTING.md` |
| G-08 | 검증 | 완료로 적힌 버전의 실기 확인이 대부분 비어 있다 — MANUAL-QA `[x]` 8 / 225. v0.1 Phase 6 ~ 14 의 창 · GPU 줄, v0.7 은 7 / 19 로 0.7.0 을 냈다 | 중 | 보는 문서에 "창" 칸을 따로 두어 자동 시험과 구분 (했다). 실기는 RH-23-7 이 받는다 — 그 전에 v0.7 줄부터 | ✅ (표시) · 📝 (실기) | `docs/qa/MANUAL-QA.md` |
| G-09 | 계획 누락 | Object 회전 · 크기 조절 툴이 없고 (툴 다섯: Select · Move · Place · TerrainBrush · Erase), Transform 에 크기 필드가 없으며, 이 일을 맡은 태스크가 등록부에 없다 | 중 | 필요한지 결정 — 필요하면 등록부에 태스크 추가 (v0.12 Advanced Creator Tools 가 자연스럽다) | 📝 | `editor/Editor.hpp` · `core/components/core/Transform.hpp` |
| G-10 | 계획 누락 | 한 프로젝트 안에서 월드 (Scene) 를 더 만드는 Studio 화면 · 태스크를 찾지 못했다 | 낮음 | 필요 여부 결정 | 📝 | `docs/18-PLATFORM.md` 5.2 |
| G-11 | 순서 | v0.8 · v0.9 가 출시되지 않은 채 (제품 버전 0.7.0 · 릴리스 노트 없음) v0.10 을 시작했다. 트랙이 서로 기다리지 않는다는 규칙 (releases 머리말) 안이지만 "창 QA 대기" 버전이 둘 쌓였다 | 중 | 창 QA 를 먼저 닫아 0.8.0 · 0.9.0 을 내거나, 쌓이는 것을 받아들인다는 결정을 적는다 | 📝 | `docs/roadmap/README.md` 1장 |
| G-12 | 릴리스 | 0.2.0 ~ 0.7.0 의 릴리스 노트는 있는데 Git 태그가 하나도 없다 | 낮음 | 릴리스 커밋에 태그 (이력을 고치는 일이라 사용자 결정) | 📝 | `docs/releases/` |
| G-13 | 감사 문서 | audit/VERSION-MIGRATION-MATRIX 의 Bytecode · Protocol 값이 2026-11-03 그대로 (10 · 16) | 낮음 | 날짜 붙은 감사라 그대로 둔다. 번호의 정본은 versioning 5장 | 📝 | `docs/audit/VERSION-MIGRATION-MATRIX.md` |
| G-14 | 완료 기준 | 등록부의 미착수 태스크 여럿은 완료 기준이 한 낱말이다 ("순서" · "우선순위 · 소비" · "—"). PS · RT-07 · RT-08 의 다수 | 중 | 그 버전을 시작할 때 카드를 채운다. v0.10 은 버전 문서의 세부 체크리스트로 풀어 두었다 | ✅ (v0.10) · 📝 | `docs/roadmap/task-registry.md` |
| G-15 | 정의 없음 | RH-23-6 운영 런북 · RH-23-8 개인정보 정책이 Missing — GA 차단 조건 | 중 | D-06 뒤 정의 (이미 등록부에 있다) | 📝 | `docs/roadmap/task-registry.md` |
| G-16 | 시험 | `studio_gui_smoke` 는 창이 없으면 SKIP 이다 — 로컬 전체 CTest 의 "통과" 가 GUI 를 포함한다고 읽으면 안 된다 | 낮음 | 보는 문서에 적음 | ✅ | `tests/CMakeLists.txt` |
| G-17 | 외부 연동 | "Graphics SDK 연동" 에 해당하는 계획을 찾지 못했다 — 렌더는 자체 RHI | 낮음 | 요구가 있으면 범위 결정 | 📝 | `docs/06-RENDERING.md` |

찾지 못한 것:

- **문서는 완료라는데 코드가 없는 기능** — 등록부 Done 표본의 완료 기준이 가리키는 시험 파일은 모두 있었다 (보는 문서에 적은 시험 이름 · 경로는 스크립트로 존재를 확인했다). 2026-11-03 감사 ([VERIFICATION-RESULTS](VERIFICATION-RESULTS.md) 2장) 와 같은 결론이다.
- **버전 사이 선행이 거꾸로 놓인 태스크** — `check_docs.py` D 검사가 0 건이다.
- **서로 모순되는 설계** — 현황 표끼리의 날짜 차이 (G-04 ~ G-06) 말고는 찾지 못했다.

## 5. 이번에 바꾼 것

| 종류 | 파일 | 내용 |
|---|---|---|
| 새 문서 | `docs/overview/CURRENT_STATUS.md` | 현황 대시보드 |
| 새 문서 | `docs/overview/FEATURE_MATRIX.md` | 기능 매트릭스 (F-101 ~ F-407) |
| 새 문서 | `docs/overview/MODULES.md` | 모듈 입구 · Mermaid 그림 4 |
| 새 문서 | `docs/roadmap/MASTER_ROADMAP.md` | 버전 표 · 진행률 · 알파 · GA 까지 남은 작업 |
| 새 문서 (생성) | `docs/roadmap/versions/v0.5.md` ~ `v1.0.md` (22) | 버전별 작업 요약 · 체크리스트 · 미완료 · 완료 조건. v0.10 은 세부 체크리스트 포함 |
| 새 문서 | `docs/development/DOCUMENTATION_GUIDE.md` | 문서 층 · 상태 표시 · 갱신 절차 · 작성 규칙 |
| 새 문서 | 이 문서 | 감사 기록 |
| 새 도구 | `tools/gen_roadmap.py` · CTest `docs_roadmap_generated` | 등록부 → 보는 문서 생성 · 어긋남 검사 |
| 수정 | `README.md` · `docs/README.md` · `AGENTS.md` · `docs/roadmap/README.md` | 입구 · 목차 · 갱신 규칙 |
| 수정 | `docs/roadmap/versioning.md` · `docs/08-NETWORK.md` · `docs/18-PLATFORM.md` · `docs/28-CREATOR-UX-GATES.md` · `docs/21-PLATFORM-FEATURES.md` | G-01 ~ G-06 |
| 수정 | `docs/13-TESTING.md` · `docs/17-SOURCE-MAP.md` · `DEVELOPMENT_LOG.md` | 새 시험 · 새 도구 · 작업 기록 |
| 통합 | — | 파일을 합치지 않았다. 현황의 입구를 overview 로 모으고 겹치는 표에 안내를 붙였다 |
| 보관 | — | 없음 (3장) |

엔진 · Studio 의 코드는 바꾸지 않았다 (`tests/CMakeLists.txt` 에 문서 검사 시험 하나를 더한 것뿐).

## 6. 검증 결과

| 검사 | 결과 | 명령 |
|---|---|---|
| 문서 링크 · ADR · 태스크 ID · 배정 | 문제 0 | `python tools/check_docs.py --root .` |
| 보는 문서 = 등록부, 세부 체크리스트 정합 | 낡은 것 0 · 문제 0 | `python tools/gen_roadmap.py --root . --check` |
| 보는 문서에 적은 소스 경로 · 시험 파일 · CTest 이름 | 모두 존재 | 일회성 스크립트 (백틱 안의 경로 · `test_*` · CTest 이름을 저장소 · `ctest -N` 과 대조) |
| Markdown 표 · 코드 블록 | 표의 열 수 불일치 0 · 닫지 않은 코드 블록 0 (새 문서) | 일회성 스크립트 |
| 전체 CTest (Windows MSVC Debug) | 90 개 중 89 통과 · 실패 0 · `studio_gui_smoke` 1 개는 SKIP (창 없음 — GUI 는 검증되지 않았다). 새 시험 `docs_roadmap_generated` 포함 (2026-11-16 실행) | `ctest --preset windows-msvc-debug` |

검증하지 못한 것:

| 무엇 | 이유 |
|---|---|
| Markdown 렌더링을 눈으로 | GitHub 화면으로 열어 보지 않았다 — 표 · 체크리스트 · Mermaid 는 문법만 확인했다 |
| 창 · GPU 기능 · Linux · macOS 실기 | 헤드리스 조사다 |
| 번호 문서 본문 전체의 줄 단위 대조 | 머리 · 현황 표 · 호환 번호 · 완료 표시 위주로 봤다. 본문 깊은 곳의 낡은 `[계획]` 표시가 더 있을 수 있다 |
| 기능 하나하나의 동작 | 시험이 등록돼 있고 통과하는지까지다 |
| 문서 안 앵커 (`#절`) | 검사 도구가 파일까지만 본다 |

## 7. 권장 사항

즉시:

| # | 무엇 | 이유 |
|---|---|---|
| 1 | v0.8 · v0.9 창 QA (17 줄) 를 확인하고 0.8.0 · 0.9.0 을 낸다 | 출시 대기 버전이 둘 쌓였다 (G-11). 사람이 창으로 해야 한다 |
| 2 | G-09 (회전 · 크기 툴) · G-10 (월드 추가 화면) 을 계획에 넣을지 정한다 | 등록부에 없는 일이다 — 임의로 태스크를 만들지 않았다 |
| 3 | 13 의 머리 CTest 수 · 20 의 머리 문장을 고친다 | G-06 · G-07 |

나중에:

| # | 무엇 | 이유 |
|---|---|---|
| 4 | 호환 번호 · 등록부 시험 이름의 자동 대조를 `check_docs.py` 에 더한다 | G-01 같은 어긋남을 사람이 찾지 않게 |
| 5 | 번호 문서의 머리 "상태" 줄을 짧게 (지금 상태 한두 줄 + 이력은 아래로) | Phase 별 문장이 쌓여 처음 읽는 사람이 현재 상태를 찾기 어렵다 |
| 6 | 버전을 시작할 때 그 버전 문서에 세부 체크리스트를 쓴다 (v0.11 부터) | 한 낱말짜리 완료 기준을 풀어 쓴다 (G-14) |
| 7 | 릴리스 커밋에 Git 태그 | G-12 |
| 8 | v0.7 의 남은 수동 QA 12 줄 | 출시된 버전의 실기 확인이 비어 있다 (G-08) |
