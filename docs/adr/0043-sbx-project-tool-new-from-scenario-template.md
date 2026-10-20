# ADR-0043. 게임 프로젝트 도구 sbx — new(빈 월드 · 시나리오 템플릿) · check, 프로젝트 검사를 sbx_sim_check 에서 옮긴다

- 상태: **Accepted** · 날짜: 2026-10-20
- 관련: [ADR-0040](0040-game-project-manifest-world-is-save-folder.md), [ADR-0041](0041-server-opens-game-project-strict-check.md),
  [18-PLATFORM](../18-PLATFORM.md) 5장, [15-BUILD](../15-BUILD.md) 7장, [플랫폼 초안 roadmap/v0_4](../platform/roadmap/v0_4_PACKAGE_MANAGER.md)

## 맥락

v0.2 M1-T4. 프로젝트를 열고(T2) 검사하는(T1) 길은 있지만 새로 만드는 길이 없다 — 지금은 매니페스트를 손으로 쓰고 세이브
폴더를 복사해야 한다. v0.2 요구 "게임 프로젝트 생성 · 빌드 · 실행"의 첫 조각. 정할 것: 만드는 코드와 진입점의 위치, 템플릿이
무엇인가, 실패했을 때 무엇이 남나, 새 프로젝트의 엔진 범위. 그리고 T1 이 검사를 결정론 하네스(sbx_sim_check
`--validate-project`)에 붙인 것이 맞나.

## 결정

```text
1. 라이브러리: project::createProject(폴더, {gameId, name?, fromScenario?, contentRoot, seed}, 엔진, 카탈로그) — Core.
   에디터 UI(M1-T5 이후)와 도구가 같은 함수를 쓴다.
2. 템플릿 = 시나리오 하나 또는 빈 월드 (새 템플릿 형식을 만들지 않는다):
   - 빈 월드: 내장 콘텐츠, 기본 경계(16 × 16 청크), 일시정지로 시작 (편집하고 실행한다)
   - 시나리오: requiredPacks 와 그 requires 를 contentRoot 에서 <프로젝트>/content/<같은 폴더 이름> 으로 복사, 시나리오를 한 틱
     돌려 초기 배치가 적용된 월드를 worlds/main 으로. 이후 시나리오의 보충 · 스크립트 동작은 따라오지 않는다 (월드의 사진).
   - 매니페스트: version 0.1.0, entryWorld "worlds/main", 엔진 범위 = 1.0 전 ">=<엔진> <1.0.0", 1.0 부터 "^<major>".
3. 안전: 폴더는 없거나 비어 있어야 한다 (AlreadyExists — 덮어쓰지 않는다). 옆의 "<폴더>.sbx-new" 에 다 만들고 validateProject
   가 오류 · 경고 0 이어야 제자리로 rename. 실패하면 임시 폴더를 지운다 — 반쯤 만든 프로젝트를 남기지 않는다.
4. 도구 sbx (tools/sbx, Core 만 링크 — 경계 규칙에 등록): `sbx new <폴더> --id … [--name] [--from] [--content-root] [--seed]`,
   `sbx check <폴더>`, `sbx templates`. 종료 코드 0 성공 · 1 검사 오류 · 2 인자/실패.
   v0.4 패키지 관리자의 init · add · remove · update · build · test 가 같은 도구에 붙는다 ([계획]).
5. sbx_sim_check --validate-project 는 지운다 (T1 에서 넣은 것) — 같은 검사의 진입점이 둘이 되지 않게 sbx check 로 옮긴다.
   CTest 이름(project_minimal_validate · project_bad_manifest_*)은 그대로, 명령만 바뀐다. sbx_sim_check 는 결정론 하네스로 남는다.
```

## 근거

```text
- 시나리오가 이미 "팩 + 경계 + 초기 배치"를 코드로 가진다 — 템플릿 형식을 따로 만들면 같은 정보가 둘이 된다.
  v0.2 M6 의 데모 게임 · v0.5 의 템플릿 3 종도 이 길(시나리오 또는 이미 있는 프로젝트를 복사)로 늘린다.
- 검사를 통과하지 못한 프로젝트를 만들면 사용자는 첫 실행에서 실패한다 (ADR-0041 — 서버가 열지 않는다). 만든 직후 같은 검사로
  확인하는 것이 가장 싸다.
- 1.0 전 엔진 범위를 "^0.1" 로 두면 엔진이 0.2.0 이 되는 날 지금 만든 모든 프로젝트가 열리지 않는다. 0.x 동안의 호환은
  마이그레이션(12_COMPATIBILITY)으로 다루고 범위는 하한만.
- 결정론 하네스는 시나리오 · 해시 · 리플레이를 다룬다. 프로젝트 도구는 만들기 · 검사 · (v0.4) 의존성 — 사용자가 다르다.
```

## 결과

- 얻는 것: `sbx new MyGame --id studio.mygame --from ecosystem_small` → `SandboxClient --world MyGame` 이 바로 된다.
  CTest project_new_empty · project_new_from_scenario (만들기 → 검사 → 서버로 30 틱 → 다시 만들기 거절).
- 포기하는 것: 시나리오의 진행 중 동작(보충 등)은 템플릿에 실리지 않는다 — 규칙으로 옮기는 것은 M2~M4 (GameMode · Feature ·
  Visual Trigger). sbx_sim_check --validate-project (한 판 만에 옮김 — 사용자 스크립트가 있었다면 sbx check 로).
- 위험: 팩 복사는 저장소 팩의 사본이라 원본이 고쳐져도 프로젝트에 반영되지 않는다 (의도 — 프로젝트가 자기 콘텐츠를 가진다).
  공유 · 갱신은 v0.4 의존성.

## 대안

| 대안                                      | 기각 사유                                                              |
|-------------------------------------------|------------------------------------------------------------------------|
| 템플릿 폴더(templates/<이름>/)를 저장소에 | 시나리오와 같은 정보를 두 번 — 팩 · 월드 사본이 원본과 어긋난다        |
| sbx_sim_check --new-project               | 결정론 하네스에 사용자 도구를 섞는다. v0.4 CLI 가 결국 따로 생긴다     |
| 폴더가 있으면 그 안에 덮어쓰기            | 사용자 파일을 잃을 수 있다. init(있는 폴더에 매니페스트만)은 v0.4 에서 |
| SandboxClient 에 "새 게임" 화면만         | 비대화 · CI 에서 만들 수 없다. 화면은 같은 함수를 부르는 M1-T5 이후 일 |

## 재검토 조건

v0.4 에서 의존성으로 팩을 공유할 때 (복사 대신 참조), 템플릿이 시나리오로 표현되지 않는 것(스크립트 · UI 원본)이 생길 때 (v0.3),
에디터의 "새 게임" 화면을 만들 때.
