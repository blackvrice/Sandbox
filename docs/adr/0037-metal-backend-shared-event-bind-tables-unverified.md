# ADR-0037. Metal 백엔드는 공유 이벤트 타임라인 · 그릴 때 묶는 번호 표로 만들고, macOS 검증 전까지 "구현 완료 · 검증 대기"로 둔다

- 상태: **Accepted** · 날짜: 2026-10-19
- 관련: [06-RENDERING](../06-RENDERING.md) 3.4 · 3.5 · 4 · 5.3장, [ADR-0018](0018-rhi-frame-protocol-committed-resources-wine-testing.md),
  [ADR-0032](0032-vulkan-volk-timeline-binary-present-d3d-parity.md), [ADR-0035](0035-msl-spirv-cross-tool-metal-indices-runtime-compile.md),
  [ADR-0036](0036-cocoa-window-appkit-objcxx-arc-unverified-build.md), [16-ROADMAP](../16-ROADMAP.md) Phase 14

## 맥락

14C = RHI 의 세 번째 백엔드. RHI 계약(프레임 순서 · 타임라인 펜스 · 지연 해제 · 업로드 링 · 검증 오류 수 · 기준 이미지 —
ADR-0018 · 0032)은 D3D12 · Vulkan 으로 굳었고, 셰이더는 14A 에서 MSL 소스 + Metal 번호(ADR-0035)로, 창은 14B 에서 CAMetalLayer
를 넘기는 Cocoa 창(ADR-0036)으로 준비됐다. 개발 · 검증 환경에는 macOS 가 없다 — 사용자는 "개발은 끝내고, 완료는 macOS 에서
시험한 뒤 — 그때까지 대기 상태"를 골랐다. 정할 것:

```text
1. 큐 펜스 (FenceValue 타임라인)        2. 메모리 · 리소스 · 배리어
3. 바인딩 (Metal 에는 디스크립터 세트가 없다)   4. 인코더 · 상태 · 스왑체인
5. 디버그 · 시험 · 미검증을 어떻게 다루나
```

## 결정

```text
1. 큐 = MTLCommandQueue 하나, 펜스 = MTLSharedEvent. submit 은 리스트의 명령 버퍼를 순서대로 commit 하고 마지막 버퍼가
   encodeSignalEvent(값 + 1) — D3D12 펜스 · Vulkan 타임라인과 같은 단조 값 공간. wait = waitUntilSignaledValue 5초씩, 30초면
   포기. 명령 버퍼 오류는 완료 처리기(Metal 스레드)가 shared_ptr 로 잡은 GpuErrorSink 에 남기고 stats().debugErrors 에 더한다.
2. GpuOnly = Private, Upload · Readback = Shared (영구 매핑 = contents). 텍스처는 Private 2D / 2DArray(layers > 1), 묶는 차원이
   다르면 뷰를 처음 쓸 때 만든다. 위험(hazard)은 Metal 이 추적(Tracked 기본) — barrier 는 검사(무효 핸들 · 패스 안 ·
   Undefined 로)만 하고 명령을 내지 않는다. 한 블리트 인코더 안의 순서는 약속되지 않으므로 앞 복사가 쓴 리소스를 읽는 복사는
   새 인코더에서. 지연 해제 = 마지막 강한 참조를 큐에 (Garbage — ARC). 명령 버퍼는 retainedReferences.
   RHI 계약에 Metal 의 제약을 더한다: copyBuffer 오프셋 · 크기는 4 의 배수(macOS), 인덱스 오프셋은 인덱스 크기의 배수,
   정점 stride · 오프셋은 4 의 배수, 정점 버퍼 4 개 — 어기면 검증 오류.
3. Argument Buffer 를 쓰지 않는다. BindGroup = 만들 때 풀어 둔 리소스 목록 (버퍼 + 오프셋 · 텍스처(뷰) · 샘플러).
   파이프라인 = 리플렉션의 (group, binding) → (종류, metalIndex, 단계) 표 (ADR-0035 의 프로그램 단위 번호 — 리플렉션 필수).
   그릴 때 바뀐 그룹만 표대로 setVertex*/setFragment* 한다. push constant = 128 바이트 사본 → buffer 0 setVertexBytes ·
   setFragmentBytes (앞부분만 바꾸는 의미는 D3D12 루트 상수와 같다). 정점 버퍼 slot s = buffer 30 - s.
   레이아웃은 모양(layoutSignature)만 — setBindGroup 의 호환 규칙 · 오류 수는 다른 백엔드와 같다.
4. 렌더 패스 = 렌더 인코더 하나, 복사 = 블리트 인코더. 인코더는 상태 없이 시작하므로 파이프라인 · 그룹 · push · 정점 버퍼 ·
   뷰포트 · 시저를 리스트가 기억하고 draw* 에서 바뀐 것만 낸다 (패스 밖 setPipeline 도 된다 — Vulkan 과 같은 사용).
   뷰포트는 뒤집지 않는다 (Metal = D3D 방향), CCW = 앞면은 MTLWindingCounterClockwise. 시저는 첨부 안으로 자르고, 비면
   그리지 않는다. draw 전에 파이프라인의 색 형식 = 패스의 첨부(RHI 번호 그대로), 정점 버퍼 · 그룹이 다 묶였는지 본다 —
   Metal 검증 계층은 그런 실수에서 프로세스를 멈추므로 먼저 원인 문장 + 검증 오류로. 디버그 라벨 = 패스 안은 인코더, 밖은
   명령 버퍼의 push/popDebugGroup (패스 밖에서 열어 안에서 닫으면 패스가 끝난 뒤 닫는다).
   스왑체인 = 창의 CAMetalLayer (device · pixelFormat BGRA8/BGRA8 sRGB/RGBA16F — RGBA8 은 BGRA8 로 대체 · framebufferOnly NO ·
   drawable clamp(frames + 1, 2, 3) · displaySyncEnabled = VSync). acquire = nextDrawable (1초 넘으면 건너뜀) → 외부 핸들,
   present = 새 명령 버퍼에 presentDrawable + commit (큐 순서로 앞선 그리기 뒤 — MoltenVK 와 같은 방식). drawable · 명령
   버퍼를 만드는 곳마다 @autoreleasepool (엔진 루프에는 AppKit 의 풀이 없다).
5. 디바이스 = MTLCreateSystemDefaultDevice, MTLGPUFamilyMetal3 가 아니면 Unsupported (06 2장). --rhi-debug · --rhi-gbv =
   첫 디바이스 전에 MTL_DEBUG_LAYER · MTL_SHADER_VALIDATION (Metal 이 오류면 프로세스를 멈춘다 — 조용히 넘어가지 않는다).
   --rhi-warp 는 경고 후 기본 GPU. 배포 목표는 13.4 로 올린다 (Apple libc++ 의 std::format 이 쓰는 to_chars(double) —
   13.0 이면 컴파일 오류, 14B 의 13.0 은 macOS 에서 빌드해 보지 않아 몰랐다). 셰이더 = 내장 MSL 을 newLibraryWithSource(MSL 3.0) → mslEntry. 타임스탬프 · 깊이 ·
   파이프라인 캐시 · 프로그램 캡처는 [계획] (caps.timestampQueries = false).
   시험: 백엔드 공통 sbx_render_tests 를 CTest render_tests_metal (--rhi metal --debug --skip-unsupported — 디바이스가 없으면
   77 = 건너뜀)로, 기준 이미지 · 허용치는 D3D12 · Vulkan 과 같은 것. macOS 없는 환경에서는 GNUstep 헤더 + 검사용 Metal 선언
   으로 clang -fsyntax-only -fobjc-arc (ADR-0036 과 같은 한계 — Apple API 이름 · 시그니처는 확인하지 못한다).
   상태 표시: 16-ROADMAP 의 Phase 14 = "구현 완료 · macOS 검증 대기". CI 의 macOS 잡 · MANUAL-QA 14C 가 통과하면 ✅ 로.
```

## 근거

```text
- MTLSharedEvent 는 GPU 가 올리는 64비트 값이라 RHI 의 FenceValue 와 일대일이다 — 프레임 슬롯 · 업로드 링 · 지연 해제 코드가
  다른 백엔드와 같다. 명령 버퍼마다 waitUntilCompleted 는 값 공간이 없어 "이 값까지 끝났나" 를 물을 수 없다.
- Argument Buffer(Tier 2)는 레이아웃마다 인코더 · useResource · 상주 관리가 필요하고 MSL 쪽 선언도 바뀐다 (SPIRV-Cross 의
  argument_buffers — ADR-0035 는 끈 채로 번호를 고정했다). 지금 장면의 그룹 수 · 바인딩 수(그리기마다 몇 개)에서는 직접
  setBuffer 가 단순하고 빠르다. 번호 표를 파이프라인이 갖는 것은 Metal 번호가 프로그램마다 다르기 때문이다.
- 상태를 리스트가 기억하면 상위 코드(Renderer · 패스)가 백엔드마다 호출 순서를 바꾸지 않는다 (06 12장 규칙: 차이는 백엔드
  한 곳에서만).
- Tracked 리소스는 Metal 이 인코더 사이의 의존을 맞춰 준다 — RHI 의 명시 배리어를 그대로 옮길 대상이 없다. 블리트 인코더
  안의 순서만 예외라 새 인코더로 나눈다.
- 별도 명령 버퍼의 presentDrawable 은 submit 이 이미 commit 한 뒤에도 present 할 수 있게 한다 (ISwapChain::present 는 submit 뒤).
- "구현 완료 · 검증 대기"는 사실 그대로다: 코드 · 빌드 연결 · 시험 · 문서는 끝났고, 실행 증거만 없다. ✅ 를 붙이지 않아
  다음 사람이 확인 없이 믿지 않게 한다.
```

## 결과

- 얻는 것: macOS SandboxClient 가 Metal 로 월드 · UI 를 그린다 (확인되면), 세 백엔드가 같은 RHI · 기준 이미지 · 검증 오류 수,
  `--rhi metal`, `SandboxClient --version` 의 "render Metal".
- 포기하는 것 / [계획]: GPU 타임스탬프 (MTLCounterSampleBuffer — Apple GPU 는 단계 경계 샘플링만), 깊이 첨부, 빌드 때
  metallib (지금은 실행 중 컴파일 — 시작 시간), MTLBinaryArchive 캐시, Argument Buffer · bindless, MTLCaptureManager
  (--rhi-capture), 소프트웨어 디바이스 (WARP · lavapipe 자리가 없다 — CI 는 실행기의 GPU).
- 위험: 실행해 보지 않은 코드다. Metal 의 검증 계층은 오류에서 멈추므로 첫 실행에서 드러난다 — CI 로그 · MANUAL-QA 14C 결과를
  받아 고친다. GitHub macOS 실행기(가상 머신)의 GPU 가 Metal 3 가 아니면 render_tests_metal 은 건너뛰고, dxc 가 없으면 그리기
  시험(셰이더)은 빠진다 — 그때는 Mac 에서 MANUAL-QA.

## 대안

| 대안                                          | 기각 사유                                                                     |
|-----------------------------------------------|-------------------------------------------------------------------------------|
| MoltenVK 로 Vulkan 백엔드를 그대로            | 06 의 원래 결정(네이티브 Metal), 번역 계층 · 배포물 추가, 14A MSL 경로가 헛됨 |
| Argument Buffer (Tier 2) 로 BindGroup         | 상주 관리 · MSL 선언 변경 — 지금 규모에서 이득이 없다 (재검토 조건)           |
| 명령 버퍼 waitUntilCompleted 로 펜스          | 값 공간이 없어 프레임 슬롯 · 지연 해제를 다시 짜야 한다                       |
| 빌드 때 metallib (xcrun metal)                | Xcode 도구가 필요하고 macOS 없는 생성 시험(14A)과 갈린다 — [계획]             |
| 검증 전까지 Metal 을 기본에서 빼기 (opt-in)   | CI 의 macOS 잡이 코드를 한 번도 컴파일하지 않게 된다                          |

## 재검토 조건

그리기마다 묶는 리소스가 많아져 setBuffer 비용이 보일 때 (Argument Buffer · 상주 힙), 시작 시간에서 MSL 컴파일이 보일 때
(metallib · 바이너리 아카이브), GPU 프로파일이 필요할 때 (카운터 샘플링), CI 의 macOS 잡 · Mac 이 결과를 내면 (상태를 ✅ 로).
