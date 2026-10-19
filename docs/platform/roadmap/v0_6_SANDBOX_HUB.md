# v0.6 — Sandbox Hub

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 목표와 범위
**핵심 목표:** 공개 Registry·개발자 커뮤니티·협업 MVP.

창작자들이 공개 패키지를 검색·설치하고 오픈소스 개선에 참여하는 기반을 구축한다.

**버전 정책:** GitHub/Git 저장소 연동을 기본으로 하며 PR/Issue/VCS를 처음부터 재구현하지 않는다.

## 2. 진입 조건
이전 버전의 모든 필수 완료 기준 충족 및 릴리스 후보 통합 테스트

## 3. 구현 요구사항

1. Account·Profile·Organization·Game/Package Listing·검색 서비스

2. Package Registry publish/version/download, immutable releases

3. GitHub Repo Link, Commit/PR/Issue 링크와 CI 결과

4. Game/Component/Map Schema Diff 기본 보고서

5. 배포 Permission·License·Signature/Hash·악성 Package 신고

6. Package Analytics(다운로드·의존·호환성), API/CLI 인증

7. Free Private Repo/Project 접근정책, 기본 역할·감사 로그

## 4. 아키텍처/데이터 영향

- 모듈 사이 역할·권한·데이터 소유자를 문서화하며, 기존 서버 권위 ECS·결정론·비동기 경계 원칙을 우선한다.

- Manifest·Script SDK·Network·Save의 변경사항은 호환 버전/Schema Migration 계약으로 관리한다.

## 5. 자동화 완료 기준

- [ ] A 공개 → B PR → Merge → Release → C 설치 순환 완성

- [ ] 패키지 버전 불변, 다운로드/서명/라이선스/권한 검증

- [ ] 비공개 프로젝트/Package 외부 유출 테스트 없음

## 6. 데모 및 검증 시나리오

최소 1개의 재현 가능한 데모 프로젝트와 CI/CTest 실행 명령, 실패 시 기대 오류를 릴리스 문서에 포함한다. Sandbox Hub 기능을 외부 사용자가 처음부터 재현할 수 있어야 한다.

## 7. 위험·보류 항목

- Cloud Hub 가용성·비용, 저장소 외부 API 이용 제한

- Moderation/Package Takedown 운영 부담

## 8. 관련 상세 명세

- `architecture/06_PACKAGE_MANAGER.md`

- `architecture/07_HUB_AND_COLLABORATION.md`

## 9. Definition of Done

- 핵심 E2E 성공, 크래시/취약점/데이터 손상 문제의 출시 차단 기준 통과.

- 새 API 및 사용자 흐름 문서화, 마이그레이션/롤백 검증 완료.

- 미구현 항목을 릴리스 노트에 명시하고 다음 버전으로 재할당.
