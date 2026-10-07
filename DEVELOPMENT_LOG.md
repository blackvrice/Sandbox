# DEVELOPMENT_LOG

작업 단위의 **이력**입니다: 무엇을, 왜 바꿨고, 어떻게 검증했고, 무엇이 남았나.
"지금 어떤 상태이고 무엇을 지켜야 하나"는 `docs/`에 있습니다. 둘을 섞지 않습니다.

새 항목은 위에 추가합니다.

---

## 2026-10-06 — Phase 7A: RHI 골격 · D3D12 디바이스 · 스왑체인 Clear · 프레임 자원 · 기준 이미지 테스트

**무엇을**

- Phase 7 을 둘로 나눴다 (ADR-0018): 7A 디바이스·Clear·프레임 자원·기준 이미지 틀 → 7B 셰이더·파이프라인·Triangle·Texture.
- `SandboxRender` (Platform 만 의존, 항상 빌드):
  - `render/rhi`: `RhiTypes`(핸들·Format·MemoryType·Usage·ResourceState·Desc·Caps·DeviceStats), `RenderDevice`(IRenderDevice ·
    ICommandList · ICommandQueue · ISwapChain · createRenderDevice), `HandlePool`, `DeferredDestructionQueue`, `UploadRing`
  - `render/dx12`: `Dx12Device`(어댑터 선택 — WARP · 고성능 하드웨어 · 소프트웨어, FL 12_0 기본, committed 리소스, RTV 힙,
    업로드 링, 프레임 슬롯, 지연 해제, InfoQueue 경고·오류 집계, DRED, 디바이스 제거 원인), `Dx12CommandList`(배리어 · Clear ·
    MRT · 버퍼/텍스처 복사 + 배치 검증 · PIX 마커), `Dx12SwapChain`(FLIP_DISCARD · tearing · resize · 최소화 skip), `Com<T>`
  - `render/asset/Image`(RGBA8 · PNG 읽기/쓰기 · 비교 · 차이 그림), stb 벤더링(`external/stb`, f58f558)
  - `render/stub` — 백엔드가 없는 OS 의 `createRenderDevice` = Unsupported
- RHI 추가 (06 3.2 초안 대비): beginFrame/endFrame/frameIndex/waitIdle, map, allocateUpload, copyTextureToBuffer, destroy 오버로드,
  alive · textureDesc · stats, lastSubmittedValue, DeviceDesc.allowFeatureLevel11 (시험 전용)
- `sbx_render_tests` (Windows, `tests/render`): clear · 두 색 첨부 · upload_quadrants · region_copy · 버퍼 왕복 + 링 감기 · 수명 ·
  사용 오류. 기준 이미지 3장. 끝에 누수 0 · Debug Layer 경고 0 · 예상한 오류 수. CTest `render_tests_warp`
- SandboxClient: `IFrameRenderer` 경계 + `ClearRenderer`(실제 시간으로 천천히 색이 바뀌는 Clear, 제목 줄에 fps), 옵션
  `--no-render --rhi-debug --rhi-gbv --rhi-warp --rhi-fl11 --vsync on|off --frames-in-flight 2|3`. 렌더러가 있으면 페이싱은 VSync
- `tools/wine/`: Wine(vkd3d) + lavapipe 실행 래퍼와 시험용 Vulkan 레이어
- 단위 테스트 스위트 `render` (모든 OS): 핸들 풀 · 지연 해제 · 업로드 링(참조 모델 2,000프레임) · 포맷 · PNG 왕복(한글 경로) · 비교.
  `client`: 가짜 렌더러 · 새 옵션

**왜**

- Phase 7 (16): 렌더러(8)가 올라설 RHI 와 첫 백엔드. 06 8.6 순서 "Window → Clear → Triangle → Texture" 의 Clear 까지.

**도중에 바꾼 것 / 발견한 것**

- MinGW 의 d3d12.h 는 d3d12sdklayers.h 를 포함하지 않는다 → 직접 include. D3D_FEATURE_LEVEL_12_2 도 없다 → 값으로.
- Wine 의 d3d12(vkd3d)가 lavapipe 에서 FL 11_0 도 거절: viewportSubPixelBits 가 0 → 시험용 Vulkan 레이어로 8. 그래도 타일드
  리소스가 없어 FL 12_0 은 안 된다 → allowFeatureLevel11 (sbx_render_tests --fl11, SandboxClient --rhi-fl11).
- 지연 해제를 "마지막 제출 값" 으로 기록하면 기록 중 파괴에서 사용 후 해제가 된다 → "다음 제출 값".
- waitIdle 이 업로드 링의 이번 프레임 구간까지 반납하면, 아직 제출 전인 업로드를 덮어쓸 수 있다 → 끝난 프레임만 반납.
- VSync 가 없는 Xvfb 에서 클라이언트가 1,500 fps 로 돌아 프레임 기반 시간의 색 변화가 깜빡였다 → 렌더러에 실제 시간을 넘긴다.
- stb 의 stbi_write_png_to_mem 은 공개 선언이 없다 → stbi_write_png_to_func. 파일 IO 는 foundation/io (Windows 한글 경로).

**검증**

```text
linux-clang(19.1.1): Debug 36/36 (bench_quick 포함), RelWithDebInfo 39/39 (balance 포함). linux-gcc(13.3): 35/35 · 38/38. 경고 0
linux-clang-asan Debug 35/35 (bench·balance 제외). doctest Linux 203 케이스 · Windows(Wine) 202 케이스
MinGW GCC 13 교차 빌드 → Wine 9 (d3d12 = vkd3d → lavapipe, Xvfb):
  sbx_render_tests --fl11: 8 케이스 통과, 누수 0, 해제 13, 경고 0 (Wine 에는 Debug Layer 가 없다 — 안내 후 계속 확인)
  SandboxClient --rhi-fl11: 스왑체인 640×400 BGRA8 · 버퍼 3, Clear 색이 시간에 따라 바뀜(스크린샷), 900×500 으로 크기 변경,
  제목 줄 "D3D12 … fps VSync 켬", Ctrl+Q 종료
  SandboxTests.exe 202 케이스 통과 (Wine 은 LANG=C.UTF-8 이어야 한글 경로를 만든다 — tools/wine/run.sh)
기준 이미지 3장의 픽셀 값을 스크립트로 확인 (clear (51,102,204) · 사분면 빨강/초록/파랑/흰색 · 노란 패치 위치)
include lint 0건, clang-format 적용
```

**미검증 / 남은 일**

- 사용자 PC: MSVC 빌드, `ctest -L render` (WARP + Debug Layer — "그래픽 도구" 설치 시), qa/MANUAL-QA Phase 7A
  (실제 VSync · DPI · 최소화 · --rhi-debug 경고 0). Wine 은 Debug Layer · Microsoft 런타임 동작을 대신하지 못한다.
- Windows 골든 4개 재기록(5C) · Phase 6 수동 QA 는 그대로 남아 있다.
- [계획] 7B: DXC 셰이더 빌드(sbx_add_shader · 리플렉션 JSON · 헤더 생성), 루트 시그니처 · PSO · BindGroup, Triangle · Texture ·
  좌표 규약 기준 이미지. Phase 8: D3D12MA, Copy 큐, 업로드 예산, 타임스탬프.

---

## 2026-10-06 — Phase 6: Windows 플랫폼 (창 · 입력 · ActionMap · Null 오디오 · SandboxClient 빈 창)

**무엇을**

- `SandboxPlatform` (Foundation 만 의존, ActionMap 파싱에 nlohmann/json PRIVATE). 항상 빌드한다 — 입력·오디오 단위 테스트가 쓴다.
  - `platform/common`: `Key`(물리 위치 · 이름 표), `PlatformEvent`/`PlatformEventQueue`, `IWindow`/`WindowDesc`/`NativeWindowHandle`,
    `HeadlessWindow`(OS 창 없는 IWindow — inject), `InputSystem`(I1 setCapture/downstream · I3 · I4 · 눌린 순간 수정자),
    `ActionMap`(settings/input.json · 정확 수정자 · 충돌 · 병합 · toJson) + `ActionState`(가장자리), `Gamepad` 인터페이스(I5),
    `IAudioBackend`, `FramePacer`/`PreciseSleeper`, `attachConsole`
  - `platform/windows`: `Win32Window`(Per-Monitor DPI v2 · WM_DPICHANGED · Raw Input 캡처 · IME 컨텍스트 분리 · WM_CHAR 서로게이트 ·
    SetCapture · 더블클릭 · 휠 · 클립보드 · 커서 · Alt 메뉴/경고음 차단 · AltGr·Shift·PrintScreen 보정), `Win32Platform`
    (고해상도 대기 타이머 · 콘솔), `Win32KeyMap`(windows.h 없는 순수 표 — 모든 OS 에서 테스트)
  - `platform/audio/NullAudioBackend`, `platform/stub`(창이 없는 OS: createWindow = Unsupported)
- `SandboxClient` (`apps/client`): 앱 상태기계(Boot → MainMenu → Connecting → InWorld(Play|Edit) → Shutdown, 전이 표, 프레임 끝 적용),
  프레임 루프(이벤트 → 입력 → 액션 → 디버그 동작 → 오디오 → 제목 줄), 제목 줄 입력 모니터(수동 QA), 기본 바인딩 JSON 내장 +
  `--input`, `--headless --frames --fps --width --height --console --log-input`. Windows: GUI 서브시스템 + main, DPI 매니페스트
- `foundation/text/Utf8.hpp`. 기본 로그 싱크가 줄마다 stderr flush
- 옵션 `SBX_BUILD_CLIENT`(ON). CTest `unit_platform` · `unit_client` · `client_version` · `client_rejects_unknown_option` ·
  `client_headless_smoke`
- ADR-0017 (물리 키 · 텍스트 입력 기본 끔 · 정확 수정자 + 눌린 순간 수정자 · HeadlessWindow · 오디오 update(dt))

**왜**

- Phase 6 (16): 렌더러(7·8)가 그릴 창과, 에디터(12)가 쓸 입력 경로를 먼저 확정한다. 실제 창 없이도 앱 루프를 시험할 수 있어야
  CI 와 Linux 에서 클라이언트 코드가 회귀하지 않는다.

**도중에 바꾼 것 / 발견한 것**

- 07 초안의 "scancode 기준 바인딩" 은 OS 마다 값이 달라 설정 파일에 적을 수 없다 → Key 자체를 물리 위치로 정의 (ADR-0017).
- 같은 프레임에 연속 두 번 탭하면 ActionState 가 두 번째를 놓쳤다 (지난 프레임도 켜짐) → 바인딩 키가 새로 눌린 프레임은 pressed.
- Wine + xdotool 시험에서 `Ctrl+Q` 가 안 먹었다: 한 프레임 안에 Ctrl↓ Q↓ Ctrl↑ Q↑ 가 모두 와서 프레임 끝 수정자가 비어 있었다
  → 키가 눌린 순간의 수정자를 InputState 에 기록 (keyPressMods).
- 두 Shift 보정이 누르지 않은 쪽 KeyUp 까지 냈다 → 알린 Shift 만 뗀다.
- Wine 에서 강제 종료하면 로그가 사라졌다 (Windows CRT 가 파이프로 넘긴 stderr 를 버퍼링) → 로그 싱크 flush.
- 07 의 `IAudioBackend::update()` → `update(dtSeconds)` (Null 이 목소리 수명을 정하려면 시간이 필요).

**검증**

```text
linux-clang(19.1.1): Debug 35/35 (bench_quick 포함), RelWithDebInfo 38/38 (balance 포함). linux-gcc(13.3): 34/34 · 37/37. 경고 0
linux-clang-asan Debug 34/34 (bench·balance 제외), doctest 193 케이스
MinGW GCC 13 (posix) 교차 빌드 → Wine 9: SandboxTests.exe 193 케이스 전부 통과, SandboxClient --version · --headless 통과
Wine 9 + Xvfb + xdotool 로 실제 Win32Window: 키(스캔 코드 → Key)·Shift·F2 글자 "hi"·클릭·더블클릭 ×2·휠·가운데 버튼·
  F3 캡처 중 Raw Input Δ(30, 10)·Esc 해제·창 크기 변경 → Resized 1000×700·제목 줄(한국어)·Ctrl+Q 종료
include lint 0건 (195 파일), clang-format 적용
```

**미검증 / 남은 일**

- MSVC 빌드와 qa/MANUAL-QA Phase 6 체크리스트 (사용자 PC): DPI 모니터 이동, 한글 IME 조합, Alt+Tab, 최소화 CPU, 클립보드.
  Wine 은 Per-Monitor DPI 를 지원하지 않는다 ("DPI 를 켜지 못했습니다" 경고는 Wine 에서만 나와야 한다).
- Windows 골든 4개 재기록(Phase 5C 에서 남은 일)은 그대로 남아 있다.
- [계획] 사용자 설정 경로(platformPaths) 자동 로드, IME 캐럿 위치·현지 배열 키 이름(Phase 8), 게임패드 구현, miniaudio.
- 다음: Phase 7 — DirectX 12 RHI (Device · SwapChain Clear → Triangle → Texture, 셰이더 빌드, WARP 기준 이미지).

---

## 2026-10-06 — Phase 5C: 리플레이(D3) · 생태계 시나리오 · 밸런스 · 10k 성능 (Phase 5 완료)

**무엇을**

- 리플레이 (D3, ADR-0016): `core/command/CommandJson`(명령 ↔ JSON, 엔티티 참조 변환), `SimulationWorld::setCommandObserver`
  (수락된 명령을 적용 전에 netId → saveId 로 바꿔 넘김), `core/replay/Replay`(ReplayRecorder · JSON Lines 파일 · playReplay).
  명령의 위치 = tick() 호출 번호(call). `sbx_sim_check --replay-roundtrip [--replay-dir]` · `--replay <file>`
- 시나리오 `ecosystem_small`(64×64, 300) · `ecosystem_survival`(256×256, 1,500, 18,000틱) · `ecosystem_10k`(192×192, 10,000) —
  풀·토끼·늑대 70/25/5 %, 호수·흙, **보충 없음**
- 밸런스: `life.reproduce` 밀도 제한(crowdRadius · crowdMax), 초식 행동(배고프고 풀이 보일 때만 찾기, 대상을 잃으면 배회),
  육식(energy < 80 일 때 사냥, 15초 휴식), 수치 조정 (11 7장 — 초안 대비 표)
- 성능: 색인 항목에 core.tags 사본, flee 가 통행 가능한 목표를 고름, 감지·충돌 읽기 패스를 Worker 와 나눔(`parallelFor`,
  `SystemContext.jobs`)
- `sbx_bench --only <접두사> --threads n` + `sim.ecosystem` (평균·p95·p99·최대·System 별·종별 수)
- `sbx_sim_check --require-prefabs` (끝에 0 이면 실패) + 종별 개체 수 출력
- kSimVersion 3 → 4, 골든 4개 (ecosystem_small 신규)
- 테스트: 명령 JSON 왕복, 리플레이 왕복(일시정지 편집·Step 포함)·변조 검출·시작 월드 불일치·파일 오류(잘림·magic·CRLF),
  옵션. CTest `det_replay_*` 4개, `det_ecosystem_small(_golden)`, `balance_ecosystem_survival_{1,2,3}`(최적화 빌드만)

**왜**

- Phase 5 완료 조건(16): 시드 3개 18,000틱 공존, D1~D5, 10k 평균 < 10 ms · p99 < 25 ms. 리플레이는 Phase 9 오토세이브·
  Phase 12 에디터 명령 로그의 바탕이다.

**도중에 바꾼 것 / 발견한 것**

- 리플레이 위치를 (tick, editSequence) 대신 tick() 호출 번호로 — 미리 큐에 넣으면 실행 틱이 편집 단계 몫까지 가져간다.
  대상은 saveId 로 — 시작 세이브를 로드한 월드는 netId 를 다시 매긴다 (ADR-0016).
- 첫 survival 실행이 10분 넘게 걸렸다: 풀이 지도를 다 덮을 때까지 지수적으로 늘었다 → 밀도 제한. 초안 수치에서는 토끼가
  "풀이 안 보이면 멈춘다" 행동 때문에 굶었고, 늑대가 토끼를 다 잡은 뒤 굶었다. 풀 crowdMax 6 은 시드 3 에서 붕괴, 8 에서 공존.
- 512×512 의 "10k" 는 3,000틱 뒤 27,000 개체가 된다 → 평형 밀도에서 10,000 근처를 유지하는 192×192 로. 비용은 밀도를 따른다.
- 물 위 flee 목표가 A* 를 확장 상한까지 헤매게 해 경로 비용 4.5 ms/틱 → 통행 가능한 점을 고르게 해 0.08 ms.
- survival 맵을 문서 초안 512×512 에서 256×256 으로 (시드 3개를 수 분 안에 — 13 6장).
- Windows 골든 항목(simVersion 3)은 더 이상 맞지 않아 지웠다 (남겨 두면 SKIP 이 아니라 "갱신 필요" 실패) → 다시 기록 필요.

**검증**

```text
linux-clang(19.1.1) · linux-gcc(13.3): Debug 30/30, RelWithDebInfo 33/33 (balance 3개 포함), -Werror
linux-clang-asan Debug 29/29 (bench 제외), doctest 168 케이스
D1·D2·D5 ecosystem_small --repeat 2 --save-at 450 --threads 0,1,8 일치
D3  det_replay_{random_walk_1k, world_save_load, eco_lifecycle, ecosystem_small} 일치. 리플레이 한 줄을 고치면
    다음 해시 레코드에서 "불일치 (D3) … 버그 (같은 simVersion)" (수동 + 단위 테스트)
D4  골든 4개 kSimVersion 4 — Clang 19 · GCC 13 같은 해시
공존 ecosystem_survival 18,000틱 시드 1·2·3: 풀 7,218/7,868/7,535 · 토끼 1,005/981/987 · 늑대 262/251/255
성능 sim.ecosystem (ecosystem_10k → 14,710 개체): Worker 1 평균 7.83 · p99 13.56 ms, Worker 0 평균 10.48 · p99 15.95 ms
MinGW GCC 13 (posix) 교차 빌드 성공, include lint 0건 (161 파일), clang-format 적용
```

**미검증 / 남은 일**

- Windows: 골든 4개 다시 기록 (`--record-golden`, ecosystem_small 포함), ctest 전체 (balance 는 Release/RelWithDebInfo 구성에서).
- Worker 0 에서 ecosystem_10k 평균 10.5 ms (기준 10 ms 근소 초과) — 감지 TagMask 색인(S5)·반경 조정 후보 (14 7.5).
- 경로 캐시·HPA*, 바이너리 리플레이(Phase 9), 에디터 명령 로그 시나리오 edit_session(Phase 12).
- 다음: Phase 6 — Windows 플랫폼 (IWindow · Win32Window · InputSystem · SandboxClient 빈 창).

---

## 2026-10-05 — Phase 5B: 감지 · 행동 · 상호작용 · 경로 · 충돌 · JobSystem

**무엇을**

- 컴포넌트 5개 (카탈로그 18개): `core.movement`(maxSpeed·accel·arriveRadius·goal·hasGoal), `core.collider`(radius·layer·mask),
  `ai.sensor`(radius), `ai.behavior`(graph·state·enteredTick·target(saveId)·blackboard[8]),
  `ai.path`(start·goal·state·submittedTick·partial·waypoints ≤16·cursor). 틱 캐시 필드는 reflect 하지 않는다 (ADR-0015)
- 콘텐츠 link 단계: action 번호·action 별 Rule 색인(priority 내림, 정의 순)·Behavior 감지 질의(sensed·seek·flee 의 TagExpr,
  그래프당 ≤ 8, 넘으면 V7). `ContentDatabase::actions/findAction/rulesForAction`
- System 7개 + Movement 조향 (`core/systems/`, 등록 `DefaultSystems.cpp`):
  Stage 5 PathCollect · 6 Sensor · 7 Behavior(FSM, 노드 어휘 전부) · 8 PathRequest · 9 Movement(조향) · 10 Interaction ·
  11 ResolveIntents(Effect op 전부: field.add/set · destroy · spawn · tag.add/remove · event) · 16 Collision
- 경로 (`core/path/`): `PathGridSnapshot`(불변, 바뀐 청크만 다시 복사), `tileLineClear`(Amanatides–Woo, 코너 컷 금지),
  A*(8방향, 정수 비용, (f, h, 순번) 전순서, 확장 상한·부분 경로, 직선 검사로 경유점 줄이기), `PathfindingService`
  (Job 계약 T→T+1, 로드 후 Submitted 재제출)
- `foundation/job/JobSystem`(Worker 풀, 0 = 그 자리 실행, `JobGroup::wait`). `SimulationWorld::setJobSystem`
- `SimulationWorld`: `SaveIndex`(saveId → EntityId), `IntentBuffer`, `PathfindingService`, `resolveSave`,
  `SystemContext` 에 catalog · saves · intents · paths. `RandomService::Purpose::Interaction`
- `sbx_sim_check --threads n[,n..]` (D5) — 실행마다 자기 JobSystem, 나란히 비교. CTest `det_eco_lifecycle` 에 `--threads 0,1,8`
- eco 팩: rabbit·wolf 에 이동·충돌·감지·행동·경로. 늑대 targetInRange 0.9 · Rule range 1.0 (충돌 반지름 합 0.7 보다 넉넉히)
- kSimVersion 2 → 3, 골든 3개 재기록
- 테스트: JobSystem, 경로(스냅샷 불변·직선 검사·우회·부분 경로·서비스 D5), 콘텐츠 link·V7, AI(사냥→먹기·이벤트·파괴,
  배타 Rule, "*" 자기 전이·stateTime, 경로 T→T+1 우회·Worker 0/4 동일, **진행 중 Job 저장→로드 D2**, 조향, 충돌)

**왜**

- 생태계가 "먹고 먹히는" 상태가 되어야 5C 의 밸런스·리플레이·성능 측정이 의미가 있다. 경로 Job 은 첫 Worker 사용처라
  D5 검사를 여기서 세운다 (Phase 3 에서 이월).

**도중에 바꾼 것 / 발견한 것**

- 문서의 ai.path_request + ai.path_follow → ai.path 하나 (구조 변경 없이 상태 전이). ai.sensor 의 mask → 그래프 질의.
  대상 참조는 saveId. 진행 중 Job 은 저장하지 않고 로드 때 같은 입력으로 다시 제출 (ADR-0015).
- 처음엔 Behavior 그래프 캐시를 포인터로 두었다가 02 C2(컴포넌트에 포인터 금지)에 맞춰 인덱스 + id 확인으로 바꿨다.
- "*" → 지금 상태 자신으로의 전이는 건너뛴다 — 그렇지 않으면 eco.herbivore 의 flee 가 매 틱 다시 들어가 stateTime 이 0.
- 중심 거리 사거리와 충돌 분리: 분리가 반지름 합까지 밀어내므로 targetInRange·range 를 그보다 크게 둬야 먹을 수 있다.
- 클라우드 컨테이너가 2코어·8 GB 인데 -j16 으로 빌드하다 cc1plus 가 OOM 으로 죽었다 → -j2. 빌드 도중 헤더를 고쳐
  서로 다른 레이아웃으로 컴파일된 목적 파일이 섞인 바이너리가 D2 테스트를 깨뜨린 일이 있었다 (다시 빌드하니 통과) —
  검증 결과는 소스를 고정한 뒤 처음부터 다시 돌린 것만 적는다.

**검증**

```text
linux-clang(19.1.1) · linux-gcc(13.3) × Debug · RelWithDebInfo, -Werror → 각 ctest 24/24 (소스를 고정한 뒤 처음부터 다시 돌린 결과)
linux-clang-asan Debug → 23/23 (bench 제외)
doctest 162 케이스
D1·D2·D5 eco_lifecycle --repeat 2 --save-at 900 --threads 0,1,8 일치. 저장 시점 101~110·211·333·517·700 에서도 D2 일치
D2  단위 테스트: 경로 Job 이 진행 중(Submitted)일 때 저장 → 로드(해시 검증 일치) → 400틱 매 틱 같은 해시
D4  골든 3개 kSimVersion 3 으로 재기록 — Clang 19 · GCC 13 같은 해시
D5  sbx_sim_check --threads 0,1,8 · 경로 서비스 단위 테스트(Worker 0·1·4) 같은 결과
실행 SandboxServer --scenario eco_lifecycle --ticks 300 정상 종료 (overruns 0)
성능 (RelWithDebInfo) eco_lifecycle 평균 tick 0.11 ms (~500 개체), random_walk_10k 2.0 ms (Phase 4: 1.9) — 14-PERFORMANCE 7.4
MinGW GCC 13 (posix) 크로스 컴파일 (Windows 대상, -Werror) 성공 (실행은 못 함)
include lint 0건 (152 파일), clang-format 적용 (사용자가 고친 test_error.cpp 는 그대로 둠)
```

**후속 (2026-10-06, 사용자 PC 피드백)**

- 사용자가 `cmake-build-debug\bin` 에서 `--record-golden tests/golden/...` 을 실행 → 상대 경로라 파일이 없었고, 도구는 골든 파일이
  없으면 기본 시나리오(random_walk_1k)를 돌린 뒤 쓰기 단계에서야 실패했다 (세 명령 모두 random_walk_1k 로 돌았다).
  → 골든 파일이 없으면 시뮬레이션 전에 절대 경로·현재 폴더와 함께 거절, 새 골든은 `--scenario` 필수, 쓸 폴더가 없으면 거절.
- Windows 콘솔(cp949)에서 한국어 출력이 깨졌다 → `foundation/io/Console`(SetConsoleOutputCP 65001, windows.h 없이 선언 하나)을
  sbx_sim_check · SandboxServer 시작 때 부른다.
- MSVC 19.44 에서 Phase 5B 코드가 빌드·실행됐다 (사용자 출력: random_walk_1k simVersion 3 최종 해시 0x3c9524ea60fc4ef7 —
  Linux 골든과 같다). 검증: linux-clang Debug 24/24, MinGW 교차 빌드, include lint 0건.
- 사용자가 저장소 루트에서 골든 3개에 Windows-x86_64-MSVC-19.44 항목을 기록했다 (simVersion 3). 세 파일 모두 Clang 19 · GCC 13 ·
  MSVC 19.44 의 **모든 체크포인트 해시가 같다** — 생태계(감지·FSM·A*·충돌, sqrt 포함)까지 세 컴파일러 일치. 콘솔 한국어 정상 출력.

**미검증 / 남은 일**

- Windows ctest 전체 (Phase 5A·5B 코드). 사용자 PC: 골든 3개에 `--record-golden` (simVersion 3 — 지금 Windows 항목 없음).
- 밸런스: 1800틱 eco_lifecycle 에서 토끼가 늘고 풀이 줄어드는 쪽 — 7장 수치 조정·18,000틱 3시드 공존은 5C.
- 경로 캐시·HPA*·그룹 경로, 감지 TagMask(S5), 10k 에서 경로 예산·감지 비용 측정 (5C 의 sbx_bench sim.ecosystem).
- 다음: 5C — Replay 기록·재생(D3), ecosystem_small/ecosystem_survival, sbx_bench sim.ecosystem 10k, 밸런스.

---

## 2026-10-05 — Phase 5A: 콘텐츠 · 생명 주기

**무엇을**

- Phase 5 를 셋으로 나눴다: **5A** 콘텐츠·생명 주기 → 5B 행동·상호작용·경로 → 5C 리플레이·밸런스·성능 (16-ROADMAP).
- 필드 타입 `FixedString<N>` / `ContentId`(47 바이트) — JSON 문자열, 해시는 길이+바이트. `Hint::PrefabRef`·`BehaviorRef`.
  `FieldAccess` 방문자로 `ComponentInfo::fields`·`findField`·`getNumber`·`applyNumber`(FieldMeta 범위로 자르기) — Rule 조건·효과용
- 콘텐츠 (`core/content/`): `TagSet`(256 비트)·`TagExpr{all, any, none}`, `ContentModel`(Prefab·Rule·BehaviorGraph·PackInfo),
  `ContentLoader` — pack.json 탐색·requires 위상 정렬·태그·머티리얼·Prefab·Rule·Behavior 파싱, 검증기 V1~V7(모든 문제를 모아
  "<파일>:<JSON 포인터>" 로 보고), contentHash(CRLF 정규화, presentation/ 제외). 내장 "core" 팩
- 컴포넌트: `core.tags`, `core.prefab`(출처), `life.energy`·`life.health`·`life.growth`·`life.reproduce` (카탈로그 13개)
- 생성: `CreateEntity{prefab}` 명령과 System 의 `SpawnQueue` 가 같은 `SimulationWorld::instantiate` 를 쓴다
  (Prefab + 덮어쓰기 키 단위 병합, 위치 우선순위, 검증, 경계). SpawnQueue 는 (parent saveId, seq) 정렬, uniqueTile
- `LifecycleSystem` 재작성: 에너지 소모·체력·나이·수명 → 사망(saveId 순, `Died` 이벤트 + `DeathCause`), 성장, 번식
  (cooldown·minEnergy·minStage·chance, `stream(Spawn, saveId)`, AdjacentEmptyTile/Nearby)
- 팩 `content/ecosystem` ("eco"): 태그 7, 머티리얼 3, Prefab grass·rabbit·wolf(11 7장 수치 + Opaque render.sprite),
  Rule 3(먹기), Behavior 2(초식·육식) — Rule·Behavior 실행은 5B
- 세이브: world.json 에 content{packs, contentHash}·tags[]·opaqueComponents[], 로드 때 태그 비트 재매핑·콘텐츠 차이 경고,
  로드 해시 검증 조건을 Opaque 이름 집합 비교로 (ADR-0014)
- 시나리오 `eco_lifecycle` (128×128, 풀 400·토끼 60·늑대 10, 300틱마다 토끼 +10, 1800틱), `IScenario::requiredPacks`
- 도구: `sbx_sim_check --validate-content <팩…>`·`--content-root`, `SandboxServer --content-root`, 기본 콘텐츠 경로
  `SBX_DEFAULT_CONTENT_DIR`
- 테스트: 콘텐츠 로더(정상 팩 + 규칙별 틀린 팩), 필드 접근, Prefab 생성·생명 주기·번식·SpawnQueue 순서, 세이브 태그 재매핑.
  CTest `det_eco_lifecycle`, `det_eco_lifecycle_golden`, `content_eco_validate`, `unit_content`. 골든 `eco_lifecycle`

**왜**

- 5B 의 Behavior·Rule 실행은 콘텐츠 모델과 Prefab 생성이 먼저 있어야 시험할 수 있다. 9개 티켓을 한 번에 검증하면
  결정론 위반의 원인을 좁히기 어렵다 — 행동 없이 생명 주기만 도는 시나리오로 먼저 D1·D2·D4 를 고정한다.

**도중에 바꾼 것 / 발견한 것**

- Prefab 생성은 ECB 가 아닌 SpawnQueue (ADR-0014 1번) — 5B 에서 System 을 Job 으로 나눠도 saveId 할당 순서가 그대로다.
- Phase 4 의 opaqueEntities("Opaque 가 있으면 해시 비교 건너뜀")로는 render.sprite 를 가진 생태계 세이브가 검증되지 않는다
  → 저장·로드 프로세스의 Opaque 이름 집합이 같으면 비교 (ADR-0014 3번, ADR-0013 2번 일부 대체).
- nlohmann::json `*j.items().begin()` 을 구조적 바인딩으로 잡으면 임시 프록시에 매달린다 — Debug 는 통과, RelWithDebInfo 에서
  invalid_iterator 예외. `j.begin()` 반복자로 고쳤다. 최적화 빌드 테스트가 잡았다.
- `requires` 는 C++20 키워드라 `PackInfo::dependencies` 로. 11 5장 예시의 flags "water" → 구현은 "Water"(대문자) — 11 0장에 기록.
- 기존 골든(random_walk_1k, world_save_load)이 변하지 않았다 → kSimVersion 은 2 그대로.

**검증**

```text
linux-clang(19.1.1) · linux-gcc(13.3) × Debug · RelWithDebInfo, -Werror → 각 ctest 24/24
linux-clang-asan Debug → 23/23 (bench 제외)
doctest 144 케이스
콘텐츠 content/ecosystem 오류·경고 0 (content_eco_validate)
D1  eco_lifecycle --repeat 2 일치
D2  eco_lifecycle 900틱 저장 → 1800틱까지 동일, 로드 해시 검증 일치 (태그 표 · Opaque render.sprite 를 지나는 왕복)
D4  eco_lifecycle 골든 신규 — Clang 19 · GCC 13 기록, 두 컴파일러 같은 해시. 기존 골든 2개 변화 없음
MinGW GCC 13 크로스 컴파일 (Windows 대상, -Werror) 성공 (실행은 못 함)
include lint 0건, clang-format 적용
```

**사용자 PC 결과 반영 (Windows · MSVC 19.44 · CLion Debug, Phase 4 코드 — 2026-10-05 19:30 의 CTest 로그)**

- 19개 중 17개 통과. D1·D2(폴더 교체 rename 포함) 통과 — Windows 실행 첫 확인. `random_walk_1k`·`world_save_load` 의
  최종 해시(600틱)가 Linux 골든과 같다 (Clang·GCC·MSVC 세 컴파일러 일치). 골든 2개는 Windows 항목이 없어 SKIP.
- 실패 1 `arch_include_lint_selftest`: 린터가 위반 메시지의 '—' 를 cp949 콘솔에 쓰다 UnicodeEncodeError.
  → 스크립트가 stdout/stderr 를 UTF-8 로 고정. 사용자가 `-X utf8` 을 붙인 수정은 유지하되, 그 수정이 들어간 쪽이
  `arch_include_lint`(저장소 전체 검사)여서 이 테스트가 픽스처를 검사하게 바뀌어 있었다 → `--root` 를 저장소로 되돌리고
  `-X utf8` 은 두 테스트 모두에 적용.
- 실패 2 `arch_link_boundary_selftest`: 중첩 CMake 구성의 링크가 LNK1104(kernel32.lib) — IDE 가 vcvars 환경을 CTest 에
  주지 않는다. → MSVC 면 구성 시점의 LIB·INCLUDE·LIBPATH 를 테스트 환경으로 넘기고, 컴파일러 자체를 못 쓰면 SKIP.
- 사용자가 `test_error.cpp` 에 `<ostream>`·`<string_view>` 를 넣은 MSVC 컴파일 수정은 유지. 같은 문제가 다른 테스트에서
  나지 않게 `sbx_doctest` 에 `DOCTEST_CONFIG_USE_STD_HEADERS` (doctest 가 std 를 앞선언하지 않고 헤더를 포함).
- IDE 가 표를 정렬한 ADR 0003~0011·external/README 는 기기 쪽을 그대로 가져왔다.

**미검증 / 남은 일**

- Phase 5A 코드의 MSVC 빌드 (MinGW 크로스만 확인). 사용자 PC: 골든 3개(`random_walk_1k`·`world_save_load`·`eco_lifecycle`)에
  `--record-golden` 으로 Windows 항목 기록 — 그 전까지 `det_*_golden` 은 SKIP 이 정상.
- Prefab 상속(`inherits`), enum 필드의 이름 표기, actions.json(Phase 12) — 11 0장 `[계획]`.
- SpawnQueue 상한 (5C 성능 측정에서), EntityRef 2-pass (5B 의 ai.* 대상 참조와 함께).
- 다음: 5B — SensorSystem·BehaviorSystem(FSM)·InteractionSystem·Rule 효과, 조향 이동·충돌, PathfindingService + JobSystem(D5).

---

## 2026-10-05 — Phase 4: World · Save/Load

**무엇을**

- 월드 (`core/world/`): `ChunkCoord`(floorDiv, y 우선 비교), 지형 레이어(SoA: material·flags·moveCost·height),
  `Chunk`(terrainRevision, revision 키 해시 캐시), `WorldGrid`(고정 경계, 기본 16×16 청크, paint, 점 포함·자르기)
- 콘텐츠 (`core/content/`): 최소 `ContentDatabase` — `TerrainMaterial` (id 정렬 인덱스, 검증, 내장 4종, contentHash)
- 명령: `PaintTerrain{materialId, cells | center·shape·radius}` — id 로 지정, 경계·상한 검증, 바뀐 청크만 revision +1
- 경계: Create/Move/AddComponent·ChangeComponent(core.transform)가 월드 밖이면 OutOfRange, Movement 는 경계로 자른다
- 공간 색인: 월드 경계 안 격자 + 카운팅 정렬, `forEachInChunk`·`countInChunk`
- WorldHash: 지형 추가 (머티리얼은 id 의 stableId). `kSimVersion` 1 → 2, 골든 재기록
- 세이브 (`core/persist/`): `saveWorld`/`loadWorld` — world.json · entities.jsonl · chunks/*.chunk, 머티리얼 재매핑,
  Opaque 보존, 컴포넌트 `MigrationRegistry`, 로드 시 worldHash 검증 (ADR-0013). `foundation/io/FileIo`
- 시나리오 `world_save_load`(128×128, 300 개체, 10틱마다 지형 칠하기). `ScenarioRunner` 가 로드한 월드로 이어 돌리기
- `sbx_sim_check --save-at T [--save-dir]` (D2: 저장→로드한 월드를 나란히 끝까지 비교)
- 테스트: 월드 격자·지형·콘텐츠·PaintTerrain·경계, 청크 파일, 마이그레이션, D2 왕복, 재매핑, Opaque, 손상 세이브, CRLF,
  커밋된 샘플 세이브 `tests/data/saves/v1_sample`. CTest `det_world_save_load`, `det_random_walk_save_paused`,
  `det_world_save_load_golden`, `unit_persist`
- 벤치: `save.world` 50k

**왜**

- 세이브 왕복(D2)이 되어야 리플레이(Phase 5)의 시작점과 에디터의 저장(Phase 12)이 성립한다. 청크는 저장·전송·경로·렌더의
  공통 단위라 (05 2.1) 먼저 고정 경계로 단순하게 세운다.

**도중에 바꾼 것 / 발견한 것**

- 원자성: 파일별 rename(초안) 대신 폴더 교체 — 세트 불일치와 지난 세이브의 청크 파일 잔재를 함께 막는다 (ADR-0013).
- 로드가 world.json 의 worldHash 와 스스로 비교한다. 첫 구현에서 "Opaque 를 가진 프로세스가 저장 → 그 컴포넌트를 아는
  프로세스가 로드" 경로가 해시 불일치로 실패하는 것을 테스트가 잡았다 → world.json 에 opaqueEntities 를 남기고 그때는 비교를 건너뛴다.
- 지형 해시를 머티리얼 인덱스가 아닌 id 로 — 콘텐츠 순서가 바뀐 재매핑 로드에서도 검증이 성립한다.
- Chunk 에 entityCount·state 를 두지 않았다 (공간 색인이 O(16) 으로 계산, state 는 스트리밍과 함께) — 05 0장.
- 공간 색인 카운팅 정렬로 50k 재구성 6.8 → 1.1 ms, 1k tick 0.29 → 0.18 ms, 10k 3.8 → 1.9 ms.
- 사용자 PC 의 IDE 가 docs 의 마크다운 표를 열 맞춤으로 바꿔 둔 것을 확인 (내용 변화 없음). 이번 문서 수정도 같은 형식으로 맞췄다.
  00·06·10·design 문서는 표 정렬 외의 차이도 있어 기기 쪽을 기준으로 가져왔다 (Phase 3 커밋 때).

**검증**

```text
linux-clang(19.1.1) · linux-gcc(13.3) × Debug · RelWithDebInfo, -Werror → 각 ctest 20/20
linux-clang-asan Debug → 19/19 (bench 제외)
doctest 126 케이스
D1  det_random_walk_repeat · det_world_save_load(--repeat 2) 일치
D2  world_save_load 250틱 저장 → 600틱까지 동일, random_walk_1k 일시정지 300틱 저장 → 420틱까지 동일, 로드 해시 검증 일치
D4  random_walk_1k(simVersion 2), world_save_load — Clang 19 · GCC 13 기록, 두 컴파일러 같은 해시
성능 (RelWithDebInfo, 클라우드 컨테이너) 50k 저장 188 ms / 로드 504 ms (기준 저장 < 1 s)
MinGW GCC 13 크로스 컴파일 (Windows 대상, -Werror) 성공 (실행은 못 함)
include lint 0건, clang-format 적용
```

**미검증 / 남은 일**

- Windows 실행·MSVC. Windows 에서 폴더 교체 rename 이 실제로 동작하는지 (탐색기·백신이 폴더를 잡는 경우) — 사용자 PC 확인 필요.
- 사용자 PC: `det_*_golden` 2개는 SKIP 이 정상 → 두 골든 파일에 `--record-golden` (simVersion 2 로 바뀌었으므로 Phase 3 에서
  기록한 Windows 항목이 있다면 그것도 다시).
- 증분 저장·zstd·Worker 직렬화 (Phase 9 오토세이브), EntityRef 2-pass (Phase 5), 스키마 마이그레이션 체인.
- 다음: Phase 5 Ecosystem (ContentDatabase 완성, Sensor·Behavior·Interaction·Rule, Pathfinding Job, Replay, D3·D5).

---

## 2026-10-05 — Phase 3: 헤드리스 시뮬레이션 · 결정론 하네스

**무엇을**

- 시뮬레이션 (`core/simulation/`): `SimulationClock`(일시정지·Step·속도·editSequence), `ISystem`/`SystemContext`/`Stage`,
  `SystemScheduler`(Stage → 등록 순서, System 별 ECB + 구조 잠금, `ISystemProfiler` 훅), `SimulationWorld`
  (Stage 0·2·3·4·5~16·17, 일시정지 편집 단계), `EventStream`, `kSimVersion = 1`
- 명령 (`core/command/`): `SimCommand` variant (Create/Delete/Move/Add/Remove/ChangeComponent, Pause/Resume/Step/Speed),
  `CommandQueue`((executeTick, issuer, sequence) 정렬), `CommandResult`. 적용기는 명령마다 원자적
- 정체성: `persist.persistence{saveId}`, `net.identity{netId}` — SimulationWorld 가 생성 직후 부여, 명령으로 변경 불가
- 난수 (`core/random/`): `CounterRng`(SplitMix64 카운터, 동결), `RandomService.stream(Purpose, saveId)`
- 공간 색인 (`core/world/SpatialIndex`): 정렬 배열 격자, radius/AABB/nearest, 결과 순서 (셀 y, 셀 x, saveId)
- System (`core/systems/`): RandomWalk(Stage 7, 진단용), Movement(9), Lifecycle(15: `core.lifetime`, `life.age`)
- WorldHash (`core/replay/`): saveId 순서, Hashed 컴포넌트 stableId 순서, 구조 오류는 Error. 엔티티별 해시·JSON 진단
- 시나리오 (`core/scenarios/`): `IScenario`, `ScenarioRunner`, `random_walk_1k`/`random_walk_10k` (명령만으로 월드를 바꾼다)
- 도구: `sbx_sim_check` (`tools/sim_check/`, `SBX_BUILD_TOOLS`) — `--repeat`(D1), `--golden`/`--record-golden`(D4),
  불일치 시 엔티티→컴포넌트→필드 진단, `--inject-divergence`(하네스 자체 시험)
- 서버: `SandboxServer --scenario <name> [--ticks] [--seed] [--realtime]` 헤드리스 실행 (최종 해시·시간 출력)
- 골든: `tests/golden/random_walk_1k.json` (Linux-x86_64-Clang-19, Linux-x86_64-GNU-13) — 툴체인별 (ADR-0012)
- 빌드 정보: `kToolchainKey` 추가
- 벤치: `sim.random_walk` 1k/10k, `sim.spatial` 50k
- 테스트: RNG(동결 값), 명령 큐, 공간 색인 단위 + 속성(전수 탐색 대조), SimulationWorld(명령 경로·원자성·일시정지·
  Lifecycle·ECB 정체성·스케줄 순서·해시 성질), 시나리오(D1 축소판), sim_check·서버 옵션.
  CTest `det_random_walk_repeat`, `det_random_walk_golden`(항목 없으면 SKIP), `det_harness_selftest`, `server_scenario_smoke`

**왜**

- 렌더러보다 먼저 헤드리스로 "정답"을 고정한다 (16-ROADMAP 순서의 이유). 이후 모든 Phase 의 회귀는 골든 해시와
  `--repeat` 로 잡는다. 시나리오가 사람(에디터)과 같은 명령 경로를 쓰므로 Phase 12 의 편집 경로가 지금부터 시험된다.

**도중에 바꾼 것 / 발견한 것**

- 사용자 Windows 실행 결과 (CLion, MinGW GCC 15.2, `cmake-build-debug`): 8개 중 7개 통과 (property 3.9 s 포함).
  `arch_link_boundary_selftest` 가 "unable to find a build program corresponding to Ninja" 로 실패 — CLion 은 Ninja 를
  PATH 가 아니라 CMAKE_MAKE_PROGRAM 으로 넘기는데 중첩 cmake 호출이 그것을 받지 못했다. → `CMAKE_MAKE_PROGRAM`·
  `CMAKE_TOOLCHAIN_FILE` 을 전달하도록 수정, PATH 에 ninja 가 없는 환경을 만들어 재현·검증. Python3 은 여전히 못 찾음
  (arch_include_lint 미등록 — 15-BUILD 9장에 안내 추가).
- WorldHash 엔티티 순서를 문서 초안의 `EntityId.index` 에서 **saveId** 로 바꿨다 (로드 후 index 가 달라져도 같은 해시 — D2).
- 골든을 툴체인별로 기록 (ADR-0012). 시나리오(입력) 변경은 simVersion 대상이 아니라는 예외를 04 6장에 명시.
- JSON 읽기 버그 (Phase 2 코드): 코드에서 만든 `Json(100)` 은 signed 정수로 저장되는데 부호 없는 필드가 이를 거절했다.
  파일에서 읽은 JSON 은 양수를 unsigned 로 만들기 때문에 Phase 2 테스트가 못 잡았다. 음수만 거절하도록 수정.
- SpatialIndex: AABB 가 int32 전 범위를 덮으면 폭×높이(2^32 × 2^32)가 u64 를 넘쳐 0 이 되고, 격자 순회가 사실상 무한 루프.
  단위 테스트("huge query")가 잡았다 → 폭·높이를 먼저 셀 수와 비교.
- `editSequence` 가 실행 중 적용된 PauseSimulation 에도 오르던 것 → "이번 tick() 이 편집 단계인가" 로 판정.
- System 시간 측정은 Core 밖에서 주입 (`ISystemProfiler`) — Core 에 `<chrono>` 를 들이지 않는다 (04 4.5).
- `--threads`(D5)는 JobSystem 이 없어 Unsupported 로 거절하고 Phase 5 로 옮겼다 (16-ROADMAP 5.9). 단일 스레드에서 D5 를
  "통과"로 표시하지 않기 위해서다.
- 측정: SpatialIndex 50k 재구성 6.8 ms — 05-WORLD 4.1 의 1 ms 조건 초과. Phase 4 카운팅 정렬로 대응 (05-WORLD 4.4).

**검증**

```text
linux-clang(19.1.1) · linux-gcc(13.3) × Debug · RelWithDebInfo, -Werror → 각 ctest 16/16
linux-clang-asan Debug → 15/15 (bench 제외)
doctest 111 케이스 / 단언 약 152만 (대부분 속성 테스트)
D1  sbx_sim_check --repeat 2 --ticks 600: 일치.  하네스 자체 시험: tick 60 에서 saveId 1 의 core.transform.position 까지 좁힘
D4  Clang 19 · GCC 13 항목 기록, 두 컴파일러 × 두 빌드 설정 모두 같은 해시 (0x0a5ae056a0fb401b @600)
성능 (RelWithDebInfo, 클라우드 컨테이너) random_walk 1k 평균 0.29 ms (기준 < 2 ms), 10k 3.8 ms
MinGW GCC 13 크로스 컴파일 (Windows 대상, -Werror): Server·Tests·sim_check·bench 빌드 성공 (실행은 못 함)
arch_link_boundary_selftest: PATH 에 ninja 없이 CMAKE_MAKE_PROGRAM 만 준 구성에서 통과
include lint 0건, clang-format 적용
```

**미검증 / 남은 일**

- Windows 실행 (MSVC·MinGW): 사용자 PC 에서 `det_random_walk_golden` 은 SKIP 이 정상 →
  `sbx_sim_check --record-golden tests/golden/random_walk_1k.json` 으로 기록해 커밋. MSVC 빌드 자체도 아직 미검증.
- D5(`--threads`), p99 tick 측정, 공간 색인 카운팅 정렬, 명령 권한·속도 제한 — Phase 4·5·9~12.
- 다음: Phase 4 World (Chunk · Terrain · Save/Load, D2).

---

## 2026-10-05 — Phase 2: Core ECS

**무엇을**

- Foundation: `SmallVector<T,N>`, `Vec2`/`Vec2i`
- ECS (`core/ecs/`): `EntityId`, `EntityManager`(LIFO·퇴역 슬롯), `ComponentPool`(4096 페이지 sparse set, P1~P3 `validate()`),
  `SBX_COMPONENT`/`ComponentTraits`(이름 규칙 컴파일 타임 검사, Persistent→Hashed 자동, `NotHashed`),
  `Registry`(구조 잠금, emplaceOrReplace, 리소스, `forEachEntityByIndex`, `poolsByStableId`, `adoptPool`),
  `View<Read/Write/Exclude>`(최소 풀 드라이버, Write 시 changed 틱), `EntityCommandBuffer`(PendingEntity, 상쇄),
  `ComponentCatalog`(명시 등록 — ADR-0011)
- 직렬화: 리플렉션 계약(`reflect(V&, T&)`), `JsonWriter/Reader`(기본값 유지·타입/범위/모르는 키 오류), `HashVisitor`(H1)
- 컴포넌트: `core.transform`, `core.velocity` + `registerCoreComponents`
- 외부: nlohmann/json v3.12.0 vendored (`external/nlohmann_json`), `external/CMakeLists.txt` 로 타깃화
- 테스트: 단위(ecs 스위트 신설), 속성 테스트(Registry vs `std::map` 참조 모델, 시드 3개), 결정성(같은 시퀀스 → 같은 id·순서)
- 벤치: `sbx_bench`(`SBX_BUILD_BENCH`), `ecs.iterate`·`ecs.churn`, 기준선 `bench/baselines/cloud-sandbox-container.json`
- CMake: MinGW 로 구성하면 경고 (사용자 CLion 이 기본 MinGW 프로필로 구성한 것을 확인했기 때문)

**왜**

- Phase 3(시뮬레이션 루프)이 올라설 저장소·순회·구조 변경·해시의 기반. 해시·세이브가 리플렉션 하나를 공유해
  "해시에 빠진 필드"(RTS H3) 문제를 구조로 막는다.

**도중에 바꾼 것 / 발견한 것**

- 구조 잠금 중 emplace 는 Release 에서도 중단 (참조 반환이라 무시 불가) — 02-ECS 0장에 반영.
- ECB emplace = 있으면 교체. Registry 에 emplaceOrReplace 추가.
- 벤치 첫 실행에서 4-컴포넌트 시나리오가 빈 view 를 재고 있었다(0.13 ns) → 방문 수 검사를 넣어 무의미한 측정이면 중단하게 함.
- GCC 13 -O2 `-Warray-bounds` 오진 (SmallVector relocate 의 memmove) → 명시적 루프로 우회.
- doctest `REQUIRE_MESSAGE` 에 삼항식을 넣으면 컴파일 실패 → `INFO` + `REQUIRE` 로.

**검증**

```text
linux-clang(19.1.1) · linux-gcc(13.3) · linux-clang-asan  ×  Debug · RelWithDebInfo, -Werror
→ 6개 조합 각 ctest 11/11 (doctest 76 케이스, 단언 약 147만 — 대부분 속성 테스트)
MinGW GCC 13 크로스 컴파일 (Windows 대상, -Werror): Server·Tests·bench 빌드 성공 (실행은 못 함)
include lint 58 파일 0건, 링크 경계 3 타깃 통과
벤치 (RelWithDebInfo, 클라우드 컨테이너): 10k×4 컴포넌트 8.2 ns/entity, 50k×4 10.9 ns/entity
사용자 PC(D:\Game\Sandbox\cmake-build-debug) 확인: CLion 기본 MinGW GCC 15.2 로 Phase 1 SandboxServer 빌드 성공,
  테스트는 빌드·실행되지 않았고 Python 을 찾지 못함 (arch 테스트 미등록)
```

**미검증 / 남은 일**

- MSVC 빌드 (특히 `SBX_COMPONENT` 의 전역 네임스페이스 특수화, View 의 템플릿 람다) — 사용자 PC 또는 CI.
- EnTT 기준선 비교, Opaque 컴포넌트, Binary/Bit/ImGui Visitor, `removedLog<T>` — 해당 Phase.
- 다음: Phase 3 헤드리스 시뮬레이션.

---

## 2026-10-05 — Phase 1: 저장소 골격

**무엇을**

- 빌드: 루트 `CMakeLists.txt`, `CMakePresets.json`(7 configure 프리셋, Ninja Multi-Config), `cmake/Sbx*.cmake`
  - `sbx_warnings`(MSVC `/utf-8` 포함), `sbx_strict_conversions`, `sbx_simulation_flags`(Core 가 PUBLIC 전파)
  - 표준 라이브러리 기능 확인(`std::expected`/`format`/`source_location`) — 안 되면 원인 한 줄로 중단
  - `CMAKE_CXX_SCAN_FOR_MODULES OFF`, 실행 파일은 `build/<preset>/bin/<Config>/`
  - `BuildInfo.hpp` 생성(버전·커밋·컴파일러)
- 경계 강제: `sbx_check_link_boundaries()`(전이적 링크 그래프), `tools/check_includes.py`(모듈 방향·금지 헤더·SFML/OpenGL·`#import`),
  각각 자체 시험(`arch_link_boundary_selftest`, `arch_include_lint_selftest` + `tests/arch/fixtures`)
- Foundation: `Types`, `Error`/`Expected`, `SBX_ASSERT`/`SBX_VERIFY`(처리기 교체 가능), `log`, `Fnv1a64`(동결 상수 테스트), `Handle<Tag>`
- Core: `core/simulation/SimConstants`(30 TPS, 고정 dt, 스냅샷 주기 약수 규칙)
- SandboxServer: `--help`/`--version`/`--log-level`, 종료 코드 0/1/2
- SandboxTests: doctest v2.5.0 vendored, 29 케이스
- 저장소 설정: `.gitignore`, `.gitattributes`, `.editorconfig`, `.clang-format`(적용 완료), `.clang-tidy`
- CI: `.github/workflows/ci.yml` (Windows MSVC, Linux clang-19/gcc-13/asan, macOS 15)

**왜**

- Phase 0 에서 정한 경계(ADR-0005)를 문서가 아니라 빌드·테스트 실패로 강제하기 위해. RTS 에서 "core 는 SFML 을 모른다"가
  문서에만 있고 코드에서 깨져 있던 것을 반복하지 않는다.

**도중에 바꾼 것**

- Clang 18 + libstdc++ 13 은 `std::expected`를 비활성화한다 → Linux 최소 요구를 Clang 19 로 올리고 구성 단계 검사 추가.
- CMake 3.28 + C++23 은 모듈 스캔을 켜서 clang-scan-deps 가 없으면 전부 실패한다 → 모듈 스캔 끔.
- `toString(enum)` 자유 함수가 doctest 와 ADL 충돌 → `errorCodeName`/`levelName`으로 개명, 규칙을 12-CODING-STANDARDS 에 추가.
- `SmallVector`는 첫 사용처인 Phase 2 로 이동.

**검증** (Linux 클라우드 환경)

```text
linux-clang (clang 19.1.1) · linux-gcc (gcc 13.3) · linux-clang-asan   ×   Debug · RelWithDebInfo, -Werror
→ 6개 조합 모두 ctest 8/8 통과 (doctest 29 케이스 / 68 단언)
SandboxServer --version → "SandboxServer 0.1.0 (commit unknown, Linux, Clang 19.1.1) / tick 30 Hz, dt 0.0333333 s"
인자 없음 → 종료 코드 1, 잘못된 인자 → 2
boundary selftest: 중간 INTERFACE 타깃을 거친 Server→Platform 링크를 구성 단계에서 FATAL_ERROR
include lint: 실제 트리 22 파일 0건, 픽스처 8건 정확히 검출
clang-18 지정 시 구성 단계에서 원인 메시지와 함께 중단됨을 확인
```

**미검증 / 남은 일**

- Windows MSVC, macOS 빌드는 이 세션에서 실행할 수 없었다 → 사용자 PC 에서 `cmake --preset windows-msvc` 1회, CI 첫 실행.
- `.github/workflows/ci.yml` 은 원격 쓰기가 금지된 경로라 채팅으로 전달했다 → 사용자가 직접 배치.
- `git init`·원격 저장소 생성은 사용자 작업 (16-ROADMAP 1.1b). 커밋 해시가 생기면 BuildInfo 에 반영된다(재구성 시).
- LICENSE (열린 질문 Q1).
- 다음: Phase 2 Core ECS.

---

## 2026-10-05 — Phase 0: 문서 세트 작성

**무엇을**

- 저장소 루트: `README.md`, `AGENTS.md`, `DEVELOPMENT_LOG.md`
- `docs/00~17` 규범·설계·절차 문서, `docs/adr/0001~0010`
- `docs/design/SANDBOX_ARCHITECTURE.md`: RTS 저장소 분석과 전체 설계의 원본 (Phase 0 입력, 이후 수정하지 않음)

**왜**

- 새 프로젝트를 코드 없이 시작하면서, 구현 전에 경계·불변식·순서를 먼저 고정하기 위해.
- 원본 설계서는 한 파일 2,000줄이라 작업 중 참조하기 어렵다. 주제별 규범 문서로 나누고
  구현에 필요한 세부(API 모양, 와이어 포맷, 스키마, 체크리스트)를 보강했다.

**검증**

- 문서 간 상대 링크 전수 확인, 코드 블록 짝 확인.
- 원본 설계서의 30개 장과 결정 항목이 분할 문서 어디로 갔는지 `docs/README.md` 4장에 대응표로 남김.

**남은 일**

- Phase 1: 저장소 골격(CMake 프리셋, Foundation/Core/Tests 타깃, CI, include 린트). [16-ROADMAP](docs/16-ROADMAP.md)
- 부록의 열린 질문(저장소 이름·라이선스 등)은 결정되는 대로 ADR로.
