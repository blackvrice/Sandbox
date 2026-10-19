# v0.7 — Multiplayer Platform

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 목표와 범위
**핵심 목표:** 서버 검색과 Self-host/무료 세션 통합.

게임 제작자가 독립 서버를 등록하고 플레이어가 공개·비밀 방으로 안전하게 접속할 수 있도록 만든다.

**버전 정책:** 인터넷 호스팅에서는 기존 ENet 평문 운용을 종료하고 암호화·인증된 전송을 도입한다.

## 2. 진입 조건
이전 버전의 모든 필수 완료 기준 충족 및 릴리스 후보 통합 테스트

## 3. 구현 요구사항

1. Directory, Room, Matchmaker, SessionAllocator, Identity/JoinTicket

2. Self-host Enrollment/Heartbeat/Healthcheck/Ownership

3. 공개 방, 비밀 코드 방, 친구 한정 접근과 서버 선택

4. 안전한 GamePackage 다운로드·버전 동기화·콘텐츠 호환

5. 무료 세션 서버 할당/유휴 종료/할당량/Abuse 제한

6. UDP 보안 계층, 연결 실패/재접속, NAT/Relay 우선순위 정의

7. Directory 장애에도 기존 GameSession 보존·정리 정책

## 4. 아키텍처/데이터 영향

- 모듈 사이 역할·권한·데이터 소유자를 문서화하며, 기존 서버 권위 ECS·결정론·비동기 경계 원칙을 우선한다.

- Manifest·Script SDK·Network·Save의 변경사항은 호환 버전/Schema Migration 계약으로 관리한다.

## 5. 자동화 완료 기준

- [ ] PC/VPS 서버가 Directory에 등록되고 타 PC에서 접속

- [ ] 공개·비공개 방 및 위조 티켓·만료 코드 거절

- [ ] 무료 세션 생성/유휴 종료/재할당, 자원 상한 테스트

- [ ] 암호화·패킷 검증·부하·장애 복구 게이트 통과

## 6. 데모 및 검증 시나리오

최소 1개의 재현 가능한 데모 프로젝트와 CI/CTest 실행 명령, 실패 시 기대 오류를 릴리스 문서에 포함한다. Multiplayer Platform 기능을 외부 사용자가 처음부터 재현할 수 있어야 한다.

## 7. 위험·보류 항목

- 무상 세션 호스팅 운영비와 악용 리스크

- CGNAT / Relay 도입 비용과 보안 전송 선택

## 8. 관련 상세 명세

- `architecture/08_NETWORK_AND_ROOMS.md`

- `architecture/09_CLOUD_AND_SELF_HOSTING.md`

## 9. Definition of Done

- 핵심 E2E 성공, 크래시/취약점/데이터 손상 문제의 출시 차단 기준 통과.

- 새 API 및 사용자 흐름 문서화, 마이그레이션/롤백 검증 완료.

- 미구현 항목을 릴리스 노트에 명시하고 다음 버전으로 재할당.
