# ADR-0101. SandboxCore 가 SandboxGameUI 를 링크한다 — 게임 UI 문서 검사는 프로젝트 검사 (validateProject) 가 부른다

- 상태: **Accepted** · 날짜: 2026-11-04
- 관련: [ADR-0079](0079-game-ui-runtime-declarative-documents-renderer-adapter.md) 3 (SandboxGameUI — Foundation 만),
  [20-CREATOR-ALPHA](../20-CREATOR-ALPHA.md) 2.2 · 2.3, [01-ARCHITECTURE](../01-ARCHITECTURE.md) 2장, 18 5.2 (프로젝트 검사)

## 맥락

ADR-0079 · 20 2.2 는 "core/content 는 presentation/ui 원문만 들고 가고, 검사는 ui/ 의 것을 sbx check 가 부른다" 고 적었다. 그런데 sbx
check · sbx doctor · Studio (v0.8) · 시험이 모두 같은 프로젝트 검사 (core/project validateProject) 를 쓴다. 검사를 tools/sbx 에만 두면
doctor · Studio 가 따로 이어 붙여야 하고 결과가 갈린다. 검사에는 콘텐츠 (신호 · 행동 · 팩 materials · 스타일) 가 필요하다 — ui/ 는
콘텐츠를 모르므로 누군가 둘을 잇는다.

## 결정

```text
1. SandboxCore 가 SandboxGameUI 를 PRIVATE 로 링크한다. core 의 허용 모듈에 ui 를 더한다 (check_includes — core → foundation · script ·
   ui · core). ui 는 그대로 Foundation 만 (core · render · ImGui 를 모른다)
2. core/content 로더는 그대로 원문만 (PackInfo.uiDocuments · uiStyles — 팩마다 문서 32 개까지). 시뮬레이션 · 서버는 UI 를 읽지 않는다
3. core/project validateProject 가 ui::parseUiStyles · parseUiDocument 를 부르고, 콘텐츠에서 신호 · 행동 · 스타일 · material 을 모아
   UiCheckContext 로 준다. 문서 사이 ui 대상 (U6) · 게임 예산 (문서 32 · 노드 4,096 — U7) 도 여기서. openProject 는 UI 를 보지 않는다
   (UI 문제는 게임 실행을 막지 않는다 — A7 · P10 과 같다)
4. 엔진 material 은 ValidateOptions.engineAssets (엔진 에셋 루트) 로 받는다 — 비면 U8 을 건너뛴다 (core 는 엔진 에셋 위치를 모른다)
```

## 근거

```text
- 검사의 단일 진입점 (sbx check · doctor · Studio · 시험) — 결과가 한 곳에서 나온다
- 의존 방향은 그대로 아래로만: ui 는 Script 처럼 Foundation 위의 잎 모듈이고, Core 가 그 위. Render · ImGui 는 여전히 ui 밖
```

## 결과

```text
+ sbx check · doctor 가 같은 U1 ~ U8 을 낸다, 시험은 validateProject 로
- Core 를 링크하는 실행 파일 (서버 포함) 이 SandboxGameUI 를 함께 링크한다 — 검사 코드뿐이라 작다 (배치 · 그리기는 클라이언트가 따로)
```
