# ADR-0088. v0.6 ~ v1.0 배치 갱신 — Runtime v2 항목 배치 · 등급전 · 순위표를 v0.8 로 (ADR-0082 3 의 일부를 대신)

- 상태: **Accepted** · 날짜: 2026-11-02
- 관련: [ADR-0082](0082-v06-v10-feature-placement.md) (이 ADR 이 3 의 순위표 · 등급전 배치를 대신 — 나머지는 그대로),
  [ADR-0086](0086-runtime-architecture-v2.md), [ADR-0087](0087-client-fixed-tick-prediction-opt-in.md), [23-ROADMAP](../23-ROADMAP.md),
  [21-PLATFORM-FEATURES](../21-PLATFORM-FEATURES.md) 4장, [24-SECURITY-GATES](../24-SECURITY-GATES.md)

## 맥락

ADR-0082 는 v0.7 에 보안 전송 · 방 · 플레이어 데이터 · 친구 · 파티 · 등급전 · 순위표를 두고, 위험으로 "v0.7 이 무거워진다 — 넘치면
등급전을 v0.8 로" 를 적었다. Runtime v2 (ADR-0086 · 0087) 가 v0.7 에 Client 고정 Tick · 입력 프레임 · 예측 · 맞춤을 더하면서 그 위험이
현실이 됐다. 또 Runtime v2 의 항목 (물리 이벤트 · 콜백 · NetworkModule · 병렬화) 을 버전에 배치해야 한다.

## 결정

```text
1. 등급전 · 레이팅 · 순위표 (21 2.8 · 2.11 의 v0.7 몫) 를 v0.8 로 옮긴다 — 업적 · 통계 API 와 같이. v0.7 은 "안전한 온라인 방 + 예측"
   에 집중. 공식 세션 결과 기록 (등급전의 바탕) 은 v0.7 에 남긴다 (방 결과를 서버가 남기는 일 — 데이터만)
2. Runtime v2 배치 (상세 · 우선순위는 23):
   v0.5  계약만 — 콜백 이름 예약 · Host capability 열 · network.model 자리 (M2-T7, P0)
   v0.6  server onPrePhysics (8b) · PhysicsCommand · collider layer · mask 거르기 · 충돌 · 트리거 Enter/Exit 이벤트 · ShapeCast · client
         수명주기 · onAction · 입력 Context · onLateUpdate · onRender (Draw) · 타이머 · defineState · AccessSet 선언 · 단계 프로파일 ·
         비동기 에셋 로드
   v0.7  Client 고정 Tick · InputFrame · PredictedWorld · 맞춤 · 예측 계측 · 네트워크 처리량 프로파일
   v0.8  NetworkModule (서버만 — HTTPS · WebSocket · egress 허락 목록) · Substep · CCD · 재질 · 에셋 스트리밍 · 메모리 예산 · Render 스레드
         검토 · 등급전 · 순위표 (1)
   v0.9  ECS 단계 안 병렬 (검토 — 측정 조건) · 스크립트 Worker (검토) · 잠 (sleep, 검토) · 성능 회귀 CI · 권장 사양 자동 분석
   v1.0  콜백 · Capability · Context 동결 (게임플레이 API 동결과 같이) · 플랫폼 매트릭스 판정
   v1.x  3D · 외부 물리 엔진 (Jolt 등) · JIT/AOT (ADR-0081) · 원격 개체 외삽
3. 버전의 핵심 목표는 ADR-0082 그대로 (v0.6 Hub · v0.7 안전한 온라인 · v0.8 마켓 · 호스팅 · v0.9 운영 · v1.0 GA). Runtime v2 항목이
   그 목표를 밀어내면 P2 · P3 부터 다음 버전으로 (23 의 미룰 순서)
4. 버전마다 보안 · 안정성 출시 기준은 24 의 표 — 릴리스 차단 항목이 하나라도 남으면 그 버전을 내지 않는다
```

## 선택지

| 안 | 내용 | 판단 |
|----|------|------|
| A (채택) | 등급전 · 순위표 v0.8, 예측 v0.7 | v0.7 의 보안 전송 + 방 + 예측이 한 버전에 들어간다 — 등급전은 방 결과 기록이 쌓인 뒤가 자연스럽다 |
| B | 예측을 v0.8 로 | 액션 게임이 v0.7 의 방에서 지연을 그대로 겪는다 — 멀티플레이 플랫폼 목표와 어긋난다 |
| C | 둘 다 v0.7 | ADR-0082 가 적은 과적 위험 그대로 |

## 결과

- 21 4장 표의 8 · 11 행 (v0.7 → v0.8), 18 6장 요약, 23 이 이 배치를 따른다. ADR-0082 의 상태 줄에 "일부 Superseded (3 의 등급전 ·
  순위표 배치) by ADR-0088" 을 적는다 (본문은 바꾸지 않는다).
