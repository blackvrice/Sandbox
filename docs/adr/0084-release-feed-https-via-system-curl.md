# ADR-0084. 릴리스 피드의 HTTPS — 시스템 curl 을 셸 없이, 프로세스 실행기는 SandboxProcess 로

- 상태: **Accepted** · 날짜: 2026-10-29
- 관련: [ADR-0072](0072-git-source-system-git-shell-free-runner-injected.md) (시스템 git 을 셸 없이 — 같은 길), [20-CREATOR-ALPHA](../20-CREATOR-ALPHA.md)
  7.6 · 7.7, [01-ARCHITECTURE](../01-ARCHITECTURE.md) (타깃 표 · 경계), [18-PLATFORM](../18-PLATFORM.md) 6.6 (v0.5 M1-T4)

## 맥락

새 버전 알림 (20 7.6) 은 M1-T3 에서 피드 **파일** 까지 만들었다. 공개 Alpha 채널은 GitHub Releases 다 — REST "List releases"
(`https://api.github.com/repos/<소유자>/<저장소>/releases`) 를 읽으려면 HTTPS 클라이언트가 있어야 한다. 저장소에는 HTTP 코드가 없고
(ENet 은 UDP), 프로세스를 띄우는 코드는 tools/sbx/Process (core 타입 package::ProcessResult 에 묶임) 뿐이었다. 정할 것: 무엇으로
HTTPS 를 읽나, 어디서 띄우나, 무엇을 거절하나, 기본 URL 을 켜나.

대안:

| 안 | 장점 | 단점 |
|----|------|------|
| A. 시스템 curl 프로세스 (셸 없이) | 새 서드파티 · 링크 의존 없음, TLS · 프록시 · 인증서는 OS 의 curl 이 (Windows 10 1803+ · macOS · 대부분의 Linux 에 기본), ADR-0072 와 같은 길 | curl 이 없으면 못 읽는다 (조용히 — 알림 기능일 뿐), 프로세스 하나 |
| B. OS API (WinHTTP · NSURLSession · Linux 는 ?) | 프로세스 없음 | OS 마다 따로 — Linux 에는 표준 HTTPS API 가 없다 (결국 libcurl), 세 구현 · 시험 |
| C. libcurl · cpp-httplib+OpenSSL 를 external/ 에 | 한 구현 | 큰 서드파티 (TLS 스택 · 인증서 저장소 · 보안 업데이트를 우리가), 빌드 시간 |

## 결정

```text
1. A — 시스템 curl. 인자 (curlArguments): --fail --silent --show-error --location --proto =https --proto-redir =https
   --max-time 5 --max-filesize 1048576 -H "Accept: application/vnd.github+json" -A SandboxClient/<버전> -- <url>.
   URL 은 https:// 만 (평문 http · ftp · file 거절), 2,048 바이트, 공백 · 제어 문자 없음 — 셸을 거치지 않고 "--" 뒤라 옵션 주입이 없다
2. 프로세스 실행기를 tools/sbx 에서 새 타깃 SandboxProcess (platform/process — 의존 Foundation 만, OS 헤더는 그 .cpp 에만 —
   SandboxCrash 와 같은 자리) 로 옮긴다. 결과 형식은 proc::ProcessResult. sbx 는 package::ProcessRunner 로 바꾸는 어댑터만 남긴다.
   경계: SandboxProcess 는 Core · Network · Platform · Render · Editor · ImGui 를 링크하지 않는다. 서버는 링크하지 않는다
3. 클라이언트는 URL 피드를 백그라운드 스레드에서 읽는다 (5 초까지 — 시작을 막지 않는다). checkReleaseFeed 는 로그를 쓰지 않고
   FeedCheck (알림 · 이유 한 줄) 를 돌려준다 — Application 이 프레임에서 받아 로그 · "알림" 창. std::async 의 future 는 소멸에서
   기다리므로 packaged_task + 떼어 낸 스레드 (끝나기 전에 클라이언트가 끝나면 결과를 버린다)
4. 실패 (curl 없음 · 오프라인 · 시간 초과 · HTTP 오류 · 형식) 는 조용히 — info 로그 한 줄. 받기 · 설치는 하지 않는다 (v0.9)
5. 기본 피드 URL 은 아직 없다 — 사용자가 client.json updateFeed · --update-feed 로 줄 때만 읽는다. 공개 Alpha 채널이 생기는
   M7-T4 에서 기본값 (그리고 끄는 법 updateCheck false) 을 정한다 — 시작마다 외부로 요청하는 것을 채널 없이 켜지 않는다
```

## 결과

- 새 서드파티 없음. curl 이 없는 기계는 새 버전 알림만 없다 (다른 기능과 무관).
- 실행 확인: `--update-feed https://api.github.com/repos/blackvrice/Sandbox/releases` → "새 버전 없음 (지금 0.4.0, 피드 0 개)",
  릴리스가 있는 공개 저장소의 피드 → "새 버전 2.103.3 이 있다 … — 받기 · 설치는 직접" 알림 (2026-10-29, Windows 11 의 curl).
- 단위 시험은 네트워크를 쓰지 않는다 (인자 · 거절). CTest client_safe_mode 는 닿지 않는 https 주소 · 평문 http 가 시작을 막지 않는지만.
- 프록시 · 회사 인증서는 curl 의 설정 (환경 변수 · .curlrc) 을 그대로 따른다.
