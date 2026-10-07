# ADR-0006. 얇은 RHI + Handle + Capability

- 상태: **Accepted** · 날짜: 2026-10-05
- 관련: [06-RENDERING](../06-RENDERING.md) 3장

## 맥락

DX12/Vulkan/Metal을 지원하되 상위 Renderer가 API를 직접 부르면 안 됩니다. 동시에 모든 차이를 억지로 숨기면
최소 공통분모로 떨어져 현대 GPU 기능을 잃습니다.

## 결정

```text
- 가상 인터페이스: IRenderDevice / ICommandList / ICommandQueue / ISwapChain (백엔드 클래스는 final)
- 리소스는 Handle(index+generation). 백엔드 객체 포인터를 상위에 노출하지 않는다.
- 공통 개념만 추상화: Buffer, Texture, Sampler, Shader, Pipeline, BindGroup(4개 빈도), Push Constants,
  RenderPass(동적), ResourceBarrier(상태 기반), Fence(타임라인 값), SwapChain
- 백엔드 고유 기능은 DeviceCaps + queryExtension<Ext>()
- 파괴는 지연 해제 큐 하나 (펜스 값 기준)
```

## 근거

```text
- 세 API 의 교집합이 "명시적 커맨드 리스트 + 파이프라인 객체 + 디스크립터 그룹 + 타임라인 펜스" 로 잘 맞는다
  (Vulkan 1.3 dynamic rendering 으로 RenderPass 객체 차이도 사라짐).
- 가상 호출은 배치 단위라 비용 무시 가능. Windows 에서 DX12·Vulkan 동시 빌드로 RHI 를 일찍 검증할 수 있다.
```

## 결과

- 얻는 것: 백엔드 추가가 상위 코드 무변경. Caps로 고급 기능 접근.
- 포기하는 것: 백엔드별 극한 최적화(일부는 Extension으로 가능).
- 위험: 첫 백엔드(DX12) 모양에 과적합 → Phase 13.0에서 Vulkan으로 조기 검증, 그 전까지 RHI 변경 허용.

## 대안

| 대안                                | 기각 사유                                                                                                       |
|-------------------------------------|-----------------------------------------------------------------------------------------------------------------|
| bgfx / Diligent / NVRHI / sokol_gfx | 요구사항이 자체 RHI. NVRHI는 Metal 미지원                                                                       |
| WebGPU (Dawn / wgpu-native)         | 한 API로 세 백엔드를 주지만 최신 기능 접근이 늦고 요구사항과 다름. **일정 붕괴 시 가장 현실적인 탈출구로 기록** |
| 템플릿 정적 다형성 RHI              | 다중 백엔드 동시 빌드 불가, 컴파일 시간                                                                         |

## 재검토 조건

1인 개발 일정이 R1 수준으로 무너질 때 WebGPU 전환을 ADR로 검토.
