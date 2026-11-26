# UI-R9 — 성능 [계획]

> 작업: [TASK_REGISTRY](../TASK_REGISTRY.md) UI-R9-01 ~ 04. 기준선: [PERFORMANCE_BASELINE](../PERFORMANCE_BASELINE.md).

- 같은 조건 (같은 장면 · 프로젝트 · 해상도 1080p · 어댑터 · 빌드 구성) 에서 ImGui 판과 GraphicsUI 판을 번갈아 잰다.
- 측정: 시작 · 프로젝트 로딩 · 평균 FPS · 1 % Low · 프레임 P50/P95/P99 · CPU · GPU · RAM · VRAM · draw call · GPU upload · 입력 지연 ·
  배치 · 바인딩 비용 · 대량 Entity · Asset 목록 · 유휴 CPU · GPU.
- 순서: 기능 정확성 → 성능. 평균 FPS 하나로 판단하지 않는다. 목표 (1080p 평균 60 · 1 % Low 50) 의 세부 값은 기준선 측정 뒤.
- 과도기 경로의 GPU 수치는 목표 렌더러와 다르다 — GPU · VRAM 비교는 R1-10 뒤 (R9-02).
