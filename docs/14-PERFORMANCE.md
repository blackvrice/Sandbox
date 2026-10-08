# 14. 성능 — 예산 · 벤치마크 · 오버레이

> **규범 문서.** 성능 예산, 측정 방법, 회귀 판정을 정합니다.
> 원칙: **측정 없이 최적화하지 않는다. 측정 없이 구조를 복잡하게 만들지 않는다.** 상태: 전부 `[계획]`.

---

## 1. 예산

### 1.1 Simulation (30 TPS = 33.33 ms/tick)

| 규모 단계 | 엔티티 | 개발 Phase | 평균 tick | p99 tick |
|-----------|--------|------------|-----------|----------|
| 1단계     | 1,000  | 3          | < 2 ms    | < 5 ms   |
| 2단계     | 10,000 | 5          | < 10 ms   | < 25 ms  |
| 3단계     | 50,000 | 15         | < 10 ms   | < 25 ms  |

"일반 부하" = Ecosystem 정상 상태. "높은 부하" = 대량 생성/파괴 + 경로 요청 폭주 + 저장 동시 진행.

### 1.2 System별 가이드 (10k, 합계 10 ms 기준, 추정 — Phase 5에서 측정으로 교체)

```text
UpdateSpatialIndex 0.8 · Sensor 1.5 · Behavior 1.5 · Movement 1.5 · Interaction+Resolve 1.0
Lifecycle 0.5 · Collision 1.5 · Structural 0.5 · Replication(클라 4명) 1.0 · 기타 0.2
```

### 1.3 Rendering

```text
60 FPS (16.6 ms) 기준: CPU 렌더 < 4 ms, GPU < 10 ms (중급 GPU)
50k 가시 스프라이트: Draw ≤ 64, 인스턴스 업로드 ≈ 2.4 MB/frame
에디터 UI(ImGui) CPU < 1 ms
```

### 1.4 Network

```text
클라이언트당 송신 < 256 KB/s (15 Hz 스냅샷)
스냅샷 직렬화 (클라 16명, 50k 월드) < 3 ms/스냅샷 — Simulation 스레드 예산에 포함
Late Join: 관련 청크 64개 + 엔티티 5,000 baseline < 2초 (LAN)
```

### 1.5 기타

```text
50k 엔티티 저장 < 1 s (Worker, 틱 차단 없음), 로드 < 2 s
콘텐츠 로드(Ecosystem) < 100 ms
메모리: 50k 엔티티 Core < 200 MB (추정)
```

---

## 2. 벤치마크 시나리오 (`sbx_bench`)

| 이름                  | 내용                                                                                                                                   | 지표                                           |
|-----------------------|----------------------------------------------------------------------------------------------------------------------------------------|------------------------------------------------|
| `ecs.iterate`         | 1k/10k/50k, 1/2/4 컴포넌트 view                                                                                                        | ns/entity                                      |
| `ecs.churn`           | 틱당 1% 생성·파괴 + 컴포넌트 추가·제거                                                                                                 | ns/op, 메모리                                  |
| `ecs.vs_entt`         | 같은 시나리오를 EnTT로 (기준선, 벤치 전용 의존성)                                                                                      | 비율                                           |
| `sim.random_walk`     | 1k/10k `random_walk_*` 시나리오, 예열 30틱 후 300틱 — **Phase 3 구현**                                                                 | tick 평균·최대                                 |
| `sim.ecosystem`       | `ecosystem_10k`(192×192, 10,000 개체 시작) 예열 30틱 후 3,000틱, `--threads n` — **Phase 5C 구현** (`--quick` 은 ecosystem_small 60틱) | tick 평균·p95·p99·최대, System별, 종별 개체 수 |
| `sim.spatial`         | 50k 재구성 + queryRadius(r=4) 10만 회 — **Phase 3 구현**                                                                               | ms, ns/query                                   |
| `sim.path`            | 틱당 요청 16/64/256                                                                                                                    | Job 대기, 적용 지연                            |
| `net.snapshot`        | 50k, 클라 1/4/16, 카메라 이동 패턴                                                                                                     | bytes/s/client, 직렬화 ms                      |
| `render.sprites`      | 1k/10k/50k                                                                                                                             | CPU ms, GPU ms, Draw 수                        |
| `render.sprite_batch` | 10k/50k 스프라이트 SpriteBatcher(컬링 · 정렬 · 묶음) CPU — **Phase 8A 구현** (`--quick` 은 10k), GPU 없이 모든 OS                      | build ms, 묶음 수                              |
| `save.world`          | 50k 저장/로드 — **Phase 4 구현** (컴포넌트 5~6개, 칠한 청크 약 260개)                                                                  | ms, 파일 크기                                  |

```text
sbx_bench --scenario sim.ecosystem --entities 10000 --ticks 3000 --seed 1 --threads 8 --out result.json
sbx_bench --budget --quick           # CI: 단축판 + 예산 초과 시 실패
```

## 3. 결과 형식과 기준선

```jsonc
{ "scenario": "sim.ecosystem", "params": { "entities": 10000, "ticks": 3000, "seed": 1, "threads": 8 },
  "machine": "dev-desktop-01", "cpu": "…", "gpu": "…", "build": "RelWithDebInfo", "commit": "…",
  "metrics": { "tick.mean_ms": 0, "tick.p99_ms": 0, "system.Sensor.mean_ms": 0 } }
```

```text
- 기준선은 머신별로 bench/baselines/<machine>.json 에 커밋. 머신 간 비교는 하지 않는다.
- 회귀: 같은 머신 중앙값(5회) 기준 +10% 경고, +25% 실패.
- 측정은 RelWithDebInfo. Debug 수치는 기록하지 않는다.
- 문서에 수치를 적을 때는 (머신, 빌드, 시드, 커밋) 을 함께 적는다.
```

## 4. 프로파일링 도구

```text
CPU    Tracy (계측 매크로 SBX_PROFILE_SCOPE, 빌드 옵션으로 제거 가능), Windows: Superluminal / VTune / WPA
GPU    PIX (D3D12), RenderDoc (D3D12/Vulkan), Xcode GPU Capture (Metal), Nsight
메모리  D3D12MA / VMA 통계 덤프, 엔진 할당 카운터
네트워크 SimulatedTransport 통계, Wireshark (ENet 디섹터 없음 → 메시지 로그 옵션 --net-log)
```

## 5. 오버레이 지표

| 오버레이   | 지표                                                                                                                                                           | 출처                                          |
|------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------|
| Graphics   | FPS, Frame Time, CPU Render Time, GPU Frame Time, Draw Calls, Triangles, Visible / Culled Entities, Texture Memory, Buffer Memory, Upload Bytes, Frame Latency | Renderer 카운터, 타임스탬프 쿼리, D3D12MA/VMA |
| Network    | Ping, Packet Loss, Bytes In / Out, Snapshot Size, Relevant Entities, Server Tick                                                                               | `TransportStats`, ClientSession               |
| Simulation | Tick Time(평균/p99), Entity Count, System별 실행 시간, Pathfinding Queue, Job Queue                                                                            | 서버 `ServerStats` 1 Hz (Control 채널)        |

```text
수집: foundation/metrics — 고정 크기 링 버퍼 카운터·히스토그램, 락 없는 단일 작성자
서버: 같은 값을 --metrics-csv <path> 로 내보내기 (장시간 테스트·벤치 분석용)
```

## 6. 최적화 착수 조건 (Phase 15 후보)

| 후보                     | 착수 조건                                                                    |
|--------------------------|------------------------------------------------------------------------------|
| 병렬 SystemScheduler     | 50k에서 평균 tick > 10 ms이고 단일 System이 40% 미만 (병렬 이득이 있는 분포) |
| Owning group / Archetype | 다중 컴포넌트 view가 tick의 30% 초과                                         |
| Spatial 증분 갱신        | 재구성 > 1 ms                                                                |
| HPA*                     | 경로 Job 평균 > 2 ms 또는 장거리 요청 비율 높음                              |
| Render 스레드 분리       | CPU 렌더 > 6 ms                                                              |
| GPU 컬링                 | 컬링 CPU > 1 ms                                                              |
| 스냅샷 필드 마스크·압축  | 클라당 > 256 KB/s                                                            |
| 변경 추적 목록화         | Replication 수집 > 1 ms                                                      |

각 최적화 커밋은 **전후 벤치 수치 + 골든 해시 동일(또는 의도된 변경)** 을 함께 제시합니다.

## 7. 기록된 측정

### 7.1 Phase 2 — ECS (2026-10-05)

```text
머신   클라우드 컨테이너 (Intel Xeon @ 2.10GHz, 2 vCPU) — 사용자 PC 아님
빌드   RelWithDebInfo, Clang 19.1.1, Linux
명령   sbx_bench --machine cloud-sandbox-container --out …
기준선 bench/baselines/cloud-sandbox-container.json
```

| 시나리오    | 엔티티 | 컴포넌트 | 중앙값 (5회)    |
|-------------|--------|----------|-----------------|
| ecs.iterate | 1,000  | 1        | 1.21 ns/entity  |
| ecs.iterate | 1,000  | 2        | 5.28 ns/entity  |
| ecs.iterate | 1,000  | 4        | 8.12 ns/entity  |
| ecs.iterate | 10,000 | 1        | 1.11 ns/entity  |
| ecs.iterate | 10,000 | 2        | 5.31 ns/entity  |
| ecs.iterate | 10,000 | 4        | 8.18 ns/entity  |
| ecs.iterate | 50,000 | 1        | 1.35 ns/entity  |
| ecs.iterate | 50,000 | 2        | 5.50 ns/entity  |
| ecs.iterate | 50,000 | 4        | 10.87 ns/entity |
| ecs.churn   | 1,000  | (틱 200) | 40.0 ns/op      |
| ecs.churn   | 10,000 | (틱 200) | 51.7 ns/op      |
| ecs.churn   | 50,000 | (틱 200) | 77.9 ns/op      |

```text
해석
- 50,000 엔티티 × 컴포넌트 4개 view 한 번 ≈ 0.54 ms. 30 TPS 예산(33 ms)의 1.6%.
  System 10개가 각각 이런 순회를 해도 Phase 3 규모 목표(평균 < 10 ms) 안이다.
- 1→2 컴포넌트에서 비용이 4배: 두 번째 풀은 indexOf(sparse 페이지 + 역참조) 로 찾는다. 예상된 sparse set 비용.
  6장의 "다중 컴포넌트 view 가 tick 의 30% 초과" 조건은 Phase 5 의 실제 시스템으로 다시 잰다.
- churn 은 ECB 경유(명령당 힙 할당 포함) 기준이다. 50k 에서 1% 교체(틱당 500 생성+500 파괴) ≈ 0.2 ms.
- EnTT 기준선(ecs.vs_entt)은 아직 없다 — 외부 의존을 벤치 전용으로 받는 작업이 남아 있다.
```

### 7.2 Phase 3 — 헤드리스 시뮬레이션 (2026-10-05)

```text
머신·빌드  7.1 과 같음 (RelWithDebInfo, Clang 19.1.1, 클라우드 컨테이너)
명령       sbx_bench --machine cloud-sandbox-container        (sim.random_walk)
           sbx_sim_check --golden tests/golden/random_walk_1k.json --profile
```

| 시나리오        | 엔티티 | 평균 tick | 최대 tick | 예산 (1.1)             |
|-----------------|--------|-----------|-----------|------------------------|
| sim.random_walk | 1,000  | 0.29 ms   | 3.1 ms    | 평균 < 2 ms ✅         |
| sim.random_walk | 10,000 | 3.8 ms    | 10.1 ms   | (Phase 5 기준 < 10 ms) |

| 시나리오    | 엔티티 | 재구성 | queryRadius (r=4) |
|-------------|--------|--------|-------------------|
| sim.spatial | 50,000 | 6.8 ms | 0.85 µs/query     |

| System (1k, `--profile`)                | 평균      |
|-----------------------------------------|-----------|
| RandomWalk (공간 질의 nearest 1회/개체) | 0.21 ms   |
| Movement                                | 0.01 ms   |
| Lifecycle                               | < 0.01 ms |

```text
해석
- 1k 평균 0.3 ms 로 Phase 3 완료 기준(< 2 ms)의 15%. 남는 대부분은 RandomWalk 의 queryNearest 와
  매 틱 SpatialIndex 재구성(비교 정렬 O(N log N))이다.
- 최대 tick 은 첫 틱(1,000 개 CreateEntity 명령 적용 + JSON 해석)이다. 정상 틱의 p99 는 따로 재지 않았다
  (sbx_bench 가 p99 를 내도록 하는 것은 sim.ecosystem 과 함께 Phase 5).
- sim.spatial 50k 재구성 6.8 ms 는 05-WORLD 4.1 의 "1 ms 초과 시 바꾼다" 조건에 걸린다 → Phase 4 카운팅 정렬 (05-WORLD 4.4).
- 10k 3.8 ms 는 Phase 5 의 2단계 예산(10 ms) 안이지만 RandomWalk 는 Behavior·Sensor 가 들어오면 빠진다.
  재구성 비용은 Phase 4 의 청크 경계 + 카운팅 정렬(05-WORLD 4장)로 O(N) 이 된다.
- Debug 빌드는 1k 평균 약 1.6~1.9 ms (Clang/GCC) — 측정값으로 쓰지 않는다.
- 이 컨테이너는 같은 바이너리로도 ecs.iterate 가 실행마다 최대 2배 흔들렸다. 기준선 파일의 ECS 수치는 Phase 2 값을 유지한다.
```

### 7.3 Phase 4 — World · Save/Load (2026-10-05)

```text
머신·빌드  7.1 과 같음 (RelWithDebInfo, Clang 19.1.1, 클라우드 컨테이너)
명령       sbx_bench --machine cloud-sandbox-container
```

| 시나리오        | 규모   | 결과                                                               | 기준                       |
|-----------------|--------|--------------------------------------------------------------------|----------------------------|
| sim.spatial     | 50,000 | 재구성 1.1 ms, queryRadius(r=4) 0.37 µs                            | (Phase 3: 6.8 ms, 0.85 µs) |
| sim.random_walk | 1,000  | 평균 tick 0.18 ms, 최대 0.36 ms                                    | 평균 < 2 ms ✅             |
| sim.random_walk | 10,000 | 평균 tick 1.9 ms, 최대 6.9 ms                                      | (Phase 5 기준 < 10 ms)     |
| save.world      | 50,000 | 저장 188 ms, 로드 504 ms (entities.jsonl 15.3 MB, 청크 파일 262개) | 50k 저장 < 1 s ✅          |

```text
해석
- 공간 색인을 경계 안 격자 + 카운팅 정렬로 바꿔 50k 재구성이 6배 빨라졌고, 행 단위 연속 구간 덕에 질의도 2배 빨라졌다.
  1k/10k tick 이 Phase 3 대비 약 40~50% 줄어든 것도 대부분 이것이다.
- 로드가 저장보다 2.7배 느리다: 줄마다 JSON 파싱 + 컴포넌트마다 리플렉션 검증(타입·범위·모르는 키). 1 s 안이라 그대로 둔다.
  더 줄여야 하면 1순위는 바이너리 엔티티 포맷이다 (09 S4: JSON 은 저작·디버그용).
- 저장은 호출 스레드에서 동기다. 30 TPS 서버에서 50k 저장은 약 6 틱을 막는다 → Phase 9 의 오토세이브는
  틱 경계 메모리 스냅샷 + Worker 직렬화로 바꾼다 (09 3.4 원래 설계).
```

### 7.4 Phase 5B — 감지 · 행동 · 경로 · 충돌 (2026-10-06)

```text
머신·빌드  7.1 과 같은 종류의 클라우드 컨테이너 (2코어), RelWithDebInfo, Clang 19.1.1
명령       sbx_sim_check --scenario eco_lifecycle --profile   ·   --scenario random_walk_10k --ticks 300
```

| 시나리오                     | 규모            | 결과                                                                                                      | 기준                               |
|------------------------------|-----------------|-----------------------------------------------------------------------------------------------------------|------------------------------------|
| eco_lifecycle 1800틱         | 약 400~550 개체 | 평균 tick 0.11 ms, 최대 5.6 ms. Worker 4 개도 0.11 ms                                                     | (10k 기준은 5C 에서)               |
| eco_lifecycle System 별 평균 | 〃              | Sensor 0.026 · PathRequest 0.018 · Collision 0.016 · Behavior 0.013 · Lifecycle 0.012 · Movement 0.006 ms | —                                  |
| sim.random_walk              | 10,000          | 평균 tick 2.0 ms (Phase 4: 1.9 ms)                                                                        | 새 System 이 비용을 더하지 않음 ✅ |

```text
해석
- 새 System 은 대상 컴포넌트가 없는 엔티티를 건드리지 않는다 — random_walk_10k 는 Phase 4 와 같은 수준.
- 이 규모에서는 경로 Job 이 작아(128² 지도, 대부분 직선으로 보여 요청 자체가 드물다) Worker 를 써도 이득이 없다.
  병렬화의 이득·대기열 길이·감지 비용은 10k 생태계(5C, sbx_bench sim.ecosystem)에서 잰다.
- 최대 tick 5 ms 대의 원인은 아직 보지 않았다 (처음 몇 틱의 풀 생성·A* 작업 버퍼 할당으로 추정 — 5C 에서 p99 와 함께 확인).
```

### 7.5 Phase 5C — 생태계 10k (2026-10-06)

```text
머신·빌드  7.4 와 같음 (2코어 클라우드 컨테이너, RelWithDebInfo, Clang 19.1.1)
명령       sbx_bench --only sim.ecosystem --threads {0,1}
시나리오   ecosystem_10k: 192×192, 풀·토끼·늑대 7,000/2,500/500 시작 → 3,000틱 뒤 14,710 (12,698/1,512/500)
```

| Worker | 평균 tick | p95      | p99      | 최대    | 기준 (16 Phase 5)                     |
|--------|-----------|----------|----------|---------|---------------------------------------|
| 0      | 10.48 ms  | 13.65 ms | 15.95 ms | 27.3 ms | 평균 < 10 ms ✗(근소) · p99 < 25 ms ✅ |
| 1      | 7.83 ms   | 10.43 ms | 13.56 ms | 31.1 ms | 평균 < 10 ms ✅ · p99 < 25 ms ✅      |

System 별 평균 (Worker 1): Sensor 3.79 · Collision 1.42 · Behavior 0.75 · Lifecycle 0.69 · Movement 0.17 · PathRequest 0.06 ms
(Worker 0: Sensor 6.03 · Collision 1.93).

```text
해석·바꾼 것
- 512×512 에 10,000 을 풀면 풀이 늘어 3,000틱 뒤 27,000 이 된다 — "10k" 측정이 아니게 된다. 평형 밀도(약 0.35 개체/타일)를
  쟀고, 10,000 근처에서 머무는 192×192 로 시나리오를 정했다.
- 비용은 개체 수보다 **밀도**를 따른다: 감지 반경 안 후보 수가 밀도에 비례한다. 줄인 것 —
  (1) 색인 항목에 태그 사본 (후보마다 레지스트리 조회 없음): 감지 7.3 → 5.6 ms
  (2) flee 목표를 통행 가능한 점으로: 물 위 목표로 A* 가 확장 상한(4,096)까지 헤매던 경로 비용 4.5 → 0.08 ms
  (3) 감지·충돌 읽기 패스를 Worker 와 나눔 (ADR-0016): Worker 1 에서 평균 10.4 → 7.8 ms
- Worker 0(단일 스레드)은 평균 기준을 0.5 ms 넘는다. 기준은 Worker 를 쓰는 서버 기준으로 판정했다 (사용자 PC 는 코어가 더 많다).
  더 줄일 후보: 감지 질의를 TagMask 별 색인으로 나누기(S5), 감지 반경 축소(밸런스와 함께), 충돌 후보 범위 축소.
- 최대 tick 30 ms 안팎은 측정 구간 앞쪽의 개체 폭증(번식) 틱이다 — 원인 분석은 Phase 15 프로파일러와 함께.
```


### 7.6 Phase 8A — 스프라이트 배치 (2026-10-07)

```text
머신       클라우드 컨테이너 2코어 (Intel Xeon 2.8 GHz), Clang 19 RelWithDebInfo — 같은 머신의 상대 비교용
명령       sbx_bench --only render
시나리오   render.sprite_batch: ±100 단위에 무작위 스프라이트, 레이어 2종 · 스프라이트 3종, 카메라에 전부 보임 (컬링 0 — 최악)
```

| 스프라이트 | 비교 정렬 (첫 구현) | LSD 기수 정렬 (채택) | 묶음 | 기준 (06 8.5)                        |
|------------|---------------------|----------------------|------|--------------------------------------|
| 10,000     | 0.83 ms             | 0.44 ms              | 1    | —                                    |
| 50,000     | 4.85 ms             | 2.19 ms              | 1    | CPU 렌더 전체 ≤ 4 ms → 배치만 ≈ 55 % |

```text
해석
- 비교 정렬(키 + 제출 순서)이 50k 에서 렌더 CPU 예산 전체를 넘었다. 키가 64비트 정수이고 같은 키는 제출 순서를 지켜야
  하므로 안정 LSD 기수 정렬(바이트 8단계, 모든 키가 같은 바이트는 건너뜀 — pass · pipeline 바이트)로 바꿨다.
  무작위 3,000개에서 기준 stable_sort 와 순서가 같은지 단위 테스트가 본다.
- 남은 비용은 인스턴스 채우기와 컬링이다. GPU 비용 · 실제 fps 는 사용자 PC 에서 (MANUAL-QA 8A). Wine + lavapipe
  (소프트웨어)에서 ecosystem_10k: 12,877 스프라이트 · Draw 1 · 54 fps — 하드웨어 수치가 아니다.
```

### 7.7 Phase 8A 후속 — --direct-sim 프레임 시간 (2026-10-07)

사용자 PC(MSVC Debug, CLion cmake-build-debug)에서 `--direct-sim ecosystem_10k --vsync off` 가 3 ~ 6 fps, Release 로 바꾸면
fps 가 오른다는 보고. 원인은 그래픽이 아니라 렌더 스레드 안의 시뮬레이션 틱이었다 (ADR-0021).

```text
머신       클라우드 컨테이너 2코어. 헤드리스 = Linux clang 19 (Debug · RelWithDebInfo), 창 = MinGW + Wine 11.19 + lavapipe
           (소프트웨어 렌더 — GPU 가 아니다. 렌더 ms 의 대부분이 CPU 래스터화), 960×600
명령       SandboxClient --headless --direct-sim ecosystem_10k --frames 120        (끝 요약)
           SandboxClient --direct-sim ecosystem_10k --vsync off --rhi-fl11  40 초 (제목 줄 · 끝 요약)
```

헤드리스 (렌더 없음, 프레임 안 진행 — 바뀌기 전 경로):

| 빌드             | 틱 평균 | 프레임당 월드 진행 | 추출 (ECS → 스프라이트) | 배치 10k (render.sprite_batch) |
|------------------|---------|--------------------|-------------------------|--------------------------------|
| clang Debug      | 82.6 ms | 40.9 ms            | 4.2 ms                  | 4.9 ms                         |
| clang RelWithDeb | 14.4 ms | 7.2 ms             | 0.4 ms                  | 0.4 ms                         |

창 (Wine, ecosystem_10k, 40 초 뒤):

| 빌드          | 바뀌기 전 (렌더 스레드에서 틱, 4 틱 따라잡기) | 바뀐 뒤 (Simulation 스레드 + 스냅숏)                                   |
|---------------|-----------------------------------------------|------------------------------------------------------------------------|
| MinGW Debug   | **4 fps**, 틱 481                             | **52 fps** (평균 69.8), 시뮬레이션 9.3/30 TPS · 틱 105 ms, 추출 0.8 ms |
| MinGW Release | 60 fps, 틱 1,134                              | 57 fps (평균 68.9), 29.8/30 TPS · 틱 17.8 ms, 추출 0.3 ms              |

```text
해석
- Debug: 틱(80 ~ 100 ms) > 틱 간격(33 ms) 이라 바뀌기 전에는 프레임마다 4 틱을 몰아 돌았다 → 프레임 ≈ 틱 × 4 ≈ 250 ms.
  바뀐 뒤에는 화면이 틱을 기다리지 않는다. 시뮬레이션은 실시간의 약 1/3 로 느려지고 제목 줄 TPS 가 그것을 보인다.
- Release: 이 컨테이너는 2코어에 소프트웨어 렌더라 Simulation 스레드와 lavapipe 가 같은 코어를 나눠 써 차이가 작다.
  코어가 많고 GPU 가 있는 PC 에서는 렌더 스레드에서 틱(5 ~ 15 ms)이 빠지는 만큼 fps 가 오른다 — MANUAL-QA 8A 에서 확인.
- 추출은 틱마다 capture(Simulation 스레드) + 프레임마다 emit(배열 보간)으로 나뉘어 렌더 쪽이 4.2 → 0.8 ms (Debug).
- Debug 의 틱 자체는 그대로 느리다 (MSVC Debug 는 반복자 검사까지 있어 clang -O0 보다 더 느리다). 성능은 Release/RWD 로 잰다.
```

### 7.8 Phase 8B — 지형 · 오버레이 · 패스별 GPU 시간 (2026-10-07)

```text
머신       7.7 과 같음. GPU 수치는 Wine 11.19 + vkd3d + lavapipe(소프트웨어 래스터) — 하드웨어가 아니다, 순서 · 비율만 본다
명령       SandboxClient --direct-sim ecosystem_survival --rhi-fl11 (960×600, 제목 줄) · --headless --direct-sim ecosystem_10k
```

| 항목                                                               | 값                                                                                                                                                                   | 출처                          |
|--------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------|-------------------------------|
| ecosystem_10k 틱 (RWD, capture 포함)                               | 13.9 ~ 14.3 ms (8B 전 14.4 ms)                                                                                                                                       | 헤드리스 끝 요약, 2회         |
| emit (스냅숏 → 그릴 거리, 10k)                                     | 0.10 ms (8A 의 프레임마다 추출 0.4 ms)                                                                                                                               | 헤드리스 끝 요약              |
| 지형 capture                                                       | 처음 36 청크 복사, 이후 바뀐 청크만 (생태계는 0)                                                                                                                     | ExtractionStats.terrainCopied |
| ecosystem_survival 전체 보기 (2.1 px/칸)                           | Draw 2 · GPU 5.80 ms (지형 3.29 · 스프라이트 2.14 · 격자 0.06 · 선 0.29) · 132 fps                                                                                   | 제목 줄 (소프트웨어)          |
| 확대 8.6 px/칸 · 격자 · 21 개 선택 · 자세히                        | Draw 5 · GPU 8.36 ms (지형 4.66 · 스프라이트 0.56 · 격자 2.24 · 선 0.87) · 80 fps                                                                                    | 제목 줄 (소프트웨어)          |
| **사용자 PC** ecosystem_10k (MSVC Release, 4.3 px/칸, --vsync off) | 1,628 fps · 렌더 CPU 0.4 ms · 추출 0.1 ms · Draw 2 · GPU 0.05 ms (지형 0.04 · 스프라이트 0.01 · 격자 0.00 · 선 0.00) · 30.0/30 TPS · 틱 5.2 ms (tick 67, 개체 9,652) | 사용자 제목 줄 (2026-10-07)   |

```text
해석
- 지형은 화면 전체를 덮는 픽셀 셰이더(Load 두 번)라 소프트웨어 래스터에서는 화면 픽셀 수에 비례해 가장 비싸다. 사용자 PC 의
  실제 GPU 에서는 0.04 ms — 예상(0.1 ms 아래)대로다. 10k 스프라이트도 GPU 0.01 ms, 프레임 전체 CPU 렌더 0.4 ms 로 06 8.5 목표
  (10k 60 FPS · CPU 렌더 ≤ 4 ms)를 크게 넘는다. 8A 빌드(VSync 켬)는 144 fps — 모니터 상한이었다.
- 격자도 전체 화면 알파라 확대했을 때 비싸다(2.24 ms, 소프트웨어). 끄면 0.
- 지형 capture 는 바뀐 청크만 복사하고 바뀌지 않은 청크는 스냅숏끼리 공유해 틱 시간에 보이지 않는다.
```

### 7.9 Phase 10A — 복제 (2026-10-08)

```text
머신       클라우드 컨테이너 2코어 (7.7 과 같은 종류), clang 19 Release, ENet 127.0.0.1 실제 UDP, Worker 1
명령       SandboxServer --world ecosystem_10k --port 47800 --snapshot-kbps {0 | 256} --ticks 330 --exit
           sbx_net_probe --connect 127.0.0.1:47800 --seconds 5   (클라이언트 1, 두 번씩)
지표       서버 끝 줄 "replication X ms" (스냅숏 만들기 EMA — 2 틱마다) · probe 끝 "복제 개체 · 받은 KB"
```

| 항목                              | 제한 없음 (`--snapshot-kbps 0`) | 기본 256 KB/s                        |
|-----------------------------------|---------------------------------|--------------------------------------|
| 스냅숏 만들기 (클라이언트 1)      | 5.4 ~ 5.7 ms                    | 1.5 ms                               |
| probe 가 받은 바이트 (약 5 초)    | 15.8 ~ 16.5 MB                  | 1.25 MB (예산대로 ≈ 250 KB/s)        |
| probe 끝의 복제 개체 (서버 9,293) | 9,189 (스냅숏 사이 생사 차이)   | 8,381 (round-robin 으로 따라오는 중) |
| 틱 (ecosystem_10k)                | 7.3 ~ 7.5 ms                    | 6.4 ~ 7.0 ms                         |

```text
해석
- 10k 엔티티가 매 틱 움직이는 생태계는 거의 모두 "바뀜" 이라 제한 없음이면 스냅숏마다 200 KB 이상이다. 1.4 의 256 KB/s 상한
  안에서는 10k 원격 클라이언트가 round-robin 으로 늦게 따라온다 — Interest(Phase 11)가 보낼 집합을 화면 근처로 줄이는 것이 답.
- 만들기 비용은 보낼 바이트에 비례한다 (예산에 걸려 미룬 엔티티는 인코딩하지 않는다). 1.4 의 "클라 16 · 50k < 3 ms" 는 아직
  재지 않았다 — Interest · 바이트 재사용 뒤에 sbx_bench net.snapshot 으로 [계획].
- 틱 예산(33 ms) 안이다: Simulation 스레드 틱 7 ms 안팎 + 스냅숏 1.5 ms ÷ 2 틱.
```

### 7.10 Phase 10B — 클라이언트가 서버 복제본을 그린다 (2026-10-08)

```text
머신       7.9 와 같음 (클라우드 컨테이너 2코어). 헤드리스 = Linux clang 19, 창 = MinGW Release + Wine 11 + lavapipe
명령       SandboxClient --headless --world ecosystem_10k --frames 600 (Release) · --frames 120 (Debug)  — 서버는 프레임 안에서
           SandboxClient --world ecosystem_small (창, 같은 프로세스 서버 스레드) · SandboxServer --world ecosystem_survival +
           SandboxClient --connect 127.0.0.1:<port> (창, Wine 두 프로세스)
지표       끝 요약 "받기 · 적용 X ms" (ClientSession::update 의 지수 평균 — 프레임마다, 스냅숏은 15 Hz) · 프레임 평균의 추출
```

| 경우                                     | 개체   | 추출 (프레임마다) | 받기 · 적용 (프레임 평균 → 스냅숏 하나) | 그 밖                                  |
|------------------------------------------|--------|-------------------|-----------------------------------------|----------------------------------------|
| 헤드리스 ecosystem_10k, clang Release    | 11,571 | 1.9 ms            | 2.1 ms → 약 8 ms                        | 서버 스냅숏 만들기 7.4 ms (Worker 0)   |
| 헤드리스 ecosystem_10k, clang Debug      | 9,700  | 9.2 ms            | 11.8 ms → 약 45 ms                      | 서버 틱 126 ms · 스냅숏 만들기 44 ms   |
| 창 ecosystem_small (로컬), MinGW Release | 730    | 0.2 ms            | 0.2 ms                                  | 55 fps (VSync · 소프트웨어 렌더 11 ms) |
| 창 ecosystem_survival (원격, 256 KB/s)   | 4,292  | 1.9 ms            | 1.5 ms                                  | 41 fps · RTT 11 ms · 받은 239 KB/s     |

```text
해석
- 8A 의 --direct-sim 은 렌더 쪽 추출이 배열 보간뿐이었다 (0.3 ms, 7.7). 지금은 Main 스레드가 복제 월드를 직접 읽고
  (엔티티마다 컴포넌트 조회 둘 · 표 조회 둘 · 표본 보간) 스냅숏도 적용한다 — 10k Release 에서 추출 1.9 ms + 66 ms 마다
  적용 약 8 ms. 60 FPS 예산 안이지만 적용 프레임이 튄다. Debug 10k 는 적용 45 ms 로 끊겨 보인다.
- 줄이는 길 [계획]: Interest(Phase 11 — 화면 근처만 받는다), 디코드 · 적용을 클라이언트 Net 스레드로 (이중 버퍼, 15),
  추출 표를 엔티티 배열로.
- Wine 원격에서 다시 맞춤 2 번 — 시작 직후 클라이언트가 수 초 멈춘 사이(폰트 · 셰이더 준비) ack 가 기록 32 개를 넘었다.
  네이티브 헤드리스 원격 15 초는 0 번.
```

