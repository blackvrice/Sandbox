# v1.0 — Sandbox Platform GA

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 목표와 범위
**핵심 목표:** 상용 서비스 안정화와 호환성 보장.

Engine, Script, Package, Hub, Cloud, Store가 하나의 유저 여정으로 연결된 안정 릴리스.

**버전 정책:** GA 범위는 지원 OS와 기능을 명시하고 초기 비지원 기술(예: 모든 플랫폼 Native JIT, 글로벌 모든 지역)을 억지로 포함하지 않는다.

## 2. 진입 조건
이전 버전의 모든 필수 완료 기준 충족 및 릴리스 후보 통합 테스트

## 3. 구현 요구사항

1. 완전한 Developer/Player 온보딩과 API/SDK Reference

2. Script/Package/Network/Save 호환성 및 Migration Guide

3. 보안 감사, 공급망 검증, 백업·복구·사고 대응

4. GA용 Support Matrix, SLA/운영 약속, 취약점 신고 경로

5. 상용 Marketplace/Hosting/Business 약관 및 가격 문서

6. 성능·안정성·자원 사용량 상한 공개 및 모니터링

7. Demo Game 3종, 문서 및 CI/Self-host 설치 프로그램

## 4. 아키텍처/데이터 영향

- 모듈 사이 역할·권한·데이터 소유자를 문서화하며, 기존 서버 권위 ECS·결정론·비동기 경계 원칙을 우선한다.

- Manifest·Script SDK·Network·Save의 변경사항은 호환 버전/Schema Migration 계약으로 관리한다.

## 5. 자동화 완료 기준

- [ ] 초보 제작자·전문 개발자·서버 운영자 각 E2E 합격

- [ ] 설치/패키지/방/결제/지속 서버/백업/팀 배포 전환 성공

- [ ] 유지보수 가능한 Compatibility Matrix와 릴리스 회귀 체크리스트

- [ ] 서비스 장애 주입/보안 점검/실제 파일럿 피드백 반영 완료

## 6. 데모 및 검증 시나리오

최소 1개의 재현 가능한 데모 프로젝트와 CI/CTest 실행 명령, 실패 시 기대 오류를 릴리스 문서에 포함한다. Sandbox Platform GA 기능을 외부 사용자가 처음부터 재현할 수 있어야 한다.

## 7. 위험·보류 항목

- 기능 다양화로 출시 범위가 무한정 커질 위험

- 다수 플랫폼/결제 국가를 단계적으로 열어 운영 리스크 제한

## 8. 관련 상세 명세

- `architecture/11_SECURITY_AND_TRUST.md`

- `architecture/12_COMPATIBILITY_AND_MIGRATION.md`

## 9. Definition of Done

- 핵심 E2E 성공, 크래시/취약점/데이터 손상 문제의 출시 차단 기준 통과.

- 새 API 및 사용자 흐름 문서화, 마이그레이션/롤백 검증 완료.

- 미구현 항목을 릴리스 노트에 명시하고 다음 버전으로 재할당.
