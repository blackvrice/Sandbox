# 14. 성능 — 예산 · 벤치마크 · 오버레이

> **규범 문서.** 성능 예산, 측정 방법, 회귀 판정을 정합니다.
> 원칙: **측정 없이 최적화하지 않는다. 측정 없이 구조를 복잡하게 만들지 않는다.** 상태: 전부 `[계획]`.

---

## 1. 예산

### 1.1 Simulation (30 TPS = 33.33 ms/tick)

| 규모 단계 | 엔티티 | 개발 Phase | 평균 tick | p99 tick |
|---|---|---|---|---|
| 1단계 | 1,000 | 3 | < 2 ms | < 5 ms |
| 2단계 | 10,000 | 5 | < 10 ms | < 25 ms |
| 3단계 | 50,000 | 15 | < 10 ms | < 25 ms |

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

| 이름 | 내용 | 지표 |
|---|---|---|
| `ecs.iterate` | 1k/10k/50k, 1/2/4 컴포넌트 view | ns/entity |
| `ecs.churn` | 틱당 1% 생성·파괴 + 컴포넌트 추가·제거 | ns/op, 메모리 |
| `ecs.vs_entt` | 같은 시나리오를 EnTT로 (기준선, 벤치 전용 의존성) | 비율 |
| `sim.ecosystem` | 1k/10k/50k (Grass 70 / Rabbit 25 / Wolf 5 %) 3,000틱 | tick 평균·p95·p99·최대, System별 |
| `sim.spatial` | 50k, queryRadius 10만 회 | ns/query |
| `sim.path` | 틱당 요청 16/64/256 | Job 대기, 적용 지연 |
| `net.snapshot` | 50k, 클라 1/4/16, 카메라 이동 패턴 | bytes/s/client, 직렬화 ms |
| `render.sprites` | 1k/10k/50k | CPU ms, GPU ms, Draw 수 |
| `save.world` | 50k 저장/로드 | ms, 파일 크기 |

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

| 오버레이 | 지표 | 출처 |
|---|---|---|
| Graphics | FPS, Frame Time, CPU Render Time, GPU Frame Time, Draw Calls, Triangles, Visible / Culled Entities, Texture Memory, Buffer Memory, Upload Bytes, Frame Latency | Renderer 카운터, 타임스탬프 쿼리, D3D12MA/VMA |
| Network | Ping, Packet Loss, Bytes In / Out, Snapshot Size, Relevant Entities, Server Tick | `TransportStats`, ClientSession |
| Simulation | Tick Time(평균/p99), Entity Count, System별 실행 시간, Pathfinding Queue, Job Queue | 서버 `ServerStats` 1 Hz (Control 채널) |

```text
수집: foundation/metrics — 고정 크기 링 버퍼 카운터·히스토그램, 락 없는 단일 작성자
서버: 같은 값을 --metrics-csv <path> 로 내보내기 (장시간 테스트·벤치 분석용)
```

## 6. 최적화 착수 조건 (Phase 15 후보)

| 후보 | 착수 조건 |
|---|---|
| 병렬 SystemScheduler | 50k에서 평균 tick > 10 ms이고 단일 System이 40% 미만 (병렬 이득이 있는 분포) |
| Owning group / Archetype | 다중 컴포넌트 view가 tick의 30% 초과 |
| Spatial 증분 갱신 | 재구성 > 1 ms |
| HPA* | 경로 Job 평균 > 2 ms 또는 장거리 요청 비율 높음 |
| Render 스레드 분리 | CPU 렌더 > 6 ms |
| GPU 컬링 | 컬링 CPU > 1 ms |
| 스냅샷 필드 마스크·압축 | 클라당 > 256 KB/s |
| 변경 추적 목록화 | Replication 수집 > 1 ms |

각 최적화 커밋은 **전후 벤치 수치 + 골든 해시 동일(또는 의도된 변경)** 을 함께 제시합니다.
