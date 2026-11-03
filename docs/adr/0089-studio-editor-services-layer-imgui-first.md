# ADR-0089. Sandbox Studio — SandboxClient 의 모드 · UI 를 모르는 Editor Services 층 · ImGui 먼저 · 파일 편집은 DocumentService

- 상태: **Accepted** · 날짜: 2026-11-03
- 관련: [25-STUDIO-ARCHITECTURE](../25-STUDIO-ARCHITECTURE.md), [10-EDITOR](../10-EDITOR.md) (E1 ~ E4 — 그대로), [ADR-0028](0028-editor-library-host-tools-preview.md),
  [ADR-0053](0053-test-play-copy-world-park-edit-world-protocol-12.md), [ADR-0079](0079-game-ui-runtime-declarative-documents-renderer-adapter.md),
  [audit/ARCHITECTURE-REVIEW](../audit/ARCHITECTURE-REVIEW.md) R3 · R4 · R5 · R7

## 맥락

알파 목표는 "CLI 없이 만들고 · 테스트하고 · 빌드" 이다. 프로젝트 생성 · 빌드 · 진단 로직은 core 에 있지만 CLI 만 부르고, 에디터는 ImGui 패널 ·
툴 · 명령 기반 편집으로 엔진 검증 도구에 가깝다. Application (808 줄) · NetworkSession (1,087 줄) 에 기능이 쌓이고 있고, 콘텐츠 편집은 월드
오버레이에만 들어간다 (팩 파일에 쓰는 길이 없다).

## 결정

```text
1. Studio 는 새 실행 파일이 아니라 SandboxClient 의 모드 (Studio 홈에서 시작) — Edit/Play (ADR-0053) · LocalServerHost · ClientWorld 를 그대로
   쓴다. 실행 파일 분리는 측정 · 배포 이유가 생기면 (서비스 층이 이미 나뉘어 있어 싸다)
2. 편집은 두 종류: 월드 편집 = SimCommand (10 E1 그대로), 파일 편집 (팩 JSON · 스크립트 · UI 문서 · 에셋 메타) = DocumentService
   (원자적 저장 · 저장 전 검사 · 바깥 변경 감지 · 되돌리기 기록). UI 코드는 파일 · Registry 를 직접 고치지 않는다. 콘텐츠 창은 [적용 (오버레이 —
   지금처럼 월드에서 즉시)] 과 [저장 (팩 파일 — 프로젝트 소스)] 을 구분해 보여 준다
3. editor/services/ 를 새로 둔다 — Project · Document · Command · Selection · Undo · Build · Test · Autosave. foundation · script · core 와
   ICommandSink 만 의존, imgui · render · platform 헤더 금지 (check_includes 규칙 추가). UI (editor/ui · apps/client/studio) 가 서비스를 부른다
4. 새 기능부터 services 에 넣는다 — 기존 패널 · Application 의 코드는 그 기능을 고칠 때 옮긴다 (대규모 리팩터 금지)
5. UI 는 ImGui 먼저 — GraphicsUI 완성을 기다리지 않는다 (ADR-0079 와 같은 판단). 웹 UI (Electron · CEF) 는 v1.0 전 도입하지 않는다
6. Studio 설정 (studio.json · 레이아웃 · 최근 · 테마) 은 사용자 설정 폴더, 게임 데이터는 프로젝트 — 섞지 않는다
7. Prefab 의 core.prefab 은 v0.5 에서 "출처 id" 로 의미를 동결 — 인스턴스 오버라이드는 v0.6 초 별도 ADR (결정론 · 세이브 영향 측정)
8. 플러그인은 네이티브 코드를 받지 않는다 (ADR-0081 을 편집기에도) — v0.8+ 스크립트 editor 도메인 · Capability
```

## 선택지

| 안 | 내용 | 판단 |
|----|------|------|
| A (채택) | Client 모드 + services 층 + ImGui | 재사용 최대 · 리팩터 최소 · UI 교체 대비 |
| B | 새 SandboxStudio 실행 파일 + 새 UI 프레임워크 | Edit/Play · 렌더 · 세션을 다시 잇는 비용, GraphicsUI 선행 의존 |
| C | 웹 기반 Studio (CEF) | 풍부한 편집기지만 의존 · 보안 면 · 렌더 통합 비용 큼 |
| D | 지금 패널에 계속 더하기 | 단기 빠름 · Application 비대 (R7) · UI 교체 불가 |

## 결과

- 좋은 점: 알파의 막힌 단계 셋 (홈 · 빌드 · 저장 위치) 이 로직 재사용으로 풀린다. 서비스는 헤드리스로 시험 (CTest studio_*).
- 비용: services 층 · Undo 통합 · DocumentService 의 원자적 저장 · 바깥 변경 감지.
- 위험: 콘텐츠 편집의 두 저장 경로 (오버레이 · 팩) 를 사용자가 헷갈림 — 화면에 늘 어느 쪽인지 표시하고, 플레이 · 빌드 전에 "저장 안 한 오버레이"
  알림 (28 G8).
