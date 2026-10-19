# 09. Network / Directory / 공개·비밀 방 설계

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 현행과 확장 범위
[현행] ENet UDP, 핸드셰이크, 버전/콘텐츠 해시 검증, 서버 권위 SimCommand, Snapshot/Delta Replication, Interest Management, 재접속 토큰, 플레이어 역할. [신규] 인터넷용 인증·암호화, Matchmaker, Room Directory, 게임/맵 목록, 비밀 코드, Self-Hosting 등록, 패키지 배송.

## 2. 서비스 구분
```text
SandboxClient ── TLS ── Sandbox Directory/Identity/Match API
       │                             │
       │                   Join Ticket (short lived)
       ▼                             ▼
SandboxServer ←── Game UDP (auth + encrypted) ── Client
```
- **Game Directory**: 게임 검색과 버전.
- **Room Directory**: `roomId`, `gameId`, `mapId`, `visibility`, `occupancy`, `serverId`, `region`, `state`.
- **Matchmaker**: 인원/지역/규칙에 맞는 세션 선택.
- **Session Allocator**: 관리형 세션 자원 배정(자체 서버에는 할당하지 않음).
- **Identity / Join Ticket**: 사용자 계정, 접근 권한, 유효기간, 재사용/Replay 방지.

## 3. 방 유형
| 유형 | 등록·검색 | 참가 방식 |
| --- | --- | --- |
| 공개 방 | 검색 가능 | 정책(비밀번호/친구/정원) 검사 후 참가 |
| 비밀 코드 방 | 일반 검색에서 제외 | 코드로 방 조회 + 서버 참가 토큰 검증 |
| 친구 한정 | 권한 있는 사용자만 표시 | 친구 관계 + 초대 검사 |
| Self-hosted 공개 | 호스트 Heartbeat 정상인 경우 노출 | 외부 접속 시험 + 참가 티켓 |
| 상시 서버 | 구독/호스트 상태 따라 노출 | 인증·권한 검증 |

## 4. 보안 전제
- 현재 ENet은 암호화를 제공하지 않으므로 인터넷 출시 전 DTLS 또는 보안 GameNetworkingSockets 등 암호화·인증 계층 필요.
- `--default-role editor` 현행값은 공개 서버 모드에서 `player` 또는 `observer`로 변경하거나 별도 모드 기본값을 둔다.
- 서버 권위 명령·속도 제한·메시지 크기 제한을 유지하고 클라이언트는 검증되지 않은 값 전송 불가.
- Join Ticket에는 방/게임/세션/만료/사용자/nonce/서명 정보를 포함하며 실제 GameServer가 검증.
- Secret room의 코드만으로 영구 인증을 부여하지 않음.

## 5. 메시지 API (TypeScript 계약 예시)
```typescript
interface IClientNetworkAPI {
  sendAction<T>(ref: MessageRef<T>, payload: T): void;
}
interface IServerNetworkAPI {
  sendToPlayer<T>(id: PlayerId, ref: MessageRef<T>, payload: T): void;
  broadcast<T>(ref: MessageRef<T>, payload: T): void;
}
```
- Entity 상태는 기존 Replication이 담당. Custom Action/Event만 Message Schema + 권한 기반 API로 제공.
- Message ID/버전·최대 크기·빈도·신뢰도·도메인 검증 필요.
- 외부 HTTP API는 별도 서버 전용 서비스, Simulation Tick의 직접 I/O 금지.

## 6. 연결 상태 기계
```text
Discover → Check Package → Acquire Join Ticket → Resolve Endpoint
→ Auth/Handshake → Sync Baseline → Ready → Play → Reconnect/Leave
```
패키지 불일치, 방 만료, 서버 도달 불가, 정원 초과, 세션 토큰 만료 각각 오류 표시.

## 7. 완료 기준
1. 공개/비밀 방 생성·검색·접속과 권한 거부.
2. 서버 버전·GamePackage 해시 불일치 거부 후 안전한 배포 경로.
3. 재접속 및 방 파기 시 등록 정보 정리.
4. 조작 패킷, replayed token, 과도한 메시지에 안전한 거부.
5. 현재 세션은 Directory 일시 장애 시 정책에 따라 지속.
