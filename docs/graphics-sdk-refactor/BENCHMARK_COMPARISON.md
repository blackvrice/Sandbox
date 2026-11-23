# Legacy 백엔드 vs Graphics SDK 성능 비교 (`BENCHMARK_COMPARISON.md`)

본 문서는 동일한 하드웨어(Windows 11, NVIDIA GeForce GTX 1660 SUPER, MSVC 19.44.35228.0 `RelWithDebInfo`)에서 Legacy 백엔드(`SBX_USE_GRAPHICS_SDK=OFF`)와 Graphics SDK 백엔드(`SBX_USE_GRAPHICS_SDK=ON`)의 실측 결과를 비교합니다.

---

## 1. 전체 GPU 테스트 스위트 비교 (`sbx_render_tests` 31개 케이스 · 527 assertions)

| 항목 | Legacy D3D12 (WARP) | Graphics SDK D3D12 (WARP) | Legacy D3D12 (GTX 1660 SUPER) | Graphics SDK D3D12 (GTX 1660 SUPER) | Legacy Vulkan (GTX 1660 SUPER) | Graphics SDK Vulkan (GTX 1660 SUPER) |
|---|---|---|---|---|---|---|
| **통과율** | 31/31 (527 assertions) | **31/31 (527 assertions)** | 31/31 (527 assertions) | **31/31 (527 assertions)** | 31/31 (527 assertions) | **31/31 (527 assertions)** |
| **월드 패스 GPU Total (`ms`)** | 0.099 ms | 1.0415 ms | 0.004288 ms | 0.052192 ms | 0.004736 ms | 0.015006 ms |
| **Terrain GPU (`ms`)** | 0.036 ms | 0.431 ms | 0.003904 ms | 0.023488 ms | 0.004352 ms | 0.009632 ms |
| **Grid GPU (`ms`)** | 0.0284 ms | 0.3943 ms | 0.000128 ms | 0.0176 ms | 0.000128 ms | 0.000096 ms |
| **리소스 누수 (`liveBuffers`/`Textures`)** | 0 / 0 | **0 / 0** | 0 / 0 | **0 / 0** | 0 / 0 | **0 / 0** |
| **Validation 경고 / 예상 밖 오류** | 0 / 0 (예상 8/8) | **0 / 0 (예상 8/8)** | 0 / 0 (예상 8/8) | **0 / 0 (예상 8/8)** | 0 / 0 (예상 8/8) | **0 / 0 (예상 8/8)** |

---

## 2. 전체 시스템 테스트 스위트(`ctest` 100개 테스트) 비교

| 항목 | Legacy 빌드 (`build/windows-msvc`) | Graphics SDK 빌드 (`build/windows-msvc-gfxsdk`) | 변화 |
|---|---|---|---|
| **전체 CTest 통과율** | 99/99 통과 (1 Skipped: `studio_gui_smoke`) | **99/99 통과** (1 Skipped: `studio_gui_smoke`, `render_tests_warp` 31/31 0.99 sec) | **100% 동일** |
| **단위 테스트 (`SandboxTests --test-suite=render`)** | 32 test cases 통과 | **33 test cases 통과** (`verifyAdapterTypeMappings` 추가) | **+1 테스트 추가 통과** |
| **결정론 (`det_*` 16개 테스트 골든 해시)** | 16/16 통과 | **16/16 통과** (WorldHash 100% 일치) | **결정론 100% 보존** |
| **아키텍처 경계 (`arch_*` 3개 테스트)** | 3/3 통과 | **3/3 통과** (`check_includes.py` + `SandboxServer` 링크 경계) | **경계 100% 준수** |

---

## 3. `SandboxClient` 실기 시나리오 렌더링 비교 (`1920×1080`, `--vsync off`, `--no-audio`)

### 3.1 Phase 5 완료 직후 (기능 완성 · Phase 6 최적화 전 기준선)

| 항목 | Legacy D3D12 | Legacy Vulkan | Graphics SDK D3D12 (Phase 5) | Graphics SDK Vulkan (Phase 5) | Phase 6 최적화 목표 |
|---|---|---|---|---|---|
| **`ecosystem_small` 평균 FPS** (`3000 frames`) | 2843.8 FPS | 3631.9 FPS | 1762.0 FPS (2839 InWorld frames) | 1663.7 FPS (2859 InWorld frames) | CPU 오버헤드 제거로 Legacy 동등/초과 |
| **`ecosystem_small` CPU `render`** | 0.18 ms | 0.11 ms | 0.38 ms (+0.20 ms) | 0.41 ms (+0.30 ms) | ≤ 0.18 ms / ≤ 0.11 ms |
| **`ecosystem_10k` 평균 FPS** (`2000 frames`) | 604.3 FPS | 623.7 FPS | 346.2 FPS (1537 InWorld frames) | 294.1 FPS (1579 InWorld frames) | CPU 오버헤드 제거로 Legacy 동등/초과 |
| **`ecosystem_10k` CPU `render`** | 0.48 ms | 0.40 ms | 0.81 ms (+0.33 ms) | 1.09 ms (+0.69 ms) | ≤ 0.48 ms / ≤ 0.40 ms |
| **VRAM 누수** | 0 B | 0 B | **0 B** | **0 B** | 0 B 유지 |
| **1% Low / 0.1% Low FPS** | 미측정 | 미측정 | Phase 6 벤치 하네스 추가 예정 | Phase 6 벤치 하네스 추가 예정 | 프레임 분포 수집 및 스파이크 최소화 |

### 3.2 Phase 5 병목 원인 분석 (Phase 6 개선 대상)

1. **매 드로우/프레임 임시 `gfx::ResourceSet` 생성·파괴**: `recordBindResources()`가 매 호출마다 `device->createResourceSet()`을 새로 생성하므로, BindGroup 버전/핸들 기반 캐싱으로 제거 필요 (`R0.25`).
2. **`prepareAndReplay()` 2-pass 오버헤드 및 힙 할당**: 매 프레임 `RecordedOp`, 사전 전이 버퍼 목록, 바인딩 배열의 반복적인 동적 메모리 할당을 재사용 스크래치 버퍼로 전환 필요 (`R0.25`).
3. **Upload Ring 이중 복사(`cpuShadow` → `Buffer::write`)**: `ecosystem_10k`에서 대용량 인스턴스 데이터를 매 프레임 두 번 복사하는 비용을 직접 매핑 또는 Dirty 범위 기반 부분 업로드(`write(data, size, offset)`)로 절감 필요 (`R0.27`).
4. **전용 Copy Queue 미활용**: 아틀라스/지형 초기 업로드를 `gfx::QueueType::Copy` + `TimelineFence`로 분리 가능 (`R0.26`).
