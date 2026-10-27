# ADR-0071. v0.4 Package Manager 범위 — 패키지 = 콘텐츠 팩, 소스는 로컬 · Git · 캐시, 팩 id 마다 한 버전 (D-05), 잠금 파일 · 트랜잭션 설치

- 상태: **Accepted** · 날짜: 2026-10-27
- 관련: [ADR-0039](0039-platform-scope-version-line-after-phase-15.md) (버전 줄), [ADR-0040](0040-game-project-manifest-world-is-save-folder.md)
  (매니페스트 · SemVer), [ADR-0054](0054-game-package-sbpkg-sha256-digest-save-binding.md) (.sbpkg · SHA-256 digest),
  [ADR-0056](0056-sandboxscript-language-subset-own-compiler-no-gc.md) (팩 하나 = 스크립트 모듈 하나), [11-CONTENT-SCHEMA](../11-CONTENT-SCHEMA.md)
  1장 (팩 · requires), [18-PLATFORM](../18-PLATFORM.md) 6.4, 초안 [roadmap/v0_4](../platform/roadmap/v0_4_PACKAGE_MANAGER.md) ·
  [architecture/06](../platform/architecture/06_PACKAGE_MANAGER.md) · [12](../platform/architecture/12_COMPATIBILITY_AND_MIGRATION.md)

## 맥락

v0.4 의 목표는 "다른 제작자의 Feature 를 설치 → 빌드 → 테스트 플레이, 깨끗한 머신에서 같은 잠금 파일로 같은 game digest, 충돌 ·
악성 매니페스트 · 깨진 파일을 거부하고 설치를 되돌린다" (초안 5장). 저장소에는 이미 콘텐츠 팩 (pack.json — id · version · requires,
요구 팩의 닫힘 · 위상 정렬 · 팩 경계 V2), 게임 프로젝트 매니페스트 (content.packs), 게임 패키지 (.sbpkg — 파일마다 SHA-256 ·
digest), SemVer · 범위 (v0.2 — "||" · "~" · "*" 미지원) 가 있다. 정할 것: 패키지가 무엇인가, 어디서 오나, 버전을 어떻게 고르나
(열린 결정 D-05), 잠금 · 설치 · 되돌리기, 무엇을 v0.4 밖에 두나.

## 결정

```text
1. 패키지 = 콘텐츠 팩 (새 컨테이너 없음). pack.json 을 넓힌다 [계획 M1]: version (SemVer, 의존에 쓰이면 필수), license (SPDX 식 —
   배포하는 팩은 필수), dependencies {팩 id: 범위 + 소스} (requires 는 그 키의 목록과 같은 뜻으로 남는다). 패키지 정체성 = 팩 id
   (이미 콘텐츠 id 의 이름공간 — "<팩>.<이름>", 스크립트 컴포넌트 id 도 같은 규칙). 같은 팩 id 의 두 패키지 = 충돌 (빌드 실패)
2. 프로젝트 매니페스트 schemaVersion 3 [계획 M1]: dependencies {팩 id: {version: 범위, path | git + rev}}. content.packs 는 지금처럼
   불러올 팩을 고른다 (프로젝트 content/ 의 팩 + 설치한 패키지)
3. 소스 [계획 M2]: 로컬 폴더 (프로젝트에서 상대 경로), Git (태그 · 커밋 — 시스템 git 실행 파일로, 새 서드파티 없음), 오프라인 캐시
   (사용자 캐시 폴더 <cache>/sandbox/packages/<digest>/ — 같은 digest 는 다시 받지 않는다). 패키지 digest = .sbpkg 와 같은 규칙
   (파일마다 "<sha256> <size> <path>\n", path 순) — 머신 · 시각 · 경로와 무관
4. D-05 버전 해결: 그래프 전체에서 팩 id 마다 한 버전 (공유 ECS 스키마는 여러 버전이 함께 살 수 없다 — 초안 06 4장). 레지스트리가
   없으므로 소스가 곧 버전이다: 각 의존은 소스 하나를 가리키고, 받은 팩의 version 이 그 팩을 가리키는 모든 범위를 만족해야 한다
   (아니면 오류 — 누가 어떤 범위를 요구했는지). 같은 팩 id 를 다른 소스 (digest 다름) 로 가리키면 충돌 — 프로젝트 매니페스트가
   그 팩을 직접 적으면 그것이 이긴다 (명시적 고정), 아니면 빌드 실패. 순환은 오류 (지금 requires 와 같다). 범위 문법은 "~" (같은
   minor — npm 과 같은 뜻) 와 "*" 를 더한다. "||" · 하이픈 범위는 계속 Unsupported (소스 하나 = 버전 하나라 합집합이 필요 없다).
   유료 패키지 EULA 는 v0.8 (D-09 와 함께)
5. 잠금 파일 sandbox.lock [계획 M3] (프로젝트 루트, JSON, 팩 id 순): 팩마다 version · 소스 (상대 경로 또는 git URL + 푼 커밋) ·
   digest · license · dependencies. sbx build · 열기는 잠금 파일만 본다 (재현). 잠금과 다른 digest 는 거절
6. 설치 = 트랜잭션 [계획 M3]: 임시 폴더에 받기 → digest · 매니페스트 (모르는 키 · 경로 탈출 · 심볼릭 링크 · 크기 상한 거절) ·
   콘텐츠 검사 (스크립트 컴파일 · 검증 포함) → 모두 통과하면 <프로젝트>/.sandbox/packages/<팩 id>/ 와 잠금 파일을 한 번에 바꾼다.
   하나라도 실패하면 프로젝트는 그대로 (되돌리기). 설치 훅 · 네이티브 실행은 없다 (팩에는 실행할 단계가 없다 — 스크립트는
   SandboxScript 소스뿐, 로드가 컴파일 · 검증한다)
7. CLI [계획 M3 · M4]: sbx add <팩> (--path | --git --rev) [--version 범위] · remove · update [팩] · install (잠금 그대로) · test
   (콘텐츠 검사 + 프로젝트의 자동 플레이 스크립트들). sbx new 는 그대로 (초안의 init = new, 팩 하나짜리 패키지 틀은 sbx new --pack)
8. Feature 의 Export / Import [계획 M4 — 그때 ADR]: 다른 팩의 스크립트 컴포넌트 · 함수를 import (지금은 팩 하나 = 모듈 하나, 팩 사이
   import 없음 — 링크 모델은 M4-T1 에서 정한다), 설치 뒤 sandbox.d.ts · tsconfig 갱신 (자동 완성), 에디터 팔레트에 패키지별 묶음
9. v0.4 밖: Registry · 검색 · publish (v0.6), 패키지 서명 (v0.6), 권한 선언 · 심사 (스크립트에 http · storage 가 없다 — v0.7 이후),
   원격 클라이언트가 패키지를 받기 (v0.7), 여러 버전 공존 (하지 않는다 — 4)
```

## 근거

```text
- 팩 = 패키지: 로더 · 검사 · 팩 경계 · 위상 정렬 · .sbpkg 가 이미 팩 단위다 — 새 단위를 만들면 같은 규칙을 두 번 쓴다
- 소스 = 버전: 레지스트리 없이 (v0.6 전) 재현성을 얻는 가장 작은 모델. 범위는 "고르기" 가 아니라 "확인" 이다 (Cargo 의 git 의존과 같다)
- 한 버전: 컴포넌트 stableId · 세이브 · 복제가 id 하나에 모양 하나를 가정한다 (ADR-0059)
- 시스템 git: libgit2 를 붙이지 않는다 (빌드 · 보안 표면). 없으면 Git 소스만 안 된다 (로컬 · 캐시는 된다)
- 트랜잭션: 초안의 "설치 롤백" 을 파일 시스템의 바꿔 끼우기로 — 중간 상태의 프로젝트가 없다
```

## 결과

- 얻는 것: 남의 Feature 팩을 경로 · Git 으로 설치하고 잠금 파일로 같은 게임을 다시 만든다. 지금 팩 · 패키지 · 검사 코드를 그대로 쓴다.
- 포기하는 것: 범위 안에서 "가장 새 버전" 을 고르는 해결 (레지스트리가 생기는 v0.6 에 다시 본다), 여러 버전 공존, 설치 훅.
- 위험: 팩 사이 스크립트 import (M4) 는 컴파일 · 링크 모델을 바꾼다 — 그 태스크의 ADR 에서 범위를 다시 줄일 수 있다. Git 소스는
  시스템 git 에 기댄다 (CI · 깨끗한 머신 시험에 git 이 있어야 한다).

## 대안

| 대안                                   | 기각 사유                                                                   |
|----------------------------------------|-----------------------------------------------------------------------------|
| 새 패키지 형식 (zip · .sbfeat)          | 팩 · .sbpkg 의 규칙을 다시 쓴다 — 팩 폴더가 이미 배포 단위다                |
| npm 식 해결 (범위 안 최고 버전)         | 고를 후보 목록 (레지스트리) 이 없다 — v0.6                                   |
| 여러 버전 공존 (이름 바꾸기 · 접두사)    | 컴포넌트 id · 세이브 · 복제가 깨진다                                         |
| libgit2 내장                            | 서드파티 · 빌드 시간 · 보안 표면 — 시스템 git 으로 충분                     |
| 설치를 content/ 에 복사                 | 사용자 팩과 섞여 업데이트 · 제거 · 되돌리기가 어렵다 — .sandbox/packages 로 나눈다 |

## 재검토 조건

Registry (v0.6) 가 생길 때 (범위 해결 · publish), 같은 팩 id 의 공존이 꼭 필요한 게임이 나올 때, 팩 사이 import 의 링크 모델이 정해질 때 (M4-T1).
