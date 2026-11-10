# ADR-0112. Studio 빌드 — BuildService (검사 → buildPackage, 일꾼 스레드 · 메인이 poll), "빌드" 창, 명령 studio.check · studio.build, --run-command

- 상태: **Accepted** · 날짜: 2026-11-09
- 관련: [ADR-0089](0089-studio-editor-services-layer-imgui-first.md) · [ADR-0108](0108-editor-services-target-command-table.md) (명령 표) ·
  [ADR-0111](0111-studio-home-project-service-recent.md) (홈 — 지금 프로젝트), [ADR-0097](0097-package-data-integrity-snapshot-ids-paths-digests.md)
  1 (빌드 스냅숏), [25-STUDIO-ARCHITECTURE](../25-STUDIO-ARCHITECTURE.md) 3장 · 5장 ⑩, [28-CREATOR-UX-GATES](../28-CREATOR-UX-GATES.md) G8,
  task-registry M6-T3

## 맥락

G8: CLI 없이 빌드한다. 패키지 만들기 (package::buildPackage — 검사 · 빌드 스냅숏 · 원자적 쓰기) 와 검사 (validateProject — 누락 에셋 A6 ·
id 없는 에셋 A2 오류, 부르지 않는 에셋 A8 경고) 는 core 에 있고 sbx 가 부른다. 빌드는 수 초 — 편집 · 틱을 막으면 안 된다 (25 S5).

## 결정

```text
1. editor/services/BuildService: check(프로젝트) = validateProject (sbx check 와 같은 — 엔진 에셋 루트 포함) → 보고 (오류 · 경고 · 에셋 그룹),
   buildNow(요청) = 검사 → 오류가 없으면 buildPackage (sbx build 와 같은 — 출력 기본 <프로젝트>/build/<gameId>-<version>.sbpkg, Export
   Profile 이름 선택) → 보고 (패키지 경로 · gameId · 버전 · 파일 수 · 바이트 · digest · 걸린 시간). 부르지 않는 에셋은 검사의 A8 경고 하나로
2. start(요청) = 일꾼 스레드 하나에서 buildNow. 결과는 메인 스레드가 프레임마다 poll() 로 (한 번만). 한 번에 하나 — 돌고 있거나 끝난 보고를
   아직 가져가지 않았으면 start 거절. 소멸은 일꾼을 기다린다. 빌드는 프로젝트 파일을 읽기만 한다 (같은 프로세스의 로컬 서버와 겹쳐도 —
   빌드 스냅숏이 읽은 뒤 바뀐 파일을 잡는다, ADR-0097 1)
3. "빌드" 창 (editor/ui/BuildPanel): 지금 프로젝트 · Export Profile (<프로젝트>/export/*.json) · [검사] [빌드] · 마지막 보고. 일은 명령 표의
   studio.check · studio.build (when = 프로젝트 · 빌드 중 아님, Ctrl+B 는 표에만 — ST-06-2). 지금 프로젝트 = --world <프로젝트 폴더> 또는 홈에서
   들어간 폴더 (시나리오 · 원격 · 패키지는 빌드할 프로젝트가 없다)
4. --run-command <id> (여러 번): 들어간 첫 프레임에 명령 표의 명령을 차례로. --quit-after-commands: 명령 (비동기 빌드 포함) 이 끝나면 끝낸다 —
   실행하지 못한 명령 · 실패한 검사 · 빌드가 있으면 종료 코드 1. 헤드리스 시험 (studio_build) · 자동화의 길 (화면과 같은 명령)
5. 오류가 있는 프로젝트는 처음부터 열리지 않는다 (openProject — 종료 코드 2). 열린 뒤 고쳐서 생긴 누락은 [빌드] 의 검사가 막는다
```

## 근거

```text
- core 재사용: sbx build · check 와 같은 결과 (같은 digest) — Studio 와 CLI 가 갈라지지 않는다
- 일꾼 하나 · poll: 렌더 · 틱 스레드와 겹치지 않는 단순한 규칙. 빌드 중 다시 빌드를 막아 같은 출력 파일을 둘이 쓰지 않는다
- 명령으로: 화면 · 단축키 · 팔레트 · 자동화 (--run-command) 가 같은 일 (ADR-0108)
```

## 결과

- 얻는 것: Studio 안에서 검사 · 빌드 (G8), 헤드리스 시험 studio_build (빈 캐시에서 연 패키지).
- 포기하는 것: 빌드 취소, 진행 막대 (단계 표시), [폴더 열기] (OS 탐색기), 배포 묶음 (sbx_release) 단추 — [계획].
- 위험: 빌드가 매우 큰 프로젝트에서 길면 창을 닫을 때 기다린다 (소멸이 일꾼을 기다림 — 취소는 [계획]).

## 대안

| 대안                                 | 기각 사유                                                             |
|--------------------------------------|-----------------------------------------------------------------------|
| sbx build 를 프로세스로               | 출력 해석 · 오류 전달이 약하다, 같은 프로세스의 core 를 쓰면 된다      |
| 메인 스레드에서 빌드                   | 수 초 동안 화면 · 틱이 멈춘다 (25 S5)                                 |
| JobSystem 의 Worker                   | 빌드는 하나뿐이고 오래 돈다 — 시뮬레이션 Worker 를 오래 붙잡지 않는다 |

## 재검토 조건

빌드 취소 · 진행, 여러 대상 빌드 (Export Profile 여럿), 게시 (PS-12-*), TestService (ST-06-9 — 빌드 뒤 서버 + N 클라이언트).
