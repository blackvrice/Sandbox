# Phase 0 — 기존 Sandbox 그래픽 시스템 성능 기준선 (`BENCHMARK_BASELINE.md`)

본 문서는 Graphics SDK 리팩토링(Phase 1~10) 전후의 성능 및 안정성을 객관적으로 비교하기 위해 동일한 하드웨어·해상도·빌드 설정에서 실제 측정한 Legacy 백엔드(`render/dx12/`, `render/vulkan/`)의 기준선(Baseline)을 기록합니다.

> [!IMPORTANT]
> 모든 수치는 실제 명령을 실행하여 측정한 값만 기록했으며, 현재 환경에서 직접 측정하지 못한 항목은 정직하게 `미측정`으로 표기했습니다.

---

## 1. 측정 환경

| 항목 | 사양 / 설정 |
|---|---|
| **OS** | Windows 11 x64 |
| **GPU (Hardware)** | NVIDIA GeForce GTX 1660 SUPER (Dedicated VRAM: D3D12 5966 MB / Vulkan 6180 MB) |
| **GPU (Software)** | Microsoft Basic Render Driver (D3D12 WARP) |
| **컴파일러 / 프리셋** | MSVC 19.44.35228.0 (`Visual Studio 2022 v17.14.39`), `windows-msvc-relwithdebinfo` (`-O2 -Zi`) |
| **CMake 옵션** | `SBX_USE_GRAPHICS_SDK=OFF`, `SBX_ENABLE_D3D12=ON`, `SBX_ENABLE_VULKAN=ON`, `SBX_BUILD_BENCH=ON` |
| **해상도 / 설정** | `1920×1080` 창 (클라이언트 렌더 영역 `1920×1061`), `--vsync off --no-audio`, `framesInFlight = 2` |
| **Vulkan Validation** | Khronos Vulkan Validation Layers `1.3.290` (`VK_LAYER_KHRONOS_validation`) |

---

## 2. CPU 마이크로벤치마크 (`sbx_bench --only render`)

실행 명령:
```powershell
.\build\windows-msvc\bin\RelWithDebInfo\sbx_bench.exe --only render
```

| 벤치마크 이름 | 입력 규모 | 실측 CPU 시간 (`ms`) | Batch 수 | Draw Call 수 |
|---|---|---|---|---|
| `render.sprite_batch` (10k) | 10,000 sprites (4 pages, Z-sort + 인스턴스 조립) | **0.235 ms** | 1 | 1 |
| `render.sprite_batch` (50k) | 50,000 sprites (4 pages, Z-sort + 인스턴스 조립) | **1.218 ms** | 1 | 1 |

---

## 3. GPU RHI & 렌더 패스 테스트 기준선 (`sbx_render_tests`)

31개 GPU 테스트 케이스(527개 단언문: Phase 7A Clear/Upload/Copy/Ring/Lifetime/Misuse, Phase 7B Draw/Texture/Cull/Validation, Phase 8A SpriteBatcher/Atlas, Phase 8B Terrain/Grid/Lines/Timestamps, Phase 8C ImGui)의 실측 결과입니다.

| 백엔드 | 어댑터 | 통과 여부 | 해제된 리소스 수 | 리소스 누수 | Validation 경고/오류 | 월드 패스 GPU Total (`ms`) | Terrain GPU (`ms`) | Grid GPU (`ms`) |
|---|---|---|---|---|---|---|---|---|
| **Legacy D3D12 (WARP)** | Microsoft Basic Render Driver | **31/31 PASS** | 127 | 0 | 경고 0 / 오류 0 (예상된 사용 오류 8/8) | **0.099 ms** | 0.036 ms | 0.0284 ms |
| **Legacy D3D12 (HW)** | NVIDIA GeForce GTX 1660 SUPER | **31/31 PASS** | 127 | 0 | 경고 0 / 오류 0 (예상된 사용 오류 8/8) | **0.004288 ms** | 0.003904 ms | 0.000128 ms |
| **Legacy Vulkan (HW)** | NVIDIA GeForce GTX 1660 SUPER | **31/31 PASS** | 127 | 0 | 경고 0 / 오류 0 (예상된 사용 오류 8/8) | **0.004736 ms** | 0.004352 ms | 0.000128 ms |

---

## 4. `SandboxClient` 실기 시나리오 렌더링 기준선 (`1920×1080`, `--vsync off`)

실행 명령:
```powershell
.\build\windows-msvc\bin\RelWithDebInfo\SandboxClient.exe --rhi <dx12|vulkan> --scenario <ecosystem_small|ecosystem_10k> --width 1920 --height 1080 --vsync off --no-audio --frames <3000|2000>
```

| 시나리오 | 백엔드 | 측정 프레임 (InWorld) | 엔티티 수 | 평균 FPS | 평균 Frame Time (`ms`) | `world` CPU (`ms`) | `extract` CPU (`ms`) | `render` CPU (`ms`) |
|---|---|---|---|---|---|---|---|---|
| `ecosystem_small` | **Legacy D3D12** | 2,566 frames | 40 | **2843.8 FPS** | **0.352 ms** | 0.00 ms | 0.01 ms | **0.18 ms** |
| `ecosystem_small` | **Legacy Vulkan** | 2,519 frames | 40 | **3631.9 FPS** | **0.275 ms** | 0.00 ms | 0.01 ms | **0.11 ms** |
| `ecosystem_10k` | **Legacy D3D12** | 1,307 frames | 9,652 | **604.3 FPS** | **1.655 ms** | 0.11 ms | 0.56 ms | **0.48 ms** |
| `ecosystem_10k` | **Legacy Vulkan** | 491 frames | 9,839 | **623.7 FPS** | **1.603 ms** | 0.16 ms | 0.53 ms | **0.40 ms** |

---

## 5. 세부 성능 지표 체크리스트 (측정 / 미측정 구분)

| 지표 항목 | Legacy D3D12 (GTX 1660 SUPER) | Legacy Vulkan (GTX 1660 SUPER) | 측정 출처 / 비고 |
|---|---|---|---|
| **평균 FPS (`ecosystem_small`)** | 2843.8 FPS | 3631.9 FPS | `SandboxClient --frames 3000` 실측 |
| **평균 FPS (`ecosystem_10k`)** | 604.3 FPS | 623.7 FPS | `SandboxClient --frames 2000` 실측 |
| **1% Low FPS** | 미측정 | 미측정 | 현재 `SandboxClient` 요약 로그는 구간 평균 FPS만 출력 (Phase 6 벤치 하네스에서 프레임별 분포 수집 예정) |
| **0.1% Low FPS** | 미측정 | 미측정 | Phase 6 벤치 하네스에서 수집 예정 |
| **CPU Frame Time (`render`)** | 0.18 ms (`small`) / 0.48 ms (`10k`) | 0.11 ms (`small`) / 0.40 ms (`10k`) | `SandboxClient` 프레임 타이머 실측 |
| **GPU Frame Time (`world passes`)** | 0.004288 ms | 0.004736 ms | `sbx_render_tests` GPU Timestamp Query 실측 |
| **CPU 사용률** | 미측정 | 미측정 | OS 프로세스 카운터 미수집 |
| **RAM 사용량** | 미측정 | 미측정 | Phase 6 메모리 프로파일러에서 수집 예정 |
| **VRAM 사용량 (어댑터 가용량 / 누수)** | 가용 5966 MB / 종료 시 누수 0 B | 가용 6180 MB / 종료 시 누수 0 B | `DeviceStats` 및 Debug Layer 실측 |
| **Draw Call / Batch 수 (`10k` / `50k`)** | 1 Draw Call / 1 Batch | 1 Draw Call / 1 Batch | `sbx_bench --only render` 실측 |
| **GPU 메모리 할당 횟수 (`sbx_render_tests`)** | 127 객체 생성 · 127 해제 | 127 객체 생성 · 127 해제 | `DeviceStats::releasedObjects` 실측 |
| **리소스 생성 및 해제 시간 (`10 케이스`)** | 전체 실행 0.31 sec (`sbx_render_tests`) | 전체 실행 0.35 sec (`sbx_render_tests`) | `sbx_render_tests` 실행 시간 실측 |
| **초기 실행 시간 (디바이스+테스트 초기화)** | < 0.45 sec (WARP 포함) | < 0.40 sec | CTest `render_tests_warp` 실측 |
| **Pipeline 생성 시간** | 미측정 | 미측정 | Phase 6 파이프라인 캐시 벤치마크에서 단독 분리 측정 예정 |
| **프레임 지연 시간 (`framesInFlight`)** | 2 frames | 2 frames | `DeviceDesc::framesInFlight = 2` |
