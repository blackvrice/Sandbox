# 16. 로드맵

> **계획 문서.** "다음에 뭘 할지"를 정할 때 봅니다. 기준: 2026-10-07 (Phase 8A 반영).

---

## 1. 규칙

```text
1. 각 Phase 가 끝나면 빌드되고 모든 테스트가 통과한다. 빅뱅 없음.
2. 커밋은 컴파일 가능한 작은 단위. 커밋마다 CTest.
3. 완료 기준은 자동 검증 가능해야 한다 (수동 QA 는 보조).
4. 1차 목표선은 Phase 12 (Windows 멀티플레이 에디터). Linux/macOS 는 그 뒤.
5. Phase 순서를 바꾸려면 ADR.
```

## 2. 전체 순서

```text
Phase 0   분석·설계·문서                          ← 완료 (2026-10-05)
Phase 1   저장소 골격 · CI · 경계 검사              ← 완료 (2026-10-05, CI 원격 실행 확인 대기)
Phase 2   Core ECS                                  ← 완료 (2026-10-05)
Phase 3   헤드리스 시뮬레이션 · 결정론 하네스           ← 완료
Phase 4   World (Chunk · Terrain · Spatial · Save/Load)  ← 완료
Phase 5   Ecosystem · Pathfinding Job · Replay       ← 헤드리스 콘텐츠 완성 (완료 2026-10-06)
Phase 6   Windows 플랫폼 (Win32 · Input · Audio)      ← 구현 완료 2026-10-06, 사용자 PC 수동 QA 대기
Phase 7   DirectX 12 RHI · 셰이더 파이프라인          ← 구현 2026-10-07 (7A Clear · 프레임 자원, 7B 셰이더 · 파이프라인 · Triangle · Texture), 사용자 PC 확인 대기
Phase 8   Renderer · Asset · ImGui                  ← 구현 완료 2026-10-07 (8A 스프라이트 · --direct-sim, 8B 지형 · 오버레이 · GPU 시간, 8C ImGui 패널), 사용자 PC 확인 대기
Phase 9   Network Foundation · Dedicated Server        ← 구현 2026-10-08 (ENet · 핸드셰이크 · 명령 · ServerHost · sbx_net_probe)
Phase 10  Replication · LocalServerHost             ← 단일 코드 경로 완성 (2026-10-08): 10A 복제 (ReplicationWriter · ClientWorld · net_convergence), 10B SandboxClient 가 로컬 · 원격 서버의 복제본을 그린다 (--direct-sim 삭제)
Phase 11  Interest Management                       ← 완료 (2026-10-08): 화면 근처만 받기 · 예산 우선순위 · 세션 토큰 다시 접속
Phase 12  Multiplayer Editor                        ← 1차 목표선. 12A 에디터 툴 · 12B 인스펙터 · 되돌리기 (2026-10-09 · 12) · 12C 역할 · 콘텐츠 · 12D 세이브 · 리플레이 (2026-10-12) — Phase 12 완료
Phase 13  Linux (X11 → Vulkan → Wayland)               ← 13A Vulkan 백엔드 · lavapipe 기준 이미지 · Windows --rhi vulkan, 13B X11 창 · Linux 클라이언트, 13C Wayland · --platform · Linux CI (2026-10-13) — Phase 13 구현 완료, CI 첫 실행 · 사용자 PC QA 대기
Phase 14  macOS (Cocoa → Metal)                       ← 14A 셰이더 MSL 경로 · Metal 번호 규칙 (2026-10-14), 14B Cocoa 창 · 14C Metal 백엔드 (2026-10-19) — Phase 14 구현 완료 · macOS 검증 대기 (CI macOS 잡 · Mac QA)
Phase 15  Optimization (측정 기반)                     ← 15A 시뮬레이션 50k · 15B 스냅숏 (2026-10-19, ADR-0038) — 결과가 같은 최적화, 50k 완료 판정은 사용자 PC 측정 대기
```

순서의 이유:

```text
- 시뮬레이션(2~5)을 렌더러(7~8)보다 먼저: 헤드리스로 정답이 확정된 뒤에 그려야
  "화면이 이상하다"가 렌더 버그인지 시뮬 버그인지 구분된다.
- 네트워크(9~10)를 에디터(12)보다 먼저: 에디터가 처음부터 명령을 서버로 보내는 구조로 태어나야 한다.
- Phase 8 의 --direct-sim(클라가 Core 를 직접 돌리는 임시 경로)은 Phase 10 완료 기준에서 삭제를 강제한다.
- Vulkan 을 Windows 에서 먼저 완성(13 사전 작업): Linux 포팅의 미지수를 "창 + 표면"으로 줄인다.
```

---

## 3. Phase별 산출물과 완료 기준

### Phase 0 — 분석·설계 ✅

```text
산출물  docs/design/SANDBOX_ARCHITECTURE.md, docs/00~17, adr/0001~0010, README, AGENTS.md, DEVELOPMENT_LOG.md
완료    문서 세트 작성 (2026-10-05)
```

### Phase 1 — 저장소 골격 ✅

| #    | 티켓                                                                                                                                                                   | 상태                                                                                                                        |
|------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------|
| 1.1  | `.gitignore`, `.gitattributes`(LF 기본, Windows 스크립트 CRLF), `.editorconfig`, `.clang-format`, `.clang-tidy`                                                        | ✅ 2026-10-05                                                                                                               |
| 1.1b | `git init` + 첫 커밋 + 원격 저장소                                                                                                                                     | ⏳ 사용자 작업 (이 세션은 사용자 PC 에서 git 을 실행할 수 없음)                                                             |
| 1.1c | `LICENSE`                                                                                                                                                              | ⏸ 열린 질문 Q1 결정 후                                                                                                      |
| 1.2  | 루트 `CMakeLists.txt`, `CMakePresets.json`(windows-msvc · windows-clangcl · linux-clang · linux-gcc · linux-clang-asan · linux-clang-tsan · macos), `cmake/Sbx*.cmake` | ✅                                                                                                                          |
| 1.3  | `SandboxFoundation`: `Types`, `Error`/`Expected`, `SBX_ASSERT`/`SBX_VERIFY`, `log`, `Fnv1a64`(동결), `Handle<Tag>`, `BuildInfo`(생성)                                  | ✅ — `SmallVector`는 첫 사용처인 Phase 2로 이동                                                                             |
| 1.4  | `SandboxCore`(시뮬레이션 상수), `SandboxServer`(`--help`/`--version`/`--log-level`), `SandboxTests`(doctest v2.5.0)                                                    | ✅                                                                                                                          |
| 1.5  | `sbx_check_link_boundaries()`(전이 검사) + `tools/check_includes.py` + 각각의 자체 시험, CTest 라벨 `arch`                                                             | ✅                                                                                                                          |
| 1.6  | `sbx_simulation_flags` (Core 가 PUBLIC 으로 전파)                                                                                                                      | ✅                                                                                                                          |
| 1.7  | CI: `.github/workflows/ci.yml` (Windows MSVC · Linux clang/gcc/asan · macOS)                                                                                           | ✅ 작성 — `.github/` 는 원격 도구로 쓸 수 없어 **사용자가 직접 배치** (채팅으로 전달), 원격 저장소 생성 후 첫 실행으로 확인 |

```text
완료 기준  세 OS CI 에서 빌드 + unit/arch 테스트 통과. 의도적으로 Server 에 Platform 링크를 넣으면 구성이 실패한다.
검증 (2026-10-05, Linux 클라우드 환경)
  linux-clang(clang 19.1.1) · linux-gcc(gcc 13.3) · linux-clang-asan  ×  Debug · RelWithDebInfo
  → 각 8/8 테스트 통과 (doctest 29 케이스 / 68 단언), -Werror
  arch_link_boundary_selftest: 중간 타깃을 거친 전이적 금지 링크를 구성 단계에서 잡음
  arch_include_lint_selftest: 픽스처 8건 위반 정확히 검출
미검증  Windows MSVC · macOS 빌드 — 이 세션에서 실행할 수 없음. 사용자 PC 또는 CI 첫 실행에서 확인 필요.
```

### Phase 2 — Core ECS  ✅

| #   | 티켓                                                                                                               | 상태                                             |
|-----|--------------------------------------------------------------------------------------------------------------------|--------------------------------------------------|
| 2.1 | `EntityId`, `EntityManager` (LIFO 재사용, 퇴역 슬롯)                                                               | ✅                                               |
| 2.2 | `ComponentPool<T>` sparse set + `validate()`, `foundation/container/SmallVector`, `foundation/math/Vec2`           | ✅                                               |
| 2.3 | `SBX_COMPONENT`, stableId 동결 테스트, Flags(+`NotHashed`), 이름 규칙 컴파일 타임 검사                             | ✅                                               |
| 2.4 | 리플렉션 계약 + `JsonWriter/Reader`, `HashVisitor`, `ComponentCatalog`(명시 등록, ADR-0011), nlohmann/json v3.12.0 | ✅                                               |
| 2.5 | `Registry` (구조 잠금, emplaceOrReplace, 리소스, 결정적 순회 도우미)                                               | ✅                                               |
| 2.6 | `View<Read/Write/Exclude>` (가장 작은 풀 드라이버, changed 갱신)                                                   | ✅                                               |
| 2.7 | `EntityCommandBuffer` (PendingEntity, 기록 순서, 생성→파괴 상쇄)                                                   | ✅                                               |
| 2.8 | 속성 테스트: Registry vs 참조 모델 10만 연산                                                                       | ✅                                               |
| 2.9 | `sbx_bench ecs.iterate / ecs.churn` 최초 기록                                                                      | ✅ (클라우드 컨테이너 기준선) — EnTT 비교는 남음 |

```text
완료 기준  02-ECS 15장 테스트 전부 통과. 10k view 순회 수치 기록.
검증 (2026-10-05)
  linux-clang · linux-gcc · linux-clang-asan × Debug · RelWithDebInfo, -Werror → 각 11/11 (doctest 76 케이스)
  MinGW GCC 13 크로스 컴파일(-Werror) 빌드 성공 — Windows 컴파일 대리 확인. 실행·MSVC 는 미검증.
  10k × 4 컴포넌트 view: 8.2 ns/entity (14-PERFORMANCE 7.1)
남은 것   EnTT 기준선(ecs.vs_entt), Opaque 컴포넌트·Binary/Bit Visitor (필요한 Phase 에서)
```

### Phase 3 — 헤드리스 시뮬레이션  ✅

| #   | 티켓                                                                                                         | 상태                                                                                          |
|-----|--------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------|
| 3.1 | `SimulationClock`, `SimulationWorld::tick()`, `SystemScheduler`(고정 Stage)                                  | ✅ — 편집 단계(일시정지), Step, 속도, ISystemProfiler 훅                                      |
| 3.2 | `SimCommand` variant + `CommandQueue` + 적용기                                                               | ✅ — Create/Delete/Move/Add/Remove/ChangeComponent + Pause/Resume/Step/Speed, 명령마다 원자적 |
| 3.3 | `SpatialIndex` (정렬 배열 격자) + 속성 테스트                                                                | ✅ — 카운팅 정렬은 Phase 4                                                                    |
| 3.4 | `RandomService` (CounterRng)                                                                                 | ✅                                                                                            |
| 3.5 | `LifecycleSystem`(Lifetime, Age), `MovementSystem`(직선 적분), `EventStream`                                 | ✅ + 진단용 `debug.random_walk`                                                               |
| 3.6 | `WorldHash` (리플렉션 기반, saveId 순서) + `kSimVersion = 1` + 정체성(`persist.persistence`, `net.identity`) | ✅                                                                                            |
| 3.7 | `sbx_sim_check --repeat --hash-at --hash-every --print-hash --golden --record-golden` + 불일치 진단          | ✅ — `--threads` 는 Phase 5 (아래)                                                            |
| 3.8 | 시나리오 `random_walk_1k` (+ `random_walk_10k`), 골든(툴체인별, ADR-0012), `SandboxServer --scenario`        | ✅                                                                                            |

```text
완료 기준  random_walk_1k 에서 D1, D5 통과. 1k 엔티티 평균 tick < 2 ms.
검증 (2026-10-05)
  D1  det_random_walk_repeat (600틱, 30틱마다 2실행 비교) 통과. 하네스 자체 시험 det_harness_selftest 통과.
  D4  골든 tests/golden/random_walk_1k.json — Linux-x86_64-Clang-19, Linux-x86_64-GNU-13 기록·일치.
      (두 컴파일러, Debug/RelWithDebInfo 모두 같은 해시였다 — 그래도 항목은 툴체인별)
  D5  ⚠ 미검증 — 시뮬레이션이 아직 단일 스레드라 워커 수가 없다. JobSystem 과 함께 Phase 5 로 옮긴다
      (--threads 는 지금 Unsupported 로 거절한다. 조용히 통과시키지 않기 위해).
  성능 1k 평균 0.29 ms (RelWithDebInfo, 클라우드 컨테이너) — 기준의 15%. 10k 3.8 ms.
  linux-clang · linux-gcc × Debug · RelWithDebInfo → 각 16/16, linux-clang-asan Debug 15/15 (doctest 111 케이스), -Werror
  MinGW GCC 13 크로스 컴파일(-Werror) 성공. Windows 실행·MSVC 는 미검증 (사용자 PC: 골든은 SKIP → --record-golden)
발견·수정
  arch_link_boundary_selftest 가 CLion(MinGW)에서 Ninja 를 못 찾던 문제 (CMAKE_MAKE_PROGRAM 전달)
  JSON: 코드에서 만든 양의 정수(signed)를 부호 없는 필드가 거절하던 문제 (Phase 2 코드)
  SpatialIndex: 거대한 AABB 질의에서 셀 수 곱이 u64 를 넘쳐 무한 루프 (단위 테스트가 잡음)
남은 것   D5(--threads), 공간 색인 카운팅 정렬(50k 재구성 6.8 ms), p99 측정, 권한·속도 제한 검증(Phase 9~12)
```

### Phase 4 — World  ✅

| #   | 티켓                                                                             | 상태                                                                   |
|-----|----------------------------------------------------------------------------------|------------------------------------------------------------------------|
| 4.1 | `ChunkCoord`, `Chunk`, `WorldGrid` (고정 경계)                                   | ✅ — 기본 16×16 청크, 최대 64×64                                       |
| 4.2 | Terrain 레이어 + `TerrainMaterial`(ContentDatabase 최소판) + `PaintTerrain` 명령 | ✅ — 내장 머티리얼 4종, 경계 검증                                      |
| 4.3 | `queryRadius/AABB/Nearest/Chunk` 완성                                            | ✅ — 카운팅 정렬, `forEachInChunk`·`countInChunk` (TagMask 는 Phase 5) |
| 4.4 | 세이브: world.json, entities.jsonl, chunk 파일, 테이블 재매핑, Opaque 보존       | ✅ — 폴더 교체 원자성 (ADR-0013)                                       |
| 4.5 | 마이그레이션 프레임워크 + 샘플 세이브 테스트                                     | ✅ — `tests/data/saves/v1_sample`                                      |
| 4.6 | `sbx_sim_check --save-at`, 시나리오 `world_save_load`                            | ✅                                                                     |

```text
완료 기준  D2 통과. Spatial 속성 테스트 통과. 50k 저장 < 1 s.
검증 (2026-10-05)
  D2  det_world_save_load (250틱 저장 → 600틱까지 나란히), det_random_walk_save_paused (일시정지 상태 저장) 통과.
      로드할 때 world.json 의 worldHash 와도 비교한다 (조건은 09 3.5).
  D4  골든 random_walk_1k 재기록 (simVersion 2), world_save_load 신규 — Clang 19 · GCC 13 같은 해시
  속성 SpatialIndex vs 전수 탐색 (무작위 월드 경계·경계 밖 점 포함) 통과
  성능 50k 저장 188 ms / 로드 504 ms. 1k tick 0.18 ms, 10k 1.9 ms. 50k 공간 재구성 1.1 ms (Phase 3: 6.8 ms)
  linux-clang · linux-gcc × Debug · RelWithDebInfo → 각 20/20, linux-clang-asan Debug 19/19 (doctest 126 케이스), -Werror
  MinGW GCC 13 크로스 컴파일(-Werror) 성공. Windows 실행·MSVC 는 미검증
kSimVersion 1 → 2 (WorldHash 에 지형, Movement 경계 자르기, 공간 색인 격자)
남은 것   증분 저장·zstd·Worker 직렬화(Phase 9 오토세이브와 함께), EntityRef 2-pass(Phase 5), 스키마 마이그레이션 체인(스키마 2 가 생길 때)
```

### Phase 5 — Ecosystem  ✅

티켓이 9개라 검증 단위로 셋으로 나눈다 (2026-10-05): **5A** 콘텐츠·생명 주기 → **5B** 행동·상호작용·경로 → **5C** 리플레이·밸런스·성능.

| #   | 티켓                                                                                                  | 단계 | 상태                                            |
|-----|-------------------------------------------------------------------------------------------------------|------|-------------------------------------------------|
| 5.1 | ContentDatabase 완성: Prefab, Tag, Rule, BehaviorGraph 로더 + 검증기(V1~V7) + contentHash             | 5A   | ✅                                              |
| 5.3 | `life.*` 컴포넌트와 Lifecycle 완성 (Energy, Health, Growth, Reproduce) + Prefab 생성(명령·SpawnQueue) | 5A   | ✅                                              |
| 5.5 | `content/ecosystem` 팩 (11-CONTENT-SCHEMA 7장 수치)                                                   | 5A   | ✅ (5B: 이동·충돌·감지·행동·경로 컴포넌트 추가) |
| 5.2 | `SensorSystem`, `BehaviorSystem`(FSM, 노드 어휘), `InteractionSystem` + `ResolveIntents`, Effect op   | 5B   | ✅                                              |
| 5.4 | `PathfindingService` (A*, PathGridSnapshot, Job 계약 T→T+1), `CollisionSystem`                        | 5B   | ✅                                              |
| 5.9 | JobSystem + `sbx_sim_check --threads 0,1,8` (D5 — Phase 3 에서 이월)                                  | 5B   | ✅                                              |
| 5.6 | Replay 기록·재생 + `--replay-roundtrip` (D3)                                                          | 5C   | ✅                                              |
| 5.7 | 골든 `ecosystem_small`, 시나리오 `ecosystem_survival`                                                 | 5C   | ✅                                              |
| 5.8 | `sbx_bench sim.ecosystem 10k`                                                                         | 5C   | ✅                                              |

```text
완료  시드 3개 18,000틱 세 종 공존. D1~D5 통과. 10k 평균 tick < 10 ms, p99 < 25 ms.   → **충족 (2026-10-06, 아래 5C 검증)**

5A 검증 (2026-10-05)
  콘텐츠  content/ecosystem 검증 오류·경고 0 (CTest content_eco_validate). 검증기 V1~V7 를 일부러 틀린 팩으로 시험
  D1·D2   eco_lifecycle (풀 400 · 토끼 60 · 늑대 10, 1800틱): --repeat 2 + 900틱 저장→로드 (태그 표·Opaque render.sprite
          를 지나는 세이브 왕복, 로드 해시 검증 일치)
  D4      골든 eco_lifecycle 신규. 기존 골든(random_walk_1k, world_save_load)은 변하지 않았다 → kSimVersion 그대로 2
  발견    nlohmann::json items().begin() 프록시를 구조적 바인딩으로 잡으면 매달린 참조 — Debug 는 통과, RelWithDebInfo 에서
          예외. 최적화 빌드 테스트가 잡았다 (콘텐츠 로더의 조건 노드 파서)
  Windows 사용자 PC (MSVC 19.44, CLion Debug, Phase 4 코드): ctest 17/19 — D1·D2 통과, random_walk_1k·world_save_load 최종 해시가
          Linux 골든과 같다. 실패 2개(린터 cp949 출력, 중첩 구성의 vcvars 환경)는 5A 에서 수정. 5A 코드의 MSVC 빌드는 미검증

5B 검증 (2026-10-06)
  D1·D2·D5 eco_lifecycle: --repeat 2 --save-at 900 --threads 0,1,8 (CTest det_eco_lifecycle). 저장 시점 101~110·211·333·517·700
          에서도 D2 일치. 단위 테스트가 경로 Job 진행 중(Submitted) 저장→로드 후 400틱 동일을 확인
  D4      kSimVersion 3 — 골든 3개 재기록, Clang 19 · GCC 13 같은 해시
  동작    eco_lifecycle 1800틱: 늑대가 토끼를 잡아먹고(60회) 토끼가 풀을 먹고 번식한다. 밸런스(토끼 증가·풀 감소)는 5C
  측정·전체 매트릭스 결과는 DEVELOPMENT_LOG 의 Phase 5B 항목

5C 검증 (2026-10-06)
  D3      리플레이 기록→재생: random_walk_1k(일시정지 편집) · world_save_load(지형) · eco_lifecycle · ecosystem_small 일치.
          변조한 리플레이는 다음 해시 레코드에서 잡힌다 (단위 테스트)
  공존    ecosystem_survival 18,000틱 시드 1·2·3 — 세 종 모두 생존 (CTest balance_*, 수치는 11 7장)
  성능    sbx_bench sim.ecosystem (ecosystem_10k, 10,000 개체 시작 → 끝 14,700): Worker 1 평균 7.6 ms · p99 12.4 ms,
          Worker 0 평균 10.4 ms · p99 16.5 ms (2코어 클라우드 컨테이너, 14-PERFORMANCE 7.5)
  D4      kSimVersion 4 — 골든 4개 (ecosystem_small 신규), Clang 19 · GCC 13 같은 해시. Windows 항목은 다시 기록 필요
```

### Phase 6 — Windows 플랫폼  ✅ (수동 QA 대기)

| #   | 티켓                                                                                             | 상태                                   |
|-----|--------------------------------------------------------------------------------------------------|----------------------------------------|
| 6.1 | IWindow / NativeWindowHandle / PlatformEvent / Key (물리 위치) + HeadlessWindow                  | ✅ 2026-10-06                          |
| 6.2 | Win32Window (DPI v2, Raw Input, IME, 클립보드, 커서) + FramePacer · attachConsole                | ✅ (Wine 시험 통과, 사용자 PC QA 대기) |
| 6.3 | InputSystem (I1·I3·I4) + ActionMap (settings/input.json, 정확 수정자, 충돌) + Gamepad 인터페이스 | ✅                                     |
| 6.4 | IAudioBackend + NullAudioBackend                                                                 | ✅                                     |
| 6.5 | SandboxClient 빈 창 + 앱 상태기계 골격 (`--headless`, 제목 줄 입력 모니터)                       | ✅                                     |

```text
완료  입력 단위 테스트 + qa/MANUAL-QA 의 Phase 6 항목
      → 단위 테스트 충족 (2026-10-06). 수동 QA 는 사용자 PC 에서 (Wine 에서 미리 확인한 항목은 아래)

검증 (2026-10-06)
  단위    tests/unit/platform · tests/unit/client (doctest), CTest client_version · client_headless_smoke (모든 OS)
  Win32   MinGW 교차 빌드 → Wine 9 + Xvfb + xdotool 로 실제 Win32Window 구동: 키(스캔 코드 → Key)·Shift·F2 글자 입력·
          클릭·더블클릭(×2)·휠·가운데 버튼·F3 캡처 중 Raw Input 상대 이동 Δ(30,10)·창 크기 변경·제목 줄(한국어)·Ctrl+Q 종료.
          SandboxTests.exe 전체 통과. Wine 은 Per-Monitor DPI 를 지원하지 않는다 → DPI·IME 조합·Alt+Tab 은 사용자 PC QA
  남음    MSVC 빌드와 qa/MANUAL-QA Phase 6 체크리스트 (사용자 PC)
```

### Phase 7 — DirectX 12 RHI

티켓이 7개라 둘로 나눈다 (2026-10-06, ADR-0018): **7A** RHI 골격 · 디바이스 · Clear · 프레임 자원 · 기준 이미지 틀 →
**7B** 셰이더 빌드 · 파이프라인 · Triangle · Texture.

| #   | 티켓                                                                                        | 단계 | 상태                                                            |
|-----|---------------------------------------------------------------------------------------------|------|-----------------------------------------------------------------|
| 7.1 | RHI 인터페이스·Desc·Caps·Handle (7A 범위: Buffer·Texture·RenderPass·Barrier·Copy·SwapChain) | 7A   | ✅ 2026-10-06 (Shader·Pipeline·BindGroup·Sampler 는 7B)         |
| 7.2 | Device/Adapter/Queue/Fence/SwapChain (Clear)                                                | 7A   | ✅ (Wine 시험 통과, 사용자 PC 대기)                             |
| 7.3 | FrameContext, 업로드 링, 파괴 대기열                                                        | 7A   | ✅                                                              |
| 7.7 | sbx_render_tests + WARP CTest (clear · upload · region copy · 수명)                         | 7A   | ✅ (triangle · texture 기준 이미지는 7B)                        |
| 7.4 | 셰이더 빌드(sbx_add_shader, DXC, 리플렉션 JSON, 헤더 생성)                                  | 7B   | ✅ 2026-10-07 (DXC NuGet 고정 · 자체 SPIR-V 리플렉션, ADR-0019) |
| 7.5 | Pipeline/RootSignature/BindGroup (Triangle)                                                 | 7B   | ✅ (triangle · coord_convention · culling_ccw)                  |
| 7.6 | Texture + 업로드 (Texture — 샘플링)                                                         | 7B   | ✅ (nearest == 사분면, texture_linear)                          |

```text
완료  clear/triangle/texture 기준 이미지 통과, Debug Layer·GBV 경고 0

7A 검증 (2026-10-06)
  Linux    RHI 순수 로직(핸들 풀 · 지연 해제 · 업로드 링 참조 모델 2,000프레임 · 이미지 PNG 왕복·비교)과 클라이언트(가짜 렌더러)
  Wine     MinGW 교차 빌드 → Wine 9 d3d12(vkd3d) → lavapipe: sbx_render_tests 8케이스 통과 (누수 0, 경고 0),
           SandboxClient 실제 창: 스왑체인 생성 · Clear · 크기 변경 · Ctrl+Q. vkd3d 는 FL 11_1 까지라 --fl11 · --rhi-fl11
  남음     사용자 PC: MSVC 빌드, ctest -L render (WARP + Debug Layer), SandboxClient 수동 QA (DPI·최소화·VSync·--rhi-debug)
           → 2026-10-06 사용자 PC 에서 render_tests_warp 8/8 (Debug Layer 성능 안내 ID 820·821 제외 후)

7B 검증 (2026-10-07)
  Linux    clang · gcc Debug/RelWithDebInfo · ASan: RangeAllocator 참조 모델 · 파이프라인 검사 · 생성기 self-test (셰이더는 OFF)
  Wine     MinGW + tools/wine/dxc.sh(Windows dxc.exe) 로 셰이더까지 교차 빌드 → Wine 11.19(WineHQ devel) d3d12(vkd3d) →
           lavapipe: sbx_render_tests 16케이스 · 222 단언 통과 (누수 0 · 디스크립터 0 · 예상 오류 6), SandboxTests render 16케이스,
           SandboxClient 실제 창에 도는 삼각형. Ubuntu 의 Wine 9 (vkd3d 1.10)은 DXIL 을 못 받는다
  남음     사용자 PC: 첫 구성의 DXC 내려받기, ctest -L render (WARP + Debug Layer — 기준 이미지 4장 재확인),
           MANUAL-QA Phase 7B (삼각형 · --rhi-debug 경고 0 · 리사이즈 · 최소화)
```

### Phase 8 — Renderer

티켓이 6개라 셋으로 나눈다 (2026-10-07, ADR-0020): **8A** 텍스처 에셋 · Camera2D · SpriteBatcher · Renderer · --direct-sim →
**8B** TerrainPass · DebugDraw · Grid · Selection · 지표 · 타임스탬프 → **8C** ImGui.

| #   | 티켓                                                            | 단계 | 상태                                                                |
|-----|-----------------------------------------------------------------|------|---------------------------------------------------------------------|
| 8.1 | AssetManager (Texture · Material), Worker 디코드 → Upload Queue | 8A   | ✅ 2026-10-07 (스프라이트 아틀라스 · materials.json. Font 는 8C)    |
| 8.2 | Camera2D, SpriteBatcher(인스턴싱 · 정렬 키 · Texture2DArray)    | 8A   | ✅ (sprite · batch_1k 기준 이미지)                                  |
| 8.5 | SandboxClient --direct-sim 으로 Ecosystem 관찰 (임시)           | 8A   | ✅ (카메라 팬 · 줌 · 일시정지 · 속도, 보간)                         |
| 8.3 | TerrainPass · DebugDraw · GridPass · SelectionPass              | 8B   | ✅ 2026-10-07 (타일 텍스처 지형 · 화면 픽셀 선 · 선택, ADR-0022)    |
| 8.6 | Graphics 오버레이 지표, 타임스탬프 쿼리                         | 8B   | ✅ (패스별 GPU ms 를 제목 줄에 — 화면 글자는 8C)                    |
| 8.4 | ImGuiRenderer(RHI) + InputState→ImGuiIO, 기본 패널              | 8C   | ✅ 2026-10-07 (1.92 동적 텍스처 · 한글 · 시뮬레이션/통계, ADR-0023) |

```text
8.1 AssetManager (Texture, Material, Font), Worker 디코드 → Upload Queue
8.2 Camera2D, SpriteBatcher(인스턴싱, 정렬 키, Texture2DArray 아틀라스)
8.3 TerrainPass(청크 메시 캐시), DebugDraw, GridPass, SelectionPass
8.4 ImGuiRenderer(RHI) + InputState→ImGuiIO, 기본 패널(Simulation, Stats)
8.5 SandboxClient --direct-sim 으로 Ecosystem 관찰 (임시)
8.6 Graphics 오버레이 지표, 타임스탬프 쿼리
완료  10k 스프라이트 60 FPS, Draw ≤ 16, sprite/batch_1k/imgui_basic 기준 이미지 (coord_convention 은 7B 에서)

8A 검증 (2026-10-07)
  Linux    clang · gcc Debug/RWD · ASan: 카메라 · 정렬 · 배치 · 패킹 · 추출 · DirectSim · 앱 InWorld 단위 테스트,
           client_direct_sim_headless (28 틱), render.sprite_batch 벤치
  Wine     11.19 + lavapipe: sbx_render_tests 22 케이스 (sprite · camera · order · batch_1k · assets · materials 추가),
           SandboxClient --direct-sim: ecosystem_small 스크린숏, ecosystem_10k 12,877 스프라이트 · Draw 1 (소프트웨어 54 fps)
  후속     사용자 PC Debug 에서 ecosystem_10k 3 ~ 6 fps (렌더 스레드의 틱 × 4 따라잡기) → 창은 Simulation 스레드 +
           불변 스냅숏 (ADR-0021). Wine Debug 4 → 52 fps (시뮬레이션은 9.3/30 TPS 로 표시), TSan 단위 테스트 (14 7.7)
  남음     사용자 PC: ctest -L render (sprite · batch_1k 를 WARP 로), MANUAL-QA Phase 8A (실제 GPU 의 10k 60 FPS 확인)

8B 검증 (2026-10-07)
  Linux    clang · gcc Debug/RWD: DebugDrawList · 선 컬링 · TerrainCache · 지형 capture · 고르기 · 박스 · 앱 입력 단위 테스트,
           TSan client (Simulation 스레드 다시 capture)
  Wine     sbx_render_tests 26 케이스 (terrain · overlay 기준 이미지, timestamps, gpu timings — vkd3d 도 타임스탬프를 준다),
           SandboxClient: 지형(호수 · 흙) · 격자 · 박스 선택 21개 · 감지 반경 · 속도 화살표 스크린숏, 패스별 GPU ms (소프트웨어)
  사용자 PC  Release ecosystem_10k --vsync off: 1,628 fps, 렌더 CPU 0.4 ms, GPU 0.05 ms (지형 0.04 · 스프라이트 0.01), 30/30 TPS ·
           틱 5.2 ms — "10k 스프라이트 60 FPS" 완료 기준 충족 (14 7.8)
  남음     사용자 PC: ctest -L render (terrain · overlay 를 WARP 로), MANUAL-QA Phase 8B 나머지 (선택 · 격자 · --rhi-debug)

8C 검증 (2026-10-07)
  Linux    clang · gcc Debug/RWD: ImGui 키 · 커서 표, 이벤트 → ImGuiIO, 가로채기(I1) · IME 켜고 끄기, 앱(패널 위 클릭 · F1),
           패널 액션, 헤드리스 SandboxClient (텍스처 요청을 레이어가 받는다 — client_direct_sim_headless 28 틱 그대로)
  Wine     sbx_render_tests 28 케이스 (imgui_basic 기준 이미지, 동적 폰트 갱신, UI 텍스처 해제), SandboxClient 패널 스크린숏
           (Noto Sans CJK 로 한글, 시뮬레이션 패널 버튼으로 일시정지)
  완료 기준 10k 스프라이트 60 FPS ✅(사용자 PC 1,628 fps) · Draw ≤ 16 ✅(월드 2 ~ 5 + UI) · sprite/batch_1k/imgui_basic ✅
  남음     사용자 PC: ctest -L render (imgui_basic 을 WARP 로), MANUAL-QA Phase 8C (맑은 고딕 한글 · 패널 · 입력 가로채기)
```

### Phase 9 — Network Foundation ✅ (구현 2026-10-08, 사용자 PC 확인 대기 — ADR-0024)

```text
9.1 INetworkTransport + Loopback + Simulated ✅          9.2 EnetTransport (vendored ENet 1.3.18) ✅
9.3 BitWriter/Reader + 메시지 카탈로그(08 5장) ✅        9.4 핸드셰이크·버전·contentHash·Reject ✅
9.5 ServerHost (Simulation 스레드 + Net IO 스레드), CommandValidator(순번·속도 제한·권한 표) ✅
9.6 SandboxServer --world --ticks N --exit ✅ (+ sbx_net_probe 접속 도구)
완료  Loopback 핸드셰이크·명령·거절 테스트, 헤드리스 서버 CI 실행

검증 (2026-10-08)
  Linux    clang · gcc Debug: unit_network 28 케이스 — 비트스트림 · 메시지 · 명령 페이로드 전부 왕복 · 형식 상한, Loopback ·
           Simulated(100 ms ± 20, 손실 5 %, seed 결정적) · ENet(127.0.0.1 실제 UDP, 3 채널, 3 KB 조각), ServerHost Inline
           (핸드셰이크 · 거절 6 가지 · 시간 초과 · ProtocolError · 명령 결과 · 권한 · 속도 제한 · 종료 틱 · 나쁜 네트워크) ·
           Threaded(Loopback · ENet). CTest server_world_exit (--world random_walk_1k --ticks 60 --exit — 해시가 헤드리스
           --scenario 와 같다), net_server_probe_smoke (두 프로세스: 서버 + probe 명령 3 개 수락)
  Windows  MinGW 빌드 + Wine: 위 단위 테스트 (winsock 경로), SandboxServer + sbx_net_probe
  남음     사용자 PC (MSVC): ctest, MANUAL-QA Phase 9 (콘솔 두 개 · 방화벽 · 다른 PC 에서 접속)
  다음     Phase 10 — 복제 (NetEntityId · Snapshot · 보간), LocalServerHost, --direct-sim 삭제
```

### Phase 10 — Replication ✅ (10A · 10B 구현 2026-10-08 — ADR-0025 · 0026, 사용자 PC 확인 대기)

```text
10.1 NetEntityId, NetIdentity 부여, NetEntityMap        10.2 Spawn/Update/Despawn, ack, baseline 32개
10.3 양자화, EntityRef 변환, tombstone                  10.4 SnapshotBuffer + 보간
10.5 LocalServerHost — 싱글플레이 경로 전환, --direct-sim 삭제
10.6 ServerStats 메시지, Network 오버레이
완료  net_convergence (100ms, 5% 손실) 통과, --direct-sim 코드 0줄
```

둘로 나눈다:

```text
10A 복제 ✅ (2026-10-08)
  10.1 ✅ NetEntityId 는 Phase 9 (net.identity), NetEntityMap = ClientWorld 의 표
  10.2 ✅ ReplicationWriter — 클라이언트별 기록 32 (변경 순번 · mask), spawn/update/despawn, ack, 예산 round-robin, epoch 다시 맞추기,
          TerrainChunk (길이 부호화). 프로토콜 2. SandboxServer --snapshot-kbps
  10.3 ◐ EntityRef 변환 ✅ (NetEntityId), tombstone 은 단조 적용으로 대신 ✅, 양자화 [계획 — 측정 뒤]
  10.4 ◐ 표본만 (ClientWorld::transformTrack — 직전 · 지금) → 10B 에서 보간 ✅
  10.6 ◐ ServerStats ✅ (Phase 9), 서버 끝 줄 replication ms · probe 복제 개체 수
  완료 기준 net_convergence (100 ms ± 20 · 5 % 손실 양방향 · 64 KB/s) ✅

10B SandboxClient 를 네트워크 위로 ✅ (2026-10-08, ADR-0026)
  10.4 ✅ InterpolationClock (서버 틱 추정 · renderTick = 추정 − 0.1 초, 앞으로만, 일시정지 = 마지막 틱) + 엔티티별 표본 둘
          (SnapshotBuffer 32 대신 — 08 9장), 순간이동 8 칸, 외삽 없음
  10.5 ✅ LocalServerHost (ServerHost + Loopback, 역할 owner · 예산 없음, 창 = 서버 스레드 · 헤드리스 = 프레임 안).
          SandboxClient --world <시나리오|세이브> · --connect host:port · --name — NetworkSession (접속 · 콘텐츠 다시 맞추기 ·
          Connecting 대기 · 실패하면 종료 코드 1). --direct-sim · DirectSim · 그 시험 삭제 (코드 0줄)
  10.6 ✅ Network 패널 (서버 · 역할 · RTT · 스냅숏 · 받은 KB/s · 적용 ms · 보간 지연 · 거절된 명령)
  그 밖 ✅ 그리기를 ClientWorld 에서 (SpriteExtraction · 선택 netId), 선택 상세 = Inspect · InspectResult (프로토콜 3),
          스냅숏 간격은 실제 시간 (×8 에서도 15 Hz), 일시정지 · 한 틱 · 속도 = 서버 명령, 명령줄 인자 UTF-8 (Windows 한글)
  완료 기준 --direct-sim 코드 0줄 ✅ · SandboxClient 가 SandboxServer 에 접속해 그린다 ✅ (Wine 4.3k, 네이티브 10k 헤드리스) ·
          싱글플레이 프레임 시간 회귀 없음 ◐ — 작은 월드는 같고, 10k 는 추출 1.9 ms + 15 Hz 적용 약 8 ms (Release, 14 7.10).
          Main 스레드 적용은 Interest(11) · Net 스레드 디코드 [계획 15] 로 줄인다
```

```text
10A 검증 (2026-10-08)
  Linux    clang · gcc Debug: ctest 42/42 — unit_network 복제 6 케이스, net_convergence (Loopback 249 개체 · 100 ms ± 20 · 5 % 손실 ·
           64 KB/s 330 개체, 바이트 단위로 같다), 결정론 골든 그대로. TSan network · net 36 케이스 경고 0
  Windows  MinGW 빌드 경고 0 + Wine: network · net · core · foundation 132 케이스
  성능     Release ecosystem_10k: 스냅숏 만들기 5.4 ~ 5.7 ms (제한 없음) · 1.5 ms (256 KB/s), 틱 7 ms 안팎 (14 7.9)
  남음     사용자 PC (MSVC): 다시 빌드 → ctest, MANUAL-QA Phase 10A
```

```text
10B 검증 (2026-10-08)
  Linux    clang · gcc Debug: ctest 43/43 — unit_network(Inspect · 보간 시계 · 표본 · 선택 상세 · LocalServerHost ×8 15 Hz),
           unit_client(NetworkSession 로컬 Inline · 서버 스레드 · 원격 Loopback 콘텐츠 다시 맞추기 · editor 거절 · 서버 종료),
           client_local_world_headless (tick 28), client_world_bad_name. TSan network · net · client
  Windows  MinGW 빌드 경고 0 + Wine: SandboxClient --world ecosystem_small 창 (55 fps, 패널 일시정지 = 서버 명령),
           SandboxServer + SandboxClient --connect --name 원격 두 프로세스 (콘텐츠 다시 맞추기, 한글 이름, 4.3k 개체)
  남음     사용자 PC (MSVC): 다시 빌드 → ctest, MANUAL-QA Phase 10B
  다음     Phase 11 — Interest (Subscribe · 화면 근처만 · 히스테리시스 · 우선순위), 재접속 토큰
```

### Phase 11 — Interest ✅ (구현 2026-10-08 — ADR-0027, 사용자 PC 확인 대기)

```text
11.1 Subscribe, relevantChunks/Entities, alwaysRelevant   11.2 히스테리시스, 우선순위·바이트 예산
11.3 Bulk TerrainChunk 캐시(revision), Late Join 흐름     11.4 Reconnect 토큰
완료  50k 월드에서 클라당 바이트 ∝ 가시 엔티티 (sbx_bench net.snapshot), Late Join < 2 s
```

```text
11.1 ✅ Subscribe = 청크 사각형 | all (프로토콜 4). 서버: 위치 → 청크 비트맵, 관심 = 구독 청크 ∪ 늘 보낼 것 (Inspect 로 선택한
        개체 · 위치 없는 개체 — 소유 개체는 Phase 12). 클라이언트: 카메라 화면 + 1 청크, 초당 2 번까지
11.2 ✅ 빠진 청크 30 스냅숏 지연 해제 (스냅숏 수 — 결정적). 예산 순서 = 늘 보낼 것 → spawn → update, 같은 단계는 중심 거리 −
        미룬 틱 × 0.5 칸 (round-robin 삭제)
11.3 ✅ 지형은 관심 청크만 · 가까운 것부터 · 클라이언트가 들고 있다 (같은 revision 이면 다시 안 보낸다). Late Join 은 10A 대로
        새 epoch 스냅숏 (EntityBaseline · Ready 없음 — ADR-0025)
11.4 ✅ 세션 토큰 (60 초, ClientQuit 면 지움, 살아 있는 연결이면 옛 연결 Kicked). SandboxClient --connect 는 시간 초과면 2 초마다
        다시 (60 초까지), 그동안 옛 복제본
완료 기준 바이트 ∝ 관심 개체 ✅ (개체당 약 19.6 B — 14 7.11) · Late Join(화면, 256 KB/s) 0.33 초 ✅.
        14 1.4 의 "클라 16 · 50k 스냅숏 < 3 ms" 는 37 ms (사용자 PC MSVC 13.5 ms) — 바이트 재사용 · Net 스레드 인코딩 [계획 15]
```

```text
11 검증 (2026-10-08)
  Linux    clang · gcc Debug: ctest 43/43 — unit_network test_interest (지연 해제 · 늘 보낼 것 · 관심 지형 · 우선순위 · 메시지 ·
           LocalServerHost 구석 구독) · 토큰 다시 접속, unit_client (interestFor · 원격 다시 접속 · 60 초 실패). TSan network ·
           net · client 73 케이스 경고 0
  Windows  MinGW 빌드 경고 0 + Wine: network · net · core · foundation · client 170 케이스. 창 ecosystem_survival: 확대하면
           관심 "청크 -1..2 × -2..1", 클라이언트 개체 2,316 → 692 · 받은 KB/s 425 → 145
  성능     Release sbx_bench --only net. (14 7.11) — 사용자 PC MSVC 도 측정 (바이트 같음, 클라 16 화면 13.5 ms)
  남음     사용자 PC (MSVC): ctest, MANUAL-QA Phase 11 (sbx_bench 항목은 끝남)
  다음     Phase 12 — Multiplayer Editor (1차 목표선)
```

### Phase 12 — Multiplayer Editor (1차 목표선)

```text
12.1 EditorContext, ICommandSink, Selection             12.2 Hierarchy/Inspector(리플렉션)/Palette
12.3 툴: Select/Move/Place/TerrainBrush/Erase + EditPreview
12.4 Rules/Behaviors 패널, 월드 오버레이, ContentOverlay 전송
12.5 권한(역할 표), Players 패널                        12.6 Undo/Redo
12.7 세이브/로드 UI, 리플레이 재생 UI
완료  edit_session 시나리오 D3, 2클라 동시 편집 통합 테스트, 권한 매트릭스 테스트
```

넷으로 나눈다 (ADR-0028 결정 1):

```text
12A 에디터 툴 ✅ (2026-10-09, ADR-0028)
  12.1 ✅ SandboxEditor (editor/, Core + Render + imgui, Network 금지) · ICommandSink · IEditorHost(= NetworkSession) · 선택(세션) ·
          Editor (EditorContext 몫)
  12.3 ✅ 툴 다섯 — 선택 · 이동(MoveEntity, 격자 맞춤) · 배치(CreateEntity, 끌기 간격) · 지형(프레임당 PaintTerrain{cells}, 오른쪽 =
          바탕) · 지우기(DeleteEntity, Delete 키) + EditPreview (그린 위치에 더한다 — 그린 틱이 확정 스냅숏에 닿으면 걷는다)
  12.2 ◐ Palette · Terrain = "편집" 패널 (Prefab 목록 · 거르기 · 머티리얼 · 브러시). Hierarchy · Inspector 는 12B
  단축키 1 ~ 5 (W A S D 가 카메라 — 10-EDITOR 9장)
12B 인스펙터 · 목록 · 되돌리기 ✅ (2026-10-12, ADR-0029)
  12.2 ✅ "인스펙터" — ComponentInfo::fields × JSON 종류로 위젯 (E4), 편집이 끝나면 선택 전부에 ChangeComponent, 컴포넌트 더하기 ·
          빼기. 서버 전용 EditorVisible 값은 선택 상세로 (프로토콜 5). "목록"(Hierarchy) — 관심 영역 안의 개체, 거르기, 두 번 = 카메라
  12.6 ✅ Undo/Redo — 사용자 동작 하나 = 한 칸, 받아들여진 명령만, 역명령은 보내기 전 값으로, 지운 개체는 Prefab + 알던 값으로
          다시 만들고 IdRemap 으로 옛 기록을 새 netId 에. 기다리는 동안 막기, 거절된 되돌리기는 칸 빼기. Ctrl+Z · Ctrl+Y · F
12C 역할 · 콘텐츠 ✅ (2026-10-12, ADR-0030)
  12.5 ✅ 역할 바꾸기 — SetRole → 서버 Net 절반이 10-EDITOR 7장 "역할 부여" 열대로 (owner · admin), 접속자 표 PlayerList 를 모두에게,
          다시 접속해도 그 역할, 전용 서버의 owner = --owner-key. "접속자" 패널 (역할 콤보). 역할이 막는 툴 · 편집 · 실행 제어를
          숨기거나 끈다 (IEditorHost::permissions — 판단은 서버)
  12.4 ✅ 콘텐츠 편집 — EditContent{prefab | rule | behavior, id, JSON | null} → 월드 오버레이, 서버가 팩 원본 + 오버레이로 콘텐츠를
          다시 만든다 (검증 V1~V7, 실패면 거절), 행동 상태는 상태 id 로 옮긴다, WorldHash · 세이브(overlay/) · 리플레이에 남는다.
          ContentOverlay 메시지로 모두에게 · 늦게 들어온 클라이언트에 전체. "콘텐츠" 창 (JSON 편집 · 검사 · 적용 · 팩 값으로 · 새 항목,
          행동 상태 · 전이 요약). 폼 · 노드 그래프 편집은 [계획]
  프로토콜 6 (EditContent · ContentOverlay · PlayerList · SetRole · SetRoleResult · Auth.ownerKey — 08 5장)
12D 세이브 · 리플레이 · 완료 기준 ✅ (2026-10-12, ADR-0031)
  12.7 ✅ 서버가 틱 사이에 저장 · 불러오기 · 리플레이 기록 · 재생 (WorldOp — 목록 = 모두, 나머지 = admin 부터, <--saves>/<이름>).
          불러오기 · 재생은 모두의 월드를 바꾼다 — generation · 새 epoch · WorldChanged (클라이언트는 새 generation 스냅숏을
          WorldChanged 까지 쥔다), 에디터 기록 비우기. 재생 = ReplayPlayer 한 call 씩 (편집 거절, 실행 제어 = 재생 멈춤 · 한 call ·
          배율, 끝나면 해시 결과 · 일시정지). "월드" 패널 · Ctrl+S
  완료 ✅ edit_session 시나리오 (편집 명령 전 종류 · 일시정지 편집 · 오버레이) D1 · D2 · D3 · D5, 두 클라이언트 동시 편집 수렴
          (test_multi_client), 역할 × 명령 권한 표 (5 역할 × 명령 12 종 + 월드 요청 5 종)
  프로토콜 7 (WorldOp · WorldOpResult · WorldList · WorldChanged · generation · 재생 통계 — 08 5장)
```

```text
12A 검증 (2026-10-09)
  Linux    clang · gcc Debug: ctest 44/44 — unit_editor 7 케이스 (가짜 host: 툴 다섯 · preview · 거절 · 경계 · 붓질),
           unit_client + test_editor_session (로컬 서버까지 배치 · 이동 · 지형 · 지우기, 앱 숫자 키 · 클릭). TSan client · editor
  Windows  MinGW 빌드 경고 0 + Wine: network · net · core · foundation · client · editor 179 케이스. 창 ecosystem_small:
           배치 · 지형 끌기 · 이동(끄는 동안 preview, 놓은 뒤 같은 자리) · 편집 패널
  남음     사용자 PC (MSVC): 다시 빌드 → ctest, MANUAL-QA Phase 12A (원격 편집 · 권한 거절 포함)
```

```text
12B 검증 (2026-10-12)
  Linux    clang · gcc Debug: ctest 44/44 — unit_editor 14 케이스 (test_undo 7: 명령 종류마다 역명령 · netId 잇기 · 거절 · 기다림),
           unit_network (선택 상세의 컴포넌트 — 프로토콜 5 왕복 · 로컬 서버 ai.sensor), unit_client (로컬 서버에서 서버 전용 값
           고치기 → 되돌리기, 지운 개체 되살리기, 앱 Ctrl+Z). TSan client · editor · network 88 케이스 경고 0
  Windows  MinGW 빌드 경고 0 + Wine: network · net · core · foundation · client · editor 187 케이스. 창 ecosystem_small: 박스 선택 →
           인스펙터(복제 · "(서버)" 컴포넌트, 이름 순) · 목록 · 배치 두 번 → Ctrl+Z (서버 "수락 4 · 거절 0")
  남음     사용자 PC (MSVC): 다시 빌드 → ctest, MANUAL-QA Phase 12B
  다음     12C — 역할 바꾸기 · Players · 권한 없을 때 툴 숨김 + Rule · Behavior · ContentOverlay
```

```text
12C 검증 (2026-10-12)
  Linux    clang · gcc Debug: ctest 44/44 (골든 그대로 — 오버레이가 없으면 WorldHash 가 같다) — unit_content · core · persist
           test_content_overlay 14 케이스 (applyOverlay 자리 · 검증 · 지문, EditContent · 상태 옮기기 · 결정론 · 명령 JSON, 세이브 왕복 ·
           깨진 오버레이), unit_network test_roles_overlay 5 (메시지 · 주인 열쇠 · 역할 부여 표 · 다시 접속 · 오버레이 전달), unit_editor
           test_editor_roles 3, unit_client test_roles_content 3 (원격 역할 → 에디터 · 로컬 콘텐츠 편집 → 되돌리기 · 패널).
           TSan network · net · client · editor · content 116 케이스 경고 0
  Windows  MinGW 빌드 경고 0 + Wine: network · net · core · foundation · client · editor · content · persist 229 케이스. 창: 원격
           서버(--default-role player --owner-key) + 클라이언트 둘 — 손님 창의 "접속자"(주인 owner · 손님 player), "편집" 패널은 선택
           툴만 · "보기만" · 2 키 거절 문구, "시뮬레이션"의 실행 제어 꺼짐. 로컬: "콘텐츠" 창 (규칙 JSON · 요약)
  남음     사용자 PC (MSVC): 다시 빌드(프로토콜 6 — 서버 · 클라이언트 둘 다) → ctest, MANUAL-QA Phase 12C
  다음     12D — 세이브 · 로드 · 리플레이 UI + 완료 기준 (edit_session D3 · 두 클라이언트 동시 편집 · 역할 × 명령 매트릭스)
```

```text
12D 검증 (2026-10-12) — Phase 12 완료
  Linux    clang Debug (커밋마다) · gcc Debug: ctest 46/46 (det_edit_session · det_replay_edit_session 추가, 골든 그대로) —
           unit_core edit_session, unit_network test_world_ops 6 · test_multi_client 2, unit_editor test_editor_world_change,
           unit_client test_world_panel 4. TSan network · net · client · editor · content 129 케이스 경고 0
  Windows  MinGW 빌드 경고 0 (커밋마다) + Wine: network · net · core · foundation · client · editor · content · persist
           230 → 238 → 243 케이스. 창 ecosystem_small --saves (sim_check 로 만든 리플레이 · 세이브): 접힌 "월드 — 보통",
           펼친 패널의 목록 · Ctrl+S → 목록에 quicksave · 결과 줄
  남음     사용자 PC (MSVC): 다시 빌드(프로토콜 7 — 서버 · 클라이언트 둘 다) → ctest, MANUAL-QA Phase 12D
  다음     Phase 13 — Linux (X11 → Vulkan → Wayland)
```

### Phase 13 — Linux

```text
13.0 (Windows) SBX_ENABLE_VULKAN_ON_WINDOWS 로 Vulkan 백엔드 완성 — 기준 이미지 DX12 와 일치
13.1 X11Window + 입력           13.2 Linux 빌드·실행        13.3 WaylandWindow (libdecor)
완료  Core·Renderer 상위 diff 0줄, Linux CI lavapipe 기준 이미지 통과
```

셋으로 나눈다 (ADR-0032 결정 1):

```text
13A Vulkan 백엔드 ✅ (2026-10-13, ADR-0032)
  13.0 ✅ render/vulkan — volk + 벤더링한 Vulkan-Headers (SDK 없음), 1.3 (dynamic rendering · sync2 · timeline · hostQueryReset),
          큐 펜스 = 타임라인 세마포어, 리소스마다 할당, BindGroup = 디스크립터 세트, 정렬 · 좌표는 D3D12 에 맞춘다. 옵션 이름은
          SBX_ENABLE_VULKAN (Linux 기본 ON, Windows 는 켜서 D3D12 와 함께). 백엔드 팩토리 · --rhi auto|dx12|vulkan
          (sbx_render_tests · SandboxClient). 스왑체인은 Win32 표면 — Windows 클라이언트 --rhi vulkan
  13.2 ◐ Linux 에서 SandboxRender · sbx_render_tests 빌드 · 실행 (헤드리스) — CTest render_tests_lavapipe: D3D12 와 같은 기준
          이미지 28 케이스 · Validation 0. 클라이언트 창은 13B
13B X11 ✅ (2026-10-13, ADR-0033)
  13.1 ✅ X11Window (Xlib, platform/linux) — 키 = evdev keycode 표 (물리 위치, 한/영 · 한자), XKB detectable auto-repeat,
          XIM 텍스트 입력(ibus · fcitx), XInput2 Raw 캡처(없으면 가운데로 되돌리기), CLIPBOARD UTF-8, _NET_WM_NAME, Xft.dpi 배율,
          cursorfont 커서. 옵션 SBX_ENABLE_X11 (libx11-dev · libxi-dev)
  13.2 ✅ Xlib 표면 스왑체인 (vkCreateXlibSurfaceKHR) → Linux SandboxClient 창 (Vulkan, 한글 UI 글꼴 Noto CJK · 나눔고딕)
13C Wayland · CI ✅ (2026-10-13, ADR-0034)
  13.3 ✅ WaylandWindow (libwayland · libdecor 틀, platform/linux) — 키 = 같은 evdev 표, xkbcommon 키맵 · 수정자 · 글자 · 데드 키
          (compose), 클라이언트 자동 반복(repeat_info), text-input-v3 입력기, pointer-constraints + relative-pointer 캡처,
          wl_data_device 클립보드, wl_cursor 테마, 정수 배율(wl_output.scale), SUSPENDED = 최소화. 옵션 SBX_ENABLE_WAYLAND
          (libwayland-dev · wayland-protocols · libxkbcommon-dev · libdecor-0-dev, 프로토콜 C 는 wayland-scanner 로 생성)
  13.2 ✅ Wayland 표면 스왑체인 (vkCreateWaylandSurfaceKHR), 실행 중 선택 --platform auto|x11|wayland (Auto = WAYLAND_DISPLAY →
          Wayland, 안 되면 X11), IWindow::backendName
  CI   ✅ Linux 잡에 X11 · Wayland 개발 파일 · Xvfb · weston · lavapipe — 창 시험까지 화면 없이 (tools/wayland/run_headless.sh)
  [계획] Linux CI 의 dxc 고정 (지금 CI 는 셰이더 없는 빌드 — 그리기 기준 이미지는 dxc 가 있는 빌드에서만), 분수 배율,
          조합 중 글자 UI, 전체 화면
```

```text
13A 검증 (2026-10-13)
  Linux    clang Debug (커밋마다 -Werror) · gcc Debug: ctest 47/47 (render_tests_lavapipe 추가) — 셰이더(Wine dxc)를 켠 빌드의
           sbx_render_tests --rhi vulkan --warp: 28 케이스, Validation 경고 0 · 오류 = 예상 8, --gbv(동기화 검증) 0, ASan · UBSan 빌드도 0.
           unit_render 백엔드 팩토리 · unit_client --rhi
  Windows  MinGW (-D SBX_ENABLE_VULKAN=ON) 경고 0 + Wine: sbx_render_tests --rhi vulkan (winevulkan → lavapipe) · --rhi dx12
           (vkd3d) 둘 다 28 케이스, SandboxTests 348 케이스. 창 ecosystem_small --rhi vulkan: 스왑체인 FIFO 3장 · 지형 · 스프라이트 ·
           패널 · 창 크기 바꾸기(다시 만들기) · Ctrl+Q
  남음     사용자 PC (MSVC): 다시 빌드 → ctest (render_tests_warp), -D SBX_ENABLE_VULKAN=ON 으로 실제 GPU 의 --rhi vulkan ·
           MANUAL-QA Phase 13A. 지울 파일: render/stub/StubRenderDevice.cpp
  다음     13B — X11 창 · 입력 · Xlib 표면 → Linux 클라이언트
```

```text
13B 검증 (2026-10-13)
  Linux    clang · gcc Debug: ctest 49/49 (platform_x11 · client_x11_world_smoke 추가 — xvfb-run -noreset), unit_platform X11 키 표
           (모든 Key 왕복). ASan · UBSan: x11 스위트 · 클라이언트 창. Xvfb + xdotool(XTEST — 실제 입력 경로) QA: 월드 창
           (lavapipe FIFO), 휠 확대 · 가운데 끌기 · WASD · 박스 선택, ImGui 글자 칸에 입력 · xclip 붙여넣기 · 복사, F1 · G,
           창 크기 바꾸기 · unmap/map, --log-input 으로 F3 캡처(XInput2 Raw Δ) · F2 글자 · 더블클릭 · 휠 · Ctrl+Q
  Windows  MinGW (-D SBX_ENABLE_VULKAN=ON) 경고 0 (X11 키 표는 모든 OS 에서 컴파일)
  남음     사용자 Linux PC: 실제 데스크톱(창 관리자 · 한글 입력기 ibus/fcitx · 실제 GPU) MANUAL-QA Phase 13B. Windows 는 다시 빌드 → ctest
  다음     13C — Wayland(libdecor · xkbcommon · text-input-v3) + --platform + Linux CI
```

```text
13C 검증 (2026-10-13)
  Linux    clang · gcc Debug: ctest 51/51 (platform_wayland · client_wayland_world_smoke 추가 — weston headless), unit_platform
           xkb 입력 해석 · unit_client --platform. ASan · UBSan: wayland · x11 스위트 · 클라이언트 창 둘 (libdecor 플러그인 누수
           억제). Wayland 만 · X11 만 · 창 없음 구성 경고 0. Xvfb 위 weston(x11 백엔드) + xdotool QA: libdecor 틀 · 월드
           (lavapipe), 최대화 · 복원(원래 크기) · 모서리 크기 조절 · 닫기 단추, --log-input 으로 글자 · 반복 · 클립보드
           (wl-copy · wl-paste 양방향) · F3 캡처(위치 고정 · Δ) · 더블클릭 · 휠 · Ctrl+Q
  Windows  MinGW (-D SBX_ENABLE_VULKAN=ON) 경고 0 (바뀐 것은 IWindow::backendName · --platform 파싱)
  남음     사용자 Linux PC: MANUAL-QA Phase 13C (GNOME · KDE · 한글 입력기 · 배율 2). CI 첫 실행. Windows 는 다시 빌드 → ctest
  다음     Phase 14 — macOS (Cocoa · Metal)
```

### Phase 14 — macOS

```text
14.0 Slang 재평가 스파이크 (ADR-0007 재검토 여부)
14.1 CocoaWindow.mm            14.2 Metal 백엔드 (.mm)      14.3 SPIRV-Cross MSL 경로
완료  Core·Renderer 상위 diff 0줄, macOS CI 기준 이미지 통과
```

셋으로 나눈다 (ADR-0035 맥락 — 개발 · 검증 환경에 macOS 가 없다):

```text
14A 셰이더 MSL 경로 ✅ (2026-10-14, ADR-0035)
  14.3 ✅ SPIRV-Cross 1.4.304.1 벤더링(코어 · GLSL · MSL) + 호스트 도구 sbx_msl, 옵션 SBX_SHADER_MSL (macOS 기본 ON). 생성기가
          Metal 번호를 매긴다 (종류마다 (group, binding) 순, push constant = buffer 0, 정점 버퍼 = 30 - slot) →
          ReflectedBinding::metalIndex + ShaderBytecode::msl · mslEntry (MSL 3.0 소스 내장, 실행 중 컴파일은 14B)
  덤   업로드 링이 비어도 앞으로 간다 — render_tests_lavapipe 의 링 감기 시험이 부하(ctest -j8)에서 가끔 실패하던 원인
상태 표시 ◐ = 구현 완료 · macOS 검증 대기 (코드 · 빌드 연결 · 시험 · 문서 끝, macOS 실행 증거 없음 — ADR-0037 5).
CI 의 macOS 잡과 MANUAL-QA 14B · 14C 가 통과하면 ✅ 로 바꾼다.

14B Cocoa 창 ◐ (2026-10-19, ADR-0036) — 작성 · 문법 검사까지, macOS 빌드 · 실행은 검증 대기 (CI macOS 잡 · MANUAL-QA 14B)
  14.1 ◐ CocoaWindow.mm (AppKit · Objective-C++ ARC — NSWindow · flipped 뷰 + CAMetalLayer · NSTextInputClient · flagsChanged ·
          ⌘ keyUp 우회 · CGAssociate 캡처 · NSPasteboard · backingScaleFactor), MacKeyMap (kVK 표 — 모든 OS 단위 시험),
          MacPlatform ("Cocoa"), 배포 목표 macOS 13.0 (14C 에서 13.4 로), 스위트 cocoa · CTest platform_cocoa (macOS 만), 한글 UI 글꼴
          (Apple SD 산돌고딕 Neo)
14C Metal 백엔드 ◐ (2026-10-19, ADR-0037) — 작성 · 문법 검사까지, macOS 빌드 · 실행은 검증 대기 (CI macOS 잡 · MANUAL-QA 14C)
  14.2 ◐ render/metal (Objective-C++ ARC): MTLSharedEvent 타임라인 · Private/Shared 메모리 · Tracked 위험 추적(배리어 = 검사만) ·
          내장 MSL newLibraryWithSource(MSL 3.0) · 리플렉션 번호 표로 그릴 때 묶기 · push = buffer 0 setBytes · CAMetalLayer
          스왑체인(nextDrawable · 별도 명령 버퍼 presentDrawable), SBX_ENABLE_METAL (macOS 기본), 기본 백엔드 Metal,
          --rhi metal, CTest render_tests_metal (백엔드 공통 케이스 · 기준 이미지 — Metal 3 가 없으면 건너뜀)
  [계획] 타임스탬프 · 깊이 · metallib · Argument Buffer · --rhi-capture (ADR-0037 결과)
  14.0 Slang 재평가 스파이크 — Metal 출력을 기준 이미지로 견줄 수 있게 된 뒤 (macOS 검증 뒤, ADR-0007 보류 유지)
```

```text
14A 검증 (2026-10-14)
  Linux    clang Debug (-D SBX_SHADER_MSL=ON, 셰이더 = Wine dxc): ctest 51/51 × 3 (ctest -j8), 내장 셰이더 7 개 · 14 단계의 MSL
           생성 · 번호 확인. 생성기 자체 시험. gcc 13 Debug (MSL 켬) 51/51. 커밋마다 clang -Werror (MSL 켬) 51/51 ·
           MinGW 경고 0 (MSL 끔 — 교차 빌드)
  남음     macOS 에서 MSL 컴파일 · 그리기 (14C)
  다음     14B — Cocoa 창
```

```text
14B 검증 (2026-10-19)
  Linux    clang · gcc Debug: ctest 51/51 (Mac 키 표 시험 추가 — 모든 OS). CocoaWindow.mm · test_cocoa_window.mm 은 GNUstep 헤더 +
           검사용 Apple API 선언으로 clang -fsyntax-only -fobjc-arc 오류 · 경고 0 (문법 · C++ 형 · ARC 만)
  Windows  MinGW 경고 0
  미검증   macOS 빌드 · 실행 · platform_cocoa — CI 의 macOS 잡 첫 실행, Mac 이 있으면 MANUAL-QA 14B
  다음     14C — Metal 백엔드
```

```text
14C 검증 (2026-10-19)
  Linux    clang · gcc Debug: ctest 51/51 (팩토리 · --rhi 시험이 Metal 이 든 빌드도 다룬다 — 바뀐 것은 백엔드 목록뿐).
           render/metal 의 .mm 넷은 GNUstep 헤더 + 검사용 Apple Metal 선언으로 clang -fsyntax-only -fobjc-arc -Wpedantic
           오류 · 경고 0 (문법 · C++ 형 · ARC 만 — Apple API 이름 · 시그니처는 확인하지 못한다)
  Windows  MinGW 경고 0
  검증 대기 macOS 빌드 · render_tests_metal · SandboxClient 그리기 — CI 의 macOS 잡 첫 실행 (로그를 받아 고친다), Mac 이 있으면
           MANUAL-QA 14C. 통과하면 Phase 14 ✅
  다음     Phase 15 — Optimization (macOS 검증과 따로 간다)
```

### Phase 15 — Optimization

```text
14-PERFORMANCE 6장의 착수 조건을 만족한 항목만. 각 커밋에 전후 벤치 + 해시 동일성.
완료  50k: 평균 tick < 10 ms, p99 < 25 ms, 50k 가시 스프라이트 Draw ≤ 64
```

측정부터 (50k 시나리오가 없었다), 결과가 비트 단위로 같은 최적화만 (ADR-0038 — 동작을 바꾸는 것은 밸런스와 함께 따로):

```text
15.0 ✅ ecosystem_50k (448 × 448, 50,000 개체, 1,000 틱 — 10k 와 같은 밀도) · sbx_bench sim.ecosystem 이 10k · 50k 를 함께
15A 시뮬레이션 ◐ (2026-10-19) — 구현 · 2코어 측정 끝, 완료 판정은 사용자 PC 측정 대기
  ✅ 공간 색인: 위치 SoA · u32 순열 정렬 · 태그 종류(intern) · 종류별 고운 색인(4 × 4 타일, 원의 현 안 칸만 — 순서 S1 아님)
  ✅ 감지: 그래프 × 태그 종류 일치 표, 일치하는 종류만 훑는다 (늑대가 풀을 건너뛴다)
  ✅ 충돌: 후보를 항목 표에서 · 상대 반지름 = 이번 틱 최대 · 쓰기 계산 병렬. 행동: 평가 병렬 (04 7장 Worker 쓰기 조건)
  ✅ 번식: 이웃 Prefab 번호 표. 셀 계산 · 이동 비용 조회 인라인. Prefab 템플릿 (BinaryCodec 바이트, contentRevision 무효화)
15B 스냅숏 ◐ (2026-10-19) — 인코딩 공유 · 청크별 관심 · netId 기수 정렬 · mask 표 (메시지 바이트 동일)
  [계획] Net 스레드 · 메시지 아레나 (클라 16 < 3 ms), 양자화 · 필드 마스크
[계획] 색인 증분 갱신(재구성 ≈ 2 ms — 보류), 시스템 간 병렬(AccessSet), 번식 · 이동 병렬, 감지의 원 안 칸 합산
판정 남음  사용자 PC: sbx_bench --only sim.ecosystem --threads <코어 수 − 1> → 50k 평균 < 10 ms · p99 < 25 ms 면 Phase 15 ✅.
           50k 가시 스프라이트 Draw ≤ 64 는 7.6 (50k 배치 = 묶음 1) ✅
```

```text
15A · 15B 검증 (2026-10-19)
  해시     ecosystem_small · eco_lifecycle (골든) · ecosystem_10k (600 틱) · ecosystem_50k (300 틱) 50 틱마다 바꾸기 전과 같음,
           --threads 0,1,4 같음 (D5), TSan(ecosystem_10k 60 틱 · Worker 3) 경고 0. 스냅숏: 바이트 · 관심 개체 수 같음
  성능     2코어 클라우드, 같은 기계 번갈아 (14 7.12): 50k 평균 Worker 0 63.7 → 38.5 ms · Worker 1 46.6 → 29.6 ms
           (p99 81 → 53 ms), 10k 9.4 → 5.5 ms. 스냅숏 클라 16 · 월드 전체 219 → 127 ms, 화면 25 → 20 ms
  Linux    clang (-Werror) · gcc Debug ctest 51/51. Windows MinGW 경고 0
  다음     사용자 PC 측정 → 남은 직렬 몫(번식 · 재구성 · 이동)이 크면 15C, 아니면 Phase 15 ✅
```

---

## 4. 지금 착수할 티켓

```text
사용자: [1.1b] git 첫 커밋 + 원격 push → CI 첫 실행,  [1.7] windows-msvc 프리셋 빌드·ctest 1회
1. [3.1] SimulationClock, SimulationWorld::tick(), SystemScheduler(고정 Stage, StructuralLockGuard 적용)
2. [3.2] SimCommand variant + CommandQueue + 적용기
3. [3.3] SpatialIndex (정렬 배열 격자) + 속성 테스트
4. [3.4] RandomService (CounterRng)
5. [3.5] Lifecycle/Movement 최소판, EventStream
6. [3.6] WorldHash (poolsByStableId + forEachEntityByIndex + 카탈로그 해시) + kSimVersion = 1
7. [3.7~3.8] sbx_sim_check --repeat/--hash-at/--threads, 시나리오 random_walk_1k
```

## 5. 위험

| #   | 위험                                     | 가능성 | 영향 | 완화                                                                         |
|-----|------------------------------------------|--------|------|------------------------------------------------------------------------------|
| R1  | 범위: 1인 개발, 15 Phase, 3 플랫폼       | 높음   | 치명 | Phase 12를 1차 목표선으로 고정. Phase마다 동작하는 상태                      |
| R2  | 툴체인 전환 (MinGW → MSVC)               | 중     | 높음 | Phase 1에서 즉시. RTS의 MinGW 간헐 컴파일 실패도 해소                        |
| R3  | RHI 과잉/과소 추상화                     | 중     | 높음 | Vulkan을 Windows에서 일찍 붙여 인터페이스 검증 (13A — RHI 인터페이스 그대로) |
| R4  | GPU 동기화·수명 버그                     | 높음   | 중   | Debug Layer/GBV/Validation 상시, 파괴 대기열 단일화, DRED, 기준 이미지       |
| R5  | 50k 성능 미달                            | 중     | 높음 | Phase 2부터 벤치, EnTT 기준선, 착수 조건 표                                  |
| R6  | ENet 암호화 부재                         | 확정   | 중   | LAN·신뢰 환경 한정 명시. 공개 서버 전 GNS 또는 DTLS (ADR 필요)               |
| R7  | 네트워크 대역폭 (50k + 다수 클라)        | 중     | 높음 | Interest, 양자화, 우선순위·예산, 측정 후 압축                                |
| R8  | 복제 엔티티 참조 꼬임                    | 중     | 중   | 재사용 금지, tombstone, 대기 목록, 수렴 테스트                               |
| R9  | 결정론 회귀가 숨음                       | 중     | 중   | Persistent → Hashed 기본, 리플렉션 자동 해시, 골든 simVersion 불일치 시 실패 |
| R10 | 셰이더 툴체인 (MSL 변환 실패, 버전 차이) | 중     | 중   | HLSL 부분집합 규약, 버전 고정, Phase 14 전 macOS 컴파일 CI                   |
| R11 | macOS 하드웨어·CI 접근                   | 중     | 중   | Apple Silicon CI 러너, 실기 확인은 마일스톤만                                |
| R12 | Wayland 복잡도                           | 높음   | 낮음 | X11 먼저, libdecor, 대안 SDL3 백엔드 (ADR-0005 대안)                         |
| R13 | 원격 편집 체감 지연                      | 중     | 중   | EditPreview, 로컬 서버는 지연 0                                              |
| R14 | 콘텐츠 표현력 부족                       | 높음   | 중   | 요구 3건 누적 시 스크립팅 검토 (03 6.4)                                      |
| R15 | ImGui 자체 렌더러 유지비                 | 낮음   | 낮음 | 폴백: 공식 dx12 백엔드 래핑 (ADR-0008)                                       |

## 6. 범위 밖 (의도적)

```text
3D 렌더링 · 스크립팅 언어 · 클라이언트 게임플레이 예측 · 공개 인터넷 서버 운영(암호화·계정)
· 워크샵/월드 공유 플랫폼 · 무한 월드 스트리밍 · 모바일/웹
→ 설계는 막지 않는다. 필요해지면 ADR 로 범위에 넣는다.
```

## 7. 열린 질문

```text
Q1 저장소 이름·라이선스      Q2 3D 시점      Q3 계정·인증      Q4 월드 공유 방식      Q5 스트리밍 시점
```
