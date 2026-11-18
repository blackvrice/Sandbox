# ADR-0167. Studio UX · Creator Workflow 보완 — ST-08 묶음 (P0 = v0.13 알파 진입 전제, P1 = v0.14, P2 = v0.21)

- 상태: **Accepted** · 날짜: 2026-11-18
- 관련: [ADR-0164](0164-defer-scene-game-view-split-to-v021.md) (ST-06-11 v0.21), [ADR-0118](0118-studio-shell-dockspace-design-system.md) (셸), [ADR-0089](0089-studio-editor-services-layer-imgui-first.md)
  (Editor Services 경계), [STUDIO-UX-AUDIT](../qa/STUDIO-UX-AUDIT.md), [29-STUDIO-UI](../29-STUDIO-UI.md)

## 맥락

사용자 창 QA 가 Studio 의 제작 흐름에서 막힌 곳을 보고했다 — 빌드가 메뉴에 없다 · 창이 너무 많고 닫을 수 없다 · 프로젝트 파일
탐색기가 없다 · 스크립트를 어디서 열고 만드는지 모른다 · Sprite 를 끌어 Object 를, Object 에서 Prefab 을 만들 수 없다 · 시작 화면에
삼각형 · 인스펙터에 이름 · 회전 · 스케일이 안 보인다 · 작업 공간 · 레이아웃 관리가 모자라다. 소스와 맞춘 결과는 감사 문서 2장.

v0.13 (Closed Creator Alpha) 의 목표는 "외부 제작자가 **Studio 만으로 (CLI 없이)** 실제 게임을 만들어 다른 PC 의 친구가 실행한다"
이고 원칙은 "새 핵심 기능을 만들지 않는다 — 통합 · 버그 · 사용성". 보고의 일부는 이 목표를 직접 막는다 (스크립트 파일 · Sprite →
Object · Prefab 을 JSON 없이), 나머지는 편의다.

## 결정

1. 새 태스크 묶음 **ST-08** (Studio UX · Creator Workflow) 을 등록한다. 감사 문서 4장의 표가 계획, 상태는 task-registry 가 정본.
2. 목표 버전은 우선순위로 나눈다 — v0.13 에 한꺼번에 넣지 않는다:
   - **P0 → v0.13** (ST-08-1 ~ 6): 알파의 "Studio 만으로" 를 막는 것 — 창 닫기 · 적은 기본 창, Project Explorer, 스크립트 열기 ·
     만들기, Sprite → Object, Object → Prefab. 모두 있는 서비스 (AssetRefs · ContentOverlay · CreateEntity · PrefabDiff · 스크립트
     편집기) 를 잇는 사용성 일이라 v0.13 원칙 ("새 핵심 기능 없음") 과 맞는다. 사용성 시험 ST-06-12 가 이것들을 본다.
   - **P1 → v0.14** (ST-08-7 ~ 13): 메뉴 정리 · Workspace · 레이아웃 지우기 · 이름 바꾸기 · Build Settings · 시작 삼각형 제거 ·
     인스펙터 · Creator/Developer 모드 · 여러 스크립트 탭. v0.14 의 플랫폼 태스크 (PS-12) 와 트랙이 달라 나란히 간다.
   - **P2 → v0.21** (ST-08-14): 독립 OS 창 (ImGui Viewports) · 고급 레이아웃 · 추가 단축키 — ST-06-11 (Scene/Game 뷰 · 떼어 낸 창)
     과 같은 곳. ST-06-11 은 당기지 않는다.
3. 스케일은 만들지 않는다 — core.transform 에 필드가 없고, 더하면 시뮬레이션 컴포넌트 · kSimVersion · 세이브 호환이 바뀐다. 인스펙터는
   있는 데이터만 (render.sprite 의 size 가 그림 크기).
4. 메뉴 · 단축키 · 팔레트는 같은 명령 id 를 쓴다. 월드를 바꾸는 새 일 (Sprite → Object · Object → Prefab) 은 Editor 의 명령 길
   (되돌리기 · 서버 검증) 로, 파일 일 (탐색기) 은 editor/services 의 새 서비스 (Foundation · Core 만) 로 한다.
5. 자동 시험은 GUI QA 를 대신하지 않는다 — 태스크마다 MANUAL-QA 에 사람 확인 줄을 더하고 미실시로 둔다.

## 결과

- v0.13 의 태스크가 여섯 늘지만 모두 통합 · 사용성이다. 알파 진입 판정 (release-gates 3장) 은 그대로.
- 프로젝트 파일 탐색기 (ProjectFiles) 가 에셋 브라우저와 겹치는 부분 (assets/ 의 이름 바꾸기 · 지우기) 은 AssetRefs 를 그대로 불러
  참조 고침 · .meta 동행이 한 길로 남는다. 에셋 브라우저는 썸네일 격자로 남는다 (지우지 않는다).
- P1 · P2 는 그 버전의 일이다 — 이 결정으로 당겨 오지 않는다.
