# ADR-0192. 나머지 대량 목록 — 콘텐츠 줄은 보기를 빌리고, 차례 키는 줄 수가 바뀔 때만 (UI-R9-03)

- 상태: **Accepted** · 날짜: 2026-11-28
- 관련: [ADR-0188](0188-hierarchy-large-list-lazy-text-index-lookup.md) (목록 패널 — 글은 보이는 줄만), [ADR-0191](0191-workspace-shown-panes-only-models.md)
  (보이는 패널만 모델)

## 맥락

UI-R9-03 은 목록 패널만 쟀다 (ADR-0188). 남은 것은 다른 목록 (콘텐츠 · 월드 · 스크립트 프로파일) 과 검색어가 있는 목록이다. 벤치를 늘려
재 보니 (RelWithDebInfo, 창 · GPU 없이, 같은 날 3 회의 중앙값) 패널 쪽 비용 (frame − 모델 만들기) 은:

- **콘텐츠** 항목 10 만 — 약 18.7 ms. `rebuildItems` 가 배치 (layout) 마다 모든 항목의 id 를 `Item::text` (std::string) 로 복사했다.
- **차례 키** — 콘텐츠 · 월드 · 프로파일 · 게임 · 인스펙터 · 툴 패널이 모델을 받을 때마다 줄 수만큼 키 (0 … n-1) 벡터를 새로 만들어 옛
  것과 비교했다. 키가 차례 번호면 줄 수가 같을 때 언제나 같다.
- **월드 · 프로파일** — 비용의 대부분이 모델 만들기 (앱 쪽 — 값 복사) 다. 프로토콜이 상한을 둔다: 세이브 목록 ≤ 256 (`kMaxSavedWorlds`),
  스크립트 시스템 표 ≤ 64 줄 (`kMaxScriptStatRows`). 10 만은 스트레스 값일 뿐이다.
- **목록 검색어** (`rabbit` — 10 만 중 1/4 이 맞음) — 맞는 줄만 남아 검색어 없을 때보다 싸거나 같다 (`buildHierarchy` 가 비용의 대부분).

## 결정

1. **콘텐츠 줄은 보기를 빌린다.** `ContentPanel::Item::text` = `std::string_view` (보기의 `ContentView::groups` 의 팩 이름 · id).
   `setView` 가 새 보기로 곧바로 줄을 다시 만든다 — 옛 보기를 가리키는 줄이 한 순간도 남지 않는다. 배치는 거르기 · 종류가 바뀌었을
   때만 다시 만든다 (`m_itemsFilter` · `m_itemsKind`). 고른 id 는 복사해 둔다 (`m_selected`).
2. **차례 키는 줄 수가 바뀔 때만.** `kit::setIndexKeys(list, keys, count)` — 수가 같으면 아무것도 하지 않고, 다르면 0 … n-1 로 다시
   만들어 `setItems`. 차례 키를 쓰는 패널 여섯 곳이 쓴다. 키가 접속 번호인 접속자 패널은 그대로 (비교가 뜻이 있다).
3. **월드 · 프로파일 패널은 그대로.** 프로토콜 상한 안에서는 0.1 ms 대다. 남은 비용은 값 모델 (앱이 frame 마다 만든다 — 보일 때만,
   ADR-0191).

## 결과

- 벤치 `gui.workspace_list` (새 — 콘텐츠 · 월드 · 프로파일 × 1k · 10k · 100k, `model_ms` = 그중 모델 만들기) + `gui.workspace_hierarchy`
  의 `query` (가장 큰 N — 검색 칸에 글자 입력으로).
- 콘텐츠 패널 쪽 (frame − 모델, 3 회 중앙값): 10 만 약 18.7 → 3.6 ms, 1 만 0.33 → 0.27 ms (PERFORMANCE_BASELINE 3.3 — 같은 기계의 A/B).
- 시험: `unit_client` 콘텐츠 패널 · 앱 시험 (팩 묶음 · 거르기 · 고르기 · 적용) 그대로 통과.
- 남은 것: 앱의 콘텐츠 보기 만들기 (`makeGuiContentView` — frame 마다 모든 id · Prefab 이름. 콘텐츠 데이터베이스에 판 번호가 없어 바뀐
  때만 만들 수 없다) · GXML 바인딩 비용 (UI-R9-03 남음).
