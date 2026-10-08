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

## Phase 7B — 셰이더 · 파이프라인 (삼각형 · 텍스처)

```powershell
# 첫 구성(CLion 의 CMake 다시 불러오기)에서 "DXC 1.9.2609.5 내려받는 중" 이 한 번 보인다 (약 53 MB, .cache\dxc\)
.\cmake-build-debug\bin\SandboxClient.exe --console
.\cmake-build-debug\bin\SandboxClient.exe --console --rhi-debug
# ctest 가 PATH 에 없으면 CLion 동봉판: & "$env:LOCALAPPDATA\Programs\CLion\bin\cmake\win\x64\bin\ctest.exe" ...
ctest --test-dir cmake-build-debug -L render --output-on-failure
ctest --test-dir cmake-build-debug -R "unit_render|shader_gen_selftest" --output-on-failure
```

```text
[ ] 구성 로그에 "셰이더: dxc = …\.cache\dxc\1.9.2609.5\build\native\bin\x64\dxc.exe". 두 번째 구성부터는 내려받지 않는다
[ ] 창 가운데에 빨강(위)·초록(왼쪽 아래)·파랑(오른쪽 아래) 꼭짓점의 삼각형이 천천히 반시계로 돈다 (약 12초에 한 바퀴).
    로그 "렌더러 Clear + 삼각형 (Phase 7B)"
[ ] 창을 가로로·세로로 길게 늘려도 삼각형이 찌그러지지 않는다 (가로세로비 보정). 최소화 → 복원 후 계속 돈다
[ ] 삼각형이 안 보이면 뒷면 컬링 = 감기 방향 규약이 틀린 것이다 (FrontFace) — 로그와 함께 알려 주십시오
[ ] --rhi-debug 로 1분: 끝 로그 "Debug Layer 경고 0 · 오류 0"
[ ] ctest -L render 통과: 16케이스, 끝 줄 "파이프라인 객체 0 · 디스크립터 0 · … 오류 6 (예상된 사용 오류 6)" + "render tests OK".
    기준 이미지 4장(triangle · coord_convention · culling_ccw · texture_linear)은 Wine 에서 만들었다 — WARP 에서 실패하면
    cmake-build-debug\tests\render\out_warp 의 *.actual.png · *.diff.png 를 보내 주십시오
```

## Phase 8A — 스프라이트 · --direct-sim

```powershell
.\cmake-build-debug\bin\SandboxClient.exe --console --direct-sim ecosystem_survival
.\cmake-build-debug\bin\SandboxClient.exe --console --direct-sim ecosystem_small --rhi-debug
ctest --test-dir cmake-build-debug -L render --output-on-failure
# fps 는 최적화 빌드로 (CLion 의 Release/RelWithDebInfo 프로필 — 폴더 이름은 프로필마다 다르다)
.\cmake-build-release\bin\SandboxClient.exe --console --direct-sim ecosystem_10k --vsync off
```

```text
[ ] 창에 어두운 녹색 월드 사각형과 풀(초록 덤불) · 토끼(갈색) · 늑대(회색)가 보이고 움직인다. 제목 줄
    "InWorld (Play) | ecosystem_survival tick … · 개체 … · ×1 · 30.0/30 TPS · 틱 … ms | 줌 … px/칸 |
     … fps · 월드 … · 추출 … · 렌더 … ms | D3D12 … fps … · 스프라이트 … · Draw 1"
[ ] Debug 빌드 ecosystem_10k: 시뮬레이션이 실시간을 못 따라가도(TPS < 30) 카메라 이동 · 줌은 부드럽다 (ADR-0021).
    제목 줄의 TPS · 틱 ms · fps 를 적어 주십시오
[ ] 움직임이 끊기지 않는다 (30 TPS 를 렌더 프레임마다 보간) — 144 Hz 모니터에서도 매끄럽다
[ ] WASD/화살표로 이동, 휠로 커서 아래를 기준으로 확대 · 축소, 가운데 버튼으로 끌면 잡은 곳이 커서를 따라온다 (8B 부터 왼쪽은 선택),
    Home 으로 월드 전체 맞춤. 창 크기를 바꿔도 찌그러지지 않는다
[ ] Space 일시정지(제목 줄 "일시정지", 개체가 멈춘다) → . 로 한 틱씩 → Space 로 재개. = / - 로 ×¼ ~ ×8
[ ] Release ecosystem_10k --vsync off: 제목 줄의 fps · TPS · 틱 · 추출 · 렌더 ms 를 적어 주십시오
    (목표 10k 스프라이트 60 FPS 이상, Draw 1~2, 30/30 TPS)
[ ] --rhi-debug 로 1분: 끝 로그 "Debug Layer 경고 0 · 오류 0"
[ ] ctest -L render 통과 (22 케이스 — sprite · batch_1k 기준 이미지는 Wine 에서 만들었다. 실패하면 out_warp 의 PNG)
```

## Phase 8B — 지형 · 격자 · 선택 · 디버그 · GPU 시간

```powershell
.\cmake-build-release\bin\SandboxClient.exe --console --direct-sim ecosystem_survival
.\cmake-build-debug\bin\SandboxClient.exe --console --direct-sim ecosystem_small --rhi-debug
ctest --test-dir cmake-build-debug -L render --output-on-failure
```

```text
[ ] 지형: 풀밭(어두운 초록)에 호수(파랑) · 흙(갈색)이 보인다. 확대(휠)하면 타일마다 밝기가 조금씩 다르다 (결)
[ ] G: 격자 — 확대할수록 타일 선이 진해지고, 32 타일마다 청크 선, 월드 가장자리는 금색 선. 다시 G 로 끈다
[ ] 왼쪽 클릭: 토끼 · 늑대 하나가 노란 상자로 선택되고 제목 줄에 "선택 eco.rabbit #… · 상태 · 에너지 … · 체력 …"
    하늘색 원(감지 반경) · 초록 선(남은 경로)과 × (목표) · 빨강 선(쫓는/피하는 대상) · 흰 화살표(속도)
[ ] 왼쪽 끌기: 노란 박스가 따라오고, 떼면 안에 든 개체가 모두 선택 ("선택 N"). Shift + 클릭으로 하나씩 더하고 빼기
[ ] Space 로 멈춘 뒤 클릭해도 바로 자세한 상태가 나온다. V: 원 · 경로 선을 끄고 켠다. Esc: 선택 해제
[ ] 선택한 개체가 죽으면 상자가 사라진다 (선택 수가 줄어든다)
[ ] 제목 줄 끝 "GPU … ms (지형 · 스프라이트 · 격자 · 선)" — 실제 GPU 에서 수치를 적어 주십시오 (Release, ecosystem_10k)
[ ] --rhi-debug 로 1분 (격자 켜고 선택한 채): "Debug Layer 경고 0 · 오류 0"
[ ] ctest -L render 통과 (26 케이스 — terrain · overlay 기준 이미지는 Wine 에서 만들었다. 실패하면 out_warp 의 PNG)
```

## Phase 8C — ImGui 패널

```powershell
.\cmake-build-release\bin\SandboxClient.exe --console --direct-sim ecosystem_10k --vsync off
.\cmake-build-debug\bin\SandboxClient.exe --console --direct-sim ecosystem_small --rhi-debug
.\cmake-build-release\bin\SandboxClient.exe --console            (메뉴 — 월드 없이 패널만)
ctest --test-dir cmake-build-debug -L render --output-on-failure
```

```text
[ ] 왼쪽 위 "시뮬레이션", 오른쪽 위 "통계" 창이 한글(맑은 고딕)로 보인다. 콘솔에 "ImGui 1.92.9b (docking) · 폰트 malgun.ttf …"
    (--font C:/Windows/Fonts/gulim.ttc 처럼 다른 폰트도 된다. 없는 파일이면 경고 후 맑은 고딕 → 영문 폰트)
[ ] 시뮬레이션 창: tick 이 오르고, TPS 가 30 근처. 일시정지 버튼 = Space, "한 틱" = . (멈췄을 때만), - / + = 속도
[ ] 격자 · 자세히 체크 = G · V 와 같다 (키로 바꾸면 체크도 바뀐다). "월드 맞춤" = Home
[ ] 개체를 클릭하면 시뮬레이션 창 아래에 선택 설명 + "선택 해제" 버튼
[ ] 통계 창: fps 그래프가 움직이고, CPU(월드 · 추출 · 렌더) · GPU 패스(… · UI) · Draw · 디바이스 수치가 나온다
    UI GPU 시간을 적어 주십시오 (Release, ecosystem_10k)
[ ] 패널 위에서 클릭 · 끌기 · 휠: 월드가 선택되거나 움직이지 않는다 (I1). 패널 밖으로 나가면 다시 된다
[ ] 패널 제목 줄을 끌어 옮길 수 있다. 창 크기를 바꿔도 패널이 깨지지 않는다
[ ] F1: 패널을 숨기고 다시 보인다. 숨긴 동안 클릭은 모두 월드로 간다
[ ] 통계 창 "ImGui 데모" → 데모 창. 글자 칸(Widgets > Text Input)에 한글 입력(조합 중 글자 포함)이 되고,
    글자 칸에 있는 동안 WASD · Space 가 게임으로 가지 않는다. Ctrl+C / Ctrl+V 가 Windows 클립보드와 오간다
[ ] 데모 창의 크기 조절 모서리 · 글자 칸 위에서 커서 모양이 바뀐다
[ ] 메뉴(월드 없음)에서도 패널이 그려지고 "월드 없음" 이 나온다
[ ] --no-ui: 패널이 없고 8B 와 같이 동작한다
[ ] --rhi-debug 로 1분 (패널 · 데모 창 켜고): "Debug Layer 경고 0 · 오류 0", 끝 로그 살아 있는 텍스처 0
[ ] ctest -L render 통과 (28 케이스 — imgui_basic 기준 이미지는 Wine 에서 만들었다. 실패하면 out_warp 의 PNG)
```

## Phase 8 — 렌더러

```text
[ ] 리사이즈·최소화 후 렌더 복구, 디바이스 제거 없음
[ ] VSync on/off, frames-in-flight 2/3 전환
[ ] 카메라 팬·줌이 부드럽다 (60 FPS 이상)
[ ] --rhi-debug 실행 시 Debug Layer 경고 0
[ ] PIX 캡처 1회 성공
```

## Phase 9 — 네트워크 서버 · 접속 (화면 없음)

PowerShell 창 두 개. 실행 파일은 `cmake-build-release\bin` (Debug 도 된다).

```powershell
# 창 1: 서버 (혼자 시험할 때는 admin — 일시정지 · 속도는 admin 부터)
.\SandboxServer.exe --world ecosystem_small --default-role admin
# 창 2: 접속 · 명령
.\sbx_net_probe.exe --pause --step 30 --resume --speed 2 --create 0,0 --seconds 3
.\sbx_net_probe.exe --name "두번째" --seconds 2
ctest --test-dir cmake-build-debug -R "server|net" --output-on-failure
```

```text
[ ] 서버: "listening udp *:7777 world ecosystem_small workers N". Windows 방화벽 창이 뜨면 "개인 네트워크" 허용
[ ] probe: "서버 콘텐츠(eco)를 읽어 다시 접속합니다" 뒤 "접속 client #1 role admin world ecosystem_small tick …"
[ ] 명령 결과 5 줄이 모두 "수락", CreateEntity 에 netId. 서버 통계 줄: 개체 수 · 틱 ms · 30 TPS 안팎 · 속도 x2
[ ] 서버 창: "들어옴: probe (#1, admin)" · "나감: probe (#1) — 상대가 끊음" 이 한글로
[ ] 서버를 --default-role editor 로 다시 띄우고 probe --pause: "거절 PermissionDenied — editor 역할은 PauseSimulation …", 종료 코드 3
[ ] probe 를 계속 붙여 둔 채(--seconds 30) 서버 창에서 Ctrl+C: probe 에 "서버가 끊었습니다: 서버 종료", 서버는 마지막 해시 줄을 찍고 끝
[ ] 서버 없이 probe: 약 10 초 뒤 "접속 실패: 127.0.0.1:7777 (…)", 종료 코드 1
[ ] (선택) 다른 PC 에서 --connect <서버 IP>:7777 — 같은 결과. 암호화가 없으니 LAN 에서만
[ ] (선택) 세이브 폴더로: SandboxServer --world <세이브 폴더> — 일시정지 상태로 저장된 월드면 probe --resume 으로 진행
[ ] ctest -R "server|net" 통과 (net_server_probe_smoke 는 UDP 47791 을 쓴다)
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
