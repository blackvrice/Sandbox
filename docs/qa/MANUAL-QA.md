# 수동 QA 체크리스트

> L7 수동 QA ([13-TESTING](../13-TESTING.md)). 자동 테스트를 대신하지 않습니다. 마일스톤(Phase 완료) 때 수행하고
> 결과를 `DEVELOPMENT_LOG.md`에 "QA: 통과 n / 실패 m (항목)"으로 남깁니다.

## Phase 6 — 창·입력

렌더러가 아직 없어 **창 제목 줄이 입력 모니터**입니다 (Phase 8 부터는 화면 오버레이). 실행:

```powershell
.\cmake-build-debug\bin\SandboxClient.exe --console --log-input     # 로그를 보려면 --console (GUI 실행 파일)
```

제목 줄 예: `Sandbox — MainMenu | 1600×900 px ×1.25 | 마우스 412,300 [Left] 더블클릭 Left 휠 +2 | 키 W LeftShift | 글자[F2 켬] "한글" | 캡처[F3 끔]`
단축키: F2 글자 입력(IME) 켜기/끄기 · F3 마우스 캡처 · F4 커서 모양 바꾸기 · Ctrl+C/Ctrl+V 글자 복사·붙여넣기 · Backspace 지우기 ·
Esc 캡처 해제(캡처 중)/글자 지우기 · Ctrl+Q 또는 Alt+F4 종료.

```text
[ ] 창 생성·닫기(X 버튼·Alt+F4·Ctrl+Q)·최소화·복원·최대화. 최소화 중 CPU 사용률이 거의 0
[ ] 리사이즈 중 깨짐·멈춤 없음 (제목 줄 px 값이 놓은 뒤 맞다)
[ ] 다른 DPI 모니터로 이동 시 배율 갱신 (제목 줄 ×1.00 → ×1.50 등, 창의 논리 크기 유지, 글자가 흐릿하지 않다).
    --console 로그에 "DPI 를 켜지 못했습니다" 경고가 없다
[ ] 한글 IME 조합 입력: F2 → 한/영 → "한글" 입력 → 제목 줄에 "한글". 조합 중 글자는 IME 창에 보인다
    (ImGui 텍스트 필드는 Phase 8 에서 다시). F2 로 끈 뒤에는 한글 상태에서도 W·A·S·D 가 키로 잡힌다 (제목 줄 "키 W")
[ ] Alt+Tab 후 키 끈적임 없음 (W 를 누른 채 Alt+Tab → 돌아오면 "키 -")
[ ] 마우스 휠(위·아래, 틸트 휠은 /±n)·가운데 버튼·더블클릭("더블클릭 Left")·X1/X2 버튼
[ ] F3 캡처: 커서가 숨고 창 밖으로 나가지 않는다. --log-input 에 MouseMove Δ 가 계속 나온다. Esc 로 풀린다. Alt+Tab 하면 가둠이 풀린다
[ ] Ctrl+C 로 복사한 글자가 메모장에 붙고, 메모장에서 복사한 한글이 Ctrl+V 로 제목 줄에 붙는다
[ ] 다른 키보드 배열(있다면): AZERTY 에서 W 자리를 누르면 "키 W" (물리 위치, ADR-0017)
```

Wine(Xvfb)에서 미리 확인한 것 (2026-10-06, 클라우드): 키·Shift·F2 글자·클릭·더블클릭·휠·가운데 버튼·F3 Raw Input 상대 이동·
크기 변경·제목 줄(한국어)·Ctrl+Q 종료. DPI·IME 조합·Alt+Tab·최소화는 Wine 으로 확인할 수 없다.

## Phase 7A — D3D12 Clear

```powershell
.\cmake-build-debug\bin\SandboxClient.exe --console                       # 기본: 하드웨어 GPU, VSync 켬
.\cmake-build-debug\bin\SandboxClient.exe --console --rhi-debug           # Debug Layer ("그래픽 도구" 설치 필요)
ctest --test-dir cmake-build-debug -L render --output-on-failure             # WARP 기준 이미지 테스트
```

```text
[ ] 창이 회색이 아니라 천천히 색이 바뀌는 어두운 색으로 칠해진다 (30초에 한 바퀴). 제목 줄 끝 "D3D12 60 fps VSync 켬"
    (모니터 주사율에 맞는 fps). --console 로그 첫 줄에 GPU 이름 · FL 12_x
[ ] 크기 조절 중·후 깨짐·검은 띠 없음, 최대화·복원 후 정상
[ ] 최소화 → 복원 후 계속 그린다. 최소화 중 CPU·GPU 사용률이 거의 0
[ ] 다른 DPI 모니터로 옮겨도 화면이 창을 꽉 채운다
[ ] --vsync off: fps 가 주사율보다 높다 (찢어짐 허용 지원이면 로그에 "tearing 지원")
[ ] --frames-in-flight 3, --rhi-warp: 같은 화면 (WARP 는 fps 가 낮을 수 있다)
[ ] --rhi-debug 로 1분 돌리고 Ctrl+Q: 끝 로그 "Debug Layer 경고 0 · 오류 0", 제목 줄에 "D3D12 경고" 가 없다
[ ] ctest -L render 통과 (render_tests_warp). 실패하면 cmake-build-debug\tests\render\out_warp 의 actual·diff PNG
```

## Phase 8 — 렌더러

```text
[ ] 리사이즈·최소화 후 렌더 복구, 디바이스 제거 없음
[ ] VSync on/off, frames-in-flight 2/3 전환
[ ] 카메라 팬·줌이 부드럽다 (60 FPS 이상)
[ ] --rhi-debug 실행 시 Debug Layer 경고 0
[ ] PIX 캡처 1회 성공
```

## Phase 10~12 — 네트워크·에디터

```text
[ ] 두 PC(LAN) 접속, 한쪽 편집이 다른 쪽에 0.5초 내 반영
[ ] 서버 재시작 없이 클라이언트 재접속 (토큰)
[ ] Observer 역할에서 편집 UI 비활성 + 서버 거절
[ ] Undo/Redo 10단계, 다른 사용자 편집과 섞였을 때 경고
[ ] 저장 → 서버 재시작 → 로드 → 동일 상태
[ ] 리플레이 재생이 라이브와 같아 보인다
```

## Phase 13·14 — 이식

```text
[ ] 같은 세이브를 Windows/Linux/macOS 에서 열었을 때 같은 화면 구성
[ ] Linux X11·Wayland 각각 입력·리사이즈
[ ] macOS Retina 배율, 메뉴 바, Cmd 단축키
```
