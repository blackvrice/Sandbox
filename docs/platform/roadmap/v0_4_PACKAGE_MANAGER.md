# v0.4 — Package Manager

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 목표와 범위
**핵심 목표:** 재사용 가능한 기능/에셋의 설치와 버전 관리.

모듈과 게임 리소스를 안전하게 설치하고 버전 고정해 다른 개발자 프로젝트에서 재사용한다.

**버전 정책:** 온라인 Registry 없이도 로컬 및 Git 의존성/잠금 파일·명시 버전으로 재현성을 갖춘다.

## 2. 진입 조건
이전 버전의 모든 필수 완료 기준 충족 및 릴리스 후보 통합 테스트

## 3. 구현 요구사항

1. Manifest·SemVer·namespaced stableId·SBOM/License 규칙

2. `sbx init/add/remove/update/build/test` CLI의 핵심 구현

3. 로컬 폴더·Git 태그/커밋·오프라인 캐시 Source

4. 의존성 그래프, 충돌 탐지, transitive lockfile 및 digest

5. Feature Bundle(Component/System/Script/Prefab/Asset) Export/Import

6. 패키지 설치 후 TypeScript 자동완성·Editor feature palette 갱신

7. 패키지 서명/해시/권한/Bytecode 검사, 승인 없는 install hook 금지

## 4. 아키텍처/데이터 영향

- 모듈 사이 역할·권한·데이터 소유자를 문서화하며, 기존 서버 권위 ECS·결정론·비동기 경계 원칙을 우선한다.

- Manifest·Script SDK·Network·Save의 변경사항은 호환 버전/Schema Migration 계약으로 관리한다.

## 5. 자동화 완료 기준

- [ ] 다른 제작자의 Inventory Feature 설치 → Build → Test Play

- [ ] Clean 머신에서 동일 lockfile로 같은 game digest 생성

- [ ] 의존성 충돌·악성 Manifest·깨진 파일 거부와 설치 롤백

## 6. 데모 및 검증 시나리오

최소 1개의 재현 가능한 데모 프로젝트와 CI/CTest 실행 명령, 실패 시 기대 오류를 릴리스 문서에 포함한다. Package Manager 기능을 외부 사용자가 처음부터 재현할 수 있어야 한다.

## 7. 위험·보류 항목

- Component stableId 및 복수 버전 공존 정책 결정 필요

- License와 에셋 재배포 범위 혼동

## 8. 관련 상세 명세

- `architecture/06_PACKAGE_MANAGER.md`

- `architecture/12_COMPATIBILITY_AND_MIGRATION.md`

## 9. Definition of Done

- 핵심 E2E 성공, 크래시/취약점/데이터 손상 문제의 출시 차단 기준 통과.

- 새 API 및 사용자 흐름 문서화, 마이그레이션/롤백 검증 완료.

- 미구현 항목을 릴리스 노트에 명시하고 다음 버전으로 재할당.
