# 버전 · 마이그레이션 매트릭스 (2026-11-03, master 4987662)

> **감사 문서.** 호환 번호마다 지금 값 · 읽는 범위 · 다를 때의 동작 · 그것을 지키는 시험. 번호의 정의는 [roadmap/versioning](../roadmap/versioning.md) 5장.
> "시험" 칸의 이름은 이 커밋에 있는 것만 — 없으면 [계획]. 값은 소스 상수를 직접 읽음 (SECOND-PASS 1장).

---

## 1. 매트릭스

| 번호 | 지금 | 읽는 범위 | 다를 때 | 지키는 시험 | 빈 곳 |
|------|------|-----------|---------|-------------|-------|
| Product | 0.6.0 (2026-11-04) | — | 릴리스 피드가 더 새 것을 알림 (바꾸지 않음) | test_user_settings (정수 비교 — v0.10 > v0.9.3) | — |
| Engine API 범위 | 새 프로젝트 ">=x.y.z <1.0.0" | 프로젝트가 정함 | 범위 밖 엔진 = 열기 거절 (문장) | test_game_project · test_semver | 1.0 전환 (NG-16) |
| Host API | 1 · 0xd8e74d9eb927e9ae (v0.7 M4-T1) | 같은 버전 + 같은 지문 | 지문 다름 = 패키지의 소스로 다시 컴파일 · 버전 다름 = 거절 | test_bytecode (지문) · test_runtime_contract | — |
| Bytecode .sbc | 10 (v0.6 NG-04 — onMigrate) | 10 | 다시 컴파일 (소스는 패키지에) | test_bytecode · test_script_callbacks (onMigrate 왕복) | 11 (M6-T4 — 줄 표) |
| Protocol | 16 (v0.6 NG-14 — 표시 digest) | 같은 값만 | 핸드셰이크 거절 (문장) — 0.5 (15) 와 접속하지 않는다 | unit_network (핸드셰이크 · Auth · Welcome 왕복) | 표시 digest 만 다르면 접속 + 경고 (test_network_session) |
| Simulation | 4 | 어느 값이든 읽음 | 리플레이 · 세이브 모두 진행하되 해시 비교를 건너뛰고 "규칙 차이" 로 알림 (core/replay/Replay.cpp simVersionDiffers · WorldSave.cpp hashSkippedReason) | det_* · test_replay | 툴체인 간 재생 (SG-06) |
| World/Save | 1 · 1 | ≤ 지금 | 더 새 것 거절 · 스크립트 컴포넌트 자동 마이그레이션 (ADR-0061) | test_world_save (스크립트 마이그레이션) · test_sample_saves | 게임 버전 마이그레이션 ✅ NG-04 (매니페스트 migrations · onMigrate — test_save_migration) |
| Game 결속 | world.json "game" | 같은 gameId | 다른 게임 · 더 새 version 거절, 옛 version · 다른 패키지는 알림 (migrations 가 있으면 체인 적용 — NG-04) | test_world_ops ("세이브는 게임에 묶인다") | 더 새 version 거절 · 옛 version 은 migrations 체인 ✅ (NG-04 — test_world_ops) |
| Package .sbpkg | 3 (에셋이 있을 때 — 없으면 2, v0.6 M6-T9) · 최소 1 | 1 · 2 · 3 | 범위 밖 거절 · digest 불일치 거절 | test_game_package · test_package_rejects | 0.5 엔진은 형식 3 (에셋 있는 게임) 을 열지 않는다 (VersionMismatch) — NG-01 스냅숏 해시. index profile (NG-08) 은 형식 무관 선택 키 — 옛 엔진은 지나친다 |
| Manifest schemaVersion | 3 | 1 ~ 3 | 더 새 것 거절 | test_game_project | — |
| Replay | 1 | 1 | 거절 | test_replay | — |
| Studio Document | 없음 | — | 깨지면 옆으로 옮기고 기본 (M1-T3 규칙) | test_user_settings (사용자 설정만) | 레이아웃 · 복구본 (M6-T6 · T7 — 1 부터) |
| Editor Plugin API | 없음 | — | 플러그인을 불러오지 않는다 | — | NG-10 (v0.21 — ADR-0099) |
| 제작자 게임 version | 제작자 것 | 세이브 ≤ 게임 | 더 새 세이브 거절 · 옛 세이브 = migrations 체인 · 백업 (NG-04) | test_save_migration · test_world_ops | NG-09 (채널 롤백) |

## 2. 번호를 올릴 때 (체크리스트 — AGENTS 5 · 6장)

```text
1  Breaking 인가 (옛 것을 못 읽나) — 못 읽으면 읽기 범위 · 거절 문장 · 마이그레이션 중 하나를 같은 커밋에
2  이 표의 행 · versioning 5장 · 해당 규범 문서 (08 · 09 · 19) 갱신
3  옛 것 읽기 시험 (고정 픽스처 — 바이트를 저장소에)
4  결정론에 닿으면 kSimVersion · 골든 (13)
5  ADR (형식 · 프로토콜 · 세이브 · 패키지)
```

## 3. 시험 이름 확인

이 표의 시험 이름은 tests/ 의 TEST_CASE · CTest 이름을 이 커밋에서 grep 으로 확인했다 (ROADMAP-CONSISTENCY 2장). 실행 결과는 DEVELOPMENT_LOG.
