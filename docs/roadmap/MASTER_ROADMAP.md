# 전체 로드맵 (v0.1 ~ v1.0) — 버전 · 진행률 · 남은 작업

> **보는 문서.** 전체 버전 계획과 진행률을 표 하나로 본다. 숫자는 손으로 적지 않는다 — `tools/gen_roadmap.py` 가
> [태스크 등록부](task-registry.md) (상태 · 목표 버전의 정본) 에서 집계해 아래 생성 구간을 다시 쓴다.
> 버전을 왜 이렇게 나눴는지는 [README](README.md) (트랙 · 의존 그래프) · [versioning](versioning.md), 버전마다의 목표 · 완료 기준 원문은
> [releases](releases.md), 출시 기준은 [release-gates](release-gates.md). 지금 상태 한 장은 [현황 대시보드](../overview/CURRENT_STATUS.md).

| 항목 | 내용 |
|---|---|
| 문서 목적 | 버전별 목표 · 진행률 · 남은 작업을 한곳에서 본다 |
| 대상 버전 | v0.1 ~ v1.0 |
| 문서 상태 | 확정 (생성 구간은 자동) |
| 정본 | [task-registry](task-registry.md) · [releases](releases.md) — 이 문서는 고치지 않고 다시 만든다 |

## 목차

1. [v0.1 ~ v0.4 (등록부 이전)](#1-v01--v04-등록부-이전)
2. 버전 표 (v0.5 ~ v1.0)
3. Closed Creator Alpha (v0.13) 까지 남은 작업
4. v1.0 (GA) 까지 남은 작업
5. [마일스톤 (성숙 단계)](#5-마일스톤-성숙-단계)
6. [집계 기준](#6-집계-기준)

## 1. v0.1 ~ v0.4 (등록부 이전)

태스크 등록부는 2026-11-03 (ADR-0096) 에 v0.5 부터 만들었다. 그 앞 버전은 Phase · 마일스톤 단위의 이력만 있어 같은 기준의 진행률을
낼 수 없다 — **집계 불가**로 두고 판정 근거 (릴리스 노트) 를 잇는다.

| 버전 | 핵심 목표 | 진행률 | 상태 | 근거 · 남은 것 |
|---|---|---|---|---|
| v0.1 | Engine Foundation — ECS · 결정론 시뮬레이션 · 청크 월드 · 네트워크 · 에디터 · 세 렌더 백엔드 (Phase 0 ~ 15) | 집계 불가 | 🟡 구현 끝 · 실기 검증 대기 | [16-ROADMAP](../16-ROADMAP.md) · [18-PLATFORM](../18-PLATFORM.md) 1.1 — Windows · Linux · macOS 실기 QA, 50k 성능 판정이 남았다 (RH-23-7 이 받는다) |
| v0.2 | Game Creator — 게임 프로젝트 · GameMode · 플레이어 행동 · Visual Trigger · 테스트 플레이 · .sbpkg | 집계 불가 | ✅ 완료 (0.2.0) | [릴리스 노트](../releases/v0.2.0.md) · 18-PLATFORM 5장 |
| v0.3 | SandboxScript — 컴파일러 · 바이트코드 VM · 스크립트 컴포넌트 · 시스템 | 집계 불가 | ✅ 완료 (0.3.0) | [릴리스 노트](../releases/v0.3.0.md) · 18-PLATFORM 6.2 |
| v0.4 | Package Manager — 의존 해석 · 잠금 파일 · 트랜잭션 설치 · 팩 사이 import | 집계 불가 | ✅ 완료 (0.4.0) | [릴리스 노트](../releases/v0.4.0.md) · 18-PLATFORM 6.4 |

<!-- GENERATED:BEGIN — tools/gen_roadmap.py 가 쓴다. 이 구간을 손으로 고치지 않는다 (정본: task-registry · releases) -->

## 2. 버전 표 (v0.5 ~ v1.0 — 태스크 등록부에서 집계)

| 버전 | 핵심 목표 | 성숙 단계 | 완료 | 판정 대기 | 전체 | 진행률 | 설계 | 수동 QA | 상태 |
|---|---|---|---:|---:|---:|---:|---:|---:|---|
| [v0.5](versions/v0.5.md) | Technical Alpha: 엔진 핵심 · CLI 로 만든 게임이 다른 폴더에서 같은 결과 | Technical Alpha | 23 | 0 | 23 | 100 % | 100 % | — | ✅ 완료 (0.5.0) |
| [v0.6](versions/v0.6.md) | 에셋 · 패키지 · SaveGame 기반 |  | 9 | 0 | 9 | 100 % | 100 % | — | ✅ 완료 (0.6.0) |
| [v0.7](versions/v0.7.md) | 게임 UI Runtime · 다국어 기반 |  | 11 | 0 | 11 | 100 % | 100 % | 7 / 19 | ✅ 완료 (0.7.0) |
| [v0.8](versions/v0.8.md) | Studio 기본 환경: 홈 · 빌드 화면 · 복구 · 테스트 실행 |  | 16 | 1 | 17 | 94 % | 100 % | 1 / 13 | 🟡 판정 대기 (태스크 1 개 확인 필요) |
| [v0.9](versions/v0.9.md) | Script IDE |  | 5 | 0 | 5 | 100 % | 100 % | 0 / 5 | 🟡 태스크 끝 · 출시 판정 대기 |
| [v0.10](versions/v0.10.md) | Creator Foundation |  | 6 | 2 | 8 | 75 % | 100 % | 0 / 3 | 🟡 판정 대기 (태스크 2 개 확인 필요) |
| [v0.11](versions/v0.11.md) | Gameplay Runtime |  | 6 | 1 | 7 | 86 % | 100 % | 0 / 2 | 🟡 판정 대기 (태스크 1 개 확인 필요) |
| [v0.12](versions/v0.12.md) | Advanced Creator Tools |  | 7 | 0 | 11 | 64 % | 91 % | — | 🟡 진행 중 |
| [v0.13](versions/v0.13.md) | Closed Creator Alpha | Closed Creator Alpha | 0 | 0 | 6 | 0 % | 100 % | — | ⬜ 미착수 |
| [v0.14](versions/v0.14.md) | 계정 · 레지스트리 · 서명 |  | 0 | 0 | 5 | 0 % | 100 % | — | ⬜ 미착수 |
| [v0.15](versions/v0.15.md) | Public Creator Alpha: 게시 · 설치 · 출시 채널 | Public Creator Alpha | 0 | 0 | 3 | 0 % | 0 % | — | ⬜ 미착수 |
| [v0.16](versions/v0.16.md) | 멀티플레이 · 방 |  | 0 | 0 | 7 | 0 % | 100 % | — | ⬜ 미착수 |
| [v0.17](versions/v0.17.md) | 예측 · Closed Beta | Closed Beta | 0 | 0 | 7 | 0 % | 100 % | — | ⬜ 미착수 |
| [v0.18](versions/v0.18.md) | 호스팅 · NetworkModule · 스트리밍 |  | 0 | 0 | 6 | 0 % | 100 % | — | ⬜ 미착수 |
| [v0.19](versions/v0.19.md) | 결제 (테스트 모드) · 에셋 마켓 |  | 0 | 0 | 2 | 0 % | 50 % | — | ⬜ 미착수 |
| [v0.20](versions/v0.20.md) | 정산 · 분석 · 등급전 |  | 0 | 0 | 4 | 0 % | 100 % | — | ⬜ 미착수 |
| [v0.21](versions/v0.21.md) | 팀 · 협업 · 플러그인 | Public Beta 후보 | 0 | 0 | 6 | 0 % | 83 % | — | ⬜ 미착수 |
| [v0.22](versions/v0.22.md) | Reliability & Performance |  | 0 | 4 | 14 | 0 % | 50 % | — | ⬜ 미착수 |
| [v0.23](versions/v0.23.md) | Security & Release Hardening |  | 0 | 0 | 11 | 0 % | 9 % | — | ⬜ 미착수 |
| [v0.24](versions/v0.24.md) | Release Candidate | Release Candidate | 0 | 0 | 4 | 0 % | 0 % | — | ⬜ 미착수 |
| [v0.25](versions/v0.25.md) | 추가 수정 · GA 후보 재검증 (필요할 때만) |  | 0 | 0 | 0 | 집계 불가 | 집계 불가 | — | ⬜ 미착수 (태스크 없음) |
| [v1.0](versions/v1.0.md) | General Availability | General Availability | 0 | 0 | 1 | 0 % | 0 % | — | ⬜ 미착수 |
| **합계** | | | **83** | 8 | **167** | **50 %** | 83 % | | |

## 3. Closed Creator Alpha (v0.13) 까지 남은 작업

| 버전 | 남은 태스크 | 그중 P0 | 남은 것 |
|---|---:|---:|---|
| [v0.8](versions/v0.8.md) | 1 | 1 | UI-4 |
| [v0.10](versions/v0.10.md) | 2 | 1 | ST-06-4 · NG-18 |
| [v0.11](versions/v0.11.md) | 1 | 0 | NG-17 |
| [v0.12](versions/v0.12.md) | 4 | 1 | ST-06-11 · ST-06-6 · ST-06-10 · NG-20 |
| [v0.13](versions/v0.13.md) | 6 | 5 | M7-T1 · M7-T2 · M7-T3 · M7-T4 · M6-T5 · ST-06-12 |
| **합계** | **14** | | |

## 4. v1.0 (GA) 까지 남은 작업

| 버전 | 남은 태스크 | 그중 P0 | 남은 것 |
|---|---:|---:|---|
| [v0.8](versions/v0.8.md) | 1 | 1 | UI-4 |
| [v0.10](versions/v0.10.md) | 2 | 1 | ST-06-4 · NG-18 |
| [v0.11](versions/v0.11.md) | 1 | 0 | NG-17 |
| [v0.12](versions/v0.12.md) | 4 | 1 | ST-06-11 · ST-06-6 · ST-06-10 · NG-20 |
| [v0.13](versions/v0.13.md) | 6 | 5 | M7-T1 · M7-T2 · M7-T3 · M7-T4 · M6-T5 · ST-06-12 |
| [v0.14](versions/v0.14.md) | 5 | 3 | PS-12-1 · PS-12-2 · PS-12-3 · PS-12-4 · PS-12-5 |
| [v0.15](versions/v0.15.md) | 3 | 3 | PS-13-1 · PS-13-2 · NG-09 |
| [v0.16](versions/v0.16.md) | 7 | 2 | PS-14-1 · PS-14-2 · PS-14-3 · PS-14-4 · PS-14-5 · PS-14-6 · PS-14-7 |
| [v0.17](versions/v0.17.md) | 7 | 2 | RT-07-1 · RT-07-2 · RT-07-3 · RT-07-4 · RT-07-5 · RT-07-6 · RT-07-7 |
| [v0.18](versions/v0.18.md) | 6 | 1 | PS-16-1 · RT-08-1 · RT-08-2 · RT-08-3 · RT-08-4 · RT-08-6 |
| [v0.19](versions/v0.19.md) | 2 | 1 | PS-17-1 · PS-17-2 |
| [v0.20](versions/v0.20.md) | 4 | 1 | PS-18-1 · PS-18-2 · PS-18-3 · PS-18-4 |
| [v0.21](versions/v0.21.md) | 6 | 1 | PS-19-1 · ST-07-1 · ST-07-2 · ST-07-3 · NG-10 · PS-19-2 |
| [v0.22](versions/v0.22.md) | 14 | 5 | RT-09-1 · RT-09-2 · RT-09-3 · RT-09-4 · RT-09-5 · RT-08-5 · PS-20-2 · RH-22-1 · RH-22-2 · RH-22-3 · RH-22-4 · RH-22-5 · RH-22-6 · RH-22-7 |
| [v0.23](versions/v0.23.md) | 11 | 11 | PS-20-1 · PS-20-3 · NG-16 · RH-23-1 · RH-23-2 · RH-23-3 · RH-23-4 · RH-23-5 · RH-23-6 · RH-23-7 · RH-23-8 |
| [v0.24](versions/v0.24.md) | 4 | 3 | RH-24-1 · RH-24-2 · RH-24-3 · RH-24-4 |
| [v1.0](versions/v1.0.md) | 1 | 1 | RH-24-5 |
| **합계** | **84** | | |

<!-- GENERATED:END -->

## 5. 마일스톤 (성숙 단계)

버전 번호와 성숙 단계는 따로다 ([versioning](versioning.md) 2장). 단계는 번호가 아니라 진입 조건을 통과했을 때 붙인다 —
조건의 원문은 [release-gates](release-gates.md) 3장. 일정 (날짜) 은 정해 두지 않았다 — 자료가 없으므로 적지 않는다.

| 마일스톤 | 계획의 첫 버전 | 개발 목표 | 필수 기능 · 선행 | 완료 · 시험 조건 | 상태 |
|---|---|---|---|---|---|
| Technical Alpha | [v0.5](versions/v0.5.md) | 내부 개발자가 CLI 로 만든 게임이 다른 폴더 · 빈 캐시에서 같은 결과 | 결정론 · 패키징 · 충돌 보고 | CTest `tech_alpha_e2e` · [릴리스 노트](../releases/v0.5.0.md) | ✅ 완료 |
| Closed Creator Alpha | [v0.13](versions/v0.13.md) | 초대한 외부 제작자가 Studio 만으로 게임을 만들고 친구가 다른 PC 에서 실행 | v0.8 ~ v0.12 전부 (Studio · IDE · UI 디자이너 · 물리 · Prefab · 타일맵) | [28](../28-CREATOR-UX-GATES.md) G1 ~ G10 · 사용성 시험 ST-06-12 · 차단 목록 M7-T4 | ⬜ 미착수 — 남은 작업은 위 3장 |
| Public Creator Alpha | [v0.15](versions/v0.15.md) | 누구나 제작 · 게시 · 설치 | 계정 · 레지스트리 · 서명 (v0.14) · 저장소 경계 결정 D-06 | 설치 · 롤백 무결성 · 신고 · 검토 | ⬜ 미착수 |
| Closed Beta | [v0.17](versions/v0.17.md) | 인증된 멀티플레이 · 예측 | 보안 전송 (v0.16) · 방 | [24](../24-SECURITY-GATES.md) N2 ~ N4 | ⬜ 미착수 |
| Public Beta 후보 | [v0.21](versions/v0.21.md) | 호스팅 · 마켓 · 팀 협업 | v0.18 ~ v0.20 | 호스팅 남용 방지 · 결제 테스트 모드 · 팀 권한 | ⬜ 미착수 |
| Release Candidate | [v0.24](versions/v0.24.md) | 기능 동결 · 전체 게이트 재검증 | 안정화 v0.22 · v0.23 | release-gates 5장 · [stabilization](stabilization.md) | ⬜ 미착수 |
| General Availability | [v1.0](versions/v1.0.md) | GA 차단 조건 0 · 승인 기록 | RC 통과 | release-gates 6장 | ⬜ 미착수 |

## 6. 집계 기준

| 항목 | 기준 |
|---|---|
| 작업 단위 | 태스크 등록부의 한 행 (예: `RT-06-10`). 한 태스크는 보통 커밋 1 ~ 4 개 ("조각") — 조각은 세지 않는다 |
| 진행률 (구현 · 자동 검증) | 그 버전의 `Done` 태스크 수 / 전체 태스크 수. `Done` 은 코드가 있고 완료 기준의 자동 시험 (또는 기록된 수동 QA) 이 있다는 뜻이다 |
| 판정 대기 | `Review` 태스크 수 — 만든 것의 창 QA 판정 (UI-4) 이나 측정 뒤의 착수 결정 (RT-09-3 ~ 5 · RT-08-5) 을 기다린다. 완료로 세지 않는다 |
| 설계 완료율 | (`Done` + `Review` + `Planned`) / 전체 — 태스크 카드가 있는 것. `Proposed` (감사가 더한 요약 수준) · `Missing` (정의 없음) 은 뺀다 |
| 수동 QA (실기 검증율) | [MANUAL-QA](../qa/MANUAL-QA.md) 의 그 버전 줄 중 `[x]` 수 / 전체. 헤드리스 시험으로 대신하지 않는다 |
| 버전 완료 | 태스크가 모두 `Done` **이고** 릴리스 노트 (`docs/releases/vX.Y.0.md`) 가 있을 때만 ✅. 태스크만 끝난 버전은 🟡 "출시 판정 대기" |
| 집계 불가 | 태스크가 없는 버전 (v0.25 — RC 를 다시 낼 때만) · 등록부 이전 버전 (v0.1 ~ v0.4) |
| 가중치 | 없다 — 태스크 크기가 다르므로 진행률은 "남은 일의 양" 이 아니라 "닫힌 항목의 비율" 이다. 남은 일의 크기는 버전 문서의 미완료 표로 본다 |
