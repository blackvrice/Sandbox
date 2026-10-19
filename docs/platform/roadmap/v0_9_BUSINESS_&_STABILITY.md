# v0.9 — Business & Stability

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 목표와 범위
**핵심 목표:** 전문 개발팀 생산성과 운영 성숙도.

팀 기반 협업과 배포 자동화를 구독 가치로 제공하고 대규모 운영 안정성을 확보한다.

**버전 정책:** Private Repository는 기본 무료이며 Pro/Business는 고급 워크플로·거버넌스에서 차별화.

## 2. 진입 조건
이전 버전의 모든 필수 완료 기준 충족 및 릴리스 후보 통합 테스트

## 3. 구현 요구사항

1. Team/Org ACL, protected branch, 승인 흐름, 감사 로그

2. Game Schema/Map Visual Diff 및 Code Review 개선

3. CI/CD: 빌드, Test, Staging, Release approval, Rollback

4. Cloud Build Quota, Artifact Retention, Secret Management

5. Business Subscription/Billing, 좌석과 권한·정산 분리

6. 관측/알림/장애 대응/가용성 목표/비용 최적화

7. 외부 오픈소스 기여자 PR→Release 자동 검증

## 4. 아키텍처/데이터 영향

- 모듈 사이 역할·권한·데이터 소유자를 문서화하며, 기존 서버 권위 ECS·결정론·비동기 경계 원칙을 우선한다.

- Manifest·Script SDK·Network·Save의 변경사항은 호환 버전/Schema Migration 계약으로 관리한다.

## 5. 자동화 완료 기준

- [ ] 서로 다른 권한의 팀원 3명 이상이 안전하게 공동 작업

- [ ] 변경 PR가 Schema+Script+Save migration 테스트를 통과

- [ ] Staging→Production 배포, 롤백, 승인/감사 추적

- [ ] 중요 보안·성능 회귀 없이 실제 파일럿 운영

## 6. 데모 및 검증 시나리오

최소 1개의 재현 가능한 데모 프로젝트와 CI/CTest 실행 명령, 실패 시 기대 오류를 릴리스 문서에 포함한다. Business & Stability 기능을 외부 사용자가 처음부터 재현할 수 있어야 한다.

## 7. 위험·보류 항목

- GitHub 전면 대체하려는 과도한 범위 확장

- 권한·비밀 관리 실수, 협업 충돌 검출 누락

## 8. 관련 상세 명세

- `architecture/07_HUB_AND_COLLABORATION.md`

- `architecture/13_TESTING_OPERATIONS.md`

## 9. Definition of Done

- 핵심 E2E 성공, 크래시/취약점/데이터 손상 문제의 출시 차단 기준 통과.

- 새 API 및 사용자 흐름 문서화, 마이그레이션/롤백 검증 완료.

- 미구현 항목을 릴리스 노트에 명시하고 다음 버전으로 재할당.
