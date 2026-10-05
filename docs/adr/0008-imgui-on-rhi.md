# ADR-0008. ImGui는 RHI 위 자체 렌더러로 통합

- 상태: **Accepted** · 날짜: 2026-10-05
- 관련: [06-RENDERING](../06-RENDERING.md) 10장

## 맥락

RTS는 ImGui를 `imgui_impl_win32` + `imgui_impl_opengl3`로 SFML 창 HWND에 직접 붙였습니다.
공식 백엔드를 쓰면 플랫폼 3개 × 렌더러 3개 조합을 유지해야 하고, Editor 쪽에 native 핸들이 새어 나옵니다.

## 결정

```text
- 렌더링: SandboxRender 의 ImGuiRenderer 1벌 (ImDrawData → 업로드 링 → 파이프라인 1개 → scissor 별 drawIndexed)
- 입력: SandboxClient 가 InputState → ImGuiIO 공급 (공식 플랫폼 백엔드 미사용)
- 폰트 아틀라스 = 일반 텍스처 에셋, ImTextureID = TextureHandle
- docking 브랜치, 멀티 뷰포트 초기 미지원
```

## 근거

```text
- ImGui 렌더링은 정점/인덱스 버퍼 + 텍스처 + scissor 뿐이라 RHI 로 수백 줄이면 된다.
- 세 백엔드에서 같은 코드가 돌고, 그 자체가 RHI 의 통합 테스트가 된다.
- Editor 가 native 타입을 하나도 모르게 된다.
```

## 결과

- 얻는 것: 유지할 ImGui 백엔드 1벌, Editor 이식성.
- 포기하는 것: 멀티 뷰포트(창 밖으로 패널 떼기), 공식 백엔드의 버그 수정 자동 수혜.
- 위험: 자체 렌더러 버그 → 폴백으로 공식 `imgui_impl_dx12`를 render/dx12 내부에서 래핑 가능.

## 대안

| 대안 | 기각 사유 |
|---|---|
| 공식 백엔드 3벌 | 유지비, native 노출 |
| 다른 UI 라이브러리(RmlUi 등) | 에디터 생산성은 ImGui가 압도적. 요구사항 |

## 재검토 조건

멀티 뷰포트가 에디터 UX의 필수 요구가 될 때.
