# 06. Component / System / Native 확장 설계

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 기본 원칙
Native와 사용자 정의 Component는 저장 방식은 달라도 **동일한 Component Schema/Registry 계약**을 사용한다. 기본 Component는 C++ 실제 타입, Custom Component는 로드 시 등록되는 Schema 기반 동적 저장소로 구현한다. ECS Component는 데이터 구성이므로 OOP 클래스 상속을 요구하지 않는다.

## 2. 현행 기반
- `SBX_COMPONENT(type, "core.transform", version, flags)`와 `ComponentCatalog::add<T>()` 명시 등록.
- `ComponentInfo`에 stableId, version, flags, typeId, fields, JSON/바이너리/해시 함수.
- `Registry::poolByStableId()`와 Sparse-Set Typed Pool.
- `FieldDesc`는 필드명·Hint·numeric·meta를 기록하나 전체 FieldKind/타입 선언 자동 생성에는 보강 필요.
- 현재 게임플레이 미등록 Component는 콘텐츠 검증 중 거절되므로 Custom 등록 후 검증 순서 재설계 필요.

## 3. Schema 확장
| 필드 | 목적 |
| --- | --- |
| `stableId` | 패키지 충돌 방지를 위한 영속 이름 (예: `studio.rpg.mana`) |
| `schemaVersion` | 세이브/네트워크 호환성 |
| `kind` | `i32`, `f32`, `f64`, `bool`, `vec2`, `string`, entityRef, enum, 배열 등 |
| `default`/range | 생성 기본값과 Inspector 검증 |
| `flags` | replicated, persistent, editor-visible, server-only |
| `permissions` | 누가 어떤 필드를 읽거나 변경 가능한가 |
| `migration` | 필드 변경 시 구버전 로드 |

## 4. Auto SDK Generation
```text
C++ reflect / SBX_COMPONENT ───┐
                                ├─ Canonical Component Schema
Custom defineComponent() ──────┘               │
                                  ┌─────────────┴───────────┐
                               .d.ts 타입             VM ComponentBinding
```
`ComponentRef<TransformData>("core.transform")`와 `defineComponent(...)`는 동일한 `ctx.read()`, `ctx.has()`, `ctx.query()`를 사용한다. 실 저장소의 선택과 검증은 Host 내부에서 처리한다.

## 5. System Scheduler와 실행 규칙
- Native System은 기존 `ISystem`과 `SystemScheduler` 사용.
- Script System은 `ScriptSystemAdapter : ISystem`으로 실행; 이벤트 핸들러는 Event Dispatcher가 호출.
- System 순서는 `(Stage, systemRegistrationOrder, stableId)` 등 **고정 규칙**으로 정의하고 재생 테스트에서 검증.
- Native가 대량 물리/경로탐색/복제/충돌/그리기 수행. Script는 퀘스트·스킬 규칙과 승인된 Native 집계 API 호출.
- 자주 필요한 커뮤니티 Script Feature는 벤치마크와 안정성 근거가 충분할 때 공식 Native Module로 승격.

## 6. 사용자 정의 데이터와 복제
- 로드 시 Schema 등록 → DynamicComponentPool 준비 → Package Build/Load/Save/Network 순서를 고정.
- 타입 ID는 로컬 실행용 인덱스이고 wire/lockfile에는 stableId와 version을 저장.
- 새 Component가 클라이언트에 보낼 수 있는 필드인지, server-only인지 명시. 알려지지 않은 필드는 임의로 수락하지 않음.
- 엔진 관리 Identity Component (`net.identity`, `persist.persistence`)는 사용자 코드에서 직접 생성/수정 금지.

## 7. 완료 기준
서버/클라이언트의 신규 Mana Schema 등록과 자동완성; Inspector 수정; Save-Restore-Migration; Replication; WorldHash; Script 권한 테스트까지 한 번에 통과.
