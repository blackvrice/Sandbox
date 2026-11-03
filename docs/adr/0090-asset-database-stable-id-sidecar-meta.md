# ADR-0090. 에셋 데이터베이스 — 사이드카 .meta 의 안정 id · 프로젝트 에셋 폴더 · 패키지에 에셋 싣기 (.sbpkg 형식 3)

- 상태: **Accepted** · 날짜: 2026-11-03
- 관련: [26-ASSET-PIPELINE](../26-ASSET-PIPELINE.md), [06-RENDERING](../06-RENDERING.md) 7.2 (경로 해시 id — 대체로 남는다),
  [09-SERIALIZATION](../09-SERIALIZATION.md) 9장 (패키지), [ADR-0054](0054-game-package-sbpkg-sha256-digest-save-binding.md) · [ADR-0077](0077-package-format-2-license-summary-sbom.md),
  [audit/ENGINE-BENCHMARK](../audit/ENGINE-BENCHMARK.md) D1 ~ D3, [audit/ARCHITECTURE-REVIEW](../audit/ARCHITECTURE-REVIEW.md) R1 · R2

## 맥락

에셋 id 는 정규화 경로의 FNV-1a 이고 (render/asset/AssetId), 에셋은 프로젝트가 아니라 전역 루트 (--assets · 저장소 assets/) 에 있으며, .sbpkg 은
팩 파일만 싣는다. 그래서 (1) 이름 · 폴더를 바꾸면 참조가 끊기고 (2) A 가 그린 그림이 B 의 PC 에 가지 않는다 — v0.5 완료 기준 C1 을 막는다.
Unity (.meta GUID) · Godot 4.4 (.uid) · Unreal (redirector) 은 모두 경로와 따로인 안정 id 를 둔다.

## 결정

```text
1. 프로젝트 에셋 폴더 assets/ (프로젝트 루트) — 팩마다가 아니라 프로젝트 하나 (팩 사이 공유). 엔진 내장 에셋은 지금의 전역 루트 그대로
2. 가져올 때 원본 옆에 <파일>.meta { "id": 32 hex (128 비트 무작위), "importer": "image@1", "settings": {} } — 버전 관리에 함께. .meta 가 없는
   파일은 경로 해시 id (현행) 로 돈다 (옛 프로젝트 · 내장 에셋)
3. 에셋 DB = core/asset (UI · 렌더 모름, 시뮬레이션 밖) — 훑어서 { id → 경로 · 종류 · 내용 해시 · 의존 } 색인, 캐시는 .sandbox/cache (버전 관리 밖)
4. 참조는 "asset:<id>" 또는 경로 (경로면 색인이 id 로) — render 는 지금처럼 경로를 받는다 (클라이언트가 id → 경로를 푼다)
5. .sbpkg 형식 3 — "assets" 항목 (참조 닫힘의 원본 + .meta, 파일마다 SHA-256 · digest — 형식 2 의 규칙 그대로). 형식 1 · 2 도 읽는다.
   클라이언트의 에셋 루트 차례 = 패키지 → 프로젝트 → 엔진 전역
6. 에셋은 표시 전용 — WorldHash · contentHash · 리플레이에 닿지 않는다 (render.sprite 가 contentHash 밖인 지금과 같다)
7. 안전: 원본 · 디코드 64 MiB, 이미지 4096², 경로는 프로젝트 안만, 가져오기 · 디코드는 Worker (24 P7)
```

## 선택지

| 안 | 내용 | 판단 |
|----|------|------|
| A (채택) | 사이드카 .meta (Unity · Godot 방식) | 경로와 따로 · 텍스트라 버전 관리 · 병합이 쉽다. 밖에서 옮기면 .meta 도 같이 (안내 + 다음 훑기가 id 로 복구) |
| B | 중앙 색인 파일 하나 (id ↔ 경로) | 병합 충돌이 한 파일에 몰린다, 파일을 밖에서 옮기면 색인이 낡는다 |
| C | 내용 해시 id | 내용을 고치면 id 가 바뀐다 (참조 끊김) — 중복 제거용 보조로만 |
| D | 경로 유지 + redirector (Unreal) | 옛 경로 파일이 쌓이고 정리 단계가 필요, 우리 규모에는 과함 |

## 결과

- v0.5: 프로젝트 assets/ · .meta · 가져오기 (PNG) · 형식 3 · 루트 차례 · 빌드 때 누락 검사 (23 M6-T10 · M6-T7). v0.6: 브라우저 · 역참조 ·
  옮기기 · 지우기 안전 · 캐시 · Hot Reload.
- 호환: 패키지 형식 번호 2 → 3 (09 · 18 4장 갱신), 옛 패키지 · 프로젝트 그대로 열린다.
