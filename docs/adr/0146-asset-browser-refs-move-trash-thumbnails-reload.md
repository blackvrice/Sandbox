# ADR-0146. Asset Browser — 참조 = 팩 materials.json, 옮기면 경로 참조를 고쳐 쓰고, 지우기는 휴지통, 썸네일은 ImGui 사용자 텍스처, 다시 읽기는 같은 스프라이트 id

- 상태: **Accepted** · 날짜: 2026-11-17
- 관련: [ADR-0090](0090-asset-database-stable-id-sidecar-meta.md) (에셋 파이프라인 · .meta id), [ADR-0097](0097-package-data-integrity-snapshot-ids-paths-digests.md),
  [ADR-0145](0145-ui-designer-model-editor-services-uses-ui.md) (UI 디자이너), [26-ASSET-PIPELINE](../26-ASSET-PIPELINE.md) 5 · 7장,
  [25-STUDIO-ARCHITECTURE](../25-STUDIO-ARCHITECTURE.md) 5장, task-registry ST-06-3

## 맥락

에셋 DB (v0.6 M6-T9) 는 assets/ 의 원본에 .meta id 를 붙이고 빌드가 빠진 에셋 (A6) 을 막는다. Studio 에서 에셋을 보고 · 옮기고 · 지우고 ·
다시 읽는 창이 없다. 옮기거나 지우면 무엇이 깨지는가 (참조), 지운 것을 되살릴 수 있는가, 썸네일을 무엇으로 그리는가, 원본을 고치면 화면이
언제 바뀌는가를 정한다.

## 결정

```text
1. 참조      프로젝트 팩 (content/*) 의 presentation/materials.json 의 sprite — "asset:<id>" (id) · "assets/<경로>" (경로). 지금 에셋을
             부르는 유일한 곳이다 (UI 문서 image · Prefab render.sprite 는 머티리얼 이름을 부른다). 엔진 에셋 ("sprites/…") 은 아니다.
             core/asset/AssetRefs: findAssetReferences · referencesTo
2. 옮기기    moveAsset — 원본과 .meta 를 같이 (이름 바꾸기 — .meta 를 못 옮기면 원본도 되돌린다), 경로 참조를 새 경로로 고쳐 쓴다 (키 차례
             그대로). id 참조는 그대로 (.meta 가 따라간다). 있는 자리 · assets 밖 · .meta · 경로 규칙 (P4) 위반은 거절
3. 지우기    deleteAsset — 참조가 있으면 거절 (어디서 부르는지 셋까지). force 면 지운다 (참조는 A6 오류가 된다). 원본 · .meta 를
             <프로젝트>/.sandbox/trash/<n>/<원래 경로> 로 옮긴다 (되살릴 수 있게 — .sandbox 는 빌드 · 패키지 밖)
4. 창        editor/ui/AssetBrowserPanel ("에셋" — 보기 메뉴): 목록 (거르기 · 크기 · 참조 수), 썸네일 (PNG 를 48 픽셀로 줄여 ImTextureData
             — ImGui 1.92 의 RegisterUserTexture, 렌더러가 다른 ImGui 텍스처처럼 올린다. 내용 해시가 바뀌면 새로, 옛 것은 지우기 요청 뒤
             등록 해제), id 없는 원본 가져오기 (importAssets), 고른 에셋의 참조 · 옮기기 · 다시 읽기 · 지우기 (참조가 있으면 확인)
5. 끌어다 놓기 끌기 원천 payload "SBX_ASSET" = 에셋 id. UI 디자이너의 image 노드가 받는다: 문서의 팩 materials.json 에 "<팩 폴더>/<파일
             이름>" = {"sprite": "asset:<id>"} (없으면 만든다 — ensureAssetMaterial, 같은 이름이 같은 그림이면 그대로 · 다른 그림이면 거절)
             를 두고 노드의 material 을 그 이름으로
6. 다시 읽기  render AssetManager::reloadSpriteFile(key, file) — requestSpriteFile 로 받은 key 의 같은 스프라이트 id 를 다시 디코드해 새
             자리에 올린다 (올리기 전까지 옛 자리). 옛 자리는 돌려받지 않는다 (아틀라스가 차면 마젠타 — 다시 열면 비워진다). 앱은 창의
             [다시 읽기] · 옮기기 · 가져오기 뒤 "asset:<id>" · 경로 · 옛 경로 키를 다시 올린다. 파일 감시 (바뀌면 저절로) 는 [계획]
```

## 결과

- 옮기기 · 이름 바꾸기 뒤에도 참조가 깨지지 않는다 (경로 참조를 고쳐 쓰고 id 참조는 그대로 — 시험이 본다).
- 지운 에셋은 휴지통에서 되살린다 (손으로). 휴지통을 비우는 명령은 [계획].
- 썸네일이 ImGui 내부 API (RegisterUserTexture — 실험) 에 기댄다. ImGui 를 올릴 때 함께 확인한다.

## 고른 것 · 버린 것

- 참조를 모든 JSON 에서 찾기 (글자 "assets/" 검색): 머티리얼 밖에는 에셋 참조가 없고, 잘못 고칠 위험이 크다 — 버림.
- 지우기 = 바로 지우기: 되돌릴 수 없다 — 휴지통.
- 썸네일을 아틀라스 (AssetManager) 에서: 아틀라스는 게임 스프라이트의 자리이고 ImGui 와 바인딩이 다르다 — ImGui 사용자 텍스처.
