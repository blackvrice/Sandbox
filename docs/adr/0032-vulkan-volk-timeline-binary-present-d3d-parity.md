# ADR-0032. Vulkan 백엔드는 volk + 벤더링한 헤더로 SDK 없이 빌드하고, 큐 펜스는 타임라인 세마포어, 정렬 · 좌표는 D3D12 에 맞춘다

- 상태: **Accepted** · 날짜: 2026-10-13
- 관련: [06-RENDERING](../06-RENDERING.md) 3.2 · 5.2 · 12 · 13 · 14장, [15-BUILD](../15-BUILD.md) 3 · 5장,
  [ADR-0006](0006-thin-rhi.md), [ADR-0018](0018-rhi-frame-protocol-committed-resources-wine-testing.md),
  [ADR-0019](0019-dxc-nuget-pin-own-spirv-reflector-root-signature-layout.md),
  [ADR-0022](0022-terrain-tile-texture-overlay-passes-gpu-timestamps.md), [16-ROADMAP](../16-ROADMAP.md) Phase 13

## 맥락

Phase 13 = Linux 포트 (X11 창 → Vulkan RHI → Wayland). 로드맵은 "Windows 에서 Vulkan 백엔드를 먼저 완성" 을 사전 작업으로 둔다.
RHI(3장)와 프레임 프로토콜(ADR-0018), SPIR-V 생성(ADR-0019)은 D3D12 로 이미 굳었다 — Vulkan 은 그 계약을 그대로 지켜야 한다
(상위 코드 diff 0 줄). 클라우드 환경에는 GPU 가 없고 GitHub 에 닿지 않는다 (Debian · PyPI 만). 정할 것:

```text
1. Phase 13 을 어떻게 나누나 (창이 없는 상태에서 백엔드를 어떻게 시험하나)
2. Vulkan 로더 · 헤더를 어떻게 얻나 (SDK? 시스템 패키지? 벤더링?), Windows 에서 켜는 옵션
3. 동기화: RHI 의 큐 펜스(단조 64비트 값) · 스왑체인
4. 메모리 · 디스크립터 · 파이프라인 레이아웃
5. D3D12 와 다른 규약 (정렬, NDC Y, 버퍼 · 복사 사이 의존, -fspv-reflect 확장)
6. 장치 고르기 · 검증 레이어 · 시험 (WARP 자리)
```

## 결정

```text
1. 13A = Vulkan 백엔드 + 헤드리스 기준 이미지 시험 (lavapipe) + Windows SBX_ENABLE_VULKAN (Win32 표면 스왑체인, --rhi vulkan).
   13B = X11 창 · 입력 · Linux 클라이언트 + Xlib 표면. 13C = Wayland(libdecor) + Linux CI. 창이 없어도 백엔드 전체(스왑체인 빼고)를
   sbx_render_tests 로 검증하고, 스왑체인은 Windows(MinGW + Wine winevulkan → lavapipe)에서 먼저 돌린다.
2. Vulkan-Headers(C 헤더만) · volk 를 external/ 에 벤더링 (Debian orig 묶음, .dsc 의 SHA256 확인). volk 가 실행 중에 로더
   (vulkan-1.dll · libvulkan.so.1)를 연다 — VK_NO_PROTOTYPES, 링크 라이브러리 · Vulkan SDK 없음. 로더가 없으면 Vulkan 만
   Unsupported (프로그램은 뜬다). 옵션 SBX_ENABLE_VULKAN: Linux 기본 ON, Windows · macOS 기본 OFF (계획의
   SBX_ENABLE_VULKAN_ON_WINDOWS 를 대신한다 — 이름 하나로 모든 OS). 백엔드 선택은 rhi/RenderDeviceFactory 한 곳:
   Auto = 플랫폼 기본 (D3D12 → Vulkan), backendCompiled · parseBackend, SBX_HAS_D3D12 · SBX_HAS_VULKAN (PUBLIC 정의).
   render/stub 은 지운다.
3. 큐 펜스 = 타임라인 세마포어 하나 (submit 마다 +1 signal, wait = vkWaitSemaphores 5초마다 · 30초 포기 — D3D12 와 같다).
   스왑체인은 binary 세마포어를 큐에 맡긴다: acquire 세마포어(프레임 슬롯마다)는 큐의 다음 제출이 COLOR_ATTACHMENT_OUTPUT 에서
   기다리고, present 는 "마지막 타임라인 값을 기다려 이미지마다의 present 세마포어를 켜는" 명령 없는 제출 뒤 vkQueuePresentKHR.
   스왑체인 이미지는 "아직 전이 안 됨" 표시 — 첫 배리어의 이전 레이아웃을 UNDEFINED 로 (RHI 는 Present 에서 시작한다고 본다).
   OUT_OF_DATE · SUBOPTIMAL · VSync 바꿈은 다음 acquire 전에 (GPU · 큐 idle 뒤) 다시 만든다. 이미지 수 = max(min, frames + 1),
   VSync 켬 = FIFO, 끔 = MAILBOX → IMMEDIATE → FIFO.
4. 메모리는 리소스마다 vkAllocateMemory (D3D12 committed 와 같은 단계, VMA 는 [계획]). 버퍼는 늘 TRANSFER_SRC|DST|STORAGE
   + 쓰임새, 이미지는 늘 TRANSFER_SRC|DST + 쓰임새 (뷰는 셰이더 · 첨부 쓰임새가 있을 때만). BindGroup = VkDescriptorSet 하나
   (FREE 플래그 풀 하나), 세트 레이아웃 · 파이프라인 레이아웃은 layoutSignature(+push 크기)로 캐시해 디바이스 수명 동안 공유,
   빈 슬롯은 빈 세트 레이아웃. push constant 는 ALL_GRAPHICS 한 구간. 파이프라인 = dynamic rendering, 뷰포트 · 시저 동적.
   커맨드 리스트 = 프레임 슬롯마다 VkCommandPool (그 프레임 첫 begin 에 리셋, 같은 프레임의 begin 은 다음 버퍼), 디바이스보다
   오래 살면 디바이스 소멸자가 풀을 먼저 놓는다 (DeviceChild).
5. 규약은 D3D12 에 맞춘다:
   - DeviceCaps 정렬 = max(D3D12 값, Vulkan 값): 상수 버퍼 256, 복사 행 256, 복사 오프셋 512 — 상위 코드는 백엔드마다 다른 배치를
     만들지 않는다.
   - NDC Y: 음수 높이 뷰포트 (y = v.y + h, height = -h), frontFace 는 그대로 (CCW → COUNTER_CLOCKWISE) — 12장 기준 이미지가 같다.
   - D3D12 가 숨기는 버퍼 · 복사 의존은 보수적인 전역 메모리 배리어로: 기록 시작(이전 제출의 쓰기 → 모든 접근), 복사 뒤 다음 복사 ·
     렌더 패스 · 끝 전(복사 쓰기 → 모든 접근), 기록 끝(모든 쓰기 → HOST 읽기 — Readback). 텍스처는 RHI 배리어가 레이아웃 전이.
   - ResourceState::Undefined = "내용을 버린다" (레이아웃 UNDEFINED) — D3D12 의 COMMON 과 달리 내용이 남지 않는다.
   - 내장 SPIR-V 에서 -fspv-reflect 의 SPV_GOOGLE_* 확장 · 시맨틱 · user type 장식을 sbx_shader_gen 이 뺀다 (spirv-opt
     --strip-reflect 와 같다) — 리플렉션은 원본으로, 드라이버에는 VK_GOOGLE_* 확장이 필요 없다.
   - 타임스탬프: 프레임 슬롯마다 kMaxTimestampsPerFrame 칸의 쿼리 풀, beginFrame 이 결과를 옮긴 뒤 호스트에서 리셋
     (hostQueryReset), resolve = vkCmdCopyQueryPoolResults(64비트 · WAIT) → Readback 버퍼, 주파수 = 1e9 / timestampPeriod.
6. 장치: --rhi-warp = CPU 장치(lavapipe) 우선, 아니면 discrete → integrated → virtual → CPU. 필수 = Vulkan 1.3 +
   dynamicRendering · synchronization2 · timelineSemaphore · hostQueryReset (fillModeNonSolid 는 있으면). --rhi-debug =
   VK_LAYER_KHRONOS_validation (없으면 경고 후 계속), --rhi-gbv = + 동기화 검증. 메시지: ERROR → 오류 수, VALIDATION 종류
   WARNING → 경고 수, 나머지(성능 · 일반)는 debug 로그만. 시험: CTest render_tests_lavapipe (Linux, 드라이버 목록을 lavapipe 하나로,
   --rhi vulkan --warp --debug) — D3D12 와 같은 기준 이미지 · 같은 허용치. Linux 는 고정 버전 DXC 를 내려받지 않는다 — PATH 의
   dxc 또는 SBX_DXC 가 있으면 내장 셰이더를 켠다.
```

## 근거

```text
- 창(13B)을 기다리면 백엔드 버그와 창 버그가 섞인다. 헤드리스 렌더 시험이 백엔드의 거의 전부를 덮고, 스왑체인은 이미 있는
  Win32 창으로 시험할 수 있다 (로드맵의 "Windows 에서 먼저").
- volk: SDK 설치 · import 라이브러리 · 로더 버전 맞추기가 없다. 벤더링한 헤더가 빌드를 재현 가능하게 한다 (시스템 헤더 버전이
  배포판마다 다르다). 로더가 없는 PC 에서도 D3D12 클라이언트가 뜬다.
- 타임라인 세마포어는 D3D12 펜스와 같은 모양 — FrameContext · 업로드 링 · 지연 해제 코드가 그대로다. binary 세마포어를 큐 안에
  숨기면 ISwapChain · ICommandQueue 인터페이스를 바꾸지 않는다 (present 한 번에 빈 제출 하나 — 비용은 작다).
- dedicated 할당은 리소스 수가 적은 지금 단계에서 충분하고 (maxMemoryAllocationCount 4096 이상), D3D12 도 아직 committed 다.
- 정렬을 D3D12 이상으로 맞추면 업로드 링 · 기준 이미지 읽기 코드가 하나다. Vulkan 의 실제 요구는 모두 그 이하다.
- 전역 메모리 배리어는 단순하고 Validation(동기화 검증 포함)이 0 이다. 세밀한 버퍼 배리어(버퍼 상태 추적)는 RHI 를 바꿔야 한다.
- 장식 빼기는 바이트코드 몇 단어 — 드라이버마다 VK_GOOGLE_* 지원을 확인하는 것보다 확실하다.
- lavapipe 는 WARP 와 같은 자리 (CPU, 결정적). 같은 기준 이미지를 통과하면 "상위 코드 diff 0 줄" 의 증거다.
```

## 결과

- 얻는 것: Linux · Windows 에서 Vulkan 으로 sbx_render_tests 28 케이스 전부 (D3D12 와 같은 기준 이미지, Validation · 동기화 검증
  0), Windows `--rhi vulkan` 클라이언트 (Wine winevulkan → lavapipe 로 확인), 백엔드 팩토리 (`--rhi auto|dx12|vulkan` —
  sbx_render_tests · SandboxClient), --version 이 빌드에 든 백엔드를 모두 보인다.
- 포기하는 것 / [계획]: Linux 창 · 표면 (13B Xlib, 13C Wayland), VMA · 서브 할당, 디스크립터 프레임 링 · bindless, VkPipelineCache
  디스크 캐시, 세밀한 버퍼 배리어, 전용 전송 큐, Linux 의 고정 버전 DXC, Linux CI (13C), 깊이 첨부.
- 위험: 리소스마다 할당 — 리소스가 수천 개가 되면 maxMemoryAllocationCount 에 닿는다 (VMA 로). 전역 배리어가 실제 GPU 에서
  패스 사이 겹침을 막는다 (지금 프레임은 패스 몇 개 — 측정 후 세밀하게). 시스템 dxc 버전이 다르면 SPIR-V 가 다르다 (기준 이미지는
  같아야 한다 — 다르면 허용치가 아니라 셰이더를 본다). lavapipe 와 실제 GPU 의 차이 (MANUAL-QA 에서 실제 GPU 로).

## 대안

| 대안                                       | 기각 사유                                                                                  |
|--------------------------------------------|--------------------------------------------------------------------------------------------|
| Vulkan SDK (find_package(Vulkan))          | 개발자마다 설치 · 버전이 다르다, Windows CI 에 SDK 설치 단계, 로더 import 라이브러리 필요  |
| 시스템 헤더 + libvulkan 링크               | 배포판마다 헤더 버전이 다르다, 로더 없는 PC 에서 실행 파일이 뜨지 않는다                   |
| 프레임마다 binary 세마포어 + VkFence       | RHI 의 단조 펜스 값과 따로 놀아 FrameContext · 지연 해제를 백엔드마다 다시 짜야 한다       |
| ICommandQueue 에 세마포어 대기를 공개      | D3D12 · Metal 에는 없는 개념이 상위로 샌다 — 스왑체인과 큐가 백엔드 안에서 만나면 된다     |
| VMA 를 처음부터                            | 벤더링 · 빌드가 하나 더 (큰 C++ 헤더), D3D12MA 와 함께 Phase 8 이후 계획 그대로 미룬다    |
| VK_GOOGLE_hlsl_functionality1 확장을 켠다  | 드라이버마다 다르고 (MoltenVK 등), 리플렉션 전용 정보를 GPU 에 넘길 이유가 없다            |
| 창(13B)부터 하고 백엔드를 그 위에서        | 창 · 입력 · 표면 · 백엔드 버그가 한꺼번에 — 헤드리스 시험이 먼저 범위를 줄인다             |

## 재검토 조건

리소스 · 할당 수가 늘어 할당 수 한도나 할당 비용이 보일 때 (VMA), 실제 GPU 프로파일에서 전역 배리어가 패스 겹침을 막을 때
(버퍼 상태 추적), 컴퓨트 · 비동기 큐 (Phase 8 이후 계획), Linux 에서 고정 DXC 가 필요해질 때 (CI 재현성 — 13C),
Metal(Phase 14)에서 같은 규약이 맞지 않을 때.
