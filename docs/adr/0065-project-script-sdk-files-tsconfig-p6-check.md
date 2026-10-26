# ADR-0065. 게임 프로젝트의 스크립트 SDK 파일 — sdk/sandbox.d.ts · tsconfig.json 을 sbx new 가 만들고, sbx check 가 P6 경고 · 스크립트 요약

- 상태: **Accepted** · 날짜: 2026-10-26
- 관련: [ADR-0040](0040-game-project-manifest-world-is-save-folder.md) (프로젝트 폴더), [ADR-0043](0043-sbx-project-tool-new-from-scenario-template.md) (sbx new · check),
  [ADR-0056](0056-sandboxscript-language-subset-own-compiler-no-gc.md) 1 · 2 (자체 컴파일러, tsc 는 IDE 쪽), [ADR-0057](0057-host-api-single-definition-cpp-table-sdk-generated.md)
  2 (d.ts 생성), [18-PLATFORM](../18-PLATFORM.md) 5.2 · 6.2 (v0.3 M5-T1), [19-SANDBOXSCRIPT](../19-SANDBOXSCRIPT.md) 14장

## 맥락

스크립트 진단 (S*) 은 v0.3 M3-T2 부터 콘텐츠 문제로 sbx check 에 나온다. 그러나 새 프로젝트에는 IDE 가 읽을 SDK 선언
(sandbox.d.ts) 과 tsconfig 가 없어, 제작자가 sbx sdk 를 따로 돌리고 tsconfig 를 손으로 써야 한다. 엔진이 바뀌면 프로젝트의
d.ts 가 낡아도 알 길이 없다. 정할 것: 파일의 자리 · 내용, 검사가 무엇을 경고하나, 패키지에 넣나.

## 결정

```text
1. 자리: <프로젝트>/sdk/sandbox.d.ts (sbx sdk 가 쓰는 것과 같은 바이트) · <프로젝트>/tsconfig.json. sbx new 가 둘 다 만든다
   (스크립트가 없어도 — 스크립트를 쓰기 시작하면 IDE 가 바로 안다)
2. tsconfig = 보수적인 설정 하나 (script::sdk::generateTsconfig): target · module ES2020, moduleResolution node, lib ES2020,
   types 없음, strict, noEmit, include = sdk/sandbox.d.ts · content/*/scripts/**/*.ts. 게임 실행 파일은 읽지 않는다
3. sbx check (project::validateProject 5 단계 — P6 경고, 오류는 아니다):
     sdk/sandbox.d.ts 가 있는데 이 엔진의 생성 결과와 다르다 (줄바꿈 CR 은 빼고 비교 — Git 의 CRLF) → "sbx sdk <프로젝트>/sdk"
     스크립트가 있는데 sdk/sandbox.d.ts 가 없다
     tsconfig.json 이 JSON 이 아니다
   요약 줄 "스크립트: 팩 n · 컴포넌트 n · 시스템 n · SDK sdk/sandbox.d.ts 최신|다름|없음" 과 시스템마다 한 줄
   ("id — content/<팩>/scripts/a.ts · onTick (n 틱마다) · onStart · onEvent (death · …) · onStop")
4. 패키지 (sbx build) 에는 넣지 않는다 — 매니페스트 · 팩 · 시작 월드만 (ADR-0054 그대로). SDK 는 만드는 쪽의 도구다
```

## 근거

```text
- 프로젝트 안에 d.ts 를 두면 IDE 설정이 tsconfig 하나로 끝나고 (경로 별칭 · 전역 설치 없음), 엔진 버전과 같이 커밋된다
- 생성 결과와 바이트 비교: Host 표가 바뀌면 (지문) d.ts 가 바뀐다 — 낡은 d.ts 는 IDE 가 없는 함수를 권하거나 있는 함수를 거절한다
- 경고 (P6): d.ts · tsconfig 는 게임 실행과 무관하다. 오류로 하면 IDE 를 쓰지 않는 제작자의 프로젝트가 열리지 않는다
- 보수적인 tsconfig: 오래된 tsc 도 읽는 옵션만 — moduleResolution node 는 확장자 없는 상대 import 와 declare module 을 푼다
```

## 결과

- 얻는 것: sbx new 직후 IDE 자동 완성 · tsc 타입 오류, 엔진을 올리면 sbx check 가 낡은 SDK 를 알린다, 스크립트 시스템 목록을 한눈에.
- 포기하는 것: tsconfig 의 사용자 설정 보존 (sbx sdk 는 d.ts 만 다시 쓴다 — tsconfig 는 손대지 않는다), 이 저장소에서 tsc 로 템플릿을
  검증 (빌드 체인에 Node 가 없다 — 사용자 IDE 에서 확인 [검토 M6 데모]).
- 위험: tsc 의 판정과 SandboxScript 의 판정이 다를 수 있다 (19 장 머리의 규칙 — SandboxScript 가 받으면 tsc 도 받는다, 거꾸로는
  아니다) — 완화: 최종 판정은 sbx check 의 S* 진단.

## 대안

| 대안                                   | 기각 사유                                                                 |
|----------------------------------------|---------------------------------------------------------------------------|
| d.ts 를 엔진 설치 폴더에서 참조        | 프로젝트를 옮기거나 다른 엔진으로 열면 경로가 깨진다, 버전이 따로 논다    |
| npm 패키지 @sandbox/sdk                 | 빌드 체인 · 배포에 Node 가 들어온다 (ADR-0056 — 게임 실행에 JS 런타임 없음) |
| 낡은 d.ts 를 오류로                    | IDE 와 무관한 게임 실행을 막는다                                          |
| 패키지에 sdk/ · tsconfig 포함           | 실행에 쓰지 않는 파일이 digest 에 든다                                    |

## 재검토 조건

템플릿을 tsc 로 자동 검증할 수단이 생기면 (CI 의 Node), 에디터가 스크립트를 직접 고치게 되면 (M5-T2 — 에디터의 오류 줄),
사용자 tsconfig 를 보존한 채 갱신할 필요가 생기면.
