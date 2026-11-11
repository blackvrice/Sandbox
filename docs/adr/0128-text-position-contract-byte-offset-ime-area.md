# ADR-0128. 텍스트 위치 계약 — 바이트 offset 이 정본, 줄 끝은 렉서와 같게, 화면 칸은 표시에만, IME 는 확정 글자만 · 커서 자리

- 상태: **Accepted** · 날짜: 2026-11-11
- 관련: [ADR-0017](0017-physical-keys-event-queue-headless-window.md) (텍스트 입력 · IME 기본 끔), [ADR-0124](0124-bytecode-line-table-format-11.md) (줄 표의 열 단위),
  [27-SCRIPT-IDE](../27-SCRIPT-IDE.md) 9장, [07-PLATFORM](../07-PLATFORM.md), task-registry NG-05

## 맥락

컴파일러 진단 · 줄 표는 (줄, 코드 포인트 열), 편집기 (ImGui) 커서는 UTF-8 바이트, LSP (v0.21) 는 UTF-16, 화면은 글리프 폭 (한글 2 칸)
이다. 변환을 쓰는 곳마다 따로 짜면 한글 · 그림 문자 · CRLF 에서 어긋난다 (감사 SECOND-PASS 4.5). 27 9장은 "위치 계산은 LF 기준" 이라
적었지만 렉서는 LF · CRLF · CR · U+2028 · U+2029 를 줄 끝으로 센다 (JS) — 둘이 다르면 홀로 CR 파일의 진단 줄이 틀린다.

## 결정

```text
1. 위치의 정본 = UTF-8 바이트 offset. 다른 단위는 foundation/text/TextPosition 의 함수로만 오간다 — lineColAt · offsetAt (단위:
   코드 포인트 · UTF-16 · 화면 칸), lineAt, displayWidth, utf16Units
2. 줄 끝 = 렉서와 같다 (LF · CRLF 하나 · CR · U+2028 · U+2029). 27 9장의 "LF 기준" 을 고친다 — CRLF 는 둘 다 같고, 홀로 CR 까지 맞춘다
3. 글자 가운데 (다바이트 · CRLF 사이 · 서로게이트 쌍의 둘째 단위 · 2 칸 글자의 둘째 칸) 는 그 글자의 앞, 넘는 줄 · 열은 끝으로 자른다
4. 화면 칸: 한글 · CJK · 전각 · 그림 문자 2, 결합 문자 · 한글 중성 · 종성 자모 · 너비 없는 글자 0, 그 밖 1, 탭은 표시하는 쪽 (편집기의 ^ 줄은
   탭을 탭으로 둔다). 표시에만 — 저장 · 진단에 쓰지 않는다. 표는 주요 구간 (전체 Unicode EAW 표가 아니다 — 빠진 글자는 1 칸)
5. 줄 끝 보존: 읽은 그대로. 편집기가 넣은 LF 를 저장할 때 detectLineEnding (원본) → applyLineEnding 으로 되돌린다 (M6-T8 의 저장이 쓴다).
   섞인 파일은 그대로
6. IME: 문서에는 확정 글자 (TextInput) 만 — 조합 중 글자는 OS 조합 창이 그린다 (Win32 WM_CHAR · Cocoa insertText). 그래서 조합 중에
   자동 완성 · 진단 · Undo 가 생기지 않는다 (구조로). 조합 · 후보 창 자리 = 글자 칸의 커서: IWindow::setTextInputArea (논리 단위) —
   ImGui Platform_SetImeDataFn 이 부른다. Win32 (ImmSetCompositionWindow · ImmSetCandidateWindow) · macOS (firstRectForCharacterRange) 구현,
   Wayland (text-input-v3 set_cursor_rectangle) · X11 은 [계획] (기본 아무것도 하지 않음)
7. 첫 사용처: 스크립트 진단 발췌 (Studio 콘텐츠 창) — 줄을 계약대로 찾고 (CR 숨김), 열에 ^ (화면 칸 · 탭 보존)
```

## 근거

```text
- 정본 하나: 단위가 넷이라 쌍마다 변환을 두면 12 개 — offset 을 거치면 4 개
- 렉서와 같은 줄 끝: 진단의 (줄, 열) 이 그대로 offset 으로 바뀌어야 한다 — 시험이 컴파일러 진단 자리로 확인한다
- 화면 칸 표를 작게: 편집기에 나오는 글자 (한국어 · 영어 · 그림 문자) 가 맞으면 된다. 빠진 글자는 ^ 가 조금 어긋날 뿐 (저장 · 진단 영향 없음)
```

## 결과

- 얻는 것: 변환 함수 하나, 진단 ↔ offset 시험, 한글 · 그림 문자 · CRLF · CR 위치, 진단 발췌의 ^, IME 창이 커서에 (Win32 · macOS).
- 포기하는 것: 전체 Unicode 폭 표 · 자소 클러스터 (이모지 ZWJ 묶음은 글자마다 센다), Wayland · X11 의 IME 자리.
- 위험: 실제 IME 창 위치는 창 QA (MANUAL-QA) 로만 본다.

## 대안

| 대안 | 기각 사유 |
|------|-----------|
| 편집기 단위 (바이트) 를 컴파일러에도 | 진단 · 줄 표가 사람이 읽는 열 (코드 포인트) 이어야 한다 — 27 9장 |
| ICU 로 폭 · 자소 | 의존이 크다 — Foundation 은 의존이 없다 |
| 줄 끝을 LF 로 정규화해 읽기 | 저장 왕복이 파일을 바꾼다 (diff 잡음) |

## 재검토 조건

LSP (v0.21 — UTF-16), 자소 클러스터 커서 이동, Wayland · X11 IME 자리, 게임 UI TextInput 의 커서 (NG-06).
