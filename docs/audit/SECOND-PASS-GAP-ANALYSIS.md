# 2차 공백 분석 · 로드맵 재구성 근거 (2026-11-03, master 4987662)

> **감사 문서.** 1차 Studio 감사 (7563550 — [GAP-MATRIX](SANDBOX-GAP-MATRIX.md) 44 행 · [ARCHITECTURE-REVIEW](ARCHITECTURE-REVIEW.md) R1 ~ R10 ·
> [ALPHA-READINESS](ALPHA-READINESS.md)) 를 그 뒤 다섯 커밋 (M2-T5 (4a) ~ M2-T6) 의 소스로 다시 확인하고, 새 공백 NG-01 ~ NG-17 의 설계와
> 로드맵 재구성 (ADR-0096 ~ 0098) 의 근거를 적는다. 1차 감사의 행은 지우지 않는다 — 이 문서 2 · 3장이 해결 여부 · 증거 · 새 목표를 잇는다.
> 목표 버전 · 상태의 정본은 [roadmap/task-registry](../roadmap/task-registry.md). "확인" = 이 커밋의 소스를 직접 읽음, "실행" = 명령을 돌림.

---

## 0. 요약

```text
1  1차 감사 뒤 바뀐 것은 트랙 A (M2 — 플레이어 표 · owner · Runtime v2 계약 · client 입력) 뿐. 트랙 B 공백 30 행 · 구조 위험 10 은 그대로
   (R7 은 조금 나빠짐 — Application 808 → 828 줄, NetworkSession 1,087 → 1,099 줄, 확인 wc -l)
2  옛 버전 줄 (v0.5 하나에 M3 ~ M7 · Studio 감사 몫까지) 은 한 버전에 너무 많다 — 2차 감사가 더한 NG 까지 넣으면 v0.5 는 끝나지 않는다.
   → v0.5 = Technical Alpha (끝난 M1 · M2 · M8 + E2E 하나), 나머지를 v0.6 ~ v0.11 로 나눔 (ADR-0096). Closed Creator Alpha = v0.11
3  패키지 · 데이터 무결성의 빈 계약 (빌드 스냅숏 · 동적 참조 · id · 경로 충돌 · 해시 분리 · Export Profile) → ADR-0097, v0.6
4  배포 수명주기의 빈 계약 (Player 모드 · SaveGame 마이그레이션 · 채널 · 롤백 · 플러그인 API 버전) → ADR-0098, v0.6 ~ v0.19
5  문서 일관성: 깨진 링크 1 (기존 ADR — 예외 처리) · ID 재사용 2 (ADR-0090 · 0091) · 낡은 "다음" 줄 1 (18 0장 "Runtime v2 계약 동결 → M2-T6") →
   검사기 tools/check_docs.py (CTest docs_consistency) 로 고정 (NG-11, 6장)
```

## 1. 저장소 재확인 (Phase 0)

| 항목 | 확인한 것 | 결과 |
|------|-----------|------|
| 버전 상수 | CMakeLists.txt:13 VERSION 0.4.0 · Bytecode kFormatVersion 9 · kHostApiVersion 1 (지문 0x1dfa888c39154dde) · kSimVersion 4 · kProtocolVersion 15 · .sbpkg 2 (최소 1) · kReplayVersion 1 · kWorldVersion 1 · kSchemaVersion 1 · kManifestSchemaVersion 3 | 문서 (18 4장 · 21 · 22) 와 같다 — versioning 5장에 한곳으로 모음 |
| SemVer 비교 | foundation/version/SemVer — 자리마다 정수 비교, `^0.x` 는 minor 고정 | v0.10 > v0.9 는 이미 맞다. 시험을 더함 (test_semver "0.x 의 자리는 정수", test_user_settings v0.10 피드) — 실행 결과는 7장 |
| 새 프로젝트 engine 범위 | core/project createProject — 0.x 이면 ">=x.y.z <1.0.0" | 0.x minor 를 잘게 나눠도 프로젝트는 안 깨진다. 1.0 으로 가면 모든 0.x 프로젝트가 거절 → NG-16 |
| 에셋 | render/asset/AssetId.hpp (Fnv1a 경로 해시) · GamePackage.cpp (assets 없음) | R1 · R2 그대로 |
| 진단 위치 | script/lang/Diagnostic.hpp:16 "offset 은 바이트, column = 코드 포인트" | 편집기 (UTF-16 · 화면 열) 와 다르다 → NG-05 |
| 콘텐츠 해시 | core/content/ContentLoader.hpp:13 — presentation/** 은 contentHash 에서 뺀다 | 시뮬레이션 해시는 이미 분리. 표시 digest 가 핸드셰이크에 없다 → NG-14 |
| 세이브 결속 | core/persist/WorldSave.hpp GameBinding {gameId, version, package} | 다른 게임은 거절하지만 같은 게임의 옛 version 을 옮기는 길이 없다 → NG-04 |
| 레이아웃 · 자동 저장 | ImGuiLayer.cpp:198 IniFilename = nullptr · ServerOptions.hpp:7 "[계획] --autosave" | GM-08 · GM-10 그대로 |
| 문서 ↔ 코드 | 18 0장 "다음" 줄에 끝난 M2-T6 이 남음 · 20 3.8 · 22 의 M2-T6 표시는 고쳐짐 | 18 고침 (이 커밋) |
| 깨진 링크 | 저장소 전체 .md 상대 링크 | 1 개 — ADR-0085 → 0057 파일 이름 오타. ADR 은 고치지 않으므로 검사기 예외 |

## 2. Gap Matrix 재확인 (1차 44 행 → 해결 여부 · 새 목표)

1차 감사의 상태 칸은 그대로 두고, 바뀐 것과 새 목표 버전 · 태스크만 적는다. "변화 없음" = 이 커밋의 소스에서 같은 근거 (파일 · 줄) 를 다시 확인.

| 행 | 1차 상태 | 지금 (근거) | 태스크 → 새 목표 |
|----|----------|-------------|------------------|
| GM-01 | Partial ⟂접근 | 변화 없음 | M6-T1 → v0.8 |
| GM-02 | Partial ⟂접근 | 변화 없음 | NG-01 · M6-T3 → v0.6 · v0.8 |
| GM-03 | Missing | 변화 없음 (GamePackage.cpp) | M6-T9 → v0.6 |
| GM-04 | Planned | 변화 없음 (AssetId.hpp) | NG-12 · M6-T9 → v0.6 |
| GM-05 | Missing | 변화 없음 | M6-T9 (가져오기 최소) v0.6 · ST-06-3 v0.10 |
| GM-06 | Planned | 변화 없음 | RT-06-12 v0.6 · ST-06-3 v0.10 |
| GM-07 | Missing | 변화 없음 | ST-06-3 → v0.10 |
| GM-08 | Missing ⟂낡음 | 변화 없음 (ImGuiLayer.cpp:198) | M6-T6 → v0.8 |
| GM-09 | Missing | 변화 없음 | ST-06-2 → v0.8 |
| GM-10 | Planned | 변화 없음 (ServerOptions.hpp:7) | M6-T7 → v0.8 |
| GM-11 | Partial | 변화 없음 | ST-06-1 → v0.8 |
| GM-12 | Partial | 프로토타입 그대로 (LanguageService — ctx. 후보) | NG-05 · M6-T8 → v0.9 |
| GM-13 | Partial | 변화 없음 — M2-T6 · T7 이 Host 표에 항목을 더함 (doc 칸 있음) | M6-T8 → v0.9 |
| GM-14 | Missing | 변화 없음 | ST-06-8 → v0.9 |
| GM-15 | Planned | 변화 없음 (.sbc 9) | M6-T4 → v0.9 |
| GM-16 | Missing | 변화 없음 | ST-06-7 → v0.9 |
| GM-17 | Partial | 변화 없음 | ST-07-3 → v0.19 |
| GM-18 | Missing | 변화 없음 | ST-06-5 → v0.10 (NG-04 먼저 — 오버라이드는 세이브 형식을 바꾼다) |
| GM-19 | Missing | 변화 없음 | ST-06-6 → v0.10 |
| GM-20 | Missing | 변화 없음 | ST-06-11 → v0.8 |
| GM-21 | Designed | 선행 M2-T5 ✅ · M2-T6 ✅ (client.send — 버튼이 쓸 길) | M3-T1 ~ T6 → v0.7 |
| GM-22 | Planned | 변화 없음 | ST-06-4 → v0.10 |
| GM-23 | Partial | 변화 없음 | ST-06-9 → v0.8 |
| GM-24 | Partial ⟂접근 | 변화 없음 | ST-06-9 → v0.8 |
| GM-25 | Partial | 변화 없음 | ST-06-10 → v0.10 |
| GM-26 | Planned | 변화 없음 | ST-06-10 → v0.10 |
| GM-27 | Partial | 변화 없음 | ST-07-1 · ST-07-2 → v0.19 |
| GM-28 | Partial | 변화 없음 — R7 이 조금 나빠짐 (1장) | ST-06-1 → v0.8 |
| GM-29 | Partial ⟂불일치 | 변화 없음 (ContentPanel) | M6-T8 → v0.9 (저장 위치 구분) |
| GM-30 | Missing | 변화 없음 | NG-10 → v0.19 |
| GM-31 ~ 33 | Implemented | 그대로 (시험 이름 같음) | — |
| GM-34 | Implemented | 그대로 — 클라이언트 안 진단 화면은 Player 오류 화면으로 | NG-03 → v0.8 |
| GM-35 | Implemented (신호) | **넓어짐** — M2-T6 client.send (test_client_script_session 키 → send → PlayerSignal → onEvent) | UI 연결 M3-T5 → v0.7 |
| GM-36 | Designed | 변화 없음 | M4-T1 ~ T4 → v0.7 |
| GM-37 | Partial | 변화 없음 | RT-08-4 → v0.16 |
| GM-38 | Planned | 변화 없음 | RT-09-2 → v0.20 |
| GM-39 | Partial | 변화 없음 — **태스크가 없었다** | NG-15 → v0.10 (새로 받음) |
| GM-40 | Missing | 변화 없음 | D-16 결정 → v1.x |
| GM-41 | Planned | 변화 없음 | PS-12-1 ~ 5 → v0.12 |
| GM-42 | Planned | 변화 없음 | PS-14-1 ~ 7 → v0.14 |
| GM-43 | Planned | 변화 없음 | PS-16-1 · PS-17-1 · PS-17-2 → v0.16 · v0.17 |
| GM-44 | Planned | 변화 없음 | PS-19-1 · PS-20-1 → v0.19 · v0.20 |

## 3. 구조 위험 재확인 (R1 ~ R10)

| 위험 | 지금 | 해결하는 태스크 → 버전 | 2차 감사가 더한 것 |
|------|------|------------------------|--------------------|
| R1 에셋이 패키지 밖 | 그대로 | M6-T9 → v0.6 | NG-01 (빌드 중 바뀐 파일) · NG-02 (스크립트가 이름으로 부르는 에셋) 이 없으면 R1 을 고쳐도 패키지가 틀릴 수 있다 |
| R2 id = 경로 해시 | 그대로 | M6-T9 → v0.6 | NG-12 (같은 id · 다른 내용) · NG-13 (대소문자만 다른 경로) |
| R3 접근 경로 없음 | 그대로 | M6-T1 · M6-T3 · ST-06-9 → v0.8 | NG-03 (받는 사람의 접근 경로 — Player) |
| R4 두 진실원 | 그대로 | M6-T8 → v0.9 | — |
| R5 되돌릴 수 없는 편집 | 그대로 | ST-06-1 → v0.8 | — |
| R6 자동 저장 없음 | 그대로 | M6-T7 · M6-T6 → v0.8 | Studio Document 버전 번호 (versioning 5장) |
| R7 앱 클래스 비대 | 조금 나빠짐 (1장) | ST-06-1 → v0.8 | M3-T3 (UI 백엔드) 를 Application 에 더 얹지 말 것 — 새 파일로 (리뷰 기준) |
| R8 디버그 정보 없음 | 그대로 | M6-T4 → v0.9 | NG-05 (줄 표의 열 단위를 코드 포인트로 고정) |
| R9 Prefab = 복사 | 그대로 | ST-06-5 → v0.10 | NG-04 먼저 |
| R10 편집기 스레드 소유 | 그대로 | ST-06-1 → v0.8 | NG-07 (헤드리스 Studio 시험이 스레드 경계를 시험) |

## 4. 새 설계 (NG — 계약 요지. 정본 자리는 각 칸의 문서)

### 4.1 NG-01 빌드 스냅숏 일관성 (ADR-0097, [26](../26-ASSET-PIPELINE.md) 8장)

```text
문제    빌드가 파일을 하나씩 읽는 동안 사용자가 저장하면 패키지에 옛 PNG + 새 스크립트가 섞인다 (지금 buildPackage 는 바로 디스크를 읽는다)
계약    1 빌드 시작 때 입력 목록 (경로 · 크기 · mtime · SHA-256) 을 만든다 = 스냅숏. 모든 읽기는 목록의 해시와 맞춰 본다
        2 읽은 내용의 해시가 목록과 다르면 빌드 실패 "빌드 중 바뀐 파일: <경로>" — 조용히 새 내용을 쓰지 않는다
        3 같은 스냅숏 → 같은 바이트 → 같은 digest (정렬 · 시각 없음 — 지금 .sbpkg 규칙 그대로)
        4 빌드 출력은 임시 파일 → 원자적 이름 바꾸기. 실패 · 취소 = 임시 파일 정리, 옛 출력 그대로
        5 Studio 는 빌드 중 편집을 막지 않는다 (2 가 잡는다)
시험    빌드 중 파일 바꾸기 (훅) → 실패 문장, 같은 입력 두 번 → 같은 digest, 취소 → 임시 파일 없음
```

### 4.2 NG-02 동적 에셋 참조 — Asset Group (ADR-0097, 26 8장)

```text
문제    정적 참조 (콘텐츠 JSON 의 에셋 id) 만 따라가면 스크립트가 이름으로 만드는 참조 ("enemy_" + n) 가 패키지에서 빠진다
계약    1 정적 참조 = 콘텐츠 · UI 문서의 에셋 필드 (스키마가 안다) — 자동 포함
        2 동적 참조 = assets/groups.json 의 그룹 {name, include: [glob · 라벨], platforms?} — 그룹 전체를 포함. 스크립트는 그룹 안의 이름만 쓸 수 있다
        3 .meta 의 labels: ["ui", "enemy"] — 그룹이 라벨로 고른다
        4 빌드 보고: 그룹마다 개수 · 크기, 어디서도 참조하지 않은 에셋 (경고 — 지우지 않음), 실행 중 그룹 밖 이름 요청 = 진단 (null 핸들)
범위    v0.6 는 그룹 · 라벨 · 보고. 원격 다운로드 · 묶음 분할 (Addressables 의 원격 번들) 은 [계획] v0.16 RT-08-4 와 같이
시험    그룹 포함 · 빠진 이름 진단 · 크기 보고
```

### 4.3 NG-03 Player 모드 (ADR-0098, [25](../25-STUDIO-ARCHITECTURE.md) 12장)

```text
결정    별도 실행 파일 대신 SandboxClient 의 모드 (--play <패키지> · 파일 연결 · 최근 게임) 로 시작. 별 실행 파일은 배포 크기 · 서명 요구가 생기면 (v0.13)
흐름    .sbpkg 더블클릭 → 검증 (digest · 형식 · engine 범위 · 플랫폼 프로필) → 풀기 (사용자 캐시, 패키지 digest 폴더) → 로컬 서버 + 클라이언트 → 플레이
        실패마다 문장 화면 (형식 · 범위 · 손상 · 엔진 낮음) + "보고 묶음 만들기" (M1)
없는 것 편집 · 콘솔 · 개발 패널 (Player 모드는 숨김), 네트워크 게시 (v0.13)
시험    헤드리스: --play <패키지> --ticks N --exit → 같은 해시, 손상 패키지 → 오류 코드 · 문장
```

### 4.4 NG-04 SaveGame 마이그레이션 (ADR-0098, 26 8장)

```text
지금    엔진 쪽: kWorldVersion · kSchemaVersion · 스크립트 컴포넌트 자동 마이그레이션 (ADR-0061). 게임 쪽: GameBinding.version (기록만)
계약    1 세이브의 game.version 과 지금 게임 version 을 SemVer 로 비교. 같으면 그대로
        2 다르면 프로젝트의 migrations/ (스크립트 — server 도메인, onMigrate(from, to, world)) 를 버전 순으로 차례 적용. 빠진 단계 = 거절 (문장)
        3 적용 전 세이브를 옆에 복사 (world.bak-<from>) — 실패 · 트랩 = 원본 그대로, 오류 문장
        4 더 새 게임 버전의 세이브 = 거절 (되돌린 게임 — 옛 것으로 읽지 않는다)
        5 마이그레이션은 결정론 (onMigrate 도 Host 표 · 연료) — 같은 세이브 + 같은 체인 = 같은 해시
시험    0.1 → 0.2 → 0.3 체인 · 빠진 단계 · 트랩 = 백업 그대로 · 새 버전 거절
```

### 4.5 NG-05 텍스트 위치 계약 ([27](../27-SCRIPT-IDE.md) 9장)

```text
단위    저장 · 컴파일러 · 줄 표 = UTF-8 바이트 offset + (줄, 열 = 코드 포인트) — 지금 Diagnostic 그대로
        편집기 (ImGui) = UTF-8 바이트. LSP (v0.19) = UTF-16 code unit (LSP 기본) — 변환은 언어 서비스 한곳 (TextPosition 변환 함수)
        화면 열 = 글리프 폭 (한글 2 칸 · 결합 문자 0 칸) — 표시에만, 저장하지 않는다
IME     조합 중 문자열은 문서에 넣지 않는다 (미리보기만) — 자동 완성 · 진단은 조합 끝에. 조합 중 커서는 조합 시작 위치
줄 끝   읽을 때 CRLF · LF 를 그대로 보존 (저장 왕복이 줄 끝을 바꾸지 않는다), 위치 계산은 LF 기준
시험    "가나다" 뒤 커서 → 바이트 9 · 코드 포인트 3 · UTF-16 3, 서로게이트 (😀) 왕복, CRLF 파일 저장 왕복, 조합 중 자동 완성 없음
```

### 4.6 NG-06 게임 UI 입력 컨트롤 ([20](../20-CREATOR-ALPHA.md) 2.6)

```text
컨트롤  TextInput (한 줄 · 여러 줄 · 최대 길이 · IME) · Slider · Checkbox · Toggle · Dropdown · ScrollView · Tab
공통    포커스 순서 (문서 순 · tabIndex) · 키보드 · 게임패드 탐색 (방향 → 가까운 컨트롤) · 비활성 · 값 바인딩 (ctx.ui)
보안    값은 클라이언트 상태 — 서버에 닿는 것은 client.send 신호뿐 (서버가 판정, N1). TextInput 의 문자열은 신호 값이 i32 라 보내지 못한다 —
        채팅 · 이름은 [계획] 문자열 신호 (길이 상한 · 서버 필터) 를 따로 결정
시험    컨트롤마다 상태 · 이벤트 · 포커스 이동 · 비활성 무시, 헤드리스 배치 골든 (M3-T2 와 같은 틀)
```

### 4.7 NG-07 Studio 시험 층 ([28](../28-CREATOR-UX-GATES.md) 3.1)

```text
층 1   Editor Services 단위 시험 (ImGui 없음) — 명령 · Undo · 문서 · 빌드 · 테스트 실행 (ST-06-1 이 가능하게 한다)
층 2   헤드리스 Studio 흐름 — 서비스만으로 "새 프로젝트 → 에셋 가져오기 → 빌드 → --play" 를 CTest 로
층 3   GUI smoke — 창을 열고 N 프레임 · 패널마다 그리기 오류 없음 (CI 는 WARP · Xvfb 없이는 건너뜀 — 건너뛴 것은 건너뛰었다고 적는다)
층 4   실기 QA — 16 형식 (명령 · OS · GPU · 결과 · 커밋). 사용성 시험 (ST-06-12) 은 28 2장
규칙   G1 ~ G9 마다 어느 층이 증거인지 28 표에 적는다 — 층 3 · 4 만 있는 기준은 Alpha 진입 때 수동 기록이 필요
```

### 4.8 NG-08 Export Profile ([26](../26-ASSET-PIPELINE.md) 8장)

```text
파일    export/<이름>.json {platform: windows|linux|macos, arch: x64|arm64, renderBackends: [...], assetGroups?: [...], textureMaxSize?}
검증    대상 플랫폼의 렌더 백엔드 · 지원 형식 확인, 그룹 존재, 미검증 플랫폼 (16 실기 기록 없음) 은 "미검증" 경고 — 막지 않음
v0.6    windows-x64 하나 + 검증 · 경고 시험. 텍스처 변환 · 플랫폼별 압축은 [계획] (측정 뒤)
```

### 4.9 NG-09 출시 채널 · 단계 배포 · 롤백 ([18](../18-PLATFORM.md) 4장 · ADR-0098)

```text
채널    게임마다 alpha · beta · stable — 릴리스는 불변 (같은 버전 번호를 다시 올리지 않는다), 채널은 포인터
단계    stable 포인터를 비율 (5 % · 25 % · 100 %) 로 옮김 — 플레이어 id 해시로 결정 (같은 플레이어는 같은 쪽)
롤백    포인터를 이전 릴리스로. 이미 새 버전으로 저장한 세이브: 새 버전 세이브는 옛 게임이 거절 (NG-04 4) → 롤백 전 백업 (NG-04 3) 으로 되돌릴지
        묻는다. 제작자는 "세이브 호환" 표시 (같은 MINOR 안) 를 릴리스에 붙인다
엔진    엔진 자체 업데이트도 같은 틀 (PS-20-1 서명 업데이트 · 되돌리기)
시험    v0.13: 포인터 · 비율 결정 · 롤백 · 세이브 거절 → 백업 복구
```

### 4.10 NG-10 Editor Plugin API 버전 ([25](../25-STUDIO-ARCHITECTURE.md) 9장 · ADR-0098)

```text
번호    Editor Plugin API = 정수 major (1 부터) + 기능 표 지문 (Host API 와 같은 방식 — 더하기 = 지문만, 빼기 · 바꾸기 = major++)
매니페스트  plugin.json {id, version, editorApi: "^1", permissions: [...], extensionPoints: [...]}
호환    범위 밖 = 불러오지 않음 (문장), 지원 중단 확장 지점 = 경고 1 MINOR 뒤 제거. 네이티브 플러그인 받지 않음 (24 P5)
v0.19   계약 · 매니페스트 검사 · 호환 시험. 그 전에는 플러그인을 불러오지 않는다
```

### 4.11 NG-12 에셋 id 충돌 (§4.1 — ADR-0097, 26 8장)

```text
같은 id · 같은 내용 (SHA-256)  하나로 (경고 없음 — 의존 패키지가 같은 에셋을 실을 때)
같은 id · 다른 내용             빌드 · 열기 오류 "에셋 id 충돌: <id> — <경로 1> · <경로 2>" — 어느 쪽도 고르지 않는다
.meta 를 복사한 파일 (복제)     가져오기가 감지 (같은 id · 다른 경로 · 원본이 아직 있음) → 복제본에 새 id, 원본 유지 (Unity 의 GUID 충돌 규칙과 같은 방향)
.meta 없음 · 깨짐               새 id + 경고 (참조가 끊길 수 있음을 알린다) — 경로 해시 id 로 돌아가지 않는다
패키지 간                       의존 패키지 에셋은 패키지 이름이 이름공간 (pkg:<name>/<id>) — 프로젝트와 섞이지 않는다
```

### 4.12 NG-13 경로 정책 (§4.2 — 26 8장)

```text
저장    프로젝트 · 패키지 안 경로 = "/" 구분 · UTF-8 · NFC 로 정규화해 기록
충돌    대소문자만 다른 두 경로 · NFC 가 같은 두 경로 = 오류 (Windows · macOS 기본 파일 시스템이 같은 파일로 본다)
금지    Windows 예약 이름 (CON · PRN · AUX · NUL · COM1-9 · LPT1-9, 확장자가 있어도), 끝 공백 · 점, < > : " | ? * · 제어 문자,
        .. · 절대 경로 · 드라이브 문자, 심볼릭 링크 · 정션 (따라가지 않고 오류)
길이    경로 240 바이트 · 구성 요소 120 바이트 (Windows MAX_PATH 아래 여유) — 넘으면 오류
시험    규칙마다 픽스처 — 세 OS 의 규칙을 한 검사 함수로 (어느 OS 에서 돌려도 같은 결과)
```

### 4.13 NG-14 시뮬레이션 해시 · 표시 digest (§4.3 — ADR-0097, 08)

```text
지금    contentHash = 시뮬레이션 콘텐츠만 (presentation/** 제외 — 확인 1장). 패키지 digest = 파일 전체
더할 것  presentationDigest = presentation/** + 프로젝트 에셋 (M6-T9) 의 digest. 핸드셰이크에 같이 보낸다 (프로토콜 16)
판정    contentHash 다름 = 거절 (지금 그대로 — 규칙이 다르다). presentationDigest 만 다름 = 접속 + 경고 "보이는 것이 서버와 다를 수 있음"
리플레이 리플레이 · 세이브는 contentHash 로 결속 (표시가 바뀐 패키지로도 재생된다)
```

### 4.14 NG-11 문서 · 태스크 참조 일관성 (§4.4 — 6장)

```text
사례    ADR-0090 결과 줄 "(23 M6-T10 · M6-T7)" — 감사 첫 판 번호. 지금 M6-T9 (에셋) · M6-T3 (누락 검사) 이고 M6-T7 은 자동 저장이 됐다 — ID 재사용
        ADR-0091 결과 줄 "(M6-T9)" — 지금 M6-T8
규칙    ID 는 다시 쓰지 않는다. ADR 은 고치지 않으므로 등록부 2장 "옛 번호" 표가 읽는 법. 새 태스크는 등록부에 행을 먼저
자동    tools/check_docs.py — L · A · T · V · S 검사 (roadmap README 5장), CTest docs_consistency (arch 라벨)
```

## 5. 추가 공백 (Phase 3 — 1차 감사에 없던 것)

| ID | 영역 | 공백 | 처리 |
|----|------|------|------|
| SG-01 | 로드맵 | v0.5 한 버전에 M3 ~ M7 + Studio 감사 몫 — 끝낼 수 없는 크기, "Creator Alpha" 이름이 실제 범위와 다름 | ADR-0096 (v0.5 = Technical Alpha) |
| SG-02 | 문서 | 태스크 ID 재사용 (M6-T7 · M6-T9) | 등록부 2장 · NG-11 |
| SG-03 | 버전 | 1.0 으로 가면 모든 0.x 프로젝트의 engine 범위 (<1.0.0) 가 거절 | NG-16 (v0.20) |
| SG-04 | 버전 | Studio Document (레이아웃 · 설정 · 복구본) 에 버전 번호가 없다 | versioning 5장 — M6-T6 · T7 이 1 부터 |
| SG-05 | 네트워크 | 표시 digest 가 핸드셰이크에 없다 — 다른 PNG 를 가진 클라이언트가 조용히 다른 화면 | NG-14 |
| SG-06 | 결정론 | 툴체인 간 리플레이 (Windows 기록 → Linux 재생) 를 시험하지 않는다 — 골든은 툴체인마다 따로 | [계획] v0.11 전 — 리플레이 하나를 세 CI 잡에서 재생하는 CTest (부동소수 결정론 04) — M7-T3 완료 기준에 더함 |
| SG-07 | 도구 | 도구 (Studio · sbx) 문자열이 한국어 · 영어 섞여 하드코딩 | PS-19-2 (v0.19), 그 전에는 새 UI 문자열을 표 하나에 (M4-T1 틀) |
| SG-08 | 접근성 | Studio 자체의 접근성 (키보드만 · 고대비 · 글자 크기) 기준 없음 | 28 에 G10 [계획] — v0.11 진입 전 키보드만으로 G1 ~ G3 |
| SG-09 | 데이터 | SaveGame 게임 버전 마이그레이션 없음 | NG-04 |
| SG-10 | 배포 | 받는 쪽 실행 경로 (Player) 가 sbx · 명령줄뿐 | NG-03 |
| SG-11 | 패키지 | 빌드 중 파일 변경 · 동적 참조 · 플랫폼 프로필 | NG-01 · NG-02 · NG-08 |
| SG-12 | 오디오 | 실제 출력 백엔드 태스크가 없었다 (ADR-0095 는 "v0.6" 이라고만) | NG-17 (v0.10, RT-06-15 의 선행) |
| SG-13 | 2D | 타일맵 태스크가 없었다 (GM-39) | NG-15 (v0.10) |
| SG-14 | 시험 | Studio 의 자동 시험 층 정의 없음 — G1 ~ G9 증거가 수동 QA 에 몰림 | NG-07 |
| SG-15 | 보안 | 계정 · 인증 위협 행이 24 에 없다 (P3 · N2 뿐) | PS-12-2 완료 기준 — 24 에 행 추가 (release-gates v0.12) |

## 6. 태스크 나누기 (Phase 4)

```text
옛 v0.5 M6 (Studio 9 태스크)  → v0.6 (M6-T9) · v0.8 (T1 · T3 · T6 · T7) · v0.9 (T4 · T8) · v0.10 (T2) · v0.11 (T5)
옛 v0.5 M7 (데모 · E2E · 채널) → M7-T6 (새 — Technical Alpha E2E, CLI 만, v0.5) + M7-T1 ~ T4 (v0.11)
옛 v0.6 RT-06-* 15 개         → v0.6 (RT-06-12) · v0.10 (나머지 — 늦으면 RT-06-2 ~ 15 를 v0.11 로, RT-06-1 은 남김)
옛 v0.6 Hub                   → v0.12 (계정 · 레지스트리) + v0.13 (게시 · 설치 · 채널 — PS-13-1 · 13-2 · NG-09 새로)
옛 v0.7                       → v0.14 (방) + v0.15 (예측)
옛 v0.8                       → v0.16 (호스팅) · v0.17 (결제 · 마켓) · v0.18 (정산 · 분석 · 등급전)
옛 v0.9                       → v0.19 (팀 · 협업 · 플러그인) · v0.20 (안정화 · 성능 · 감사 준비)
기준                          한 MINOR 에 사용자 목표 하나 · P0 태스크 대략 10 개 이하 (추정 — 지금까지 M 하나 ≈ 4 ~ 9 태스크)
```

## 7. 이 감사의 검증

```text
실행    python -X utf8 tools/check_docs.py --root .   → 결과는 ROADMAP-CONSISTENCY 2장
        빌드 · 전체 ctest (docs_consistency 포함)       → DEVELOPMENT_LOG 의 이 항목
안 함   NG 설계의 구현 · 시험 (모두 [계획]) · GUI · 실기 QA · 벤치 (코드 경로를 바꾸지 않았다)
```
