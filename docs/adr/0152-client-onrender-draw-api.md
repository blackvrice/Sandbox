# ADR-0152. client onRender · Draw API — 모든 onLateUpdate 뒤, 그리기 명령만 (월드 좌표 · 색 상태 · 프레임 1,024), 선은 RenderWorld::script

- 상태: **Accepted** · 날짜: 2026-11-17
- 관련: [ADR-0140](0140-client-script-lifecycle-session.md) (client 수명주기), [ADR-0137](0137-call-token-capability-runtime-check.md)
  (호출 토큰), [ADR-0067](0067-client-scripts-domain-client-onupdate-read-only-display.md) (client 스크립트 표시), [22-RUNTIME-V2](../22-RUNTIME-V2.md) 3장,
  [24-SECURITY-GATES](../24-SECURITY-GATES.md) C1, [06-RENDERING](../06-RENDERING.md) 8장, task-registry RT-06-9

## 맥락

Runtime v2 는 client onRender 를 "프레임 — 추출 뒤 · 제출 앞, ClientRenderContext (Draw 명령만 — 선 · 원 · 사각형 · 글자), 프레임 1,024
명령" 으로 예약했다 (콜백 표 Planned — 허락 EcsRead · Draw). 렌더러는 ECS 를 모르고 (경계 규칙), 월드 좌표 선 목록 (DebugDrawList —
LinePass, 줌과 무관한 두께) 과 ImGui 배경 목록 (client.label 의 글자) 이 이미 있다. Host 함수의 매개변수는 4 개까지 (kMaxHostParams).

## 결정

```text
1. 언제      ClientScriptSystems::run 의 끝 — 모든 시스템의 onLateUpdate 뒤 (카메라 · 따라가기가 정해진 뒤), (팩, id) 차례.
             앱의 추출 (NetworkSession::extract) 이 그 프레임의 명령을 RenderWorld 로 옮긴다 — 제출 앞
2. 허락      EcsRead · Draw (표 그대로 — Time · Ui · Display 없음). Draw 는 onRender 에서만 (onUpdate 의 drawLine 은 S404 ·
             도우미 함수면 HostError)
3. 함수      client.drawColor(rgba: i32 — 0xRRGGBBAA, 호출마다 흰색에서) · drawLine(x0, y0, x1, y1) · drawCircle(x, y, r) ·
             drawRect(x0, y0, x1, y1) · drawText(x, y, text) — 월드 좌표. 색을 상태로 둔 까닭: 매개변수 4 개 상한 (선 + 색 = 5)
4. 거절      유한하지 않거나 ±1e9 밖의 좌표 · 반지름 0 ~ 10,000 밖 = HostError (그 호출의 그리기를 다 버린다). 글자는 256
             바이트로 자른다. 프레임 1,024 개 (모든 시스템 합) 를 넘으면 버리고 센다 (droppedDraws — 트랩 아님, 24 C1)
5. 화면      선 · 원 (32 마디) · 사각형 → RenderWorld::script (DebugPass 안, 디버그 선 아래 — 2 픽셀), 글자 → ScriptOverlayInfo::texts
             → ImGui 배경 목록 (월드 자리 가운데, 색 그대로 — RGBA8 이 IM_COL32 배치와 같다). 변환은 apps/client/presentation/
             ScriptDraws (순수 — 시험)
6. 형식      .sbc 16 — 시스템 표 onRender (client 시스템만), Host 지문 (함수 다섯)
```

## 결과

- 스크립트가 조준선 · 범위 원 · 경로 · 떠 있는 글자를 그린다 — 권위 ECS · GPU 는 만지지 못한다 (C1).
- 화면 좌표 그리기는 없다 — HUD 는 게임 UI 문서 (v0.7) 로. 채운 도형 · 이미지는 [계획].

## 고른 것 · 버린 것

- drawLine(x0, y0, x1, y1, color): 매개변수 5 개 — Host 표 상한 4 를 늘리면 바인딩 · 검증 · 지문 전체가 바뀐다 — 색 상태로.
- 새 GPU 패스 (스크립트 전용): LinePass 와 같은 일 — DebugPass 안에서 한 번 더 그린다 (시간 표시는 debug 와 같이).
- 상한을 넘으면 트랩: 그리기는 표시라 잃어도 결과가 같다 — label · print 와 같이 버리고 센다.
