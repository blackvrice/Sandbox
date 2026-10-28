# ADR-0076. sbx test 의 자동 플레이 자리 (playthrough.json · tests/*.json) · 패키지 틀 (sbx new --pack) 과 기본 라이선스

- 상태: **Accepted** · 날짜: 2026-10-28
- 관련: [ADR-0055](0055-sample-minigame-play-script-server-equals-local.md) (자동 플레이 스크립트), [ADR-0071](0071-v0-4-package-manager-scope-pack-as-package-single-version-resolution.md)
  1 · 7 (라이선스 · CLI — sbx test · new --pack), [ADR-0075](0075-cross-pack-script-import-static-link.md) (팩 사이 import),
  [13-TESTING](../13-TESTING.md) 5장, [15-BUILD](../15-BUILD.md) 7장, [18-PLATFORM](../18-PLATFORM.md) 6.4 (v0.4 M4-T2)

## 맥락

ADR-0071 7 은 "sbx test (콘텐츠 검사 + 프로젝트의 자동 플레이 스크립트들)" 와 "sbx new --pack (팩 하나짜리 패키지 틀)" 을 정했다.
정할 것: 프로젝트의 어느 파일이 자동 플레이인가, 어떻게 돌리나 (서버를 띄우나), 틀에 무엇을 넣고 라이선스를 무엇으로 두나.

## 결정

```text
1. 자동 플레이 = <프로젝트>/playthrough.json (샘플이 이미 쓰는 자리) 다음 <프로젝트>/tests/*.json (이름 순, 하위 폴더 · 다른 확장자
   제외). 형식은 ADR-0055 그대로
2. sbx test <프로젝트>: 프로젝트 검사 (sbx check 와 같은 validateProject — 오류면 자동 플레이를 돌리지 않고 종료 1) → 스크립트마다
   새로 연 월드 (openWorldSource) 에서 같은 프로세스로 끝까지 (runPlayScript — 헤드리스 서버 --script 와 같은 러너 · 단계, 단계 상한
   maxTicks × 2 + 1,000 · 없으면 2,000,000) → expect. 한 줄씩 "통과 | 실패 <파일> — <결과 요약> [· 다른 점]", 끝에 "시험: 통과 n ·
   실패 m". 하나라도 실패하면 종료 1. 스크립트가 없으면 검사만으로 통과
3. sbx new --pack <폴더> --id <팩 id> [--license <SPDX>]: pack.json (version 0.1.0 · license · description) · scripts/main.ts (컴포넌트
   <팩>.counter + export function bump) · scripts/tick.ts (server 시스템 <팩>.tick) · prefabs/thing.json. 옆의 임시 폴더에 만들어
   콘텐츠 검사 (오류 · 경고 0) · 패키지 훑기를 통과해야 제자리로 — 비지 않은 폴더는 거절 (AlreadyExists, sbx new 와 같다)
4. 라이선스를 적지 않으면 "LicenseRef-proprietary" (SPDX 의 사용자 정의 식 — 권리를 내주지 않는 쪽). 출력이 배포 전에 바꾸라고 알린다
```

## 근거

```text
- playthrough.json · tests/: 샘플 두 개가 이미 루트의 playthrough.json 을 쓴다. 여러 개는 tests/ 에 — 프로젝트 폴더의 다른 파일과
  섞이지 않는다
- 같은 프로세스: 서버를 띄우지 않아도 헤드리스 서버와 같은 러너 · 같은 결과 (ADR-0055 의 보장). 빠르고 포트가 없다
- 기본 라이선스: 사용자가 정하지 않은 공개 라이선스를 대신 붙이지 않는다 (MIT 를 기본으로 하면 의도하지 않은 공개가 된다)
```

## 결과

- 얻는 것: 패키지 제작 → 설치 → 시험이 sbx 명령으로 이어진다 (new --pack → add → test). CI 가 sbx test 하나로 프로젝트를 본다.
- 포기하는 것: 스크립트마다 다른 시드 · 시작 월드 (프로젝트의 시작 월드 하나), 병렬 실행.
- 위험: 자동 플레이가 길면 sbx test 가 오래 걸린다 (maxTicks 가 상한).

## 대안

| 대안                                   | 기각 사유                                                        |
|----------------------------------------|------------------------------------------------------------------|
| sbx test 가 SandboxServer 를 띄운다    | 포트 · 프로세스 관리 — 같은 러너를 같은 프로세스에서 돌리면 된다  |
| 자동 플레이를 매니페스트에 나열        | 매니페스트 판 (schemaVersion) 을 올려야 한다 — 폴더 규칙이 충분하다 |
| 틀의 기본 라이선스 MIT                 | 사용자가 고르지 않은 공개 라이선스                               |

## 재검토 조건

자동 플레이마다 다른 시작 월드 · 시드가 필요할 때, 시험이 많아져 병렬이 필요할 때.
