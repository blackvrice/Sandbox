# 26. 에셋 파이프라인 · 제작 데이터 수명주기

> **설계 문서** (2026-11-03, master a1307fc). 결정은 [ADR-0090](adr/0090-asset-database-stable-id-sidecar-meta.md). 근거:
> [audit/ENGINE-BENCHMARK](audit/ENGINE-BENCHMARK.md) D1 ~ D3 (Unity .meta GUID · Godot .uid · Unreal redirector). 런타임 아틀라스 ·
> 업로드는 [06-RENDERING](06-RENDERING.md) 7장 그대로. 상태: **현행** · **[계획 vX]** · **[제안]**.

---

## 0. 지금 (2026-11-03 소스)

```text
런타임   render/asset/AssetManager — 경로 → SpriteId, Worker 디코드 (PNG · stb) → 아틀라스 (Texture2DArray 1024² × 4) → 프레임 예산
         업로드 (8 MB). 실패 = 마젠타 + 경고 한 번. 해제 · 다시 불러오기 없음 ("[계획] refcount · 파일 감시")
id      render/asset/AssetId — id = FNV-1a64(정규화 경로) ("[계획] .meta GUID"). 이름 · 폴더를 바꾸면 id 가 바뀐다 → 참조가 끊긴다
에셋     assets/<팩>/*.png + materials.json (저장소 assets/ — 실행 파일 옆 dataDir, M1-T4). 팩 (content/) 과 폴더가 따로
참조     Prefab 의 render.sprite (경로 문자열 — 클라이언트 전용 Opaque, contentHash 밖), 지형 머티리얼 → materials.json
패키지   .sbpkg 형식 2 (core/package — 파일마다 SHA-256 · digest · 라이선스 요약) — 팩 파일만 싣는다. **에셋은 싣지 않는다** (확인:
         core/package/GamePackage.cpp · core/project/GameProject.cpp 에 assets 경로가 없다). 클라이언트는 에셋 루트 하나 (--assets,
         기본 저장소 assets/ — apps/client/main.cpp:189) 를 쓴다 → A 가 그린 PNG 는 B 의 PC 에 가지 않는다 (마젠타). v0.5 완료 기준
         C1 (A 제작 → B 실행) 의 차단 위험 — ARCHITECTURE-REVIEW R1, 23 M6-T9
검사     콘텐츠 V1 ~ V13 은 JSON 정의만 — 에셋 파일이 있는지는 로드 · 렌더 때 (마젠타) 에야 안다
```

## 1. 결정 요지 (ADR-0090)

```text
A1 안정 에셋 id = 128 비트 무작위 (가져올 때 한 번) — 소스 파일 옆 사이드카 <파일>.meta (JSON { "id": "a1b2…", "importer": "image", "settings": {…} })
   를 버전 관리에 함께 (Godot .uid · Unity .meta 와 같은 해법). 경로 해시 id (현행) 는 .meta 가 없을 때의 대체 — 옛 프로젝트 그대로 돈다
A2 Asset Database = core/asset (UI 모름 · 시뮬레이션 밖) — 프로젝트를 훑어 { id → 경로 · 종류 · 해시 · 의존 } 색인. Studio · 패키지 빌드 ·
   서버 콘텐츠 검사가 같은 색인. 색인 자체는 캐시 (.sandbox/cache/assetdb.json — 버전 관리 밖, 지우면 다시 만든다)
A3 참조는 id 로 (render.sprite = "asset:<id>" 또는 지금처럼 경로 — 경로면 색인이 id 로 푼다). 이름 바꾸기 · 옮기기는 Studio 에서
   (.meta 를 같이 옮김) — 밖에서 옮기면 다음 훑기가 같은 id 의 .meta 를 찾아 경로를 고친다 (id 는 .meta 에 있으므로)
A4 원본 · 가져오기 결과 분리 — 원본 (PNG · WAV · …) 은 프로젝트, 결과 (디코드 · 압축 · 플랫폼 형식) 는 .sandbox/cache/imported/<id>-<설정 해시>
A5 런타임 형식 · 결정론 — 에셋은 표시 (클라이언트) 만. 시뮬레이션 · WorldHash · contentHash 에 닿지 않는다 (지금 render.sprite 가 contentHash
   밖인 것과 같다). 시뮬레이션이 읽는 데이터 (콘텐츠 JSON) 는 에셋이 아니라 콘텐츠다 (11)
```

## 2. 데이터 종류 · 수명주기

| 종류 | 자리 | 만드는 것 · 고치는 것 | 저장 · 로드 | 패키징 | 복구 |
|------|------|------------------------|-------------|--------|------|
| Editor Source Data (팩 JSON · 스크립트 · UI 문서) | 프로젝트 content/<팩>/ | Studio DocumentService (원자적 저장) · 텍스트 편집기 | 콘텐츠 로더 (V1 ~ V13 · S 진단) | 그대로 (.sbpkg 파일 목록 · digest) | 버전 관리 · 자동 저장의 저장 안 한 문서 (25 7장) |
| 에셋 원본 + .meta | 프로젝트 assets/ [제안 — 지금 저장소 assets/] | 가져오기 (복사 + .meta) | 에셋 DB 훑기 | 참조된 원본 (또는 결과 — 4장) + .meta | 버전 관리 |
| Imported Asset Cache | .sandbox/cache/imported/ | 가져오기 Job | 런타임 로더 | 싣지 않는다 (받는 쪽이 원본에서 다시 — 또는 v0.8 플랫폼 형식 미리 만들기) | 지우면 다시 만든다 |
| Runtime Data | 메모리 (아틀라스 · 레지스트리) | 로더 · 시뮬레이션 | — | — | — |
| Script Overlay · World Overlay | 월드 세이브 overlay/ (09 3장) | EditContent (테스트 · 실행 중 편집) | 세이브 로드 | 월드와 같이 | 세이브 |
| World Save | worlds/<w>/ (09) | Ctrl+S · 자동 저장 | 세이브 로드 · 마이그레이션 | .sbpkg 의 월드 | 자동 저장 (recovery/) |
| GamePackage | .sbpkg (09 9장) | BuildService | --world <패키지> | — | 다시 빌드 |
| User Settings · Studio 설정 | 사용자 폴더 (client.json · input.json · studio.json · layouts) | 설정 창 | 시작 · 안전 모드 (깨지면 옆으로) | 싣지 않는다 | 기본값 |

생명주기 (에셋 하나):

```text
가져오기 (Asset Browser 끌어다 놓기 · 메뉴) → 원본 복사 → .meta { id, importer, settings } 생성 → 가져오기 Job (디코드 · 검사 —
  크기 · 형식 · 압축 폭탄 상한 4장) → 캐시 결과 → 색인 갱신 → 브라우저 썸네일
참조 (Prefab render.sprite 에 끌어다 놓기) → 문서에 "asset:<id>" → 색인의 역참조 갱신
이름 바꾸기 · 옮기기 (Studio) → 파일 + .meta 같이 이동 → id 그대로 → 참조 그대로
지우기 → 역참조가 있으면 목록 보여 주고 확인 (Unreal 의 redirector · 참조 경고와 같은 목적) → 휴지통 폴더 (.sandbox/trash — 되돌리기 가능)
바뀜 감지 (밖에서 고침) → 파일 해시가 바뀌면 다시 가져오기 Job → 런타임 Hot Reload (아틀라스 칸 다시 업로드) [계획 v0.6]
빌드 → 패키지에 넣을 에셋 = 참조 닫힘 (Prefab · 머티리얼 · UI 문서가 참조한 id 의 전이 닫힘) + 누락이면 빌드 경고 → 오류 (설정)
```

## 3. Importer

| 종류 | 원본 | v0.5 | 이후 |
|------|------|------|------|
| image | PNG | 현행 디코드 (stb) + [제안] .meta · 크기 상한 (4096² · 64 MiB 디코드) | JPEG · 아틀라스 그룹 설정 · 압축 (BC · ASTC) v0.8 |
| sound | WAV · OGG | 없음 (오디오 백엔드 D-14 — M2-T6) | v0.6 |
| font | TTF | 글꼴 대체 (M4-T2) 의 자리 | v0.6 |
| ui | presentation/ui/*.json | 콘텐츠로 (에셋 아님 — 텍스트 문서) | — |
| model (3D) | glTF | 없음 | v1.x (3D 결정 D-16) |

## 4. 안전 · 예산 (24 의 P · D 행에 더함)

```text
파일 크기 상한 (원본 64 MiB · 디코드 결과 64 MiB · 이미지 4096²), 압축 폭탄 = 디코드 전 헤더 크기 확인, 경로 = 프로젝트 안만 (.. · 링크 거절 —
패키지 규칙 09 9장 그대로), 가져오기는 Worker 에서 (메인 스레드 I/O 없음), 실패 = 그 에셋만 오류 (브라우저 표시 · 마젠타), 동시 가져오기 4 개
```

## 5. 의존 · 참조 · 패키지 · 충돌

```text
색인    { id → 경로 · 종류 · 내용 해시 · 의존 id[] }, 역참조 { id → 참조한 문서[] } — 콘텐츠 로드가 render.sprite · materials · UI 문서를
        훑어 만든다 [계획 v0.6]. v0.5 는 빌드 때 한 번 훑어 누락만 (M6-T3)
패키지  외부 패키지 (v0.4 설치 .sandbox/packages/…) 의 에셋 id 가 겹치면 — 128 비트 무작위라 우연한 충돌은 무시 가능, 같은 id 의 다른 내용
        (복사해 고친 에셋) = 경고 + 프로젝트 쪽이 이긴다 (팩 차례 규칙과 같은 방향)
버전    .meta 의 "importer" 가 형식 버전을 갖는다 (image@1). 올리면 다시 가져오기 — 원본은 그대로
```

## 6. 초보자용 데이터 편집 (Key/Value · 배열 · 객체 · 컴포넌트)

```text
리플렉션 FieldMeta (현행 — Inspector 가 쓴다: 범위 · 힌트 PrefabRef 등) 를 콘텐츠 JSON 편집에도: 스키마 (11 의 키 표) → 폼 (숫자 = 범위 슬라이더 ·
태그 = 고르기 · 참조 = 고르기 · 배열 = 더하기/빼기/끌어 순서 · 객체 = 접기). 같은 폼이 Prefab · actions · signals · GameMode 에 —
지금 트리거 폼 (editor/ui/TriggerForm) 의 일반화 [계획 v0.6]. JSON 원문 보기는 남긴다 (고급 사용자). YAML 은 도입하지 않는다 — 콘텐츠 로더 ·
contentHash · 리플레이 · 오버레이가 모두 JSON (nlohmann) 위이고, 형식을 둘 두면 검사 · 정규화 비용이 둘이 된다
```

## 7. 상태 표

| 항목 | 상태 |
|------|------|
| PNG 디코드 · 아틀라스 · 업로드 예산 | 현행 (06 7장) |
| 경로 해시 id | 현행 (대체로 남긴다) |
| .meta 안정 id · 에셋 DB 색인 · Asset Browser 기본 (가져오기 · 썸네일 · 끌어다 놓기) | [계획 v0.5 M6-T9 최소 — 이번 감사가 추가] · v0.6 (참조 찾기 · 옮기기 · 지우기 안전) |
| 빌드 때 누락 에셋 검사 | [계획 v0.5 M6-T3] |
| 캐시 분리 · 다시 가져오기 · Hot Reload | [계획 v0.6] |
| 사운드 · 글꼴 importer | [계획 v0.5 M2-T6 · M4-T2 결정] · v0.6 |
| 압축 · 플랫폼 형식 · 스트리밍 · 메모리 예산 | [계획 v0.8] (22 10장) |
