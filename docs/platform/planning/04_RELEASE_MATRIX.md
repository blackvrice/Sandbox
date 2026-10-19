# 17. 버전별 의존성 / 출시 게이트 / 결정을 내려야 할 사항

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 버전 요약
| 버전 | 산출물 | 제품 검증 |
| --- | --- | --- |
| v0.1 | 기존 Engine baseline 정리 | ECS·서버·Editor 회귀 없는 기준선 |
| v0.2 | No-code Game Creator | 비개발자 미니게임 완주 |
| v0.3 | SandboxScript VM/SDK | Custom Component/System·격리 |
| v0.4 | Package Manager | 타인 Feature 설치·잠금 재현 |
| v0.5 | Creator Alpha | A 제작 → B 실행 |
| v0.6 | Sandbox Hub | Publish → 기여 PR → Release → Install |
| v0.7 | Multiplayer Platform | 인증 방/자체 서버/무료 세션 |
| v0.8 | Marketplace + Hosting | 결제·정산·상시 서버 |
| v0.9 | Business | 팀 리뷰→자동 빌드→승인→배포 |
| v1.0 | GA | 사용자·운영·보안·호환성 기준 달성 |

## 2. 의존성과 우선순위
- GamePackage는 Script 이전에 Manifest/세계/저장·플레이 구조 정리 필요.
- Custom Component/Script는 기본 Registry·Reflection/Host ABI 정비 필요.
- Package Registry는 로컬 Package Manager·서명/Lockfile 이후 구축.
- Directory/방 목록은 공개 인터넷용 Network 보안/Identity 선행.
- Marketplace는 Entitlement·라이선스·성능/비용 계측 후 공개.
- Business는 개인/팀 개발자가 실제로 협업하는 지표 확보 후 제공.

## 3. 주요 Open Decisions
| 번호 | 의사결정 | 의사결정 기한 |
| --- | --- | --- |
| D-01 | SandboxScript 숫자 타입/예외/GC/Import subset | v0.3 진입 전 |
| D-02 | GamePackage `.sbpkg` 직렬화·서명·압축 | v0.2 종료 전 |
| D-03 | Component Schema의 Dynamic Pool 구현 | v0.3 초기 |
| D-04 | Script SDK/Host Registry 정의 소스(단일 IDL) | v0.3 초기 |
| D-05 | SemVer 충돌 해결·유료 패키지 EULA | v0.4 초기 |
| D-06 | GitHub 연동 범위/Registry 저장소 경계 | v0.6 초기 |
| D-07 | 공개 네트워크 보안 Transport 및 NAT 정책 | v0.7 진입 전 |
| D-08 | 무료 세션 자원·Relay 비용 한도 | v0.7 초기 |
| D-09 | 판매 국가·수수료·환불·정산 사업자 | v0.8 진입 전 |
| D-10 | Business seat/CI Quota/SLA | v0.9 초기 |

## 4. 기능 추적 원칙
버전 종료 시 모든 요구사항은 `done`, `deferred`, `removed` 중 하나의 상태로 기록한다. 참조 코드는 Git commit/PR, 테스트 케이스/로그, Schema 변경사항, 마이그레이션 지침과 연결한다.
