# ADR-0019. DXC 는 NuGet 패키지로 고정해 내려받고, 리플렉션은 자체 SPIR-V 파서로 하며, D3D12 루트 시그니처는 BindGroup 슬롯마다 표 두 개로 만든다

- 상태: **Accepted** · 날짜: 2026-10-07
- 관련: [06-RENDERING](../06-RENDERING.md) 3.4·5.1·6·14장, [ADR-0007](0007-hlsl-shader-pipeline.md), [ADR-0018](0018-rhi-frame-protocol-committed-resources-wine-testing.md),
  [16-ROADMAP](../16-ROADMAP.md) Phase 7B

## 맥락

Phase 7B 는 셰이더 빌드 · 파이프라인 · BindGroup · Triangle · Texture 다. ADR-0007 은 "HLSL → DXC → DXIL / SPIR-V, SPIR-V 기준
리플렉션, 도구 버전 고정, 오프라인 컴파일" 까지 정했다. 구현하면서 정할 것이 다섯이었다.

```text
1. DXC 를 어디서 얻는가. 사용자 PC(Windows · CLion · MSVC)와 개발 세션(Linux 클라우드, GitHub 릴리스 접근 불가) 모두.
2. 리플렉션 도구. ADR-0007 의 SPIRV-Cross 는 C++ 라이브러리 + CMake 빌드 — 지금 필요한 것은 바인딩 · cbuffer 오프셋 ·
   정점 입력뿐이다.
3. 생성물의 모양. 바이트코드를 파일로 배포할지, 실행 파일에 넣을지. cbuffer 를 C++ 에서 어떻게 채우는가.
4. HLSL register ↔ BindGroup 번호 규칙과 push constant 의 자리.
5. D3D12 루트 시그니처를 무엇으로 만드는가 (리플렉션? 레이아웃?) · shader-visible 힙을 어떻게 나누는가.
```

## 결정

```text
1. DXC = NuGet 패키지 Microsoft.Direct3D.DXC, 버전 1.9.2609.5 · SHA256 을 cmake/SbxShaders.cmake 에 고정.
   Windows 호스트는 처음 구성할 때 내려받아 <저장소>/.cache/dxc/<버전>/ 에 푼다 (nupkg 약 53 MB, 프리셋끼리 공유,
   .gitignore). SBX_DXC 로 다른 dxc 를 줄 수 있다. 옵션 SBX_BUILD_SHADERS — 기본 ON(Windows) / OFF(그 밖, Vulkan 은 Phase 13).
   패키지에는 dxil.dll 이 함께 있어 DXIL 이 서명된다. 생성기가 서명(컨테이너 해시 ≠ 0)을 검사해 빌드에서 막는다.
2. 리플렉션 = tools/shader/sbx_shader_gen.py (Python 3 표준 라이브러리만). DXC -spirv -fspv-reflect 의 SPIR-V 를 직접
   읽는다 (OpName · OpMemberName · Decorate · UserSemantic · 타입). SPIRV-Cross 는 Metal(MSL 변환, Phase 14)에서만.
3. 생성물 (빌드 폴더 generated/render/generated/, 저장소에 커밋하지 않는다):
     <Pascal>Shader.hpp  namespace sbx::render::shaders::<name> — cbuffer · push 구조체(std::array<f32,N> 멤버) +
                         offsetof/sizeof static_assert, k<Name>Group/Binding 상수, kPushConstantBytes,
                         reflection() · vs() · ps() 선언
     <Pascal>Shader.cpp  DXIL · SPIR-V 바이트 배열(실행 파일에 내장)과 ShaderReflection 표
     <name>.reflect.json 사람·도구용 (06 6.3)
   내용이 같으면 파일을 다시 쓰지 않는다 (불필요한 재컴파일 방지).
4. 바인딩 규칙: register(<b|t|s|u>N, spaceG) — G = BindGroup 번호 0~3, N = 그룹 안 binding.
   한 그룹 안에서 binding 은 레지스터 종류를 가리지 않고 고유 (t0 · s1. t0 · s0 은 생성기가 "겹친다" 오류).
   push constant 는 cbuffer 하나: [[vk::push_constant]] + register(b0, space7) → D3D12 루트 상수, ≤ 128 바이트.
5. D3D12 루트 시그니처는 파이프라인의 BindGroupLayout 모양(layoutSignature) + push 크기로 만들고 디바이스가 캐시한다:
     [push 가 있으면] 루트 상수 b0 space7 → 슬롯 s 마다 CBV/SRV/UAV 표 하나 + 샘플러 표 하나 (항목마다 범위 하나,
     register = binding, space = s). 루트 시그니처 1.0 (D3D12SerializeRootSignature), ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT.
   shader-visible 힙: CBV/SRV/UAV 65,536 · Sampler 2,048 각 하나. BindGroup = 힙의 연속 구간 (RangeAllocator — first-fit +
   병합). 디스크립터는 createBindGroup 때 한 번 쓰고, destroy 는 GPU 가 다 쓴 뒤(지연 해제) 구간을 돌려준다.
   setBindGroup 은 그룹의 레이아웃 모양이 파이프라인 슬롯의 모양과 같아야 한다 (다르면 기록하지 않고 오류).
```

그 밖에 작게 정한 것:

```text
- 셰이더 모델 6.0, -HV 2021 -Zpc(column_major) -O3 -Qstrip_debug. DXIL 은 -Qstrip_reflect, SPIR-V 는 vulkan1.3 +
  -fvk-use-dx-layout (cbuffer 오프셋이 DXIL 과 같다 → 하나의 C++ 구조체가 두 백엔드에 맞는다).
- 검사는 백엔드 독립 함수(render/rhi/PipelineValidation)가 그래픽 API 를 부르기 전에 한다: 레이아웃 중복 binding,
  셰이더 바인딩 ↔ 레이아웃(그룹 · binding · 종류 · 단계), push 크기, 정점 입력 ↔ 속성(semantic), 바인드 그룹 항목 ↔
  레이아웃(정확히 한 번, 살아 있는 리소스, StorageBuffer stride). 어기면 무효 핸들 + 오류 로그 + 오류 수 (ADR-0018 과 같다).
- createShader 는 바이트코드를 복사한다. 파이프라인을 만든 뒤 셰이더는 바로 파괴해도 된다.
- CBV 오프셋은 256 의 배수 (D3D12). 정점 버퍼 stride 는 파이프라인에 있고 setVertexBuffer 는 버퍼 · 오프셋만.
- 엔진 규약 CCW = 앞면 → FrontCounterClockwise = TRUE. NDC y 위 +1 · 텍스처 원점 좌상단은 기준 이미지로 확인한다
  (coord_convention: 래스터한 사분면 == 업로드한 사분면, culling_ccw).
- 클라우드 시험: Wine 9.0(Ubuntu) 의 vkd3d 1.10 은 DXIL 을 컴파일하지 못한다 (vkd3d result -4). WineHQ wine-devel 11.19 를
  dpkg -x 로 풀어 쓰면 된다 (설치 없이, tools/wine/run.sh 의 WINE). dxc.exe 자체는 Wine 9 로도 돈다 (tools/wine/dxc.sh).
```

## 근거

```text
- NuGet 은 Microsoft 가 내는 공식 바이너리이고 버전 경로가 고정이라 해시로 묶기 쉽다. Windows SDK 의 dxc 는 SDK 버전마다
  다르고 dxil.dll 이 없는 설치도 있다. vcpkg 의 directx-dxc 는 포트 버전이 DXC 버전과 따로 움직인다. GitHub 릴리스는
  이 개발 세션에서 막혀 있지만 NuGet 은 열려 있어 같은 바이트를 양쪽에서 쓴다.
- 자체 SPIR-V 파서 · 코드 생성기는 Python 한 파일(약 650 줄), 의존성 0, 빌드 시간 0 이다. SPIRV-Cross 를 붙이면 서브모듈 + CMake 타깃 + 호스트 빌드
  (교차 빌드에서는 호스트 도구를 따로 빌드해야 한다)가 생긴다. 필요한 정보는 SPIR-V 장식에 그대로 있다.
  --self-test(손으로 만든 SPIR-V)와 CTest shader_gen_selftest 가 파서를 지킨다.
- 바이트코드 내장: 7B 의 셰이더는 엔진 내장(기본 도형 · 스프라이트 · ImGui)이라 실행 파일과 수명이 같다. 파일 배포 ·
  경로 · 버전 불일치가 없다. 콘텐츠 셰이더(모드)는 Phase 8 이후 별도 ADR.
- static_assert 구조체: cbuffer 패킹 실수(float3 뒤 float 등)가 빌드 오류가 된다 — 런타임 기준 이미지까지 가지 않는다.
- 슬롯마다 표 두 개(리소스 · 샘플러)는 D3D12 의 제약(샘플러는 따로 된 힙)을 따르는 가장 단순한 대응이다. 3.4 의 BindGroup =
  "힙의 연속 구간" 과 1:1 이라 setBindGroup 이 SetGraphicsRootDescriptorTable 두 번이다. 루트 CBV(동적 상수)는 Phase 8 의
  프레임 상수에서 측정 후.
- 힙 크기: 65,536 은 Resource Binding Tier 1 상한(1,000,000) 안쪽이며 바인드 그룹 수천 개에 충분하다. 2,048 은 샘플러 힙
  상한.
```

## 결과

- 얻는 것: `.hlsl` 하나를 고치면 바이트코드 · 리플렉션 · C++ 구조체가 함께 갱신된다. 바인딩 오류가 그리기 전에 원인 문장으로
  보고된다. 클라우드 세션에서 DXIL 그리기까지 시험한다 (Wine 11 + vkd3d + lavapipe — 기준 이미지 triangle · coord_convention ·
  culling_ccw · texture_linear).
- 포기하는 것: SPIRV-Cross 리플렉션의 넓은 범위(배열 리소스 · 바인드리스 · 특수 상수). 필요해지면 파서를 넓히거나 재검토.
  Windows 첫 구성에 인터넷이 필요하다 (오프라인이면 SBX_DXC).
- 위험: vkd3d 와 WARP 의 선형 필터 정밀도 차이 → texture_linear 는 허용 오차를 더 둔다 (6 단계 · 2%). 사용자 PC 의
  `ctest -L render`(WARP + Debug Layer)로 다시 확인한다.

## 대안

| 대안                                   | 기각 사유                                                                  |
|----------------------------------------|----------------------------------------------------------------------------|
| Windows SDK 의 dxc                     | SDK 버전마다 다르다, dxil.dll 이 없는 설치가 있다, Linux 교차 빌드에 없다  |
| vcpkg directx-dxc                      | 저장소가 vcpkg 를 쓰지 않는다. 버전 고정이 포트 버전에 묶인다              |
| SPIRV-Cross / spirv-reflect 리플렉션   | 서브모듈 + 호스트 빌드 부담. 지금 필요한 정보는 SPIR-V 장식에 다 있다      |
| DXIL 리플렉션 (ID3D12ShaderReflection) | Windows 전용 · 빌드 호스트에 dxcompiler 를 C++ 로 링크해야 한다            |
| 바이트코드를 파일로 배포               | 내장 셰이더에는 이득이 없고 경로 · 버전 불일치만 생긴다                    |
| 루트 시그니처를 리플렉션에서 직접      | 같은 레이아웃의 파이프라인끼리 바인드 그룹을 공유하지 못한다 (모양이 기준) |
| 루트 시그니처 1.1 (정적 디스크립터)    | 1.0 으로 충분하고 MinGW 헤더 · 오래된 런타임과 맞는다. 성능 측정 후 재검토 |

## 재검토 조건

DXC 업그레이드(단독 커밋 + 기준 이미지 재확인), 콘텐츠 · 모드 셰이더, 바인드리스(Caps), Phase 13 Vulkan(같은 리플렉션으로
VkDescriptorSetLayout), Phase 14 Metal(SPIRV-Cross MSL), 디스크립터 힙 부족 경고가 실제로 나올 때.
