# DEVELOPMENT_LOG

작업 단위의 **이력**입니다: 무엇을, 왜 바꿨고, 어떻게 검증했고, 무엇이 남았나.
"지금 어떤 상태이고 무엇을 지켜야 하나"는 `docs/`에 있습니다. 둘을 섞지 않습니다.

새 항목은 위에 추가합니다.

---

## 2026-10-05 — Phase 1: 저장소 골격

**무엇을**

- 빌드: 루트 `CMakeLists.txt`, `CMakePresets.json`(7 configure 프리셋, Ninja Multi-Config), `cmake/Sbx*.cmake`
  - `sbx_warnings`(MSVC `/utf-8` 포함), `sbx_strict_conversions`, `sbx_simulation_flags`(Core 가 PUBLIC 전파)
  - 표준 라이브러리 기능 확인(`std::expected`/`format`/`source_location`) — 안 되면 원인 한 줄로 중단
  - `CMAKE_CXX_SCAN_FOR_MODULES OFF`, 실행 파일은 `build/<preset>/bin/<Config>/`
  - `BuildInfo.hpp` 생성(버전·커밋·컴파일러)
- 경계 강제: `sbx_check_link_boundaries()`(전이적 링크 그래프), `tools/check_includes.py`(모듈 방향·금지 헤더·SFML/OpenGL·`#import`),
  각각 자체 시험(`arch_link_boundary_selftest`, `arch_include_lint_selftest` + `tests/arch/fixtures`)
- Foundation: `Types`, `Error`/`Expected`, `SBX_ASSERT`/`SBX_VERIFY`(처리기 교체 가능), `log`, `Fnv1a64`(동결 상수 테스트), `Handle<Tag>`
- Core: `core/simulation/SimConstants`(30 TPS, 고정 dt, 스냅샷 주기 약수 규칙)
- SandboxServer: `--help`/`--version`/`--log-level`, 종료 코드 0/1/2
- SandboxTests: doctest v2.5.0 vendored, 29 케이스
- 저장소 설정: `.gitignore`, `.gitattributes`, `.editorconfig`, `.clang-format`(적용 완료), `.clang-tidy`
- CI: `.github/workflows/ci.yml` (Windows MSVC, Linux clang-19/gcc-13/asan, macOS 15)

**왜**

- Phase 0 에서 정한 경계(ADR-0005)를 문서가 아니라 빌드·테스트 실패로 강제하기 위해. RTS 에서 "core 는 SFML 을 모른다"가
  문서에만 있고 코드에서 깨져 있던 것을 반복하지 않는다.

**도중에 바꾼 것**

- Clang 18 + libstdc++ 13 은 `std::expected`를 비활성화한다 → Linux 최소 요구를 Clang 19 로 올리고 구성 단계 검사 추가.
- CMake 3.28 + C++23 은 모듈 스캔을 켜서 clang-scan-deps 가 없으면 전부 실패한다 → 모듈 스캔 끔.
- `toString(enum)` 자유 함수가 doctest 와 ADL 충돌 → `errorCodeName`/`levelName`으로 개명, 규칙을 12-CODING-STANDARDS 에 추가.
- `SmallVector`는 첫 사용처인 Phase 2 로 이동.

**검증** (Linux 클라우드 환경)

```text
linux-clang (clang 19.1.1) · linux-gcc (gcc 13.3) · linux-clang-asan   ×   Debug · RelWithDebInfo, -Werror
→ 6개 조합 모두 ctest 8/8 통과 (doctest 29 케이스 / 68 단언)
SandboxServer --version → "SandboxServer 0.1.0 (commit unknown, Linux, Clang 19.1.1) / tick 30 Hz, dt 0.0333333 s"
인자 없음 → 종료 코드 1, 잘못된 인자 → 2
boundary selftest: 중간 INTERFACE 타깃을 거친 Server→Platform 링크를 구성 단계에서 FATAL_ERROR
include lint: 실제 트리 22 파일 0건, 픽스처 8건 정확히 검출
clang-18 지정 시 구성 단계에서 원인 메시지와 함께 중단됨을 확인
```

**미검증 / 남은 일**

- Windows MSVC, macOS 빌드는 이 세션에서 실행할 수 없었다 → 사용자 PC 에서 `cmake --preset windows-msvc` 1회, CI 첫 실행.
- `git init`·원격 저장소 생성은 사용자 작업 (16-ROADMAP 1.1b). 커밋 해시가 생기면 BuildInfo 에 반영된다(재구성 시).
- LICENSE (열린 질문 Q1).
- 다음: Phase 2 Core ECS.

---

## 2026-10-05 — Phase 0: 문서 세트 작성

**무엇을**

- 저장소 루트: `README.md`, `AGENTS.md`, `DEVELOPMENT_LOG.md`
- `docs/00~17` 규범·설계·절차 문서, `docs/adr/0001~0010`
- `docs/design/SANDBOX_ARCHITECTURE.md`: RTS 저장소 분석과 전체 설계의 원본 (Phase 0 입력, 이후 수정하지 않음)

**왜**

- 새 프로젝트를 코드 없이 시작하면서, 구현 전에 경계·불변식·순서를 먼저 고정하기 위해.
- 원본 설계서는 한 파일 2,000줄이라 작업 중 참조하기 어렵다. 주제별 규범 문서로 나누고
  구현에 필요한 세부(API 모양, 와이어 포맷, 스키마, 체크리스트)를 보강했다.

**검증**

- 문서 간 상대 링크 전수 확인, 코드 블록 짝 확인.
- 원본 설계서의 30개 장과 결정 항목이 분할 문서 어디로 갔는지 `docs/README.md` 4장에 대응표로 남김.

**남은 일**

- Phase 1: 저장소 골격(CMake 프리셋, Foundation/Core/Tests 타깃, CI, include 린트). [16-ROADMAP](docs/16-ROADMAP.md)
- 부록의 열린 질문(저장소 이름·라이선스 등)은 결정되는 대로 ADR로.
