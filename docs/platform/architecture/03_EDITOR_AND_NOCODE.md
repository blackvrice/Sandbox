# 04. Editor / No-code / GameMode 설계

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 목적
비개발자가 게임을 만들 수 있도록 Unity식 Component Inspector와 StarCraft UMS식 Trigger/조건·효과를 제공하고, 개발자는 동일 모델을 Script API로 확장한다. 기존 SandboxEditor의 멀티플레이 편집과 `ICommandSink`는 유지한다.

## 2. 편집 모드
- **Authoring**: World, Prefab, Schema, Feature 설치/설정. Inspector/Hierarchy/Asset Browser.
- **Test Play**: 편집 월드에서 분기된 게임 세션(복사 또는 체크포인트) 실행. 종료 시 실험 상태 폐기/선택 저장.
- **Release**: GamePackage 빌드, 콘텐츠 해시 고정, 배포 채널 테스트.
플레이 중 임의의 스키마 변경은 명시적 Hot Reload 정책과 동기화 장벽을 거친다.

## 3. 필수 사용자 흐름
1. `Create Game`에서 템플릿/장르/시작 World 선택.
2. `Feature Catalog`에서 Movement, Camera, Score, Health, Timer 등 구성 요소 선택.
3. 객체 추가 → Component Inspector 속성 수정 → UI 입력 바인딩.
4. 시각 트리거 작성: `On Player Enter Zone` → `If Score >= 10` → `Show Result`.
5. 로컬 세션에서 Test Play 후 Build/Share.

## 4. 게임 라이프사이클
```text
Created → Loading → Lobby → Starting → Running → Finished → Stopped
```
- `GameMode`는 참가자 제한, 팀, 승리 조건, 재시작 및 세션 정리 정책 정의.
- 전환은 서버가 권위 있게 수행하고 클라이언트는 이벤트를 수신.
- 로비/맵 다운로드 실패·버전 미일치는 명확한 에러 상태 제공.

## 5. Visual Trigger 규칙
- 원본 그래프와 컴파일된 실행 표현을 별도 저장.
- Trigger Node는 이벤트·조건·효과의 **typed schema**를 사용. Script System과 같은 Native Host API 사용.
- 최대 깊이, 루프 횟수, 반복 이벤트 제한, 순환 참조 검사를 도입.
- 각 노드의 권한은 `ServerGameplay`, `ClientPresentation`, `EditorOnly`로 명시.

## 6. Inspector와 Reflection
- [현행] 기본 Component의 Reflection 기반 Inspector 존재.
- [신규] Custom Component Schema 등록 후 자동 속성 편집 UI.
- 타입별 위젯: f32 범위 슬라이더, enum 드롭다운, Vec2 좌표, Entity/Prefab reference picker.
- 단순 숫자 수정과 게임 로직 제약이 있는 `Health`, `Currency`는 분리; 불법 필드 수정은 서버에서 차단.

## 7. 테스트
- 비개발자: 기존 코드 없이 미니게임 제작·플레이.
- 두 에디터 사용자 동시 수정: 결과 수렴·Undo/Redo·권한.
- Trigger 재진입 폭주: budget에 의해 중단되고 월드 무결성 보존.
- Play → Stop 후 Authoring 원본 오염 없음.
