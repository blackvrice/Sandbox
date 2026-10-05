# ADR-0003. Server Authoritative + Delta Snapshot, 싱글플레이도 Loopback

- 상태: **Accepted** · 날짜: 2026-10-05
- 관련: [08-NETWORK](../08-NETWORK.md), [01-ARCHITECTURE](../01-ARCHITECTURE.md)

## 맥락

RTS는 lockstep을 미래 방향으로 두고 결정론을 그 전제로 삼았습니다. 이 프로젝트는 대형 월드(50k),
여러 사용자의 동시 편집, 중도 참여(Late Join), 헤드리스 전용 서버가 필요합니다.

## 결정

```text
1. 서버만 진짜 SimulationWorld 를 가진다. 클라이언트는 SimCommand 를 보내고 Delta Snapshot 을 받는다.
2. Lockstep 은 기본 방식으로 쓰지 않는다.
3. 싱글플레이도 같은 프로세스 안에 ServerHost 를 띄우고 LoopbackTransport 로 연결한다.
4. 클라이언트 게임플레이 예측은 하지 않는다. 에디터 EditPreview 만 예외.
```

## 근거

```text
- Lockstep 은 모든 클라가 전체 월드를 계산해야 하고, Late Join 에 전체 상태 전송이 필요하며,
  가장 느린 클라에 맞춰진다. 5만 엔티티 + 편집 환경에서 성립하지 않는다.
- 서버 권한이면 권한·검증이 한 곳(CommandValidator)에 모인다.
- 싱글/멀티 코드 경로를 하나로 만들면 "싱글은 되는데 멀티에서 깨진다"가 구조적으로 사라진다.
```

## 결과

- 얻는 것: Interest로 대역폭 제어, Late Join, 단일 코드 경로, 헤드리스 서버.
- 포기하는 것: 클라이언트 즉시 반응(최대 ~150 ms 지연), 서버 CPU 비용 집중.
- 위험: 대역폭 → 양자화·Interest·예산. 체감 지연 → EditPreview.

## 대안

| 대안 | 기각 사유 |
|---|---|
| Deterministic Lockstep | 위 근거 |
| 클라이언트 예측 + 롤백 | RTS식 간접 조작·편집에서 이득 작고 복잡도 큼 |
| 싱글플레이 직접 경로 | 코드 경로 이원화 |

## 재검토 조건

직접 조작(캐릭터 조종) 콘텐츠가 핵심 요구가 될 때 — 해당 엔티티 한정 예측을 ADR로.
