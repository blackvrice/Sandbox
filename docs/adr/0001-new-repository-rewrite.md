# ADR-0001. RTS를 리팩터링하지 않고 새 저장소에서 재작성한다

- 상태: **Accepted** · 날짜: 2026-10-05
- 관련: [design/SANDBOX_ARCHITECTURE](../design/SANDBOX_ARCHITECTURE.md) 1~3장

## 맥락

RTS 저장소는 SFML 렌더링, 상속 기반 엔티티(`IGameElement` → `Unit` God 객체), RTS 전용 enum,
렌더 스레드의 월드 읽기 락, 전역 `DataRegistry::global()`에 묶여 있습니다. 동시에 고정 틱, Generation EntityId,
명령 기반 입력, 리플레이, WorldHash, 결정론 하네스처럼 검증된 아이디어도 있습니다.
새 목표(범용 Sandbox, 50k 엔티티, 서버 권한 멀티, DX12/Vulkan/Metal)는 RTS와 구조적 전제가 다릅니다.

## 결정

새 저장소 `D:\Game\Sandbox`에서 처음부터 작성합니다. RTS는 동결된 참고자료입니다.
RTS 코드는 복사하지 않고 **재작성**하며, 아이디어를 가져올 때는 ADR 또는 커밋 메시지에 출처를 적습니다.

## 근거

```text
1. 의존성(SFML/OpenGL 제거, MSVC, D3D12), CMake 구조, CI, ADR 이력이 전부 다르다.
2. RTS 의 해시 회귀 안전망은 "동작 불변 리팩터링"을 위한 것인데, 이번 작업은 동작 불변이 목표가 아니다.
3. RTS 로드맵(Phase 1 트랙 A 진행 중)을 깨지 않는다. 두 프로젝트가 독립적으로 진행 가능하다.
```

## 결과

- 얻는 것: 첫날부터 경계가 강제되는 구조, RTS 로드맵과의 간섭 없음.
- 포기하는 것: RTS의 동작하는 게임 기능(전투 공식, AI, HUD)을 바로 쓰지 못함.
- 위험: 재작성 범위 과대 → [16-ROADMAP](../16-ROADMAP.md) R1.

## 대안

- RTS 저장소 안 `sandbox/` 하위 폴더: 빌드·CI·문서 규칙이 섞임. 기각.
- RTS를 점진 리팩터링: SFML·상속 모델 제거가 사실상 전면 재작성이고 RTS 안전망과 충돌. 기각.

## 재검토 조건

없음.
