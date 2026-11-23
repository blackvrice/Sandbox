# ADR-0173. Graphics SDK 어댑터의 다중 큐 동기 · 제출 실패 · 대기 시간 초과

- 상태: **Accepted** · 날짜: 2026-11-23
- 관련: [ADR-0168](0168-graphics-sdk-rhi-backend.md), [ADR-0172](0172-graphics-sdk-adapter-contract-fixes.md),
  [06-RENDERING](../06-RENDERING.md) 3.4 (지연 해제)

## 맥락

ADR-0168 의 어댑터는 Copy · Compute 전용 큐 (`QueueType::Copy · Compute`) 를 열었지만 수명 관리는 Graphics 큐 타임라인 하나만 봤다.

```text
1. 지연 해제   destroy() 가 Graphics 큐의 다음 펜스로 해제를 미뤘다 — 전용 Copy 큐에 아직 그 자원을 쓰는 일이 있어도 Graphics 펜스가
               지나면 풀었다 (해제 뒤 사용)
2. 프레임 슬롯  슬롯을 다시 쓸 때 Graphics 펜스만 기다리고 명령 풀 · 업로드 링 버퍼를 다시 썼다 — 전용 큐가 같은 슬롯 프레임에
               낸 일이 끝나지 않았을 수 있다
3. 제출 실패   submit 이 실패해도 지난 펜스 값을 돌려주고 오류만 셌다 (장치 손실도 같은 길)
4. 대기        wait() 가 5 초 한 번에서 시간 초과면 그냥 돌아갔다 — 부르는 쪽이 아직 GPU 가 쓰는 자원을 재사용 · 해제했다
```

## 결정

1. **해제는 쓰는 큐가 모두 끝난 뒤.** destroy 할 때 전용 Copy · Compute 큐에 끝나지 않은 일이 있으면 그 큐의 마지막 제출 값을
   함께 적어 두고 (`SecondaryGarbage`), 그 큐들이 거기까지 끝나면 Graphics 지연 해제로 넘긴다 (beginFrame · endFrame · waitIdle).
   전용 큐가 없거나 비었으면 예전과 같다 (비용 없음).
2. **슬롯은 전용 큐도 기다린다.** 전용 큐 제출은 그 프레임 슬롯의 copyFence · computeFence 에 적고, 슬롯을 다시 쓸 때 기다린다.
3. **제출 실패.** 장치 손실이면 `markDeviceLost` (다음 프레임부터 그리지 않는다), 그 밖은 오류. 돌려주는 값은 지난 펜스 (제출되지
   않은 일은 기다릴 것이 없다).
4. **대기는 끝까지, 상한은 장치 손실.** 5 초 창으로 나눠 경고하며 계속 기다리고 합쳐 60 초면 장치 손실로 본다 (GPU 멈춤 — TDR).
   SDK 가 DeviceLost 를 주면 곧바로 장치 손실.

## 결과

- 전용 큐를 쓰는 코드가 해제 · 재사용 경합 없이 안전하다 (Sandbox 렌더러는 지금 Graphics 큐만 쓴다 — 렌더 시험의 다중 큐 시험이 쓴다).
- GPU 가 멈추면 60 초 뒤 장치 손실로 바뀌어 프레임이 멈추지 않고 렌더러가 빠진다.
