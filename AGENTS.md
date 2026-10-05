# AGENTS.md

이 저장소에서 일하는 모든 AI 에이전트(및 사람)가 따르는 규칙입니다.
`docs/`가 설계·규칙·절차의 단일 진실원입니다. 이 파일은 그 문서들로 가는 입구와 작업 절차입니다.

---

## 1. 작업 전에 읽을 것

```text
항상            docs/README.md (문서 지도) → docs/16-ROADMAP.md (지금 어느 Phase 인가)
코드를 쓰기 전  docs/12-CODING-STANDARDS.md (리뷰 체크리스트 포함)
Simulation 코드 docs/04-DETERMINISM.md  ★ 다른 모든 문서보다 우선한다
경계를 넘는 변경 docs/01-ARCHITECTURE.md 의 의존성 규칙
```

## 2. 작업 절차 (모든 작업에 적용)

```text
1. 현재 구조 확인      실제 저장소를 조사한다. 존재하지 않는 클래스·파일을 추측해서 고치지 않는다.
2. 문제 분석
3. Architecture 결정  문서와 충돌하면 문서를 먼저 고치거나 ADR 을 추가한다.
4. 변경 파일 목록 작성
5. 구현               컴파일 가능한 작은 단위로. 대규모 Rewrite 를 한 번에 하지 않는다.
6. Compile
7. Test               ctest --preset <preset>
8. Runtime Verification  실행 파일을 실제로 돌려 본다 (Server 는 --ticks N --exit 로 헤드리스 확인)
9. Benchmark          성능에 영향이 있는 변경이면 sbx_bench 결과를 남긴다
10. Documentation Update  같은 커밋에서
```

## 3. 빌드 툴체인

- Windows는 **MSVC**(Visual Studio Build Tools) 또는 clang-cl을 씁니다. MinGW는 지원하지 않습니다. 근거: [ADR-0009](docs/adr/0009-msvc-toolchain.md)
- 반드시 `CMakePresets.json`의 프리셋으로 구성·빌드합니다. 컴파일러 경로를 하드코딩하지 않습니다.
- 빌드나 실행이 실패하면, 이번 변경이 만든 문제를 고친 뒤에 커밋합니다.
- 자동으로 실행할 수 없으면, 시도한 정확한 명령과 막힌 이유를 최종 응답에 적습니다.

## 4. 경계 규칙 (위반하면 리뷰에서 막힘)

```text
- foundation/ core/ network/ 는 그래픽 API·OS 창·ImGui·오디오 헤더를 include 하지 않는다.
  (d3d12.h dxgi.h vulkan.h Metal.h Cocoa.h windows.h X11/* imgui.h)  → tools/check_includes.py 가 검사
- SandboxServer 는 SandboxPlatform / SandboxRender / SandboxEditor 를 링크하지 않는다.  → CMake 가 검사
- SandboxRender 는 SandboxCore(ECS)를 모른다. ECS → RenderWorld 변환은 SandboxClient/presentation 에.
- Component 에 GPU/OS native 핸들이나 포인터를 넣지 않는다. Handle 을 쓴다.
- Simulation 은 입력 장치·벽시계·스레드 완료 순서를 보지 않는다.
```

## 5. 커밋 규칙

- 작업이 끝나면 `git status`로 범위를 확인하고, 관련 파일만 커밋한 뒤 현재 브랜치를 push 합니다.
- **리팩터링 커밋은 골든 WorldHash가 같아야 합니다.** 시뮬레이션 동작을 의도적으로 바꾼 커밋은
  `kSimVersion`을 올리고 골든 해시를 갱신하며, 커밋 메시지에 이유를 적습니다. ([13-TESTING](docs/13-TESTING.md))
- 커밋 메시지: `<영역>: <요약>` (예: `ecs: add sparse set component pool`). 영역 = 최상위 디렉터리 또는 문서명.

## 6. 문서 규칙

- 구조를 바꾸면 **같은 커밋에서** 문서를 고칩니다.
- 결정을 내리거나 뒤집으면 `docs/adr/`에 **새 ADR을 추가**합니다. 기존 ADR은 수정하지 않고 `Superseded` 처리만 합니다.
- 모듈·폴더·진입점·타깃을 추가/이동/삭제하면 [17-SOURCE-MAP](docs/17-SOURCE-MAP.md)을 갱신합니다.
- `content/` 데이터의 모양을 바꾸면 [11-CONTENT-SCHEMA](docs/11-CONTENT-SCHEMA.md)를 갱신합니다.
- 와이어 포맷·세이브 포맷을 바꾸면 [08-NETWORK](docs/08-NETWORK.md) / [09-SERIALIZATION](docs/09-SERIALIZATION.md)과 버전 번호를 갱신합니다.
- 작업 단위마다 `DEVELOPMENT_LOG.md`에 "무엇을, 왜, 어떻게 검증했나, 남은 일"을 남깁니다.
- 문서에 수치를 쓸 때는 출처(측정값/추정/데이터 파일)를 함께 적습니다.
- 구현되지 않은 것은 `[계획]`으로 표시합니다.

## 7. 주석 규칙

- 자명하지 않은 로직, 복잡한 조건, 좌표 변환, 동기화, 결정론 관련 가정, 백엔드별 차이에는 짧은 주석을 답니다.
- 자명한 getter/setter, 이름으로 의도가 드러나는 코드에는 주석을 달지 않습니다.
- 사용자나 다른 도구가 만든 기존 변경을 요청 없이 되돌리지 않습니다.
