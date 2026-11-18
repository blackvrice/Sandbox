# ADR-0165. v0.12 통합 시험 — 보석 동굴 (samples/dungeon · 템플릿 dungeon) · 제작 흐름을 헤드리스로

- 상태: **Accepted** · 날짜: 2026-11-18
- 관련: [ADR-0154](0154-v011-integration-orbs-physics-minigame.md) (v0.11 통합 시험), [ADR-0156](0156-tilemap-terrain-materials-sprite-cells.md)
  (타일맵), [ADR-0157](0157-2d-animation-clips-state-machine.md) (애니메이션), [ADR-0158](0158-prefab-variants-overrides-propagation.md)
  (Prefab 변형), [ADR-0160](0160-gamepad-bindings-input-triggers.md) (게임패드), [13-TESTING](../13-TESTING.md), task-registry NG-20

## 맥락

NG-20 의 완료 기준은 "헤드리스 흐름 CTest · 세이브 → 로드 · 리플레이 해시", 흐름은 "가져오기 → Prefab → 맵 → 스크립트 → 애니메이션 →
빌드 → 실행 → 저장 · 로드". 창 없이 (CI · 이 PC — 화면을 가리지 않는다) 제작자의 길을 한 번에 지나야 한다.

## 결정

```text
1. 게임       samples/dungeon "보석 동굴" — 타일 벽 (지형 머티리얼 + tiles.png 의 칸) · Prefab 변형 (붉은 슬라임 inherits 슬라임) ·
              주인공 애니메이터 (idle · walk · cheer) · 키보드 + 게임패드 게임 키 · 물리 앞 스크립트 · 트리거 보석. 템플릿 id dungeon
2. 흐름 시험  unit test_v012_integration — createProject (빈 월드) → 팩 · 에셋을 두고 매니페스트 → importAssets (.meta) → 콘텐츠
              (변형이 풀렸다 · 애니메이션 원문) → 바닥 월드에 PaintTerrain (편집기 브러시) → 시작 월드 저장 → validateProject →
              buildPackage → 패키지를 열어 자동 플레이 (승리) + ReplayRecorder → 끝 월드 저장 → 불러오기 = 같은 해시 · 나란히 30 틱
              같은 해시 → 시작 세이브에서 playReplay = 불일치 없음 · 같은 끝 해시
3. 실행 시험  CTest sample_dungeon (sample_playthrough.cmake) — 헤드리스 서버 = 로컬 플레이 (클라이언트 추출이 타일 스프라이트 ·
              애니메이션을 돈다) = 패키지 (서버 · 로컬), 서버 · client 스크립트 트랩 0. project_templates 가 템플릿 dungeon 도 만든다
4. 샘플 월드  커밋한 worlds/main 은 흐름 시험이 만든 것 (SBX_NG20_WRITE_SAMPLE) — 손으로 만든 이진 청크가 없다
```

## 결과

- v0.12 의 새 기능 (타일맵 · 변형 · 애니메이션 · 패드 · playAnimation) 이 한 게임에서 같이 돈다 — 헤드리스로 매 커밋.
- 창에서 보이는 그림 (타일 · 애니메이션 프레임) · 실제 패드는 MANUAL-QA (v0.12 줄).
- 찾은 것: client 스크립트에서 `e.entity !== null` 은 필드라 좁혀지지 않는다 — 지역 변수에 담아 좁힌다 (샘플 · 주석). 검사기의 필드
  좁히기는 [계획].
