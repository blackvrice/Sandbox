# ADR-0010. Transport 1차 구현은 ENet

- 상태: **Accepted** · 날짜: 2026-10-05
- 관련: [08-NETWORK](../08-NETWORK.md) 2장

## 맥락

요구사항: Simulation과 Socket을 분리하고(`INetworkTransport`), 신뢰성 UDP를 직접 만들지 않는다.
후보: ENet, standalone Asio, SteamNetworkingSockets / GameNetworkingSockets(Valve, 2026년 v1.6.x 릴리스로 유지보수 재개).

## 결정

`INetworkTransport` 뒤에 1차로 `EnetTransport`, 함께 `LoopbackTransport`·`SimulatedTransport`를 둡니다.
GameNetworkingSockets는 암호화·NAT·릴레이가 필요해질 때 2차 구현으로 추가합니다.

## 근거

```text
- ENet: C, 의존성 0, 신뢰/비신뢰(순차) 채널·조각화 내장, 세 플랫폼 빌드가 쉽다. 수십 년 사용.
- GNS: 암호화·혼잡 제어·릴레이가 있지만 protobuf + OpenSSL/libsodium 의존으로 빌드가 무겁다.
- Asio: 범용 IO. 신뢰성 계층을 직접 만들어야 하므로 요구사항 위반.
```

## 결과

- 얻는 것: 빠른 Phase 9 진입, 단순한 의존성.
- 포기하는 것: 암호화(ENet 없음), NAT 통과.
- 위험: 공개 서버 운영 불가 → [16-ROADMAP](../16-ROADMAP.md) R6. LAN·신뢰 환경 한정을 문서·UI에 명시.

## 대안

| 대안                               | 기각/보류 사유              |
|------------------------------------|-----------------------------|
| GameNetworkingSockets              | 보류 (후속 2차 구현)        |
| Asio + 자체 RUDP                   | 요구사항 위반               |
| SteamNetworkingSockets(Steamworks) | Steam 종속, GNS 경로로 흡수 |

## 재검토 조건

공개 인터넷 서버 운영 또는 Steam 연동이 범위에 들어올 때.
