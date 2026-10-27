# ADR-0073. 잠금 파일 형식 · 설치 폴더 · content/ 팩은 잠그지 않는다 · 로더의 여러 루트

- 상태: **Accepted** · 날짜: 2026-10-27
- 관련: [ADR-0071](0071-v0-4-package-manager-scope-pack-as-package-single-version-resolution.md) 5 · 6 (잠금 파일 · 설치
  트랜잭션), [ADR-0072](0072-git-source-system-git-shell-free-runner-injected.md) (푼 커밋), [09-SERIALIZATION](../09-SERIALIZATION.md)
  12장, [18-PLATFORM](../18-PLATFORM.md) 5.2 · 6.4 (v0.4 M3-T2)

## 맥락

ADR-0071 은 "sandbox.lock 에 팩마다 version · 소스 · digest · license · dependencies, 설치는 <프로젝트>/.sandbox/packages/<팩 id>/
와 잠금 파일을 한 번에 바꾼다, 잠금과 다른 digest 는 거절" 까지 정했다. 구현하며 정할 것: 파일 모양, 프로젝트의 content/ 팩
(제작자가 고치는 원본) 도 digest 로 잠그나, 로더가 두 폴더를 어떻게 읽나, 설치 폴더가 잠금과 다를 때 무엇을 하나, 바꿔 끼우기의
단계.

## 결정

```text
1. sandbox.lock = 정규형 JSON (키 순서 고정, 들여쓰기 2): {"lockVersion": 1, "packs": {"<팩>": {version, license, source, digest?,
   dependencies}}} — packs 는 팩 id 순 (JSON 객체 키 순). source = {"content": true} | {"path": 적은 그대로} | {"git", "rev", "commit"}.
   모르는 키 · 다른 lockVersion · 모양 오류는 모두 모아 거절한다 (매니페스트와 같은 태도)
2. content/ 팩은 digest 로 잠그지 않는다 (source {"content": true} · version · dependencies 만). 그 폴더는 제작자가 매일 고치는
   원본이다 — 잠그면 저장할 때마다 잠금이 깨진다. 잠그는 것은 남이 만든 path · git 팩뿐 (설치한 것)
3. 설치 폴더 <프로젝트>/.sandbox/packages/<팩 id>/ 에는 path · git 팩만. 로더는 루트 여럿을 차례로 읽는다 (ContentLoader 의
   ContentRoot — content/ 다음 .sandbox/packages, 같은 팩 id 가 두 곳에 있으면 V1). 팩 폴더 표시는 루트 접두사를 붙인 프로젝트
   기준 경로 ("content/eco/…" · ".sandbox/packages/inv/…") — contentHash 와 무관 (표시뿐)
4. 열기 · 검사 (P8 — 새 규칙): 잠금이 있으면 모양, 그리고 잠금의 path · git 팩마다 설치 폴더가 있고 pack.json id · digest 가
   같은가, 설치 폴더에 잠금에 없는 폴더가 없는가. 잠금이 없는데 설치 폴더에 팩이 있으면 오류. P8 이 하나라도 있으면 설치 폴더를
   콘텐츠 루트로 쓰지 않는다 (고친 팩을 읽지 않는다 — 서버 · 클라이언트 · sbx check 모두 같은 checkProject)
5. 설치 = 임시 (.sandbox/packages.staging) 에 캐시의 팩을 훑기 파일만 복사 → 다시 훑어 digest 확인 → 콘텐츠 검사 (content/ +
   staging 을 같은 로더로 — 스크립트 검증 포함) → packages → packages.old, staging → packages, 잠금을 원자적으로 쓰기, old
   지우기. 실패하면 이름을 되돌리고 staging 을 지운다. 프로세스가 중간에 죽어 남은 .sandbox/packages.* 는 다음 설치가 지운다
6. .sbpkg 는 sandbox.lock 과 설치 폴더의 읽은 팩을 담는다 — 푼 쪽도 같은 P8 확인으로 연다
7. 잠금이 있어도 매니페스트 dependencies 와의 일치 (빠진 · 낡은 잠금) 는 이번에 보지 않는다 — sbx add · install 과 함께 M3-T3
```

## 근거

```text
- content/ 를 잠그지 않음: 재현이 필요한 것은 남의 코드 (받은 팩) 다. 제작자 자신의 팩은 이미 프로젝트 폴더 · VCS 에 있다
- 여러 루트: 설치한 팩을 content/ 에 복사하지 않는다 (ADR-0071 대안 — 사용자 팩과 섞인다). 로더의 팩 id 찾기 · requires · 위상
  정렬을 그대로 쓴다
- P8 이면 설치 폴더를 읽지 않음: 고쳐진 남의 스크립트가 한 틱도 돌지 않는다 (콘텐츠 오류가 뒤따라 나와도 원인은 P8 이 먼저)
- 바꿔 끼우기 = 이름 바꾸기 두 번 + 원자적 쓰기: 같은 볼륨 안의 rename 은 원자적이다. 셋을 한 번에 하는 파일 시스템 연산은 없다
  — 남는 창 (두 rename 사이 · 잠금 쓰기 전) 에 죽으면 열기가 P8 로 거절하고 다시 설치하면 고쳐진다 (어긋난 채로 돌지 않는다)
```

## 결과

- 얻는 것: 설치한 팩을 digest 로 고정하고, 고친 · 남는 · 잠금 없는 설치를 열기 전에 거절한다. .sbpkg 가 설치한 팩을 그대로 옮긴다.
- 포기하는 것: content/ 팩의 재현 보장 (VCS 의 일), 바꿔 끼우기의 완전한 원자성 (위 창 — P8 이 받쳐 준다).
- 위험: 열 때마다 설치한 팩 전체를 SHA-256 으로 훑는다 (팩 상한 256 MiB — 큰 팩이 늘면 표시 파일 · mtime 캐시를 [계획]).
  팩 폴더 표시가 "content/<팩>" 으로 바뀌어 스크립트 트랩 로그의 경로도 프로젝트 기준이 된다 (세이브 폴더 · 시나리오는 그대로).

## 대안

| 대안                                       | 기각 사유                                                                 |
|--------------------------------------------|---------------------------------------------------------------------------|
| content/ 팩도 digest 로 잠금               | 저장할 때마다 잠금이 깨진다 — 제작 흐름을 막는다                           |
| 설치 팩을 content/ 로 복사                 | ADR-0071 — 사용자 팩과 섞여 업데이트 · 제거 · 되돌리기가 어렵다            |
| P8 을 경고로 (고친 팩도 연다)              | 고친 남의 스크립트가 돈다 — 재현 · 안전 모두 잃는다                        |
| 잠금을 TOML · 줄 형식으로                  | 매니페스트 · pack.json 과 같은 JSON 파서 · 오류 모양을 쓴다                |

## 재검토 조건

제작자가 content/ 팩도 고정하고 싶어 할 때 (팀 · CI 재현), 설치한 팩이 커져 열기 시간이 문제일 때, 매니페스트와 잠금의 자동 일치가
들어올 때 (M3-T3).
