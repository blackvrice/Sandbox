# ADR-0040. GameProject = 매니페스트 sandbox.project.json + 콘텐츠 팩 + 세이브 폴더 형식의 월드, SemVer 는 Foundation

- 상태: **Accepted** · 날짜: 2026-10-19
- 관련: [18-PLATFORM](../18-PLATFORM.md) 4 · 5장, [ADR-0039](0039-platform-scope-version-line-after-phase-15.md),
  [11-CONTENT-SCHEMA](../11-CONTENT-SCHEMA.md) 1장, [09-SERIALIZATION](../09-SERIALIZATION.md) 6장,
  [ADR-0013](0013-save-folder-swap-and-load-hash.md), [플랫폼 초안 architecture/02](../platform/architecture/02_GAME_PACKAGE_FORMAT.md)

## 맥락

v0.2 Game Creator 의 첫 태스크 (M1-T1). 게임 하나를 담을 단위가 없다 — 지금은 서버가 `--world` 로 시나리오 이름 또는
세이브 폴더를 열고, 콘텐츠 팩은 `--content-root` (기본: 저장소의 content/) 에서 찾는다 (WorldSource). 초안은
`sandbox.project.json` · `src/` · `worlds/` · `prefabs/` · `.sbpkg` 를 제안했지만 "예시이며 기존 콘텐츠 팩과 호환 어댑터가
필요"라고 적었다. 정할 것:

```text
1. 프로젝트 폴더 구조와 기존 콘텐츠 팩 · 세이브 형식의 관계     2. 매니페스트 필드 (v0.2 에 필요한 만큼)
3. 버전 · 엔진 범위 표기와 그 코드의 위치                        4. 검사와 오류 보고 형식
5. 코드 위치 (모듈 경계)                                         6. 무엇을 미루나
```

## 결정

```text
1. 프로젝트 = 폴더: sandbox.project.json + content/<팩 폴더>/ (11 의 팩 그대로) + worlds/<이름>/ (09 6장의 세이브 폴더 그대로).
   새 월드 형식을 만들지 않는다 — 월드(지형 · Prefab 인스턴스 · 오버레이)는 에디터가 저장하는 세이브 폴더다.
   초안의 src/ (스크립트, v0.3) · prefabs/ (지금은 팩 안) · .sbpkg (D-02, M5) 는 해당 태스크에서.
2. 매니페스트 schemaVersion 1: schemaVersion · gameId · name · version · engine · description? · content.packs? · entryWorld.
   모르는 키는 오류 (pack.json 과 같은 엄격함). 새 키(dependencies · scriptApi · network …)는 schemaVersion 2 로 더한다.
   schemaVersion 이 다르면 그 하나만 보고하고 멈춘다. 정규형 쓰기(키 순서 · 들여쓰기 2)는 다시 읽으면 같은 값.
3. SemVer 2.0.0 엄격 파서 · 우선순위와 좁은 범위 문법(>= > <= < = ^, 부분 버전은 npm 과 같은 뜻, 공백 = AND)을
   foundation/version 에 둔다 — 의존 없는 범용 코드이고 v0.4 패키지 관리자 · 프로토콜이 다시 쓴다. "||" · "~" · "*" ·
   하이픈은 Unsupported 로 거절 (D-05). 엔진 버전 = CMake project VERSION (BuildInfo) — 범위 밖이면 오류.
4. 문제는 콘텐츠 로더의 ContentIssue (파일 · JSON 포인터 · 규칙 · 메시지) 로 모두 모은다. 규칙 P1 형식 · P2 식별자 ·
   P3 버전 · P4 경로 · 월드 · P5 콘텐츠 참조 + 팩의 V1~V7 (file 은 "content/<팩 폴더>/…"). 프로젝트 검사는 시작 월드를
   persist::loadWorld 로 실제로 연다 — "형식은 맞는데 안 열린다"를 남기지 않는다. 월드의 contentHash 가 프로젝트 콘텐츠와
   다르면 경고(열린다), 월드가 쓰는 팩이 매니페스트에 없으면 오류.
5. 코드: core/project (SandboxCore — 서버가 링크한다, 그래픽 없음). 실행 확인은 sbx_sim_check --validate-project
   (--validate-content 와 같은 자리). 서버 · 클라이언트가 프로젝트를 여는 것은 M1-T2.
6. 경로 규칙: entryWorld 는 프로젝트 안 상대 경로 ('/' 구분, 마디 [A-Za-z0-9_.-], '.' · '..' · 절대 경로 · 역슬래시 ·
   Windows 예약 이름 · '.' 로 끝나는 마디 금지) — 어느 OS 에서 만든 프로젝트든 같은 뜻, 프로젝트 밖을 가리키지 않는다.
```

## 근거

```text
- 월드 = 세이브 폴더: 저장 · 마이그레이션 · 오버레이 · 해시 검증 (ADR-0013) · 에디터 저장/불러오기 (12D) 를 그대로 얻는다.
  별도 맵 형식은 두 형식 사이 변환과 시험을 만든다.
- 팩을 프로젝트 안에: 서버의 콘텐츠 해시 핸드셰이크 · 오버레이가 팩 단위라 프로젝트가 팩 집합을 고정하면 서버 · 클라이언트가
  같은 콘텐츠를 연다. 저장소의 content/ 를 공용으로 쓰는 것은 v0.4 의존성.
- 엄격한 키: 모르는 키를 무시하면 오타가 조용히 기본값이 된다 (pack.json 과 같은 이유).
- ContentIssue 재사용: 편집기 · 도구가 이미 이 형식을 보여 준다 (콘텐츠 창 · --validate-content).
```

## 결과

- 얻는 것: 게임 하나의 단위와 검사 (매니페스트 · 엔진 범위 · 팩 · 월드 열기), 범용 SemVer, CTest (`project_*` 3 개 · unit_project).
- 포기하는 것: 프로젝트 사이 팩 공유 (v0.4 전까지 팩을 복사), 초안의 src/ · prefabs/ 폴더 구조 (해당 버전에서).
- 위험: 세이브 폴더가 월드 원본이 되므로 실행 상태(틱 · 개체 상태)와 편집 원본이 한 형식 — Test Play 가 원본을 덮지 않게
  M5-T1 에서 분기 세션으로 막는다. 큰 월드의 검사는 loadWorld 비용만큼 느리다 (검사 도구 · 서버 시작 때만).

## 대안

| 대안                                            | 기각 사유                                                                 |
|-------------------------------------------------|---------------------------------------------------------------------------|
| 새 월드/맵 형식 (worlds/main.world 한 파일)     | 저장 · 마이그레이션 · 해시 · 에디터 경로를 다시 만든다                    |
| 모르는 키를 경고로                              | 오타가 조용히 기본값이 된다. pack.json 과 규칙이 달라진다                 |
| SemVer 를 core/project 안에                     | 패키지 관리자 · 프로토콜 협상이 다시 쓴다 — 의존 없는 Foundation 이 맞다  |
| npm 범위 문법 전부 (`||` · `~` · `*` · 하이픈)  | v0.2 에 필요 없다. 프리릴리스 규칙과 함께 v0.4 (D-05) 에서                |

## 재검토 조건

v0.3 에서 스크립트 소스 · 사용자 컴포넌트 스키마가 프로젝트에 들어올 때 (schemaVersion 2), v0.4 의존성 · 잠금 파일,
M5 GamePackage 형식 (D-02), 월드 하나가 커서 검사 시간이 문제가 될 때.
