# ADR-0018. RHI 는 beginFrame/endFrame 으로 프레임 슬롯을 돌리고, D3D12 첫 구현은 committed 리소스로 하며, 클라우드에서는 Wine + vkd3d 로 시험한다

- 상태: **Accepted** · 날짜: 2026-10-06
- 관련: [06-RENDERING](../06-RENDERING.md) 3·4·5.1·14장, [ADR-0006](0006-thin-rhi.md), [16-ROADMAP](../16-ROADMAP.md) Phase 7

## 맥락

Phase 7 의 티켓은 7개라 Phase 5 처럼 나눈다: **7A** RHI 골격 · D3D12 디바이스 · Clear · 프레임 자원 · 기준 이미지 틀 →
**7B** 셰이더 빌드 · 파이프라인 · Triangle · Texture. 7A 에서 정할 것이 다섯이었다.

```text
1. 프레임 경계: 06 의 IRenderDevice 초안에는 beginFrame/endFrame 이 없다. 그런데 FrameContext(할당자·업로드 링 구간·
   지연 해제)는 "어느 프레임 슬롯인가" 를 디바이스가 알아야 돌릴 수 있다.
2. 메모리: 06 은 D3D12MA 다. 7A 의 리소스는 렌더 타깃 몇 개와 복사용 버퍼뿐이다.
3. 기준 이미지 읽기: 06 의 ICommandList 초안에는 텍스처 → 버퍼 복사가 없고, 매핑은 mapUploadBuffer 하나다.
4. 지연 해제의 펜스 값: "마지막 제출 값" 으로 기록하면, 기록만 하고 아직 제출하지 않은 리스트가 쓰는 리소스를
   그 앞 제출이 끝나자마자 해제한다.
5. 검증: 이 프로젝트의 개발 세션은 Linux 클라우드다. D3D12 는 Windows 에만 있다.
```

## 결정

```text
1. IRenderDevice::beginFrame() / endFrame(). beginFrame 이 프레임 번호를 올리고, 그 슬롯(번호 % framesInFlight)의
   마지막 펜스를 기다린 뒤 업로드 링을 반납하고 지연 해제 큐를 비운다. endFrame 이 이번 프레임에 제출한 마지막 펜스를
   슬롯에 적는다. 커맨드 리스트는 슬롯마다 할당자 하나를 갖고, 슬롯 할당자는 프레임마다 한 번만 Reset 한다.
2. D3D12 첫 구현은 CreateCommittedResource. D3D12MA 는 리소스 수·크기가 늘어나는 Phase 8(텍스처·아틀라스·청크 메시)에서
   넣는다. RTV 는 CPU 힙(1024) + 자리 목록. shader-visible 힙은 7B.
3. RHI 추가: ICommandList::copyTextureToBuffer, IRenderDevice::map(Upload·Readback 영구 매핑), allocateUpload(링 구간),
   destroy 의 타입별 오버로드, alive · textureDesc · stats(DeviceStats). 버퍼↔텍스처 복사 배치(행 256·오프셋 512)는
   caps 로 알리고, 어기면 기록하지 않고 오류를 센다 (Debug Layer 가 없어도 잡힌다).
4. destroy 는 "다음 제출 값"(마지막 제출 + 1)을 기록한다. 제출이 더 없으면 waitIdle 이 신호 하나를 올려 비운다.
5. 클라우드 시험: MinGW 교차 빌드 → Wine 9 의 d3d12(vkd3d) → lavapipe(Mesa 소프트웨어 Vulkan) + Xvfb.
   lavapipe 의 viewportSubPixelBits(=0)를 8 로 올리는 시험 전용 Vulkan 레이어를 쓴다 (tools/wine/ — 빌드에 들어가지 않는다). vkd3d 는 타일드
   리소스가 없어 FL 12_0 을 못 하므로 DeviceDesc.allowFeatureLevel11 (sbx_render_tests --fl11, SandboxClient --rhi-fl11)
   로 11_x 를 허용한다. 제품 기본은 그대로 FL 12_0 이상.
   기준 이미지는 이 경로로 만들었다 — Clear · 업로드 · 복사는 비트 단위로 같아야 하는 결과라 GPU 와 무관하다.
```

그 밖에 작게 정한 것:

```text
- WRL ComPtr 대신 render/dx12 의 최소 Com<T> (MSVC · MinGW 같은 동작).
- Debug Layer 메시지는 ID3D12InfoQueue 를 프레임마다 비워 센다 (경고·오류만 저장). RegisterMessageCallback(InfoQueue1)은
  Windows 11 + 새 SDK 에만 있어 쓰지 않는다. RHI 사용 오류(잘못된 정렬·무효 핸들)도 같은 오류 수에 더한다.
- DRED: Debug 빌드와 --rhi-debug 에서 자동 브레드크럼·페이지 폴트 켬. 디바이스 제거는 원인을 한 번 로그.
- PIX 마커: BeginEvent(metadata 0, UTF-16) — WinPixEventRuntime 없이.
- 스왑체인: FLIP_DISCARD, 버퍼 = framesInFlight + 1, BGRA8Unorm, ALLOW_TEARING 지원 시 VSync 끔에서 사용, Alt+Enter 끔.
  최소화(크기 0)면 acquire 가 skip. resize 는 waitIdle 후 ResizeBuffers.
- 텍스처는 COMMON 으로 만들고 Undefined = COMMON 으로 매핑한다 (첫 배리어가 Undefined → X).
- 기준 이미지 테스트는 sbx_render_tests (doctest + 자체 옵션). 공유 디바이스로 돌고 끝에 누수 0 · Debug 경고 0 ·
  예상한 사용 오류 수를 검사한다. 실패하면 out/<name>.actual.png · diff.png.
- 클라이언트: Application 은 IFrameRenderer 만 안다 (가짜로 단위 테스트). ClearRenderer 는 실제 시간으로 색을 천천히 바꿔
  화면이 GPU 를 거쳐 갱신되는지 눈으로 보이게 한다. 렌더러가 있으면 기본 페이싱은 VSync.
```

## 근거

```text
- 프레임 슬롯은 세 API 공통의 모양이다 (Vulkan 은 프레임별 커맨드 풀·디스크립터 풀, Metal 은 세마포어 N). 디바이스가
  슬롯을 돌려야 지연 해제·업로드 링·할당자가 같은 펜스로 묶인다 (06 4.1 의 "프레임·업로드·파괴가 같은 값 공간").
- committed 리소스는 정확하고 단순하다. 7A 의 리소스 수(수십)에서는 할당기 이점이 없다 — D3D12MA 의 이득은 많은 작은
  텍스처·버퍼에서 나온다.
- "다음 제출 값" 은 최대 한 프레임 늦게 해제할 뿐이고, 기록 중 파괴(에디터의 실행 취소 등)에서 GPU 가 해제된 메모리를
  읽는 일을 막는다. 단위 테스트(lifetime)가 기록 → 파괴 → 제출 순서를 시험한다.
- Wine 경로로 이 세션에서 D3D12 코드가 실제로 돈다: 디바이스·큐·펜스·커맨드 리스트·배리어·Clear·MRT·업로드 링 감기·
  버퍼↔텍스처 복사·지연 해제·스왑체인 생성·리사이즈·Present. 사용자 PC 검증 전에 오류 대부분을 잡는다.
```

## 결과

- 얻는 것: 7B 가 셰이더·파이프라인만 더하면 된다. 클라우드 세션에서 GPU 코드 회귀를 잡는다.
- 포기하는 것: 메모리 할당기(Phase 8 로). Wine 은 Debug Layer · DPI · 실제 VSync 를 흉내 내지 못한다.
- 위험: vkd3d 의 동작이 Microsoft 런타임과 다를 수 있다 → 기준 이미지 테스트를 사용자 PC 의 WARP(`ctest -L render`)로
  다시 확인한다. FL 11 허용이 제품 경로로 새지 않게 기본값은 false 이고 옵션 이름에 시험용이라 적는다.

## 대안

| 대안                                           | 기각 사유                                                      |
|------------------------------------------------|----------------------------------------------------------------|
| 프레임 경계를 스왑체인 acquire/present 에 묶기 | 오프스크린(기준 이미지·썸네일·헤드리스 렌더)에 스왑체인이 없다 |
| 처음부터 D3D12MA                               | 7A 에 쓸 곳이 없고 MinGW 교차 빌드 호환을 따로 확인해야 한다   |
| destroy 에 "마지막 제출 값"                    | 기록 중 파괴에서 사용 후 해제 (GPU 페이지 폴트)                |
| 클라우드에서는 컴파일만                        | 펜스·배리어·복사 배치 실수는 실행해야 보인다                   |
| vkd3d-proton (DXVK 계열)                       | 빌드·설치 부담. Wine 내장 vkd3d + 레이어 하나로 충분했다       |

## 재검토 조건

Phase 7B(shader-visible 힙·루트 시그니처), Phase 8(D3D12MA·Copy 큐·업로드 예산), Phase 13 Vulkan 백엔드(같은 프레임 프로토콜이
맞는지), Render 스레드 분리.
