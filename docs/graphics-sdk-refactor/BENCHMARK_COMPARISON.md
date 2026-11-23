# Legacy 백엔드 vs Graphics SDK 성능 비교 (`BENCHMARK_COMPARISON.md`)

본 문서는 동일한 하드웨어(Windows 11, NVIDIA GeForce GTX 1660 SUPER, MSVC 19.44.35228.0 `RelWithDebInfo`)에서 Legacy 백엔드(`SBX_USE_GRAPHICS_SDK=OFF`)와 Graphics SDK 백엔드(`SBX_USE_GRAPHICS_SDK=ON`)의 실측 결과를 비교합니다.

---

## 1. 전체 GPU 테스트 스위트 비교 (`sbx_render_tests` 33개 케이스 · 559 assertions)

Phase 6에서 멀티스레드 병렬 커맨드 기록(`R0.25`) 및 전용 Copy Queue 전송(`R0.26`) 테스트 2종이 추가되어 총 33개 GPU 테스트 케이스로 확장되었습니다.

| 항목 | Legacy D3D12 (WARP) | Graphics SDK D3D12 (WARP) | Graphics SDK D3D12 (GTX 1660 SUPER) | Graphics SDK Vulkan (GTX 1660 SUPER + VVL) |
|---|---|---|---|---|
| **통과율** | 33/33 (559 assertions) | **33/33 (559 assertions)** | **33/33 (559 assertions)** | **33/33 (559 assertions)** |
| **다중 Queue 활성** | Graphics (단일) | **G + Copy + Compute** | **G + Copy + Compute** | **G + Copy + Compute** |
| **월드 패스 GPU Total (`ms`)** | 0.0949 ms | 0.5634 ms | 0.044992 ms | 0.015264 ms |
| **리소스 누수 (`liveBuffers`/`Textures`)** | 0 / 0 | **0 / 0** | **0 / 0** | **0 / 0** |
| **Validation 경고 / 예상 밖 오류** | 0 / 0 (예상 8/8) | **0 / 0 (예상 8/8)** | **0 / 0 (예상 8/8)** | **0 / 0 (예상 8/8)** |

---

## 2. Phase 6 핫 패스 최적화 전/후 비교 (`GraphicsSDK` 단독, 1920×1080 `--vsync off`)

- 최적화 항목: `GraphicsSdkCommandList` 풀 재사용(프레임당 힙 할당 제거), 파이프라인 지연 발행, 중복 버퍼 `transition()` 제거, `submit()` 스택 배열 배치, `allocateUpload()` 4KB 패딩 병합.

| 시나리오 / 백엔드 | Phase 5 (최적화 전) | Phase 6 (최적화 후) | 개선율 |
|---|---|---|---|
| `ecosystem_small` (D3D12) | 1762.0 FPS (`render 0.38 ms`) | **2016.2 FPS** (`render 0.32 ms`) | **+14.4% FPS / -15.8% render ms** |
| `ecosystem_10k` (Vulkan) | 294.1 FPS (`render 1.09 ms`) | **369.4 FPS** (`render 0.81 ms`) | **+25.6% FPS / -25.7% render ms** |

---

## 3. 동일 세션 Legacy vs Graphics SDK 비교 (평균 FPS · 1% Low · 0.1% Low · `render ms`)

실행 조건: Windows 11, GTX 1660 SUPER, `1920×1080`, `--vsync off --no-audio`, `RelWithDebInfo` (`ecosystem_small` 3,000f / `ecosystem_10k` 2,000f, 동일 세션 연속 실행).

| 시나리오 | 지표 | Legacy D3D12 | Graphics SDK D3D12 | Legacy Vulkan | Graphics SDK Vulkan | 비고 |
|---|---|---|---|---|---|---|
| **`ecosystem_small`** | **평균 FPS** | 1102.4 FPS | 927.8 FPS | 2528.0 FPS | 1467.6 FPS | 동일 세션 실측 |
| **`ecosystem_small`** | **1% Low FPS** | 169.7 FPS (5.89 ms) | 156.9 FPS (6.37 ms) | 712.9 FPS (1.40 ms) | 536.7 FPS (1.86 ms) | |
| **`ecosystem_small`** | **0.1% Low FPS** | 121.9 FPS (8.21 ms) | **121.0 FPS (8.27 ms)** | 382.3 FPS (2.62 ms) | **387.5 FPS (2.58 ms)** | **Vulkan 0.1% Low 우위** |
| **`ecosystem_small`** | **CPU `render`** | 0.69 ms | 0.82 ms | 0.21 ms | 0.47 ms | |
| **`ecosystem_10k`** | **평균 FPS** | 297.2 FPS | 249.0 FPS | 326.5 FPS | 246.9 FPS | ~9,500개체 |
| **`ecosystem_10k`** | **1% Low FPS** | 86.6 FPS (11.54 ms) | 81.4 FPS (12.28 ms) | 100.2 FPS (9.98 ms) | 76.9 FPS (13.01 ms) | |
| **`ecosystem_10k`** | **0.1% Low FPS** | 23.4 FPS (42.77 ms) | **23.0 FPS (43.57 ms)** | 22.3 FPS (44.77 ms) | **24.3 FPS (41.16 ms)** | **Vulkan 0.1% Low +9.0% 우위** |
| **`ecosystem_10k`** | **CPU `render`** | 0.77 ms | 1.02 ms | 0.65 ms | 1.10 ms | |
| **공통** | **VRAM 누수** | 0 B | **0 B** | 0 B | **0 B** | 전 구간 누수 0 |

---

## 4. 전체 시스템 테스트 스위트(`ctest` 100개 테스트) 비교

| 항목 | Legacy 빌드 (`build/windows-msvc`) | Graphics SDK 빌드 (`build/windows-msvc-gfxsdk`) | 변화 |
|---|---|---|---|
| **전체 CTest 통과율** | 99/99 통과 (1 Skipped: `studio_gui_smoke`) | **99/99 통과** (1 Skipped: `studio_gui_smoke`, `render_tests_warp` 33/33 포함) | **100% 동일** |
| **단위 테스트 (`SandboxTests --test-suite=render`)** | 32 test cases 통과 | **33 test cases 통과** | **+1 테스트 추가 통과** |
| **결정론 (`det_*` 16개 테스트 골든 해시)** | 16/16 통과 | **16/16 통과** (WorldHash 100% 일치) | **결정론 100% 보존** |
| **아키텍처 경계 (`arch_*` 3개 테스트)** | 3/3 통과 | **3/3 통과** | **경계 100% 준수** |
