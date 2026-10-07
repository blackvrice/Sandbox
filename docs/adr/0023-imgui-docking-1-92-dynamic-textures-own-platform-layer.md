# ADR-0023. ImGui 는 1.92 docking 을 벤더링하고, 렌더러는 동적 텍스처 프로토콜로 RHI 위에, 입력은 PlatformEvent 로 직접 먹인다

- 상태: **Accepted** · 날짜: 2026-10-07
- 관련: [ADR-0008](0008-imgui-on-rhi.md) (구체화), [06-RENDERING](../06-RENDERING.md) 10 · 14장,
  [07-PLATFORM](../07-PLATFORM.md) (I1), [16-ROADMAP](../16-ROADMAP.md) 8.4

## 맥락

Phase 8C 는 ImGui 를 붙인다 (8.4: ImGuiRenderer(RHI) + InputState → ImGuiIO, 기본 패널). ADR-0008 은 "RHI 위 자체 렌더러 ·
공식 플랫폼 백엔드 미사용 · docking 브랜치" 를 정했다. 구현에서 정할 것:

```text
1. 어느 ImGui, 어디서 가져오나 (이 세션은 GitHub 에 닿지 않는다 — 패키지 저장소만)
2. 폰트 아틀라스: 1.91 식 정적 아틀라스(글리프 범위를 미리 굽는다) vs 1.92 동적 텍스처(쓰는 글자만, 백엔드가 갱신)
3. 한글: UI 가 한국어다 — 기본 폰트(ProggyClean)에는 한글이 없다. 폰트 파일을 저장소에 넣을지
4. 입력 순서 · 가로채기(I1), 글자 입력(IME), 커서, 클립보드
5. 패널이 세션을 바로 바꿀지
```

## 결정

```text
1. Dear ImGui v1.92.9b docking 브랜치를 external/imgui 에 그대로 벤더링 (imgui*.{h,cpp} · imconfig.h · imstb_* · LICENSE).
   출처 = crates.io dear-imgui-sys 0.18.0 의 third-party/cimgui/imgui — cimgui 가 서브모듈로 둔 업스트림 사본 (수정 없음,
   crate SHA256 기록). PyPI imgui-bundle 의 사본은 Python 바인딩 수정 · stack-layout PR 이 섞여 있어 쓰지 않았다.
   정적 라이브러리 sbx_imgui, IMGUI_DISABLE_OBSOLETE_FUNCTIONS, 경고 끔(서드파티). backends/ · misc/ 는 넣지 않는다.
2. 1.92 동적 텍스처 (ImGuiBackendFlags_RendererHasTextures). ImGuiRenderer 가 ImDrawData::Textures 의 요청을 처리한다:
   WantCreate → RGBA8 텍스처 + 바인드 그룹(텍스처 · 선형 샘플러) + 전체 업로드, WantUpdates → 바뀐 사각형만, WantDestroy →
   지연 해제. ImTextureID = 렌더러 표의 칸 + 1. Alpha8 은 업로드 때 흰색 + 알파로.
   그리기: 업로드 링(정점 · uint16 인덱스) · 파이프라인 하나(직선 알파) · 명령마다 scissor + drawIndexed(vertexOffset —
   RendererHasVtxOffset). 패스는 LoadOp::Load. ResetRenderState 는 1.92.8 방식(platformIo.DrawCallback_ResetRenderState).
3. 폰트 파일을 저장소에 넣지 않는다. --font <ttf|ttc> → Windows 맑은 고딕(C:/Windows/Fonts/malgun.ttf) → 내장 벡터 폰트(영문).
   동적 아틀라스라 한글 11,172 자를 미리 굽지 않는다 — 화면에 나온 글자만.
4. ImGuiLayer (apps/client/ui): PlatformEvent → io.Add*Event (수정자는 좌우 키 상태로 ImGuiMod_*), 표시 크기 = 창 논리 크기,
   FramebufferScale = 프레임버퍼 / 논리. 프레임 순서: 이벤트 → ImGui::NewFrame → InputSystem.setCapture(WantCaptureMouse,
   WantCaptureKeyboard) → 게임 입력 · 패널 → ImGui::Render → 렌더. 키보드 내비게이션은 끈다 (켜면 패널을 한 번 누른 뒤 WASD 를
   ImGui 가 가져간다). 글자 칸에 들어가면 창의 글자 입력(IME)을 켜고 나오면 끈다. 커서는 ImGui 위면 ImGui 모양, 아니면 앱 것.
   클립보드는 창. 커서를 가둔 동안(F3) ImGui 는 마우스를 보지 않는다. imgui.ini 는 쓰지 않는다 [계획 — Phase 12].
5. 패널은 PanelActions 를 돌려주고 Application 이 단축키와 같은 길로 적용한다 (패널이 세션 · 카메라를 직접 만지지 않는다).
   "시뮬레이션"(진행 · TPS · 일시정지 · 한 틱 · 속도 · 격자 · 자세히 · 맞춤 · 선택), "통계"(fps 그래프 · CPU 구간 · GPU 패스 ·
   그리기 · UI · 디바이스 · ImGui 데모). F1 로 숨긴다. 렌더러가 없으면(헤드리스) 레이어가 텍스처 요청을 받은 척한다.
```

그 밖에:

```text
- UI GPU 시간: Renderer::record 에 ui 콜백 — 월드 패스가 끝난 뒤 부르고 타임스탬프 kMarkUi(7) 를 쓴다 → GpuPassTimes.uiMs.
  메뉴(월드 없음)에서도 지우기 + 삼각형 뒤에 UI 를 그린다.
- IFrameRenderer: render(…, ImDrawData*), attachImGui / detachImGui(컨텍스트를 지우기 전에 UI 텍스처를 놓는다), info()
  (패널의 렌더러 수치). IWorldSession::info() (패널의 진행 수치).
- 옵션 --font, --no-ui. 기준 이미지 imgui_basic 은 내장 비트맵 폰트(13 px)로 — 구현마다 같은 글자.
```

## 근거

```text
- 1.92 동적 텍스처는 한글 UI 에 맞다: 정적 아틀라스는 한글 범위를 굽느라 수 MB · 시작 지연이 생기고 크기를 바꿀 때마다
  다시 굽는다. 요청을 처리하는 코드는 텍스처 만들기 · 영역 복사 — RHI 에 이미 있는 것뿐이다 (업로드 링 · copyBufferToTexture).
- PlatformEvent 를 직접 먹이면 OS 별 imgui_impl_* 가 필요 없다 — 헤드리스 창으로 단위 테스트가 입력 · 가로채기 · IME 켜기를 본다.
- 폰트 파일(수 MB, 라이선스)을 저장소에 두지 않아도 Windows 사용자는 바로 한글을 본다.
- 패널 → 액션은 키보드 단축키와 같은 함수(togglePause 등)를 부르므로 동작이 한 곳에서 정해진다.
```

## 결과

- 얻는 것: 화면 패널(시뮬레이션 · 통계), 한글 UI, UI GPU 시간, 기준 이미지 imgui_basic, 입력 가로채기(I1)의 실제 구현.
- 포기하는 것: 멀티 뷰포트(창 밖으로 떼기 — ADR-0008 그대로), 레이아웃 저장(ini), 다른 언어 폰트 자동 선택(맑은 고딕만).
  Linux · macOS 에서는 --font 를 줘야 한글이 보인다 (렌더 백엔드도 Phase 13 · 14).
- 위험: ImGui 버전 올림은 텍스처 프로토콜 · 콜백 이름이 바뀔 수 있다 (1.92.8 의 ResetRenderState 처럼) — external/README 의
  버전과 이 ADR 을 같이 본다. 동적 폰트는 처음 나온 글자에서 아틀라스 갱신(업로드)이 생긴다 — 프레임당 작다.

## 대안

| 대안                                          | 기각 사유                                                                        |
|-----------------------------------------------|----------------------------------------------------------------------------------|
| 1.91 정적 아틀라스 + 한글 범위                | 시작 때 수 MB 를 굽는다, 크기 바꾸면 다시. 1.92 의 이점(필요한 글자만)을 버린다  |
| PyPI imgui-bundle 의 사본                     | Python 바인딩용 수정 · stack-layout PR 이 섞인 포크                              |
| 공식 imgui_impl_win32 + imgui_impl_dx12       | ADR-0008 이 기각 — native 노출, 백엔드 조합마다 유지                             |
| Noto Sans KR 을 저장소에 (assets/fonts)       | 수 MB · OFL 고지. Windows 에는 맑은 고딕이 있고 --font 로 바꿀 수 있다           |
| 패널이 세션을 직접 호출                       | 단축키와 동작이 두 군데로 갈라진다, 단위 테스트가 패널을 그려야 동작을 본다      |

## 재검토 조건

Phase 12 에디터(SandboxEditor 로 패널 이동, 도킹 레이아웃 · ini 저장), 멀티 뷰포트 요구, Vulkan · Metal 백엔드(같은
ImGuiRenderer 가 RHI 로 돈다 — 텍스처 포맷 · scissor 규약 확인), ImGui 버전 올림.
