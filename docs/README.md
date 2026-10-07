# Sandbox 문서

이 폴더는 프로젝트의 **단일 진실원(single source of truth)** 입니다.
코드와 문서가 어긋나면 둘 중 하나가 버그입니다 — 어느 쪽인지 판단해서 같은 커밋에서 고칩니다.

마지막 전면 개정: 2026-10-05 (Phase 0)

---

## 1. 문서 맵

| #  | 문서                                          | 언제 읽나                                              | 성격          |
|----|-----------------------------------------------|--------------------------------------------------------|---------------|
| 00 | [개요](00-OVERVIEW.md)                        | 처음 합류했을 때. 무엇을 왜 만드나, 용어집             | 규범          |
| 01 | [아키텍처](01-ARCHITECTURE.md)                | 타깃·레이어·스레드·데이터 흐름. 경계를 넘는 변경 전    | 규범          |
| 02 | [ECS](02-ECS.md)                              | EntityId, Registry, View, ECB, 리플렉션, 변경 추적     | 규범          |
| 03 | [시뮬레이션](03-SIMULATION.md)                | 틱 파이프라인, SimCommand, Behavior, Rule, Pathfinding | 규범          |
| 04 | [결정론](04-DETERMINISM.md)                   | **시뮬레이션 코드를 건드리는 모든 경우**               | 규범 · 최우선 |
| 05 | [월드](05-WORLD.md)                           | Chunk, Terrain, Spatial Index                          | 규범          |
| 06 | [렌더링](06-RENDERING.md)                     | RHI, DX12/Vulkan/Metal, 셰이더, 에셋, 프레임           | 규범          |
| 07 | [플랫폼](07-PLATFORM.md)                      | 창, 입력, 오디오, 네이티브 핸들                        | 규범          |
| 08 | [네트워크](08-NETWORK.md)                     | Transport, 프로토콜, 복제, Interest, Late Join         | 규범          |
| 09 | [직렬화](09-SERIALIZATION.md)                 | 세이브, 리플레이, 비트스트림, 버전·마이그레이션        | 규범          |
| 10 | [에디터](10-EDITOR.md)                        | 패널, 툴, 편집 명령, Undo, 권한                        | 설계          |
| 11 | [콘텐츠 스키마](11-CONTENT-SCHEMA.md)         | Prefab, Rule, Behavior, Terrain JSON                   | 규범          |
| 12 | [코딩 규칙](12-CODING-STANDARDS.md)           | 코드를 쓰기 전 한 번, 리뷰할 때마다                    | 규범          |
| 13 | [테스트](13-TESTING.md)                       | 변경을 검증할 때. 골든 해시 절차                       | 규범          |
| 14 | [성능](14-PERFORMANCE.md)                     | 예산, 벤치 시나리오, 오버레이 지표                     | 규범          |
| 15 | [빌드](15-BUILD.md)                           | 툴체인, 프리셋, 의존성, CI, 실행 옵션                  | 참조          |
| 16 | [로드맵](16-ROADMAP.md)                       | 다음에 뭘 할지                                         | 계획          |
| 17 | [소스 지도](17-SOURCE-MAP.md)                 | "이 코드가 어디 있지?"                                 | 참조          |
| —  | [수동 QA 체크리스트](qa/MANUAL-QA.md)         | 마일스톤 QA                                            | 절차          |
| —  | [결정 기록 (ADR)](adr/README.md)              | "왜 이렇게 했지?"                                      | 이력          |
| —  | [원본 설계서](design/SANDBOX_ARCHITECTURE.md) | RTS 분석 결과, 전체 설계의 출처                        | 이력 (동결)   |

**규범**은 코드가 따라야 하는 계약입니다. 어기면 리뷰에서 막습니다.
**설계**는 아직 구현되지 않은 것의 청사진, **참조**는 현재 상태의 설명, **계획**은 순서와 우선순위,
**이력**은 바꾸지 않는 기록입니다.

> Phase 0 시점에는 코드가 없으므로 모든 규범 문서의 내용이 사실상 `[계획]`입니다.
> 구현이 들어오면 해당 절의 `[계획]` 표시를 지우고, 구현과 다르게 된 부분은 문서를 고칩니다.

---

## 2. 급할 때 읽을 순서

```text
처음 온 사람          00 → 01 → 17 → 16
ECS / 시뮬레이션 수정  04 → 02 → 03 → 12 → 13
렌더러 작업           01 → 06 → 07 → 12
네트워크 작업         01 → 08 → 09 → 04
에디터 작업           10 → 08 → 02 → 11
콘텐츠 작성           11 → 03
성능 작업             14 → 04 → 13
```

---

## 3. 문서 유지 규칙

[AGENTS.md](../AGENTS.md) 6장과 한 쌍입니다.

1. **구조를 바꾸면 문서를 같은 커밋에서 고칩니다.**
2. **결정을 바꾸면 ADR을 추가합니다.** 기존 ADR은 수정하지 않습니다.
3. **`DEVELOPMENT_LOG.md`는 이력, `docs/`는 현재 계약.** 섞지 않습니다.
4. 수치에는 출처(측정 / 추정 / 데이터 파일)를 적습니다.
5. 구현되지 않은 것은 `[계획]`으로 표시합니다.
6. `design/SANDBOX_ARCHITECTURE.md`는 수정하지 않습니다. 설계가 바뀌면 규범 문서와 ADR을 고칩니다.
7. 버전 번호(`kSimVersion`, `kProtocolVersion`, `WorldVersion`, `SchemaVersion`, 컴포넌트 버전)를
   올리는 변경은 해당 문서의 이력 표에 한 줄을 추가합니다.

---

## 4. 원본 설계서 → 분할 문서 대응표

| 원본 장      | 내용                                                              | 옮겨진 곳                |
|--------------|-------------------------------------------------------------------|--------------------------|
| 0            | 요약, 핵심 결정 K1~K10                                            | 00 · adr/0001~0010       |
| 1~3          | RTS 분석, 재사용, 폐기                                            | design (동결) · 00 6장   |
| 4            | ECS                                                               | 02                       |
| 5            | Simulation (틱, 명령, Behavior, Rule, Random, Save, Replay, Hash) | 03 · 04 · 09             |
| 6            | World / Chunk                                                     | 05                       |
| 7            | Thread                                                            | 01 5장                   |
| 8 · 9        | Client · Dedicated Server                                         | 01 3~4장                 |
| 10 · 11      | Network · Replication                                             | 08                       |
| 12           | Editor                                                            | 10                       |
| 13           | Platform                                                          | 07                       |
| 14~20        | RHI, DX12, Vulkan, Metal, Shader, Asset, Rendering Flow           | 06                       |
| 21 · 22      | Network · Simulation Data Flow                                    | 08 · 03                  |
| 23 · 24 · 25 | Dependency · Directory · CMake                                    | 01 · 17 · 15             |
| 26           | Migration Plan                                                    | 16                       |
| 27 · 28      | Test · Benchmark                                                  | 13 · 14                  |
| 29 · 30      | 위험 · 대안                                                       | 16 5장 · 각 ADR의 "대안" |
