# v0.2 — Game Creator

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 목표와 범위
**핵심 목표:** No-code 게임 제작의 첫 번째 완성.

기존 World 편집을 GameProject, GameMode, Feature·Prefab 기반 플레이 가능한 미니게임 제작으로 확장한다.

**버전 정책:** 스크립트 없이도 제작 가능한 규칙은 Native Feature와 Visual Trigger/Rule 양식으로 제공. 동적 사용자 Component는 다음 버전으로 남긴다.

## 2. 진입 조건
이전 버전의 모든 필수 완료 기준 충족 및 릴리스 후보 통합 테스트

## 3. 구현 요구사항

1. GameProject/World/SaveGame/GamePackage 개념과 경로·Manifest 초안 구현

2. 게임 라이프사이클 Lobby/Starting/Running/Finished/Stop 및 서버 전환

3. 기본 Feature Bundle: Spawn, Score, Health, Timer, Win/Lose, Camera, Input

4. Visual Trigger 최소 Node(이벤트/조건/효과)와 Inspector 양식 제공

5. Edit↔TestPlay 상태 분리, World 변경 원본 오염 차단

6. 하나의 GamePackage를 Headless Server와 Local Play에서 실행

## 4. 아키텍처/데이터 영향

- 모듈 사이 역할·권한·데이터 소유자를 문서화하며, 기존 서버 권위 ECS·결정론·비동기 경계 원칙을 우선한다.

- Manifest·Script SDK·Network·Save의 변경사항은 호환 버전/Schema Migration 계약으로 관리한다.

## 5. 자동화 완료 기준

- [ ] 비개발자가 템플릿만으로 간단한 대전/미니게임 제작·저장·실행

- [ ] 게임 종료 조건과 Lobby/Restart 서버 권위 상태 검증

- [ ] Edit/Play 분리, Undo/Redo, 패키지 검증 자동 테스트

## 6. 데모 및 검증 시나리오

최소 1개의 재현 가능한 데모 프로젝트와 CI/CTest 실행 명령, 실패 시 기대 오류를 릴리스 문서에 포함한다. Game Creator 기능을 외부 사용자가 처음부터 재현할 수 있어야 한다.

## 7. 위험·보류 항목

- 게임 데이터를 지나치게 이른 시점에 특정 장르 모델에 종속

- 현행 Rule/Behavior 콘텐츠 포맷을 깨뜨리는 변경

## 8. 관련 상세 명세

- `architecture/02_GAME_PACKAGE_FORMAT.md`

- `architecture/03_EDITOR_AND_NOCODE.md`

## 9. Definition of Done

- 핵심 E2E 성공, 크래시/취약점/데이터 손상 문제의 출시 차단 기준 통과.

- 새 API 및 사용자 흐름 문서화, 마이그레이션/롤백 검증 완료.

- 미구현 항목을 릴리스 노트에 명시하고 다음 버전으로 재할당.
