# v0.3 — SandboxScript

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 목표와 범위
**핵심 목표:** TypeScript 개발자 확장 SDK와 안전한 VM.

게임 개발자가 Custom Component/System으로 Native 엔진 기능을 안전하게 조합한다.

**버전 정책:** TypeScript 제한 부분집합 → Typed IR → `.sbc` → Verifier → C++ SandboxVM. JS 런타임 직접 실행·전체 TS 의미 지원 금지.

## 2. 진입 조건
이전 버전의 모든 필수 완료 기준 충족 및 릴리스 후보 통합 테스트

## 3. 구현 요구사항

1. Language subset, 타입/숫자·할당·예외·모듈·결정론 규범 동결

2. `defineComponent`, `defineSystem`, `.d.ts` SDK, C++ Reflection 기반 선언 생성

3. Dynamic Component Schema/Pool, Inspector/Save/Replication 연결

4. `IServerContext`, `IClientContext`, Host Function Registry·권한 검증

5. `onStart`/`onTick`/`onUpdate`/`onEvent`/`onStop` Lifecycle

6. ScriptSystemAdapter → SystemScheduler, 명령은 ECB/승인된 API로 제한

7. Fuel/Memory/Stack/Query/Event 제한, 비정상 Bytecode 거부

8. IDE Language Service와 테스트/디버깅·프로파일링 기본 통합

## 4. 아키텍처/데이터 영향

- 모듈 사이 역할·권한·데이터 소유자를 문서화하며, 기존 서버 권위 ECS·결정론·비동기 경계 원칙을 우선한다.

- Manifest·Script SDK·Network·Save의 변경사항은 호환 버전/Schema Migration 계약으로 관리한다.

## 5. 자동화 완료 기준

- [ ] Script로 새로운 Mana Component 및 Regeneration System 생성

- [ ] 자동완성, Inspector, Save/Load, Network replication 연결

- [ ] 실행량 초과·권한 위반·결정론 위반·VM crash 보호 테스트

- [ ] VM과 C++ Native 모듈 간 API 스키마 계약 자동 검사

## 6. 데모 및 검증 시나리오

최소 1개의 재현 가능한 데모 프로젝트와 CI/CTest 실행 명령, 실패 시 기대 오류를 릴리스 문서에 포함한다. SandboxScript 기능을 외부 사용자가 처음부터 재현할 수 있어야 한다.

## 7. 위험·보류 항목

- 자체 VM/Compiler의 언어 의미 구현 규모

- VM/Natively generated code 메모리 안전과 결정론

- Native JIT는 이 버전 완료조건에 포함하지 않음

## 8. 관련 상세 명세

- `architecture/04_SCRIPT_VM_AND_SDK.md`

- `architecture/05_COMPONENT_SYSTEMS.md`

## 9. Definition of Done

- 핵심 E2E 성공, 크래시/취약점/데이터 손상 문제의 출시 차단 기준 통과.

- 새 API 및 사용자 흐름 문서화, 마이그레이션/롤백 검증 완료.

- 미구현 항목을 릴리스 노트에 명시하고 다음 버전으로 재할당.
