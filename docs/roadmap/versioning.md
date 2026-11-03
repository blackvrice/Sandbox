# 버전 체계 — 제품 버전 · 성숙 단계 · 호환 번호 (2026-11-03, ADR-0096 · ADR-0099)

> **규범 문서.** 제품 버전 (v0.5 · v0.10 · v0.21 · v1.0) 을 어떻게 늘리고 비교하는지, 그것과 따로인 성숙 단계 (Technical Alpha ~ GA) 와
> 호환 번호 (프로토콜 · 바이트코드 · 세이브 …) 를 어떻게 다루는지. 버전마다의 목표 · 태스크는 [README](README.md) · [releases](releases.md) ·
> [task-registry](task-registry.md), 출시 기준은 [release-gates](release-gates.md), 결정은 [ADR-0096](../adr/0096-roadmap-versioning-small-milestones.md) ·
> [ADR-0099](../adr/0099-roadmap-v010-split-stabilization-versions.md) (v0.10 나눔 · v0.22 ~ v0.24 안정화 · RC).

---

## 1. 제품 버전

```text
형식      SemVer MAJOR.MINOR.PATCH (foundation/version/SemVer — 자리마다 정수). v0.10.0 > v0.9.x, v0.20.0 > v0.19.x, v1.0.0 > v0.99.x
          (시험 test_semver "0.x 의 자리는 정수", 릴리스 피드 test_user_settings). 문자열 사전순으로 비교하지 않는다
MINOR     하나의 개발 마일스톤 (2 · 3장). 목표가 크면 다음 MINOR 로 나눈다 — 번호를 아끼지 않는다
PATCH     그 마일스톤 출시 뒤의 고침 (기능 추가 없음)
v1.0.0    "다 쓰고 난 다음 번호" 가 아니라 GA 기준 (release-gates 6장) 을 모두 통과했을 때 바꾼다. 통과 못 하면 v0.25 · v0.26 … 로 계속
프리릴리스 -alpha.N · -beta.N · -rc.N (같은 MINOR 의 정식보다 앞선다)
```

## 2. 성숙 단계 (버전 번호와 따로)

| 단계 | 뜻 | 들어가는 조건 (release-gates 3장) | 지금 계획의 첫 버전 (바뀔 수 있다) |
|------|----|-----------------------------------|----------------------------------|
| Technical Alpha | 엔진 핵심 · 내부 개발자 · CLI 허용 | 결정론 · 기본 패키징 · 충돌 보고 | v0.5 |
| Closed Creator Alpha | 초대한 외부 제작자 · Studio 로 만들기 · 기본 작업에 CLI 없음 | 28 G1 ~ G10 · 저장 · 복구 · 사용성 시험 | v0.13 |
| Public Creator Alpha | 누구나 제작 · 공유 | 계정 · 인증 · 배포 서비스의 보안 기준 (24) | v0.15 |
| Closed / Public Beta | 멀티플레이 · 호스팅 · 마켓 · 운영 | 보안 전송 · 운영 · 복구 · 신고 · 실사용 성능 | v0.17 ~ v0.21 |
| Release Candidate | 기능 동결 · 전체 게이트 재검증 | release-gates 5장 (안정화 v0.22 · v0.23 통과) | v0.24 (못 하면 v0.25 …) |
| General Availability | 장기 호환 · 운영 · 보안 감사 · 지원 OS 실기 | GA 기준 전부 (release-gates 6장) | 미정 (기준을 통과한 RC 빌드가 v1.0.0) |

## 3. 버전 목록

로드맵이 쓰는 버전 — 새 버전은 이 목록 끝에 더한다 (tools/check_docs.py 가 태스크의 목표 버전을 이 목록과 맞춘다).
`v0.5` `v0.6` `v0.7` `v0.8` `v0.9` `v0.10` `v0.11` `v0.12` `v0.13` `v0.14` `v0.15` `v0.16` `v0.17` `v0.18` `v0.19` `v0.20` `v0.21` `v0.22`
`v0.23` `v0.24` `v0.25` `v1.0` `v1.x`

## 4. 옛 버전 줄 → 새 버전 (2026-11-03 이전 문서의 "[계획 vX]" 읽는 법)

2026-11-03 이전 문서 (22 · 24 · 25 ~ 28 · audit · ADR-0078 ~ 0095) 의 버전 표기는 옛 줄 (v0.6 Hub · v0.7 Multiplayer · v0.8 Marketplace ·
v0.9 Business · v1.0 GA) 기준이다. 문서마다 고쳐 쓰지 않고 이 표와 태스크 등록부 (목표 버전의 정본) 로 읽는다.

| 옛 표기 | 무엇 | 지금 목표 (ADR-0099 뒤) |
|---------|------|------------------------|
| v0.5 (Creator Alpha 전체) | M1 · M2 · M8 | v0.5 (끝남) |
| v0.5 M3 · M4 | 게임 UI 런타임 · 다국어 기반 | v0.7 |
| v0.5 M5 | 리플레이 뷰어 | v0.8 |
| v0.5 M6 (T1 · T3 · T6 · T7) | Studio 홈 · 빌드 화면 · 레이아웃 · 자동 저장 | v0.8 |
| v0.5 M6-T9 | 프로젝트 에셋 · .sbpkg 3 | v0.6 |
| v0.5 M6-T4 · T8 | 줄 표 · 스크립트 편집기 | v0.9 |
| v0.5 M6-T2 · T5 · M7 | 템플릿 · 튜토리얼 · 데모 · A → B · 공개 Alpha 채널 | v0.10 (M6-T2) · v0.13 (M6-T5 · M7-T1 ~ T4) |
| v0.6 Runtime v2 (RT-06-*) | 물리 단계 · 콜백 · 타이머 | v0.10 · v0.11 · v0.12 (태스크마다 — 등록부), RT-06-12 는 v0.6 |
| v0.6 트랙 B (ST-06-*) | Studio 서비스 · 브라우저 · UI 디자이너 · 디버거 | v0.8 ~ v0.13 |
| v0.6 Hub | 계정 · 레지스트리 · 서명 | v0.14 |
| v0.7 Multiplayer · 예측 (RT-07-*) | 보안 전송 · 방 · 예측 | v0.16 · v0.17 |
| v0.8 Marketplace & Hosting (RT-08-*) | 호스팅 · NetworkModule · 마켓 | v0.18 · v0.19 |
| v0.8 등급전 · 순위표 · 업적 (ADR-0088) | | v0.20 |
| v0.9 Business & Stability (RT-09-*) | 팀 · CI/CD · 성능 회귀 · 권장 사양 · 서명 업데이트 | v0.21 · v0.22 · v0.23 |
| v1.0 GA | 동결 · 감사 · 실기 | v0.24 RC → GA 기준 (release-gates 6장) |

### 4.1 1차 배치 (ADR-0096, 2026-11-03 오전) → 2차 배치 (ADR-0099)

1차 배치로 쓴 문서 (ADR-0097 · 0098, audit/SECOND-PASS-GAP-ANALYSIS · ROADMAP-CONSISTENCY · VERSION-MIGRATION-MATRIX 의 일부 줄) 의 v0.10 이후 표기:

| 1차 | 2차 | 무엇 |
|-----|-----|------|
| v0.5 ~ v0.9 | 그대로 | — |
| v0.10 | v0.10 · v0.11 · v0.12 | 22 태스크를 Creator Foundation 7 · Gameplay Runtime 6 · Advanced Creator Tools 9 로 |
| v0.11 Closed Creator Alpha | v0.13 | |
| v0.12 Hub | v0.14 | |
| v0.13 Public Creator Alpha | v0.15 | |
| v0.14 방 | v0.16 | |
| v0.15 예측 | v0.17 | |
| v0.16 호스팅 | v0.18 | |
| v0.17 결제 · 마켓 | v0.19 | |
| v0.18 정산 · 분석 · 등급전 | v0.20 | |
| v0.19 팀 · 협업 · 플러그인 | v0.21 | |
| v0.20 안정화 | v0.22 (신뢰성 · 성능) · v0.23 (보안 · 출시 강화) | 태스크 성격대로 나눔 — 일괄 +2 가 아니다 |
| v0.21 이후 GA 후보 | v0.24 RC · v0.25+ | |

## 5. 호환 번호 (제품 버전과 따로 — 제품 버전이 올라도 저절로 올리지 않는다)

| 번호 | 정의 자리 | 지금 (2026-11-03) | 올리는 때 | 옛 것 읽기 |
|------|-----------|-------------------|-----------|-----------|
| Product Version | CMake project VERSION · sbx --version | 0.4.0 (v0.5.0 은 M7-T5 에서) | 마일스톤 출시 | — |
| Engine API (프로젝트 engine 범위) | sandbox.project.json "engine" · GameProject P3 | 새 프로젝트 ">=x.y.z <1.0.0" | — (범위는 제작자 것) | 0.x 는 모두 열림, 1.0 전환은 NG-16 |
| Host API Version · 지문 | script/api/HostApi.hpp kHostApiVersion · hostApiFingerprint | 1 · 0x1dfa888c39154dde | 빼기 · 바꾸기 = 버전++, 더하기 = 지문만 | 지문이 다르면 다시 컴파일 (소스는 패키지에 있다) |
| Bytecode (.sbc) | script/vm/Bytecode.hpp kFormatVersion | 9 | 형식 바꿈 (M6-T4 = 10) | 다시 컴파일 |
| Protocol | network/protocol/Messages.hpp kProtocolVersion | 15 | 메시지 · 명령 와이어 바꿈 | 핸드셰이크에서 거절 (08) |
| Simulation | core/simulation/SimVersion.hpp kSimVersion | 4 | 규칙 바꿈 (04 6장) — 골든 갱신 | 리플레이 "규칙 차이" |
| World/Save | core/persist kWorldVersion · kSchemaVersion | 1 · 1 | 세이브 모양 바꿈 | 마이그레이션 (09, ADR-0013 · 0061) |
| Package (.sbpkg) | core/package kFormatVersion | 2 (M6-T9 = 3) | 패키지 모양 바꿈 | 1 · 2 · (3) 읽기 |
| Project Manifest schemaVersion | core/project kManifestSchemaVersion | 3 | 키 더함 · 바꿈 | 1 ~ 3 읽기 |
| Replay | core/replay kReplayVersion | 1 | 리플레이 파일 모양 바꿈 | — |
| Studio Document | (studio.json · 레이아웃 · editor.json) | 없음 — [계획 v0.8] 1 부터 | 모양 바꿈 | 깨지면 옆으로 옮기고 기본 (M1-T3 규칙) |
| Editor Plugin API | (없음) | [계획 v0.21] 1 부터 (NG-10) | 확장 지점 바꿈 | 매니페스트의 범위로 거절 · 경고 |
| Game (제작자 게임 버전) | sandbox.project.json "version" | 제작자 것 | 제작자 | SaveGame 마이그레이션 (NG-04) |

자세한 호환 범위 · 시험은 [audit/VERSION-MIGRATION-MATRIX](../audit/VERSION-MIGRATION-MATRIX.md). 번호를 바꾸는 변경은 Breaking 여부 ·
마이그레이션 · 지원 범위 · 메시지 · 자동 시험 · ADR 을 같은 커밋에 (AGENTS 5 · 6장).
