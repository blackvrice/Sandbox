# ADR-0114. Studio 레이아웃 · 설정 — 사용자 폴더 studio/layouts/<이름>.ini (ImGui 텍스트를 앱이 꺼내 서비스가 저장), studio.json 은 서비스마다 자기 키

- 상태: **Accepted** · 날짜: 2026-11-10
- 관련: [ADR-0089](0089-studio-editor-services-layer-imgui-first.md) 6 (Studio 설정 · 사용자 폴더), [ADR-0083](0083-i18n-accessibility-foundation.md)
  (사용자 설정 파일은 프로젝트 밖) · 20 7.5 (M1-T3 — 깨진 파일 옮기기 · 안전 모드), [ADR-0111](0111-studio-home-project-service-recent.md) (studio.json 최근 목록),
  [ADR-0113](0113-studio-autosave-recovery-local-save-hook.md) (자동 저장 주기), [25-STUDIO-ARCHITECTURE](../25-STUDIO-ARCHITECTURE.md) 4장 S4,
  task-registry M6-T6, 감사 GM-08 · R6

## 맥락

창 배치를 저장하지 않았다 (ImGuiLayer — IniFilename = nullptr, 패널 대부분이 NoSavedSettings). 다시 열 때마다 창이 처음 자리다 (감사 R6 ·
GM-08). 25 4장은 레이아웃을 사용자 설정 폴더 studio/layouts/<이름>.ini 에, Studio 설정 (최근 · 레이아웃 · 테마 · 자동 저장 주기) 을 studio.json
에 두고 게임 프로젝트와 섞지 않기로 했다 (S4). studio.json 은 이미 ProjectService 가 최근 목록만 들고 통째로 썼다 — 다른 키를 더하면 서로
지운다. ImGui 의 ini 는 창 이름 · 위치 · 크기 · 접힘 · 도킹 노드를 담는 줄 텍스트다.

## 결정

```text
1. editor/services/LayoutService (ImGui 모름 — 텍스트만): <설정 폴더>/studio/layouts/<이름>.ini. 이름 [a-z0-9_-] 1 ~ 32 (파일 이름 그대로).
   "default" = 기본 (파일이 없으면 창들의 처음 자리 — 코드의 SetNextWindowPos(FirstUseEver)). select (그 텍스트 · 지금 것으로 기억) · save
   (지금 것에 — 같은 텍스트면 쓰지 않는다) · saveAs (다른 이름 → 지금 것) · names
2. 깨진 ini (1 MiB 넘음 · UTF-8 아님 · NUL · ini 줄 ([종류][이름] · 키=값 · ;주석) 이 아닌 줄) = "<이름>.ini.broken" 으로 옮기고 기본 배치 +
   알림 (M1-T3 의 규칙 — ImGui 의 읽기는 오류를 알리지 않으므로 앞에서 본다)
3. 앱 (ImGuiLayer · Application): IniFilename 은 그대로 없음 (ImGui 가 작업 폴더에 쓰지 않는다). 시작에 지금 레이아웃을 적용
   (LoadIniSettingsFromMemory), ImGui 가 바뀌었다고 하면 (WantSaveIniSettings — 몇 초 모아서) 꺼내 save, 끝낼 때 save. 바꾸기 (고르기 · 기본
   배치로) 는 프레임 밖에서: ClearIniSettings → 이미 만든 창도 FirstUseEver 를 다시 받게 → 텍스트 적용. 적용 전에 끝나면 그 텍스트를 저장
4. 패널의 NoSavedSettings 를 뗀다 (편집 · 인스펙터 · 목록 · 콘텐츠 · 게임 · 행동 · 접속자 · 월드 · 키 · 시뮬레이션 · 통계 · 네트워크). 이름이
   바뀌는 창은 이미 "###id" (같은 설정)
5. studio.json = 서비스마다 자기 키 (editor/services/StudioSettings): {schemaVersion 1, recentProjects (ProjectService), layout (LayoutService),
   autosaveSeconds (AutosaveService 주기 — --autosave-seconds 가 이긴다)}. 쓰기 = 읽고 · 한 키 바꾸고 · 원자적으로 (다른 키 · 모르는 키
   그대로). 깨진 파일 (객체 아님) 은 studio.json.broken 으로 옮기고 기본값 (main 이 서비스보다 먼저 읽어 한 번만 알린다). 키 하나가 틀리면
   그 키만 무시 + 알림 (파일은 다른 서비스의 키도 든다 — 옮기지 않는다). 더 새 schemaVersion = 읽기만, 쓰지 않는다
6. 화면: "레이아웃" 창 (지금 · 고르기 · 이름으로 저장 · 기본 배치로 — 처음에는 접힘). 명령 studio.save-layout · studio.reset-layout.
   --layout <이름> = 그 레이아웃으로 시작하고 기억 (틀린 이름은 인자 오류 2). 안전 모드 = 아무 파일도 읽거나 쓰지 않는다 (이 실행만 기본 배치)
```

## 근거

```text
- 텍스트만 아는 서비스: 레이아웃 규칙 (이름 · 깨짐 · 기억) 을 ImGui 없이 시험한다 (editor/services 경계 — ADR-0108). 적용 · 꺼내기 몇 줄만
  ImGui 쪽
- IniFilename 을 켜지 않는다: 켜면 ImGui 가 상대 경로 · 자기 시점에 쓴다 — 이름 · 깨짐 · 안전 모드를 우리가 정할 수 없다
- 키마다 쓰기: 한 파일 (25 4장 그대로) 을 여러 서비스가 나눠 들면서 서로 지우지 않는다. 서비스마다 파일을 나누면 25 의 설정 위치가 흩어진다
- 키 하나 틀림은 옮기지 않는다: 최근 목록이 멀쩡한데 layout 하나 때문에 통째로 잃지 않게
```

## 결과

- 얻는 것: 다시 열면 같은 배치 (20 M6-T6 기준), 이름 붙인 레이아웃 · 기본 배치로, studio.json 의 자동 저장 주기, CTest studio_layout ·
  test_layout_service.
- 포기하는 것: 기본 레이아웃 셋 (장면 편집 · 스크립트 · 테스트 — 25 4장) — 지금 창들이 떠 있는 창이라 DockBuilder 로 짠 배치가 없다
  [계획 — 스크립트 편집기 (M6-T8) · Scene/Game 뷰 분리 (ST-06-11) 와 함께]. 레이아웃 지우기 · 이름 바꾸기 UI, 테마 · 편집기 글꼴 키 [계획 ST-06-2].
- 위험: ImGui 의 ini 형식이 판마다 줄을 더한다 (1.92 의 LastUsed) — 모르는 줄은 ImGui 가 건너뛰고, 우리의 깨짐 검사는 줄 모양만 본다.
  ImGui 내부 API (ClearIniSettings · 창의 조건 플래그) 를 쓴다 — ImGui 를 올릴 때 확인 (external/imgui 고정 판).

## 대안

| 대안                                         | 기각 사유                                                                 |
|----------------------------------------------|---------------------------------------------------------------------------|
| io.IniFilename 을 사용자 폴더 경로로           | 이름 · 깨진 파일 · 안전 모드 · 여러 레이아웃을 우리가 정할 수 없다         |
| 레이아웃마다 JSON (창 → 자리) 을 우리가 정의   | 도킹 노드까지 다시 만든다 — ImGui 텍스트가 이미 전부 담는다                |
| Studio 설정을 client.json 에                   | 게임 클라이언트 · Player 모드 (NG-03) 와 섞인다 (25 S4 · 4장)              |
| 서비스마다 파일 (recent.json · layout.json)    | 25 4장의 한 파일 결정과 다르고 사용자가 찾기 어렵다                        |

## 재검토 조건

기본 레이아웃 셋 (DockBuilder — M6-T8 · ST-06-11), 떼어 낸 창 (ImGui 뷰포트 — 25 4장 다중 모니터), 테마 · 글꼴 키 (ST-06-2), ImGui 판 올림.
