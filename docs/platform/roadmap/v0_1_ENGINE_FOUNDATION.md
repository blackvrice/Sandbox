# v0.1 — Engine Foundation

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 목표와 범위
**핵심 목표:** 기반 기능 안정화.

코어 ECS·시뮬레이션·네트워크·에디터·렌더러의 기존 기능을 공식 기준선으로 정리한다.

**버전 정책:** **[현행]** Sparse-Set ECS, C++ Reflection, 30 TPS Simulation/WorldHash, ENet ServerHost, Delta Replication, Interest Management, Multiplayer Editor(Phase 12), DX12, 기초 Vulkan 구현 확인.
**[계획/유의]** macOS Metal, Linux X11/Wayland 일부, 인터넷 보안, Script/Hub/Marketplace/Cloud는 아직 포함하지 않는다.

## 2. 진입 조건
현행 Phase 12 기반 기능 확인, 기존 시스템 회귀 테스트

## 3. 구현 요구사항

1. README·ADR·기존 설계 문서에 새 제품 비전과 초기 비목표의 충돌을 기록한다.

2. CMake 링크 경계, Server Headless, Renderer와 Core 분리를 보존한다.

3. ECS/Replay/Network/Editor의 CTest 및 성능 기준선을 수집한다.

4. 현행 LAN/신뢰 환경 제약을 제품 UI·문서에 명확히 노출한다.

## 4. 아키텍처/데이터 영향

- 모듈 사이 역할·권한·데이터 소유자를 문서화하며, 기존 서버 권위 ECS·결정론·비동기 경계 원칙을 우선한다.

- Manifest·Script SDK·Network·Save의 변경사항은 호환 버전/Schema Migration 계약으로 관리한다.

## 5. 자동화 완료 기준

- [ ] 서버 `--world` 구동 및 클라이언트 LAN 접속·편집·저장·복원 검증

- [ ] Replay/WorldHash 및 네트워크 수렴 테스트 회귀 없음

- [ ] 신규 범위 변경 ADR 승인 및 다음 버전의 API 초안 작성

## 6. 데모 및 검증 시나리오

최소 1개의 재현 가능한 데모 프로젝트와 CI/CTest 실행 명령, 실패 시 기대 오류를 릴리스 문서에 포함한다. Engine Foundation 기능을 외부 사용자가 처음부터 재현할 수 있어야 한다.

## 7. 위험·보류 항목

- 현행 README 및 `docs/00-OVERVIEW.md`와 신제품 범위 불일치

- Vulkan 이후 Linux/macOS 백엔드 개발과 기능 로드맵 충돌

## 8. 관련 상세 명세

- `architecture/01_SYSTEM_ARCHITECTURE.md`

- `architecture/13_TESTING_OPERATIONS.md`

## 9. Definition of Done

- 핵심 E2E 성공, 크래시/취약점/데이터 손상 문제의 출시 차단 기준 통과.

- 새 API 및 사용자 흐름 문서화, 마이그레이션/롤백 검증 완료.

- 미구현 항목을 릴리스 노트에 명시하고 다음 버전으로 재할당.
