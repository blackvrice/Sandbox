# ADR-0194. 작업 공간 "키" 패널 — 키 다시 묶기 · 겹침 표시는 ImGui "키" 창과 같은 KeyBindingEditor (UI-R2-08 일부)

- 상태: **Accepted** · 날짜: 2026-11-28
- 관련: [ADR-0083](0083-i18n-accessibility-foundation.md) 5 (키 다시 묶기 — v0.7 M4-T3), [ADR-0187](0187-workspace-input-press-owner-escape.md) (입력 나누기),
  [ADR-0178](0178-studio-dock-layout-persistence.md) (배치 저장 — 패널 목록이 바뀌면 옛 배치는 프리셋으로)

## 맥락

UI-R2-08 (명령 팔레트 · 단축키) 에서 남은 것은 떠 있는 팔레트 (오버레이 — GUI-UP-03) 와 **단축키 바꾸기 · 충돌 표시** 다. 바꾸기 · 겹침은
ImGui 개발자 창 "키" (`apps/client/ui/KeysPanel` — v0.7 M4-T3) 가 한다: `KeyBindingEditor` (엔진 기본 · 지금 맵, rebind · reset · resetAll ·
덮어쓰기만 input.json 에 저장), 표 (엔진 액션 · 팩 게임 키 play.* · 겹침 — 노랑), 바꾸기 = 다음에 누른 키 (앱이 입력의 날 값에서
`captureBinding` — Esc 취소, ImGui 가 마우스를 원하면 마우스 버튼은 바인딩이 아니다). 팝업이 필요 없어 막히지 않았다.

## 결정

1. **패널 = 보기 · 의도만.** `editor/gui/KeyBindingsPanel` — 모델 (`KeyBindingsModel` — 줄 · 겹침 · 저장 안 함 · 저장할 수 있나 · 바꾸는 중 ·
   결과) 은 앱이 `buildKeysView` (ImGui 창과 같은 함수) → `makeGuiKeysModel` 로, 일은 의도 `KeyBinding` (value = `KeysPanelRequest::Kind`
   차례 — static_assert, command = 액션) → 앱의 `applyKeysRequest` (ImGui 창과 같은 함수). 잡기 · 저장 · 바인딩 적용 코드는 하나다.
2. **목록 + 고른 줄.** 줄마다 단추를 두지 않고 (가상 목록) 고른 줄에 막대의 바꾸기 · 기본 — 겹침 줄 (경고색) · "엔진 키" · "게임 키 (팩)"
   머리는 고르지 않는다. ↑↓ 는 액션 줄만, Enter = 바꾸기. 바꾸는 중에는 같은 자리가 "취소" 이고 패널은 키를 쓰지 않는다 (누른 키는
   앱이 잡는다). 저장은 설정 폴더가 있고 저장 안 한 것이 있을 때만 (안전 모드 안내).
3. **작업 공간 위를 누른 것은 바인딩이 아니다.** `handleKeyCapture` 의 마우스 버튼 거르기에 작업 공간을 더한다 — `DockWorkspace::wantsPointer`
   (패널 · 탭 · 분할선 위 = 참, 통과 장면 칸 = 거짓). ImGui 의 wantMouse 와 같은 규칙.
4. 자리: 도구 그룹의 단축키 바로 뒤 탭 (세 프리셋). 패널 18 개 — 옛 .guidock 은 복원이 거절 → `.broken` + 프리셋 (ADR-0178).

## 결과

- 시험: `unit_client` "keys panel" (ImGui 보기 → 모델, 줄 · 겹침 · 머리, 고르기 · ↑↓ 가 머리를 건너뜀, 바꾸기 · 기본 · 모두 기본값 · 저장 의도와
  꺼짐 규칙, 바꾸는 중 "취소" · 키 무시) + app (홈의 작업 공간 — 패널로 첫 엔진 액션을 고르고 바꾸기 → 패널 위 클릭은 무시 → F8 → 그
  액션 = F8 · 바뀜 · 저장 안 함 → 기본 → 바꾸기 · Esc = 취소).
- UI-R2-08 은 여전히 일부 — 떠 있는 팔레트 (Ctrl+Shift+P) 는 GUI-UP-03 (오버레이).
- Studio 명령의 단축키 (CommandService) 를 사용자가 바꾸는 것은 이 패널의 일이 아니다 — 그 단축키는 행동 표의 키를 keyHint 로 보일 뿐이다
  (29-STUDIO-UI, [ADR-0122](0122-command-palette-shortcut-dispatch-themes.md)). 바꾸는 키는 여기 (행동 표).
