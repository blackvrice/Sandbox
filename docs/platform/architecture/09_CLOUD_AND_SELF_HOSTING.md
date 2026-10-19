# 10. Sandbox Cloud / Self-Hosting 설계

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 운영 모델
- **Self-Hosting**: 개발자가 PC/VPS/기존 클라우드에서 SandboxServer 실행. Directory 등록 무료, 트래픽·운영비 개발자 부담.
- **Free Session Hosting**: 플랫폼이 유휴 세션/할당 가능한 작업자 자원에서 짧은 세션 제공. 시간/동시 접속/월 사용량 한도.
- **Paid Persistent Hosting**: 월 구독으로 24시간 인스턴스/월드 실행, 디스크·백업·모니터링·가용성 정책.
- **GCP/AWS 사용**: 초기에는 기존 인프라로 호스팅하고, 대규모 시 컨테이너/Kubernetes/Agones 등의 오케스트레이션 채택 검토. 범용 클라우드 직접 구축은 범위 밖.

## 2. Self-Hosting 등록 프로토콜
```text
Developer → Hub: Create Server Registration
Hub → Developer: serverId + one-time enrollment token
SandboxServer → Hub TLS: exchange token for rotating credentials
Server → Directory: Heartbeat (gameId, map, version, region, players)
Directory → Client: available endpoint + short-lived join ticket
Client → SandboxServer: authenticated encrypted UDP
```
- Heartbeat에는 서버 운영자가 주장한 값과 플랫폼이 검증한 값 구분.
- 외부 포트 접근 검증: direct UDP connect 가능 여부. CGNAT일 경우 Relay/다른 네트워크가 필요할 수 있음.
- 주소·서버 토큰·운영자 개인정보는 공개 목록에 그대로 노출하지 않음. Direct 접속 자체는 공인 IP 노출 가능성을 고지.

## 3. 실행 명령 (현행 검증 예시)
```bash
SandboxServer --world ecosystem_survival --port 7777 --max-clients 16 --default-role player --saves saves
SandboxClient --connect 127.0.0.1:7777
```
이 명령들은 현재 로컬/LAN 개발 서버용이며 공개 인터넷 보안이 완성되기 전에는 인터넷 노출 금지. 향후 `--game package.sbpkg --directory-register` 등의 신규 옵션 추가 검토.

## 4. Management API 및 Agent
- `ServerRegistration`: developerId, gameId, serverId, public/private, availability.
- `Session`: sessionId, lease, state, hostNode, packageDigest.
- `Allocation`: region, CPU/RAM quota, maxPlayers, idleTimeout.
- `HostAgent`: launch, restart, image pull, logs, metrics, graceful shutdown.
- `PersistentWorld`: volume, snapshot, backup retention, import/export.

## 5. 서버 생명주기
```text
Registered → Provisioning → Starting → Healthcheck → Online
→ Draining → Saving → Stopped/Restarting → Terminated
```
- 세션 서버: 마지막 접속자 퇴장 후 짧은 유휴 시간 경과 시 종료.
- 상시 서버: 이용자 0명이어도 계약한 정책에 따라 실행, 장애 발생 시 재시작·백업 복원.
- World Save는 원자적 작성, 서버 종료 전에 snapshot + 충분한 flush.
- 런타임 계측: Tick p95/p99, CPU, RAM, Snapshot bitrate, 접속 실패, 패키지 로딩 시간.

## 6. 보안과 과금
- 공식 호스팅은 컨테이너/프로세스 격리, cgroup/job object 자원 제한, Native 코드 실행 정책, 네트워크 egress 제약 필요.
- 무료 세션은 사용자·개발자·IP·Game별 할당 상한 및 악용 탐지.
- 월 구독은 예약 CPU/RAM/디스크/지역/백업 정책과 실제 운영원가에 기반해 책정.
- 구독 종료 시 데이터 보존 기간, 내보내기, 환불, 서버 중단 시점을 명문화.
- Relay 트래픽은 자기 비용이 발생하므로 별도 무료 한도 및 유료 확대 모델 가능.

## 7. 출시 기준
온라인 서버 보안, 방 등록 정합성, 장애 재시작, 복구, 자원 강제 제한, 결제 상태 변경 처리, 24h 지속 운전 시험 통과. 공개 SLO는 실제 운영 지표 확보 후 단계적으로 고지.
