# ADR-0072. Git 소스 — 시스템 git 을 셸 없이, 실행기는 도구가 넣는다, 저장소 뿌리 = 팩, 푼 커밋을 기록

- 상태: **Accepted** · 날짜: 2026-10-27
- 관련: [ADR-0071](0071-v0-4-package-manager-scope-pack-as-package-single-version-resolution.md) 3 (소스 — 시스템 git, 새 서드파티
  없음), [01-ARCHITECTURE](../01-ARCHITECTURE.md) (core 는 OS 헤더를 모른다 — AGENTS 4장), [09-SERIALIZATION](../09-SERIALIZATION.md) 10장,
  [18-PLATFORM](../18-PLATFORM.md) 6.4 (v0.4 M2-T2)

## 맥락

ADR-0071 은 Git 소스를 "시스템 git 실행 파일로" 정했다. 저장소에는 프로세스를 띄우는 코드가 없고, core 는 windows.h 등 OS 헤더를
include 할 수 없다 (check_includes). 의존의 URL · rev 는 다른 제작자가 쓴 pack.json · 매니페스트에서 온다 — 셸을 거치면 명령 주입,
'-' 로 시작하면 git 옵션 주입, git 의 원격 도우미 (`ext::`) 는 임의 명령 실행이다. 정할 것: 어디서 띄우나, 무엇을 거절하나,
어떤 git 명령으로 받나, 저장소의 어디가 팩인가.

## 결정

```text
1. core/package/GitSource 는 실행기를 받는다 (ProcessRunner — argv 와 작업 폴더 → 종료 코드 · 출력). 띄우는 코드는 tools/sbx/Process
   (Windows CreateProcessW — CommandLineToArgvW 가 되돌리는 따옴표, 그 밖 posix_spawnp + addchdir). 셸을 거치지 않는다. 시험도
   같은 실행기를 쓴다 (tests 에 그 파일을 넣는다 — SbxOptions 와 같다). 서버 · 클라이언트는 git 을 띄우지 않는다 (설치는 sbx 의 일)
2. 모양 검사 (git 을 띄우기 전): url — 비지 않음 · 2,048 바이트 · '-' 로 시작 안 함 · 공백 · 제어 문자 없음 · "::" (원격 도우미
   <transport>::<address>, ext:: 포함) 없음. rev — [A-Za-z0-9._/-] · 256 바이트 · '-' 시작 · ".." 없음 (태그 · 브랜치 · 커밋 해시)
3. 받기: git clone --quiet --no-checkout -- <url> <임시> → git checkout --quiet --detach <rev> -- → git rev-parse HEAD (40 글자 확인).
   모든 git 호출에 -c core.askPass= -c credential.interactive=never (대화형 질문 없이 실패) · -c core.autocrlf=false -c core.eol=lf
   (줄 끝 변환 끔 — Windows 의 기본 core.autocrlf=true 가 LF 를 CRLF 로 바꿔 머신마다 digest 가 달라지는 것을 실행 확인에서 찾았다.
   저장소의 .gitattributes eol 은 저장소가 정한 것이라 그대로). 시스템 · 사용자 git 설정 (인증 · 프록시) 은 그대로 쓴다. 얕은 받기 (--depth) 는 커밋 해시를 못 받는 서버가 있어 쓰지 않는다 (큰 저장소 최적화는 [계획])
4. 저장소 뿌리 = 팩 (pack.json). 하위 폴더 (monorepo) 는 [계획]. 꺼낸 폴더를 로컬 소스처럼 캐시에 (PackSource — .git 은 digest 에
   들지 않는다), 임시 작업 폴더는 지운다. 푼 커밋을 돌려준다 — 잠금 파일 (M3) 이 커밋 · digest 를 고정한다
5. git 이 없으면 NotFound "Git 소스는 시스템 git 이 필요하다 (로컬 폴더 · 캐시 소스는 된다)". 받기 · 꺼내기 실패는 git 의 마지막
   400 바이트를 붙인 IoError
6. 도구: sbx fetch --git <URL> --rev <태그 · 커밋> [--id <팩>] [--cache-dir <폴더>] — 푼 커밋 · digest · 캐시 자리
```

## 근거

```text
- 실행기 주입: core 의 경계 (OS 헤더 금지) 를 지키고, 시험이 가짜 실행기로 "모양이 틀리면 띄우지 않는다" 를 볼 수 있다
- 셸 없음 + 옵션 끝 "--" + '-' 시작 거절 + "::" 거절: 다른 사람이 쓴 문자열이 명령 · 옵션 · 도우미로 읽히는 길을 모두 막는다
- clone + checkout: 태그 · 브랜치 · 커밋 해시를 같은 길로 받는다 (fetch <sha> 는 서버 설정 allowReachableSHA1InWant 에 기댄다)
- 뿌리 = 팩: 팩 하나 = 저장소 하나가 가장 흔하고, 경로 규칙 (하위 폴더 탈출) 을 지금 늘리지 않는다
```

## 결과

- 얻는 것: Git 태그 · 커밋으로 팩을 받아 커밋 · digest 로 고정할 수 있다 (재현 · 오프라인은 캐시 digest).
- 포기하는 것: libgit2 내장 (git 없는 기계에서 Git 소스), 얕은 받기, monorepo 하위 폴더, 받은 저장소의 서명 확인 (v0.6 서명과 함께).
- 위험: 로컬 폴더 소스는 디스크의 바이트 그대로라, 그 폴더를 autocrlf 로 꺼낸 사람과 아닌 사람의 digest 가 다르다 — 잠금 파일로
  재현하려면 Git 소스나 캐시 digest 를 쓴다 (M5 재현 시험에서 다시 본다). 시스템 git 의 버전 · 설정 차이 (credential.interactive 는 git 2.46+ — 옛 git 은 그 설정을 무시한다). 사용자 git 설정의
  자격 증명 도우미가 창을 띄울 수 있다 (core.askPass 만 끈다).

## 대안

| 대안                                  | 기각 사유                                                            |
|---------------------------------------|----------------------------------------------------------------------|
| std::system · popen (셸)              | URL · rev 가 셸 문법으로 읽힌다 — 명령 주입                          |
| core 에서 직접 CreateProcess          | core 는 OS 헤더를 모른다 (AGENTS 4장 · check_includes)                |
| libgit2                               | ADR-0071 — 서드파티 · 빌드 · 보안 표면                               |
| git fetch --depth 1 <rev>             | 커밋 해시는 서버 설정에 따라 받을 수 없다                            |
| 저장소 전체 (.git 포함) 를 캐시에      | digest 가 git 내부 파일 (팩 아님) 에 따라 달라진다                    |

## 재검토 조건

git 이 없는 사용자가 Git 소스를 자주 쓸 때 (내장 클라이언트), monorepo 패키지가 필요할 때, 큰 저장소의 받기 시간이 문제일 때.
