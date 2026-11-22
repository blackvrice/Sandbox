# Legacy 백엔드 vs Graphics SDK 성능 비교 (`BENCHMARK_COMPARISON.md`)

본 문서는 동일한 하드웨어(Windows 11, NVIDIA GeForce GTX 1660 SUPER, MSVC 19.44.35228.0 `RelWithDebInfo`)에서 Legacy 백엔드(`SBX_USE_GRAPHICS_SDK=OFF`)와 Graphics SDK 백엔드(`SBX_USE_GRAPHICS_SDK=ON`)의 실측 결과를 비교합니다.

---

## 1. Phase 1 RHI 코어 동작 및 GPU 테스트 비교 (`sbx_render_tests` 10개 코어 RHI 케이스)

Phase 1(`R0.3`~`R0.11`)에서 활성화된 10개 RHI 코어 테스트 케이스(Device Caps, Clear Single/MRT, Upload Orientation, Sub-region Copy, Upload Ring Wrap-around 12 Frames, Generational Handle Lifetime, Unaligned Copy Misuse, GPU Timestamps, Pipeline/BindGroup Layout Validation)의 비교입니다.

| 항목 | Legacy D3D12 (WARP) | Graphics SDK D3D12 (WARP) | Legacy D3D12 (GTX 1660 SUPER) | Graphics SDK D3D12 (GTX 1660 SUPER) | Legacy Vulkan (GTX 1660 SUPER) | Graphics SDK Vulkan (GTX 1660 SUPER) |
|---|---|---|---|---|---|---|
| **통과율** | 10/10 (101 assertions) | **10/10 (101 assertions)** | 10/10 (101 assertions) | **10/10 (101 assertions)** | 10/10 (101 assertions) | **10/10 (101 assertions)** |
| **CTest 소요 시간** | 0.43 sec | **0.42 sec** | 0.31 sec | **0.31 sec** | 0.35 sec | **0.34 sec** |
| **해제된 GPU 객체 수** | 18 | **18** | 18 | **18** | 18 | **18** |
| **리소스 누수 (`liveBuffers`/`Textures`)** | 0 / 0 | **0 / 0** | 0 / 0 | **0 / 0** | 0 / 0 | **0 / 0** |
| **미처리 지연 해제 (`pendingDestructions`)** | 0 | **0** | 0 | **0** | 0 | **0** |
| **Validation 경고 / 예상 밖 오류** | 0 / 0 | **0 / 0** | 0 / 0 | **0 / 0** | 0 / 0 | **0 / 0** |

---

## 2. 전체 시스템 테스트 스위트(`ctest` 100개 테스트) 비교

| 항목 | Legacy 빌드 (`build/windows-msvc`) | Graphics SDK 빌드 (`build/windows-msvc-gfxsdk`) | 변화 |
|---|---|---|---|
| **전체 CTest 통과율** | 99/99 통과 (1 Skipped: `studio_gui_smoke`) | **99/99 통과** (1 Skipped: `studio_gui_smoke`) | **100% 동일** |
| **단위 테스트 (`SandboxTests --test-suite=render`)** | 32 test cases 통과 | **33 test cases 통과** (`verifyAdapterTypeMappings` 추가) | **+1 테스트 추가 통과** |
| **결정론 (`det_*` 16개 테스트 골든 해시)** | 16/16 통과 | **16/16 통과** (WorldHash 100% 일치) | **결정론 100% 보존** |
| **아키텍처 경계 (`arch_*` 3개 테스트)** | 3/3 통과 | **3/3 통과** (`check_includes.py` + `SandboxServer` 링크 경계) | **경계 100% 준수** |

---

## 3. 상위 렌더러 시나리오 비교표 (Phase 4~6 완료 후 채움 예정)

| 항목 | Legacy D3D12 | Legacy Vulkan | Legacy Metal | Graphics SDK D3D12 | Graphics SDK Vulkan | Graphics SDK Metal | 변화 |
|---|---|---|---|---|---|---|---|
| `ecosystem_small` 평균 FPS | 2843.8 FPS | 3631.9 FPS | 미측정 (macOS 필요) | Phase 4 측정 예정 | Phase 4 측정 예정 | 미측정 (macOS 필요) | 대기 |
| `ecosystem_10k` 평균 FPS | 604.3 FPS | 623.7 FPS | 미측정 (macOS 필요) | Phase 4 측정 예정 | Phase 4 측정 예정 | 미측정 (macOS 필요) | 대기 |
| 1% Low FPS | 미측정 | 미측정 | 미측정 | Phase 6 측정 예정 | Phase 6 측정 예정 | 미측정 | 대기 |
| CPU Frame Time (`render`) | 0.18 ms / 0.48 ms | 0.11 ms / 0.40 ms | 미측정 | Phase 4 측정 예정 | Phase 4 측정 예정 | 미측정 | 대기 |
| GPU Frame Time (`world passes`) | 0.004288 ms | 0.004736 ms | 미측정 | Phase 4 측정 예정 | Phase 4 측정 예정 | 미측정 | 대기 |
| VRAM 누수 | 0 B | 0 B | 미측정 | **0 B (Phase 1 검증)** | **0 B (Phase 1 검증)** | 미측정 | 동일 |
| Draw Call (`10k` sprites) | 1 | 1 | 미측정 | Phase 4 측정 예정 | Phase 4 측정 예정 | 미측정 | 대기 |
