# ADR-0098. 배포 수명주기 — Player 모드 (SandboxClient), SaveGame 게임 버전 마이그레이션, 출시 채널 · 단계 배포 · 롤백, Editor Plugin API 버전

- 상태: **Accepted** (설계 — 구현은 v0.6 ~ v0.19 태스크) · 날짜: 2026-11-03
- 관련: [audit/SECOND-PASS-GAP-ANALYSIS](../audit/SECOND-PASS-GAP-ANALYSIS.md) 4.3 · 4.4 · 4.9 · 4.10, [25-STUDIO-ARCHITECTURE](../25-STUDIO-ARCHITECTURE.md) 9 · 12장,
  [26-ASSET-PIPELINE](../26-ASSET-PIPELINE.md) 8장, [ADR-0054](0054-game-package-sbpkg-sha256-digest-save-binding.md) (세이브 결속), [ADR-0061](0061-script-component-save-auto-migration-drop-default-strict.md) (스크립트 컴포넌트 마이그레이션),
  [24-SECURITY-GATES](../24-SECURITY-GATES.md) P5 · O2

## 맥락

만드는 쪽 (Studio · sbx) 은 계획이 있지만 받는 쪽의 수명주기는 비어 있었다: 패키지를 받은 사람이 실행하는 길은 명령줄뿐이고, 게임이 0.1 → 0.2 로
바뀌면 세이브를 옮기는 길이 없으며 (엔진 쪽 kSchemaVersion · 스크립트 컴포넌트 마이그레이션만), 게시 뒤 문제가 생기면 되돌리는 규칙이 없고,
플러그인은 "v0.8 이후" 라는 방향만 있고 버전 계약이 없다.

## 결정

```text
1. Player 모드 (NG-03, v0.8): 새 실행 파일 대신 SandboxClient 의 모드 (--play <패키지> · 파일 연결 · 최근 게임). 검증 (digest · 형식 · engine 범위 ·
   Export Profile) → 사용자 캐시에 풀기 (digest 폴더) → 로컬 서버 + 클라이언트. 실패마다 문장 화면 + 보고 묶음. 편집 · 개발 패널 숨김.
   별도 실행 파일은 배포 크기 · 서명 요구가 생기면 (v0.13) 다시 판단
2. SaveGame 마이그레이션 (NG-04, v0.6): world.json "game".version 과 지금 게임 version 비교 → migrations/ 의 onMigrate(from, to) 를 차례 적용
   (server 도메인 · 결정론 · 연료), 빠진 단계 = 거절, 적용 전 백업 (world.bak-<from>), 실패 = 원본 그대로, 더 새 게임의 세이브 = 거절
3. 출시 채널 (NG-09, v0.13): 릴리스는 불변, 채널 (alpha · beta · stable) 은 포인터. 단계 배포 = 플레이어 id 해시로 비율 결정. 롤백 = 포인터를 되돌림 +
   새 버전 세이브는 2 의 거절 → 백업 복구 안내. 엔진 업데이트도 같은 틀 (PS-20-1)
4. Editor Plugin API (NG-10, v0.19): 정수 major + 기능 표 지문 (Host API 방식), plugin.json 의 editorApi 범위 · permissions · extensionPoints.
   범위 밖 = 불러오지 않음, 지원 중단 = 한 MINOR 경고 뒤 제거, 네이티브 플러그인 받지 않음 (24 P5). v0.19 전에는 플러그인을 불러오지 않는다
```

## 선택지

| 안 | 내용 | 판단 |
|----|------|------|
| Player A (채택) | SandboxClient 모드 | 렌더 · 입력 · 로컬 서버 경로가 이미 있다. 실행 파일이 하나 더 늘면 서명 · 배포 · CI 가 두 배 |
| Player B | 별도 SandboxPlayer 실행 파일 | 작은 배포 · 편집 코드 없음 — 서명 배포 (v0.13) 때 다시 |
| 세이브 A (채택) | 게임이 쓰는 스크립트 체인 + 백업 | 게임마다 다른 변환 · 결정론 · 같은 VM 경계 |
| 세이브 B | 엔진이 필드 이름으로 자동 변환 | 의미 변화 (단위 · 분할) 를 모른다 |
| 롤백 A (채택) | 불변 릴리스 + 포인터 | 같은 버전 번호 = 같은 바이트 (재현) |
| 롤백 B | 같은 버전 덮어쓰기 | 설치한 사람마다 다른 바이트 — 신고 · 재현 불가 |

## 결과

- 시험 계획은 SECOND-PASS 4.3 · 4.4 · 4.9 · 4.10 의 "시험" 줄 — 태스크 완료 기준 (task-registry).
- GameBinding (ADR-0054) 은 그대로 — version 비교를 2 가 더한다. 세이브 형식 번호 (kWorldVersion · kSchemaVersion) 는 바꾸지 않는다 (migrations 는 프로젝트 쪽).
- 24 에 행 추가는 각 태스크 커밋에서 (설치 무결성 · 롤백 · 플러그인).
