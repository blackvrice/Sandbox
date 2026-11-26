# 성능 · 품질 기준선

> **측정한 것만 숫자로 적는다** (출처 = 명령 · 커밋 · 환경). 기능 정확성을 먼저 보고 성능을 본다. 평균 FPS 하나로 판단하지 않는다.
> 환경: Windows 11 Pro 26300 · MSVC 14.44 (VS 2022) · GTX 1660 SUPER (이 PC). 헤드리스 = OS 창 · GPU 없이 (`--headless`).

## 1. 빌드 · 시험 기준선 (리팩터링 전 — master `0f97fbc`)

| 항목 | 결과 | 명령 · 출처 |
|---|---|---|
| CI 다섯 잡 (Linux GCC 13 · Clang 19 · Clang 19 ASan · Windows MSVC · macOS) | 녹색 | run 45144888678 (2026-11-23) |
| 로컬 `ci-werror` (/WX) 빌드 | 통과 | `cmake --build build/ci-werror --config Debug` |
| 로컬 전체 CTest (Debug, project_doctor 제외 — 이 PC 의 AppData 경로 문제로 리팩터링 전부터 실패) | 96 중 95 · `unit_project` 은 `-j 8` 병렬에서만 실패, 단독 재실행 통과 | `ctest --preset windows-msvc-debug -E project_doctor -j 8` (DEVELOPMENT_LOG 2026-11-23) |
| 골든 WorldHash (det_* 16 개 — golden · replay · save/load) | 통과 | 같은 CTest |

## 2. 실행 기준선 — 헤드리스 클라이언트 (Release, master `0f97fbc`)

UI 가 그려지지 않는 경로다 (헤드리스는 렌더러가 없다) — UI 변경이 월드 · 프레임 루프를 늦추지 않았는지 보는 대조용.

| 시나리오 | 결과 (3 회) | 명령 |
|---|---|---|
| 홈 300 프레임 | 3/3 `SandboxClient 끝: frames 300 state Shutdown` | `SandboxClient --headless --frames 300 --fps 0` |
| 월드 random_walk_1k 300 프레임 | 평균 1,164.9 · 1,210.3 · 953.1 fps, 1 % Low 135.8 · 166.9 · 114.7 fps, 월드 0.43 ~ 0.51 ms · 추출 0.09 ~ 0.12 ms | `SandboxClient --headless --world random_walk_1k --frames 300 --fps 0` |

## 3. GraphicsUI 홈 (이 브랜치 — UI-R1)

| 항목 | 결과 | 명령 · 출처 |
|---|---|---|
| 첫 프레임 | 컴포넌트 13 · 그리기 명령 32 (사각형 8 · 글자 14 · clip 5) · 최근 4 · 배치 0.76 ~ 0.84 ms · 그리기 명령 0.025 ~ 0.032 ms (Release, 3 회) | `SandboxClient --headless --ui graphicsui --frames 300 --fps 0 --console` 의 "GraphicsUI 홈:" 줄 |
| 정상 상태 (599 프레임 평균) | 배치 0.008 ms · 그리기 명령 0.008 ~ 0.010 ms · 가장 긴 배치 0.75 ~ 1.00 ms (= 첫 프레임 — 글꼴 측정 · 측정 캐시가 차갑다) · 버린 입력 0 · 작업자 오류 0 (Release, 3 회) | `--frames 600` 의 "GraphicsUI 평균" 줄 |
| Debug 첫 프레임 | 배치 2.70 ms · 그리기 명령 0.37 ms | 같은 명령 (Debug) |

- 배치 · 그리기 명령 시간은 CPU 만이다 (ImDrawList 로 옮기는 시간 · GPU 시간 제외). 최근 목록 4 개 = 이 PC 의 사용자 목록 (읽기만).
- ImGui 홈과의 같은 조건 비교 (창 · GPU · FPS · 1 % Low · 입력 지연) 는 **창을 띄워야 한다** — 이 세션은 사용자 화면에 창을 띄우지
  않았다 (UI-R0-09 · R9-01, QA_CHECKLIST Q-00 · Q-01).

## 4. 아직 재지 않은 것 (UI-R9)

| 항목 | 왜 아직 | 언제 |
|---|---|---|
| 창 실행 FPS · 1 % Low · P50/P95/P99 (1080p, ImGui vs GraphicsUI) | 창 · GPU 실행 — 사람 · 실기 | UI-R0-09 · R9-01 |
| 시작 시간 · 프로젝트 로딩 | 같은 시나리오의 반복 측정 도구가 없다 | UI-R9-01 |
| CPU · GPU · RAM · VRAM · draw call · upload | 과도기 경로는 ImGui 렌더러의 것 — 목표 렌더러 (R1-10) 뒤에 비교해야 의미가 있다 | UI-R9-02 |
| 대량 목록 (10 만 행) · 바인딩 비용 | Hierarchy (R3-02) 뒤 | UI-R9-03 |
| 유휴 CPU · GPU | 지금은 매 프레임 다시 배치 · 그리기 (LayoutCache 가 배치를 재사용) — 변화가 없으면 건너뛰기는 UI-R9-04 | UI-R9-04 |

목표 (요청): 1080p 평균 60 FPS 이상 · 1 % Low 50 FPS 이상 — 기준선 측정 뒤 세부 목표를 정한다.
