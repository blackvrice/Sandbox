# 16. 핵심 데이터·API 계약 초안

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 공통 ID 정책
`gameId`, `packageName`, `componentStableId`는 사람이 읽을 수 있는 namespace 기반 안정 ID. `roomId`, `sessionId`, `serverId`, `orderId`, `userId`는 서비스가 발급하는 불투명 식별자. 단기 Token은 ID와 별개이다.

## 2. 주요 도메인 객체 예시
```json
{
  "game": {"id": "studio.rts", "version": "0.5.0", "digest": "sha256:..."},
  "room": {"roomId": "rm_001", "visibility": "public", "state": "running", "maxPlayers": 8},
  "server": {"serverId": "srv_001", "kind": "self-host", "health": "online"}
}
```
게임 / 서버 / 방 메타데이터의 공개 범위를 분리하고, 내부 IP·Secret·결제 정보는 반환하지 않는다.

## 3. Hub API (초기 제안)
| Verb/Path | 기능 | 인증 |
| --- | --- | --- |
| `GET /v1/games` | 공개 게임 탐색 | 공개 |
| `POST /v1/projects` | 프로젝트 생성 | 사용자 |
| `GET /v1/packages/{name}/versions` | 패키지 버전 조회 | 공개/라이선스 |
| `POST /v1/packages/publish` | 패키지 제출 | 제작자 |
| `POST /v1/builds` | 검증 빌드 실행 | Project 권한 |
| `POST /v1/servers/enroll` | 자체 서버 등록 | Server Owner |
| `POST /v1/rooms` | 방 생성 요청 | Player/Owner |
| `POST /v1/rooms/{roomId}/join` | Join Ticket 발급 | Player |
| `POST /v1/orders/checkout` | 결제 생성 | 구매자 |
| `POST /v1/webhooks/payments` | PSP 웹훅 | 서명 검증 |

## 4. Host API Contract
```text
component.read(entityHandle, componentHandle) -> readonly snapshot | null
component.has(entityHandle, componentHandle) -> bool
component.query(componentHandle, queryBudget) -> iterator/handle
command.setField(...) -> queued validated command/result
command.spawn(...) -> queued command
network.broadcast(messageId, typed payload) -> validated event
```
Host ABI는 명시적 domain/capability/fuel limits와 오류 코드를 가져야 한다. 실제 C++ API는 추후 구현에 맞춰 세부 명세를 동결한다.

## 5. 데이터 소유자
- GameProject 원본/Repo: Git 백엔드 또는 Hub(선택에 따라 단일 권위).
- GamePackage/PackageVersion: Registry/Object Storage.
- Room/Join Ticket: Directory/Identity Service.
- World Runtime/Save: 현재 GameServer(자체 호스트는 개발자 서버, 공식 서버는 호스팅 저장소).
- Order/Entitlement: Billing/Commerce Service.

## 6. 오류와 멱등성
모든 쓰기 API는 가능한 경우 Idempotency-Key 사용. 실패 응답은 `code`, `message`, `requestId`, `retryable` 포함. 방 배정과 결제/정산은 중복 요청 방지. Heartbeat·게임 서버 등록에는 만료와 자격 증명 회전 정책 필요.

## 7. 이 문서의 위치
SDK 및 REST 계약은 아직 동결된 사양이 아니다. v0.2~0.4에서 실제 타입/IDL 기반 CodeGen으로 좁히고, v0.6부터 OpenAPI·Proto/Schema 문서와 자동 계약 테스트를 추가한다.
