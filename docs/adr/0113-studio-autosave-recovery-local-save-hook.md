# ADR-0113. Studio 자동 저장 · 크래시 복구 — 사용자 폴더의 복구본, 로컬 서버의 프로세스 안 세이브 (프로토콜 그대로), 복구 = 프로젝트의 새 세이브로

- 상태: **Accepted** · 날짜: 2026-11-10
- 관련: [ADR-0089](0089-studio-editor-services-layer-imgui-first.md) 6 (Studio 설정 · 사용자 폴더), [ADR-0053](0053-test-play-copy-world-park-edit-world-protocol-12.md)
  (테스트 플레이 — 맡긴 편집 월드), [ADR-0111](0111-studio-home-project-service-recent.md), [25-STUDIO-ARCHITECTURE](../25-STUDIO-ARCHITECTURE.md) 7장,
  [24-SECURITY-GATES](../24-SECURITY-GATES.md) D3, [09-SERIALIZATION](../09-SERIALIZATION.md), task-registry M6-T7

## 맥락

충돌 · 강제 종료 때 편집한 작업을 잃지 않아야 한다 (24 D3, 벤치마크 F1 — Roblox 5 분 자동 저장). 월드 세이브는 서버의 일이다 (WorldOp
Save — 서버의 세이브 폴더 = 프로젝트 worlds/). 25 7장은 복구본을 사용자 폴더에 두고 프로젝트를 덮어쓰지 않기로 했다. 세이브 형식 (09) 은
로드가 world.json 의 worldHash 를 확인한다. 50k 개체 저장은 측정 188 ms (14 7.3 — 클라우드 RelWithDebInfo) · 853 ms (14 7.17 — 이 기계
Windows Release, 옛 sbx_bench 0.2.0 빌드 — 추정).

## 결정

```text
1. editor/services/AutosaveService: 복구본 = <설정 폴더>/recovery/<프로젝트 경로 해시 FNV-1a 64>/ — world/ (세이브 형식 그대로), documents/
   (저장하지 않은 문서 글 — 합 4 MiB), recovery.json {project, savedAt, clean, documents}. tick · 해시의 정본은 world/world.json (세이브와 함께
   원자적으로 바뀐다 — recovery.json 에 따로 적으면 세이브 뒤 · 정보 쓰기 전에 죽었을 때 서로 다르다). 안전 모드는 끈다 (사용자 폴더를 쓰지 않는다)
2. 주기 (기본 180 초 — --autosave-seconds, 0 = 끔) · 플레이 시작 전 (테스트 플레이 요청 때). 프로젝트에 들어가면 (begin) 먼저 clean = false 를
   적고 (깨끗이 끝난 앞 실행의 복구본은 지운다), 앱이 깨끗이 끝나면 (Shutdown) clean = true. 다음 시작에 clean = false 이고 남긴 것이 있으면
   복구 제안 — 고를 때까지 새 자동 저장은 멈춘다 (복구본을 덮지 않는다)
3. 세이브 길 = ServerHost::requestLocalSave(dir) · takeLocalSaves() — 같은 프로세스의 편집기만 (네트워크 메시지가 아니다 — 프로토콜 16 그대로,
   원격 서버는 자동 저장하지 않는다). Sim 단계의 틱 사이에 편집 월드를 (테스트 플레이 중이면 맡긴 원본 — 플레이 결과가 아니라 편집을 지킨다)
   persist::saveWorld. 세션 (IWorldSession::requestRecoverySave) 이 앱과 잇는다
4. 복구 = 복구본 world/ 를 <프로젝트>/worlds/recovered-<n> 으로 복사 (있는 이름은 건너뛴다 — 덮어쓰지 않는다) → 사용자 폴더의 복구본을
   지운다 → WorldOp Load (기존 길 — 서버가 worldHash 를 확인한다). 버리기 = 복구본을 지운다. 지우지 못하면 (다른 프로그램이 잠깐 잡음 —
   몇 번 다시) clean 으로 적어 다시 제안하지 않는다. 화면: "복구" 창 [복구] [버리기], 자동화: --recover
5. 저장은 지금 Sim 스레드에서 동기 — 50k 개체면 틱이 그만큼 멈춘다 (주기 180 초에 한 번). 틱 경계 메모리 스냅숏 + Worker 직렬화 (09 3.4 의
   원래 설계 · 14 7.3) 는 [계획]
6. (시험용) --abort-after-autosaves <n>: n 번째 자동 저장 뒤 std::_Exit(3) — 충돌 흉내. 시간 초과로 죽이는 시험은 쓰지 않는다 (CMake 가
   돌아온 뒤에도 프로세스가 몇 초 더 살아 자동 저장을 이어 썼다 — 처음 시험에서 본 일)
```

## 근거

```text
- 프로세스 안 길: 로컬 서버는 같은 프로세스 — 프로토콜을 늘리면 (17) 모든 원격 · 리플레이 · 호환에 닿는데, 원격 자동 저장은 지금 요구가 아니다
- 프로젝트의 새 세이브로 복구: 기존 Load 길 (게임 결속 · 마이그레이션 · 해시 확인) 을 그대로 쓰고, 사용자가 고른 뒤에만 프로젝트에 쓴다 (25 7장)
- 정본을 세이브에: 충돌은 아무 때나 온다 — 두 파일의 순서에 기대지 않는다
```

## 결과

- 얻는 것: 충돌 · 강제 종료 뒤 편집 월드 · 저장하지 않은 문서 글을 되찾는다 (24 D3), CTest studio_recovery.
- 포기하는 것: 원격 서버 자동 저장, 문서 글 복구의 화면 (지금은 파일 · 목록만 — DocumentService 를 쓰는 편집기와 함께), 비동기 직렬화.
- 위험: 큰 월드의 자동 저장이 틱을 멈춘다 — 50k 개체 0.2 ~ 0.9 초 (14 7.3 · 7.17 — 30 TPS 에서 6 ~ 26 틱, 180 초에 한 번). 주기를 늘리거나
  끈다 (--autosave-seconds), Worker 직렬화는 [계획].

## 대안

| 대안                                       | 기각 사유                                                                  |
|--------------------------------------------|----------------------------------------------------------------------------|
| WorldOp 에 Autosave 종류 (프로토콜 17)      | 원격 · 호환에 닿는다 — 지금 요구는 로컬 편집뿐                              |
| 복구본을 프로젝트 worlds/ 에                 | 프로젝트를 사용자가 고르지 않은 파일로 더럽힌다 (25 S4 · 7장)               |
| 클라이언트 복제 월드를 저장                  | 서버 전용 값이 없어 같은 WorldHash 가 아니다                                |
| 복구본을 제자리에서 바로 열기 (서버 세이브 폴더 밖) | 서버의 세이브 폴더 규칙 · 이름 검사를 늘려야 한다 — 복사가 단순하다      |

## 재검토 조건

Worker 직렬화 (09 3.4), 원격 서버 자동 저장, 문서 복구 화면 (M6-T8 스크립트 편집기), Studio Soak (RH-22-2).
