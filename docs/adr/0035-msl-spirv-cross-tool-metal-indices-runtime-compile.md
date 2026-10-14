# ADR-0035. MSL 은 벤더링한 SPIRV-Cross 호스트 도구로 만들어 소스로 내장하고, Metal 번호는 생성기가 종류마다 매긴다

- 상태: **Accepted** · 날짜: 2026-10-14
- 관련: [06-RENDERING](../06-RENDERING.md) 5.3 · 6장, [ADR-0007](0007-hlsl-shader-pipeline.md) (경로의 Metal 부분을 구체화 ·
  metallib 대신 실행 중 컴파일), [ADR-0019](0019-dxc-nuget-pin-own-spirv-reflector-root-signature-layout.md),
  [16-ROADMAP](../16-ROADMAP.md) Phase 14

## 맥락

Phase 14 = macOS (Cocoa 창 · Metal 백엔드). 개발 · 검증 환경(클라우드 Linux · 사용자 Windows PC)에는 macOS 가 없어 Metal 을
돌려 볼 수 없다. 그래서 Phase 14 를 나눈다: 14A = 여기서 검증할 수 있는 셰이더 경로(HLSL → SPIR-V → MSL)와 번호 규칙,
14B = Cocoa 창 · Metal 백엔드 (`[계획]` — Mac 또는 CI 의 macOS 잡에서 확인). ADR-0007 은 "SPIR-V → SPIRV-Cross → MSL →
xcrun metal → metallib" 만 정했다. 정할 것:

```text
1. SPIRV-Cross 를 어떻게 얻나 (버전 고정, 세 OS)
2. Metal 의 바인딩 번호 — BindGroup(그룹 0~3 · binding) 이 Metal 의 단계별 buffer · texture · sampler 표로 어떻게 가나
3. push constant · 정점 버퍼의 자리
4. MSL 을 빌드 때 metallib 로 컴파일하나, 소스로 내장해 실행 중에 컴파일하나
5. 시험 (Metal 없이)
```

## 결정

```text
1. SPIRV-Cross 1.4.304.1 (Debian orig 묶음, 서명된 .dsc 의 SHA256, 수정 없음)의 코어 · GLSL · MSL 컴파일러 소스만
   external/spirv-cross 에 넣고 정적 라이브러리 sbx_spirv_cross 로 빌드한다. 이것을 쓰는 호스트 도구
   tools/shader/sbx_msl (C++) 가 SPIR-V 한 단계 → .metal 을 만든다. 엔진 실행 파일에는 들어가지 않는다.
   옵션 SBX_SHADER_MSL (macOS + SBX_BUILD_SHADERS 면 기본 ON, 다른 OS 는 시험용으로 켠다). 교차 빌드에서는 쓸 수 없다
   (호스트 도구를 실행한다).
2. 직접 번호 (인자 버퍼 없음). 생성기(sbx_shader_gen.py)가 프로그램의 모든 단계 바인딩을 (group, binding) 순서로 늘어놓고
   종류마다 0 부터 번호를 매긴다: 버퍼(cbuffer · StructuredBuffer · RW) · 텍스처(Texture · RWTexture) · 샘플러. 번호는
   ReflectedBinding::metalIndex 로 내장 리플렉션에 들어가고, 같은 번호를 sbx_msl --bind 로 넘겨 MSL 을 만든다. sbx_msl 은 번호를
   정하지 않는다 — 셰이더가 쓰는 리소스 중 번호를 받지 못한 것이 있으면 실패한다 (SPIRV-Cross 의 자동 번호가 섞이지 않게).
   Metal 백엔드(14B)는 파이프라인의 리플렉션에서 (group, binding) → 번호 표를 만들어 BindGroup 을 그 번호로 건다.
3. push constant = buffer 0 (setVertexBytes · setFragmentBytes), 리소스 버퍼 = 1 부터, 정점 버퍼 slot s = buffer 30 - s
   (4 개까지). 그래서 리소스 버퍼는 26 개, 텍스처 31 개, 샘플러 16 개까지 (넘으면 생성기 오류). 상수는 render/rhi/
   ShaderTypes.hpp 의 kMetal* 와 생성기의 METAL_* — 같아야 한다 (단위 시험이 확인).
   MSL 3.0, platform macOS. 정점 입력은 [[stage_in]] + [[attribute(location)]] (Metal 정점 기술자의 속성 번호 = location).
4. MSL 소스를 내장한다 (ShaderBytecode::msl · mslEntry — 진입점 이름은 SPIRV-Cross 가 예약어를 피해 바꿀 수 있다). Metal
   백엔드가 newLibraryWithSource 로 실행 중에 컴파일하고 셰이더마다 한 번 캐시한다. 빌드 때 metallib 는 [계획].
5. 시험 (모든 OS, SBX_SHADER_MSL 빌드면 MSL 까지): 내장 리플렉션의 번호가 규칙대로인지, MSL 에 진입점 · 단계 한정자가 있고
   각 리소스가 "<이름> [[texture(n)]]" 처럼 그 번호로 묶였는지, push constant 가 buffer 0 인지. 생성기 자체 시험이 번호 매기기와
   한계를 본다. MSL 을 Metal 로 컴파일 · 그리기 기준 이미지는 14B (macOS).
```

## 근거

```text
- 배포판 spirv-cross 패키지는 판이 제각각이고(Ubuntu 24.04 는 2021 판) Windows 에는 없다. 소스를 고정해 넣으면 세 OS 가
  같은 MSL 을 만든다 — DXC 를 고정한 것과 같은 이유 (ADR-0019). 필요한 파일만 (약 2 MB, 컴파일 약 30 초).
- 번호를 생성기가 정하면 리플렉션(백엔드가 보는 것)과 MSL(셰이더가 보는 것)이 한 곳에서 나온다. SPIRV-Cross 의 기본
  번호(장식 순)에 맡기면 백엔드가 그 규칙을 따로 흉내 내야 한다.
- 인자 버퍼(Argument Buffer Tier 2)는 BindGroup 과 모양이 가장 닮았지만 리소스마다 useResource 와 인코더가 필요하고 디버깅이
  어렵다. 지금 셰이더의 리소스는 단계마다 셋 이하 — 직접 번호가 단순하다.
- metallib 를 빌드 때 만들려면 macOS 빌드 머신에 Xcode 전체(metal 컴파일러)가 필요하다 (Command Line Tools 에는 없다).
  소스 내장은 어느 OS 에서든 빌드되고 Linux 에서 생성 결과를 시험할 수 있다. 실행 중 컴파일 비용은 셰이더 14 개에 몇 ms 수준
  (추정 — 14B 에서 잰다)이고 한 번뿐이다.
```

## 결과

- 얻는 것: Metal 이 쓸 MSL 과 번호가 모든 OS 의 빌드에서 만들어지고 시험된다. Phase 14B 의 Metal 백엔드는 셰이더 변환 없이
  번호 표만 보고 건다. SPIRV-Cross 버전 고정.
- 포기하는 것 / [계획]: 빌드 때 MSL 컴파일 오류 잡기 (지금은 실행 중 — 14B 에 macOS CI 에서 모든 내장 MSL 을 컴파일하는 시험을
  둔다), metallib, 인자 버퍼, 14.0 Slang 재평가 스파이크 (Metal 출력을 기준 이미지로 견줄 수 있는 14B 이후로 미룬다 — ADR-0007
  의 보류 유지).
- 위험: SPIRV-Cross 가 만든 MSL 이 실제 Metal 컴파일러에서 실패할 수 있다 (HLSL 부분집합 규약 · 06 6.2 로 줄인다, 14B 에서
  확인). cbuffer 의 float3 배치는 SPIRV-Cross 가 packed 형으로 맞춘다 — 기준 이미지로 확인할 때까지 미검증.

## 대안

| 대안                                     | 기각 사유                                                                                           |
|------------------------------------------|-----------------------------------------------------------------------------------------------------|
| 배포판 · Vulkan SDK 의 spirv-cross CLI   | 판이 고정되지 않고 Windows 에서 따로 설치해야 한다                                                  |
| 실행 중에 SPIR-V → MSL (MoltenVK 방식)   | 엔진 실행 파일에 SPIRV-Cross 가 들어가고 변환 오류가 실행 중에야 보인다 (ADR-0007 의 오프라인 원칙) |
| SPIRV-Cross 의 기본 번호 + 리플렉션 따로 | 백엔드가 SPIRV-Cross 의 규칙을 흉내 내야 한다 — 어긋나도 빌드가 모른다                              |
| 인자 버퍼 (BindGroup = 인자 버퍼 하나)   | useResource · 인코딩 · 디버깅 부담 — 리소스가 많아질 때 다시 본다                                   |
| 빌드 때 metallib                         | macOS 빌드 머신에 Xcode 전체가 필요하고 Linux 에서 시험할 수 없다                                   |

## 재검토 조건

한 단계의 리소스가 직접 번호 한계(버퍼 26 · 텍스처 31 · 샘플러 16)에 가까워질 때 (인자 버퍼), 실행 중 MSL 컴파일이 시작
시간을 눈에 띄게 늘릴 때 (metallib 또는 MTLBinaryArchive), Slang 의 Metal 경로가 기준 이미지를 통과할 때 (ADR-0007).
