# 12. 코딩 규칙

> **규범 문서.** 코드를 쓰기 전에 한 번, 리뷰할 때마다 읽습니다. 맨 아래 체크리스트가 리뷰 기준입니다.

---

## 1. 언어와 표준

```text
C++23. 컴파일러: MSVC (최신 Visual Studio 툴셋), Clang 18+, Apple Clang (Xcode 16+). GCC 는 CI 보조.
허용·권장: concepts, ranges(성능 경로 제외), std::span, std::expected, std::optional, std::variant,
           constexpr, designated initializers, [[nodiscard]], std::format, std::to_chars/from_chars,
           std::jthread, std::bit_cast, enum class, three-way comparison
주의: std::print / std::generator / std::mdspan / 모듈(import std) — 세 툴체인 지원 확인 전 사용 금지
금지: RTTI 에 의존하는 설계(dynamic_cast 로 분기), 예외를 시뮬레이션 핫 경로 제어 흐름에 사용,
      매크로로 제어 흐름 숨기기, using namespace (헤더), 전역 가변 싱글턴
```

## 2. 이름

| 대상                    | 규칙                                   | 예                                             |
|-------------------------|----------------------------------------|------------------------------------------------|
| 네임스페이스            | `sbx::<module>` 소문자                 | `sbx::ecs`, `sbx::rhi`, `sbx::net`             |
| 타입·클래스·구조체·enum | PascalCase                             | `ComponentPool`, `RenderWorld`                 |
| 함수·메서드·변수        | camelCase                              | `queryRadius`, `currentTick`                   |
| 멤버 변수               | `m_` + camelCase                       | `m_freeIndices`                                |
| 상수·constexpr          | `k` + PascalCase                       | `kTickRate`, `kFixedDt`                        |
| enum 값                 | PascalCase                             | `QueueType::Graphics`                          |
| 매크로                  | `SBX_` + 대문자                        | `SBX_ASSERT`, `SBX_COMPONENT`                  |
| 인터페이스              | `I` 접두사                             | `IRenderDevice`, `INetworkTransport`           |
| 파일                    | 타입명과 같게, `.hpp` / `.cpp` / `.mm` | `ComponentPool.hpp`                            |
| 콘텐츠 id·stableId      | `<namespace>.<snake_case>`             | `life.energy`, `eco.rabbit`                    |
| 열거형 → 이름 함수      | `<대상>Name()` — **`toString` 금지**   | `errorCodeName(ErrorCode)`, `levelName(Level)` |

`toString`이라는 자유 함수는 doctest 가 값 출력에 쓰는 이름과 ADL 로 충돌해 `CHECK(a == b)`가 컴파일되지 않습니다 (Phase 1에서 실제로 발생).

## 3. 파일 구성

```text
- #pragma once
- include 순서: 대응 헤더 → 같은 모듈 → 다른 sbx 모듈 → 서드파티 → 표준. 그룹 사이 빈 줄.
- 공개 헤더는 모듈 디렉터리 루트, 내부 구현 헤더는 detail/ 또는 .cpp 옆 *Internal.hpp
- 헤더에서 무거운 서드파티(json, d3d12, vulkan, windows.h) include 금지 → 전방 선언 또는 pimpl
- 한 파일 800줄 초과 시 분할 검토 (RTS 의 105 KB 매니저를 반복하지 않는다)
```

## 4. 소유권과 수명

```text
- 단독 소유 = std::unique_ptr 또는 값. shared_ptr 은 공유 수명이 실제로 필요할 때만 (사유 주석).
  Core 시뮬레이션 데이터에 shared_ptr 금지 (Registry 가 유일한 소유자).
- 관찰 = 참조(널 불가) 또는 원시 포인터(널 가능, 비소유). 소유 원시 포인터 금지.
- 엔티티·리소스·에셋 참조는 핸들(index + generation). 포인터를 오래 들고 있지 않는다.
- RAII 로 모든 OS/GPU 자원 해제. GPU 자원은 RHI destroy (지연 해제) 를 거친다.
```

## 5. 오류 처리

| 상황                                              | 방법                                                                                           |
|---------------------------------------------------|------------------------------------------------------------------------------------------------|
| 프로그래머 오류(불변식 위반)                      | `SBX_ASSERT(cond, msg)` — Debug에서 중단, Release에서 제거                                     |
| Release에서도 확인할 치명 조건                    | `SBX_VERIFY(cond, msg)` — 로그 후 중단                                                         |
| 복구 가능한 실패(파일 없음, 파싱 실패, 검증 실패) | `std::expected<T, Error>` 반환                                                                 |
| 외부 입력(네트워크, 세이브, 콘텐츠)               | 절대 assert로 처리하지 않는다. 검증 후 Error                                                   |
| 예외                                              | 서드파티(nlohmann::json)가 던지는 것은 경계에서 잡아 `Error`로 변환. 엔진 코드는 던지지 않는다 |

`Error`는 `{ ErrorCode code; std::string message; std::string context; }`. 사용자에게 보이는 메시지는 원인과 위치(파일:JSON 포인터, 틱, netId)를 담습니다.

## 6. 스레드

```text
- 데이터마다 소유 스레드를 주석으로 적는다: // owner: Simulation thread
- 스레드 간 전달은 01-ARCHITECTURE 5.3 의 채널로만. 공유 가변 상태 + 락으로 해결하지 않는다.
- std::mutex 는 큐·풀 같은 기반 구조 안에만. 도메인 코드에 락이 보이면 설계를 다시 본다.
- 원자 변수는 memory_order 를 명시한다.
- Worker Job 은 값 캡처. 참조 캡처는 수명이 Job 보다 긴 불변 데이터만 (주석 필수).
```

## 7. 성능 관례

```text
- 핫 루프(System, 렌더 배치)에서 힙 할당 금지 → 프레임/틱 아레나, 재사용 버퍼, SmallVector
- 핫 루프에서 가상 호출·std::function 지양 (System 단위 1회는 무방)
- 문자열은 경계(로드·로그·UI)에서만. 런타임 식별은 해시 id
- "빠를 것 같아서" 구조를 복잡하게 하지 않는다. 14-PERFORMANCE 의 측정이 먼저다
```

## 8. 시뮬레이션 코드 추가 규칙

[04-DETERMINISM](04-DETERMINISM.md) 8장 체크리스트가 이 문서보다 우선합니다. 요약:

```text
dt 상수 · unordered 순회 금지 · 전순서 정렬 · 새 상태는 Hashed 컴포넌트 · RandomService 만
· Job 결과 제출 순서 적용 · 동작 변경 시 kSimVersion++ 과 골든 갱신
```

## 9. 플랫폼·백엔드 코드

```text
- 플랫폼·그래픽 API 헤더는 platform/<os>/, render/<backend>/ 안의 .cpp/.mm 에서만.
- 공개 헤더에 HWND, VkDevice, id<MTLDevice>, Display* 등 금지 → NativeWindowHandle, pimpl
- 백엔드별 #ifdef 를 상위 코드에 흩뿌리지 않는다. 분기는 팩토리(createWindow, createRenderDevice) 한 곳.
- Objective-C 는 .mm 안에서만, ARC 사용.
```

## 10. 주석·문서

```text
- 자명하지 않은 것만: 결정론 가정, 좌표 변환, 동기화, 백엔드 차이, 게임 규칙의 의도, 성능상 이유.
- "무엇"이 아니라 "왜". 코드가 말하는 것을 반복하지 않는다.
- 주석 언어: 한국어 또는 영어 (한 파일 안에서 섞지 않기를 권장). 식별자는 영어.
- 공개 API 헤더에는 계약(전제조건, 스레드, 수명)을 적는다.
- 구조를 바꾸면 docs/ 를 같은 커밋에서 (AGENTS.md 6장).
```

## 11. 포맷·정적 분석

```text
clang-format  저장소 루트 .clang-format (LLVM 기반, 들여쓰기 4, 줄 120)
clang-tidy    bugprone-*, performance-*, modernize-* 일부, cppcoreguidelines-owning-memory  (CI 경고)
경고          MSVC /W4 /permissive- , Clang/GCC -Wall -Wextra -Wpedantic -Wshadow -Wconversion(Core 한정)
              CI 에서는 경고 = 에러
Sanitizer     Linux CI 에서 ASan+UBSan 빌드로 SandboxTests, TSan 빌드로 스레드 테스트
```

## 12. 리뷰 체크리스트

```text
[ ] 경계: 금지 헤더·금지 링크 없음 (tools/check_includes.py, CMake 검사 통과)
[ ] 컴포넌트: 포인터·native 핸들 없음, 등록·리플렉션·Flags 지정, 크기 ≤ 64B 또는 사유
[ ] 시뮬레이션: 04-DETERMINISM 8장 체크리스트 통과
[ ] 구조 변경은 ECB 로, 순회 중 create/destroy/emplace/remove 없음
[ ] 외부 입력은 검증 후 Error, assert 로 처리하지 않음
[ ] 핫 루프 힙 할당 없음
[ ] 스레드 소유 주석, 공유 가변 상태 없음
[ ] 테스트 추가 (13-TESTING 의 해당 레벨)
[ ] 성능 영향 시 벤치 수치 첨부
[ ] 문서·ADR·SOURCE-MAP·버전 이력 갱신
[ ] DEVELOPMENT_LOG.md 항목
```
