# 14. 테스트·관측·출시 운영 기준

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 품질 게이트
| 층 | 필수 시험 | 기준 |
| --- | --- | --- |
| Foundation/Core | 단위·속성·결정론·WorldHash | 기존 Golden 회귀 없음 |
| Package/Schema | Manifest/Lockfile/License/Migration | 입력 변형에도 안전하게 실패 |
| Script | Parser/Typed IR/Verifier/VM/Host | 동등성 + Fuel/Memory 제약 |
| Game Editor | Inspector/Trigger/Undo/Play Mode | 서로 다른 두 사용자의 결과 수렴 |
| Network | NAT 직접/릴레이, Auth, 재접속 | 권한 우회·비정상 패킷 거절 |
| Cloud | Allocation/Health/Restart/Backup | 장애 주입 후 회복 가능 |
| Commerce | Payment/Refund/License/Settlement | 중복 이벤트 안전, 정산 추적 |

## 2. 성능 지표
- 30 TPS에서 Tick budget 총 33.33ms. 실측 목표는 기기별로 정의하고 프로파일링 데이터 없이 임의 수치의 성능 보장 금지.
- 주요 텔레메트리: tick p50/p95/p99, stage별 ms, VM cpu/fuel/memory/host-calls, 네트워크 송수신·손실, asset loading p95, game launch conversion.
- Native 컴파일의 이득은 Compiler 비용, 캐시 적중, VM↔Host 왕복을 포함한 벤치마크로 판정.

## 3. 개발 워크플로
- 변경마다 `cmake --build` + `ctest` + Script/Package 검증 + Security test.
- PR에서 Component Schema diff, 패키지 Lockfile diff, API changes, 예상 성능 영향 검사.
- 각 vX 릴리스는 수동 데모가 아니라 자동 확인 가능한 시나리오를 포함.

## 4. 서비스 운영
- Control Plane / GameServer Plane 별 logs/metrics/traces 분리, 개인정보 마스킹.
- 상태 페이지, 배포 단계, 점진적 롤아웃, 장애 공지, 사고 대응 심각도 등급.
- 백업 복구 훈련, 삭제 정책, 관리형 월드 RPO/RTO는 비용·운영 수준에 맞춰 후속 확정.

## 5. 기본 Gate
모든 실패 경로(패킷·패키지·VM·호스팅·결제)가 관측 가능한 오류 코드를 남기고, 무결성이 깨진 경우 조용히 계속 실행하지 않는다.
