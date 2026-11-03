# ADR-0100. 출시 강화 · RC · GA 승인 — 안정화 두 버전, Soak · Sanitizer · 퍼저 · 공급망 · 복구 · 실기 검증, RC 동결, 기록 기반 GA 승인

- 상태: **Accepted** (계획 — 구현은 v0.22 ~ v0.24 태스크) · 날짜: 2026-11-03
- 관련: [roadmap/stabilization](../roadmap/stabilization.md), [roadmap/release-gates](../roadmap/release-gates.md) 4 ~ 6장, [ADR-0099](0099-roadmap-v010-split-stabilization-versions.md),
  [24-SECURITY-GATES](../24-SECURITY-GATES.md), [14-PERFORMANCE](../14-PERFORMANCE.md), [platform/architecture/11](../platform/architecture/11_SECURITY_AND_TRUST.md)

## 맥락

GA 기준 (release-gates — 1차) 은 "외부 감사 · 실기 · 서명 업데이트 · 성능 회귀 CI" 를 목록으로만 가졌고, 그것을 만드는 태스크 · 방법 · 기록
형식이 없었다. 지금 CI 는 ASan + UBSan 한 잡 · 세 OS 헤드리스 시험이고, TSan 프리셋은 CI 에 없으며, 커버리지 퍼저 · 장시간 Soak · SBOM ·
Actions SHA 고정 · 서명 · 실기 QA 기록 (v0.1 부터 남음) 이 없다 (stabilization 0장 — 이 커밋의 소스 · 워크플로를 읽어 확인).

## 결정

```text
1. 안정화를 두 버전으로: v0.22 Reliability & Performance (Soak · Sanitizer · 누수 · 종료 경합 · 성능 기준선 · 회귀 CI · 교차 툴체인 결정론),
   v0.23 Security & Release Hardening (퍼저 · 샌드박스 탈출 · 네트워크 · 계정 보안 · 공급망 · 서명 업데이트 · 복구 훈련 · 운영 · 개인정보 · 실기)
2. ASan 과 TSan 은 따로 빌드 · 따로 CI 잡 (한 빌드에 섞을 수 없다). 억제 목록은 줄마다 이유
3. Soak 는 짧은 판 (CI nightly) 과 긴 판 (RC — 24 ~ 72 시간 [추정]) 둘. 시간 · 문턱은 측정 뒤 RH-24-1 · RH-22-1 이 정한다 (지금 숫자를 정하지 않는다)
4. 성능 회귀 CI 는 같은 러너에서 기준선 대비 % 로만 판정 — CI 러너를 기준 하드웨어로 쓰지 않는다. 절대 예산은 기준 하드웨어 측정으로
5. 병렬화 Review 태스크 (RT-09-3 ~ 5 · RT-08-5) 는 측정이 예산 초과를 보일 때만 — 아니면 v1.x (GA 를 막지 않는다)
6. RC = 기능 · 호환 번호 동결 브랜치, 들어가는 것은 차단 버그 고침뿐. 실패하면 다음 0.x RC (v0.25 …)
7. GA 승인 = 차단 조건 표 (release-gates 6.1) 의 항목마다 근거 · 판정 · 판정자 · 날짜를 적은 기록. 기록이 없는 항목은 통과가 아니다.
   승인한 RC 빌드의 바이트에 v1.0.0 을 붙인다 (다시 빌드하지 않는다)
8. 정의되지 않은 운영 · 개인정보 일은 Missing 상태로 등록부에 두고, Missing 인 채로는 GA 를 막는다
```

## 선택지

| 안 | 내용 | 판단 |
|----|------|------|
| A (채택) | 버전 둘 + RC + 기록 기반 승인 | 무엇을 검증했는지가 기록으로 남는다 — "통과했다" 를 다시 확인할 수 있다 |
| B | 각 기능 버전에 안정화를 흩어 넣음 | 장시간 · 교차 툴체인 · 감사는 기능이 모인 뒤에만 의미가 있다 |
| C | GA 직전 한 번에 | 문제를 늦게 찾아 GA 가 날짜에 밀려 기준을 낮추게 된다 |

## 결과

- 등록부에 RH-22-1 ~ 7 · RH-23-1 ~ 8 · RH-24-1 ~ 5 를 더한다 (ADR-0099). CI 바꿈 (TSan 잡 · 퍼저 · nightly Soak · Actions SHA 고정) 은
  그 태스크 커밋에서 — 이 ADR 은 워크플로를 바꾸지 않는다.
- 14 의 예산 표 · 13 의 CI 매트릭스 · 24 의 계정 행은 각 태스크에서 갱신한다.
