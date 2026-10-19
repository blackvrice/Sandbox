# ADR-0042. 핸드셰이크가 게임 정체성을 싣는다 — 프로토콜 8, 클라이언트는 표시 · 안내만

- 상태: **Accepted** · 날짜: 2026-10-19
- 관련: [ADR-0041](0041-server-opens-game-project-strict-check.md), [ADR-0040](0040-game-project-manifest-world-is-save-folder.md),
  [08-NETWORK](../08-NETWORK.md) 4 · 5장, [18-PLATFORM](../18-PLATFORM.md) 5장

## 맥락

v0.2 M1-T3. M1-T2 로 서버가 게임 프로젝트를 돌리지만 접속한 클라이언트는 그것이 어떤 게임인지 모른다 — Welcome 에는
월드 이름(64 바이트로 잘림)과 경계뿐이고, 콘텐츠가 다르면 Reject 가 팩 id 와 contentHash 만 준다. 원격 클라이언트는 팩을
자기 `--content` 루트(기본: 저장소 content/)에서 찾으므로 프로젝트 팩이면 "팩을 찾을 수 없다"로 끝난다. 정할 것: 무엇을 어떤
메시지에, 형식 · 상한, 클라이언트가 무엇을 하나, 매니페스트 쪽 제약.

## 결정

```text
1. GameIdentity{gameId str(64), name str(256), version str(128)} 를 Welcome 끝 · Reject 끝에 (kProtocolVersion 7 → 8).
   게임이 아니면 셋 다 빈 문자열. gameId 없이 name · version 만 있으면 형식 오류. 서버(ServerHost)가 상한으로 자른다.
   값은 매니페스트에서 (net::gameIdentityOf — version 은 SemVer 문자열). 서버가 사는 동안 바뀌지 않는다 (월드를 불러와도).
2. 클라이언트는 표시 · 안내만: "월드" 패널에 "게임 <이름> — <gameId> <버전>", --headless 끝에 "game …" 줄, sbx_net_probe 에
   "게임 …" 줄. ContentMismatch 로 팩을 읽지 못하면 "게임 … 의 팩은 그 프로젝트의 content/ 에 — --content <프로젝트>/content"
   를 실패 문구에. 프로젝트를 스스로 찾거나 내려받지 않는다 (v0.5 공유 · v0.7 다운로드).
3. Hello 에는 싣지 않는다 — 클라이언트가 "이 게임만" 을 요구하는 것은 방 · 디렉터리(v0.7)의 일이다.
4. 매니페스트 content.packs 는 16 개까지 (P2) — Reject 가 실을 수 있는 팩 수(kMaxPacks)와 같다 (static_assert).
```

## 근거

```text
- 버전 협상 규칙(정확 일치)이 이미 있어 필드를 끝에 붙이고 버전을 올리는 것이 가장 단순하다 (프로토콜 2~7 과 같은 방식).
- 이름을 따로 싣는 이유: WorldMeta.name 은 64 바이트 상한이라 한글 이름이 잘린다 (매니페스트는 64 글자 = 최대 256 바이트).
- 자동 탐색 · 다운로드는 신뢰 · 무결성(서명 · 다이제스트) 문제가 따라온다 — 패키지 형식(D-02)과 보안 전송(D-07) 뒤로.
- 팩 수를 맞춰 두지 않으면 17 번째 팩부터 Reject 에 안 실려 클라이언트가 콘텐츠를 맞출 수 없다.
```

## 결과

- 얻는 것: 접속자가 게임 이름 · id · 버전을 본다, 프로젝트 팩 문제의 해결 방법이 실패 문구에 나온다. CTest
  net_server_probe_project (실제 UDP), unit 시험 (메시지 왕복 · 형식 오류 · 상한 · ServerHost · NetworkSession).
- 포기하는 것: 프로토콜 7 클라이언트와의 접속 (정확 일치 규칙 — VersionMismatch 로 거절된다).
- 위험: 게임 버전이 같은지 확인하지 않는다 — 지금은 contentHash 가 실제 호환을 지킨다. 게임 버전 정책은 M5 GamePackage.

## 대안

| 대안                                    | 기각 사유                                                            |
|-----------------------------------------|----------------------------------------------------------------------|
| WorldMeta.name 에 "게임 · 버전" 을 섞기 | 64 바이트에 잘리고, 기계가 읽을 수 없다                              |
| 새 메시지 GameInfo                      | Welcome 직후 한 번뿐인 정보 — 메시지 id 와 순서 규칙만 늘어난다      |
| 클라이언트가 프로젝트 폴더를 찾아 읽기  | 어디서 찾을지 정할 근거가 없다 (경로는 기계마다 다르다). 공유는 v0.5 |

## 재검토 조건

M5 GamePackage (게임 버전 · 다이제스트로 호환을 판정할 때), v0.7 방 · 디렉터리 (Hello 에 원하는 게임을 싣거나 서버 목록이
게임 정체성을 보일 때), 콘텐츠 팩 전송을 만들 때.
