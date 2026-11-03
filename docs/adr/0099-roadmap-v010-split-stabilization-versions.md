# ADR-0099. 로드맵 2차 배치 — v0.10 을 셋으로 나눔 (Creator Foundation · Gameplay Runtime · Advanced Creator Tools), Closed Creator Alpha = v0.13, 안정화 v0.22 · v0.23, RC v0.24

- 상태: **Accepted** · 날짜: 2026-11-03
- 관련: [ADR-0096](0096-roadmap-versioning-small-milestones.md) (버전 체계 — 이 ADR 이 v0.10 이후 배치를 대신한다), [ADR-0100](0100-release-hardening-rc-ga-approval.md)
  (안정화 · RC · GA 승인), [roadmap/releases](../roadmap/releases.md) · [roadmap/task-registry](../roadmap/task-registry.md) · [roadmap/versioning](../roadmap/versioning.md) 4.1

## 맥락

ADR-0096 의 1차 배치는 v0.10 한 버전에 22 태스크 (트랙 B 콘텐츠 도구 6 · Runtime v2 실행 14 · 타일맵 · 오디오 출력) 를 두었다. ADR-0096 자신의
기준 ("MINOR 하나 = 목표 하나 · P0 대략 10 개 이하") 을 어긴다 — 사용자 목표가 셋 (만들기 도구 · 물리 · 충돌 · 렌더 실행 · 고급 2D 도구) 이다.
또 v0.20 하나에 성능 회귀 · 권장 사양 · 서명 업데이트 · 운영 콘솔 · 보안 감사 · 1.0 전환이 섞여 있고, 장시간 안정성 · Sanitizer · 퍼저 ·
공급망 · 복구 · 실기 · RC 의 검증 태스크는 아예 없었다 (stabilization 0장 — CI 에 TSan · 퍼저 · Soak · SBOM 없음).

## 결정

```text
1. v0.10 Creator Foundation (ST-06-3 · ST-06-4 · M6-T2 · RT-06-1 · RT-06-7 · RT-06-8 · RT-06-10 + 통합 시험 NG-18)
   v0.11 Gameplay Runtime (RT-06-2 · RT-06-3 · RT-06-4 · RT-06-5 · RT-06-9 · NG-17 + NG-19)
   v0.12 Advanced Creator Tools (ST-06-5 · ST-06-6 · ST-06-10 · NG-15 · RT-06-6 · RT-06-11 · RT-06-13 · RT-06-14 · RT-06-15 + NG-20)
   — 22 태스크를 7 · 6 · 9 로. 버전마다 통합 시험 태스크 하나 (NG-18 ~ 20)
2. Closed Creator Alpha 는 v0.13 (태스크 그대로 — M7-T1 ~ T4 · M6-T5 · ST-06-12). 이 버전은 새 핵심 기능을 만들지 않는다 (통합 · 사용성 · 복구)
3. 뒤 버전: v0.14 계정 · 레지스트리 · v0.15 게시 · 설치 (Public Creator Alpha) · v0.16 방 · v0.17 예측 (Closed Beta) · v0.18 호스팅 · v0.19 결제 ·
   v0.20 정산 · 분석 · v0.21 팀 · 협업 · 플러그인 — 1차의 v0.12 ~ v0.19 를 순서 그대로 옮김 (태스크 구성 그대로)
4. 1차 v0.20 (안정화) 은 성격대로 v0.22 Reliability & Performance (RT-09-1 · RT-09-2 · PS-20-2 · Review 넷) 와 v0.23 Security & Release
   Hardening (PS-20-1 · PS-20-3 · NG-16) 로 나누고, 빠졌던 검증 태스크 RH-22-1 ~ 7 · RH-23-1 ~ 8 을 더한다. RC 는 v0.24 (RH-24-1 ~ 4),
   GA 승인은 RH-24-5 (목표 v1.0). v0.25+ 는 RC 를 다시 낼 때만
5. 우선순위: v0.12 통합 시험이 쓰는 NG-15 Tilemap · RT-06-14 애니메이션을 P2 → P1. v0.12 의 P2 여섯은 시작 때 필요성을 판정해 미루면 목표를 고친다
6. 새 ID 접두어 RH-NN-n (Release Hardening). 상태 Missing (필요하지만 정의되지 않음 — RH-23-6 런북 · RH-23-8 개인정보) 를 등록부에 더한다
7. 버전마다 태스크는 한 곳에만 — releases 의 "태스크" 줄이 배정이고, 검사기가 등록부 목표 · 선행 순서와 맞춘다 (check_docs R · D)
```

## 선택지

| 안 | 내용 | 판단 |
|----|------|------|
| A (채택) | 성격대로 나누고 뒤 버전을 다시 매김 | 버전마다 목표 하나 · 통합 시험 하나 |
| B | v0.10 을 그대로 두고 0.10.1 · 0.10.2 로 | PATCH 는 고침 전용 (ADR-0096 C) |
| C | 1차 번호에 일괄 +2 | v0.20 (안정화) 이 v0.22 하나로 가고 v0.23 · v0.24 의 자리가 섞인다 — 성격별로 나눌 수 없다 |
| D | 안정화 · RC 를 버전 없이 "GA 전 작업" 으로 | 기준 · 기록 · 버전 피드에 보이지 않는다 — 출시 판단이 사람 기억에 기댄다 |

## 결과

- ADR-0096 의 결정 1 ~ 3 · 5 ~ 8 은 그대로, **4 의 버전 배치 (v0.11 Closed Creator Alpha 등) 와 결과의 버전 표기** 를 이 ADR 이 대신한다 (일부 Superseded).
  ADR-0098 의 버전 표기 (Player 별도 실행 파일 v0.13 · 채널 v0.13 · 플러그인 v0.19) 는 versioning 4.1 표로 읽는다 (v0.15 · v0.15 · v0.21).
- 1차 배치로 쓴 감사 문서의 v0.10 이후 표기는 고쳐 쓰지 않고 versioning 4.1 로 읽는다 (감사는 그날의 기록).
- 성숙 단계는 기준으로 붙인다 (Closed Creator Alpha 첫 계획 v0.13 · Public v0.15 · Closed Beta v0.17 · Public Beta 후보 v0.21).
