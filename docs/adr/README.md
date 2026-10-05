# 결정 기록 (ADR)

"왜 이렇게 했지?"에 답하는 기록입니다.

## 규칙

```text
1. 결정을 내리거나 뒤집으면 새 ADR 을 추가한다. 기존 ADR 은 수정하지 않는다 (오타 제외).
2. 뒤집힌 ADR 은 상태만 "Superseded by ADR-NNNN" 으로 바꾼다.
3. 형식: 맥락 → 결정 → 근거 → 결과(얻는 것 / 포기하는 것 / 위험) → 대안 → 재검토 조건
4. 파일명: NNNN-kebab-case-title.md
```

## 목록

| # | 제목 | 상태 | 날짜 |
|---|---|---|---|
| [0001](0001-new-repository-rewrite.md) | RTS를 리팩터링하지 않고 새 저장소에서 재작성한다 | Accepted | 2026-10-05 |
| [0002](0002-custom-sparse-set-ecs.md) | 자체 Sparse Set ECS를 쓴다 (RTS ADR-0003 반전) | Accepted | 2026-10-05 |
| [0003](0003-server-authoritative.md) | Server Authoritative + Delta Snapshot, 싱글플레이도 Loopback | Accepted | 2026-10-05 |
| [0004](0004-determinism-scope.md) | 결정론은 같은 바이너리 안에서만, 시뮬레이션은 float | Accepted | 2026-10-05 |
| [0005](0005-dependency-boundaries.md) | 타깃 의존 경계, Render는 Core를 모른다, 네이티브 플랫폼 계층 | Accepted | 2026-10-05 |
| [0006](0006-thin-rhi.md) | 얇은 RHI + Handle + Capability | Accepted | 2026-10-05 |
| [0007](0007-hlsl-shader-pipeline.md) | HLSL 단일 소스 + DXC + SPIRV-Cross, 오프라인 컴파일 | Accepted | 2026-10-05 |
| [0008](0008-imgui-on-rhi.md) | ImGui는 RHI 위 자체 렌더러로 통합 | Accepted | 2026-10-05 |
| [0009](0009-msvc-toolchain.md) | Windows 툴체인을 MSVC로 | Accepted | 2026-10-05 |
| [0010](0010-enet-transport.md) | Transport 1차 구현은 ENet | Accepted | 2026-10-05 |
