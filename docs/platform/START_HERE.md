# Sandbox Platform 설계 문서 인덱스

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 문서 집합 목적
이 자료는 2026-10-13 기준 Sandbox C++23 저장소 상태와 지금까지 논의된 **UGC Game Platform + GitHub형 Developer Hub + Package Manager + Cloud + Marketplace** 제품 비전을 통합한 *신규 아키텍처 설계 초안*이다. 프로젝트 원본 소스 또는 기존 규범 문서를 수정하지 않고 독립 문서로 만든다.

## 먼저 읽을 순서
1. `planning/01_VISION_AND_SCOPE.md` — 제품 목적, 고객, 비목표.
2. `planning/04_RELEASE_MATRIX.md` — v0.1~1.0 지도와 결정할 문제.
3. `roadmap/` — 각 버전별 요구사항과 완료 조건.
4. `architecture/` — Component, Script VM, Package, Hub, Network, Cloud, Commerce 상세 계약.

## 현행 구현 확인 경계
- **확인된 범위**: GitHub `blackvrice/Sandbox`, `README.md`, `apps/server/main.cpp`, `apps/server/ServerOptions.*`, `core/ecs/ComponentCatalog.hpp`, `core/ecs/Reflection.hpp`, `core/ecs/Registry.hpp`, `core/simulation/SystemScheduler.hpp`, `docs/00`, `01`, `02`, `08`, `10`, `11`, `15`, `16` (2026-10-13 조회).
- **설계 중인 기능**: Custom TypeScript Compiler/VM, GamePackage UGC, 동적 Component, Package Manager, Hub, Directory/Room, 유료 호스팅, Marketplace, Business 구독.
- **주의**: 예시 API/Manifest/명령어는 표준으로 확정된 구현이 아니라 제안이다. 이전 문서의 초기 범위와 충돌하므로 신규 제품 범위 ADR을 먼저 기록한다.

## 공통 운영 원칙
- 출시 버전과 Script API, Bytecode, Network Protocol, Simulation, Save Schema 버전은 분리.
- 모든 버전은 핵심 E2E, 단위/보안/결정론/호환성 테스트를 통과해야 종료.
- 무료 개발 및 Self-hosting과 유료 상시 서버/거래/Business 구독을 병행.
- Native 성능과 검증된 UGC Script/Package 격리의 경계를 유지.

## 전체 파일 목록

### planning
- [planning/01_VISION_AND_SCOPE.md](planning/01_VISION_AND_SCOPE.md)
- [planning/02_BUSINESS_MODEL.md](planning/02_BUSINESS_MODEL.md)
- [planning/03_API_DATA_CONTRACTS.md](planning/03_API_DATA_CONTRACTS.md)
- [planning/04_RELEASE_MATRIX.md](planning/04_RELEASE_MATRIX.md)

### architecture
- [architecture/01_SYSTEM_ARCHITECTURE.md](architecture/01_SYSTEM_ARCHITECTURE.md)
- [architecture/02_GAME_PACKAGE_FORMAT.md](architecture/02_GAME_PACKAGE_FORMAT.md)
- [architecture/03_EDITOR_AND_NOCODE.md](architecture/03_EDITOR_AND_NOCODE.md)
- [architecture/04_SCRIPT_VM_AND_SDK.md](architecture/04_SCRIPT_VM_AND_SDK.md)
- [architecture/05_COMPONENT_SYSTEMS.md](architecture/05_COMPONENT_SYSTEMS.md)
- [architecture/06_PACKAGE_MANAGER.md](architecture/06_PACKAGE_MANAGER.md)
- [architecture/07_HUB_AND_COLLABORATION.md](architecture/07_HUB_AND_COLLABORATION.md)
- [architecture/08_NETWORK_AND_ROOMS.md](architecture/08_NETWORK_AND_ROOMS.md)
- [architecture/09_CLOUD_AND_SELF_HOSTING.md](architecture/09_CLOUD_AND_SELF_HOSTING.md)
- [architecture/10_MARKETPLACE_AND_COMMERCE.md](architecture/10_MARKETPLACE_AND_COMMERCE.md)
- [architecture/11_SECURITY_AND_TRUST.md](architecture/11_SECURITY_AND_TRUST.md)
- [architecture/12_COMPATIBILITY_AND_MIGRATION.md](architecture/12_COMPATIBILITY_AND_MIGRATION.md)
- [architecture/13_TESTING_OPERATIONS.md](architecture/13_TESTING_OPERATIONS.md)

### roadmap
- [roadmap/v0_1_ENGINE_FOUNDATION.md](roadmap/v0_1_ENGINE_FOUNDATION.md)
- [roadmap/v0_2_GAME_CREATOR.md](roadmap/v0_2_GAME_CREATOR.md)
- [roadmap/v0_3_SANDBOXSCRIPT.md](roadmap/v0_3_SANDBOXSCRIPT.md)
- [roadmap/v0_4_PACKAGE_MANAGER.md](roadmap/v0_4_PACKAGE_MANAGER.md)
- [roadmap/v0_5_CREATOR_ALPHA.md](roadmap/v0_5_CREATOR_ALPHA.md)
- [roadmap/v0_6_SANDBOX_HUB.md](roadmap/v0_6_SANDBOX_HUB.md)
- [roadmap/v0_7_MULTIPLAYER_PLATFORM.md](roadmap/v0_7_MULTIPLAYER_PLATFORM.md)
- [roadmap/v0_8_MARKETPLACE_&_HOSTING.md](roadmap/v0_8_MARKETPLACE_&_HOSTING.md)
- [roadmap/v0_9_BUSINESS_&_STABILITY.md](roadmap/v0_9_BUSINESS_&_STABILITY.md)
- [roadmap/v1_0_SANDBOX_PLATFORM_GA.md](roadmap/v1_0_SANDBOX_PLATFORM_GA.md)
