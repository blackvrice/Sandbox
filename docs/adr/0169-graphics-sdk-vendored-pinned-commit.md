# ADR-0169. Graphics SDK 공급 — external/graphics-sdk 에 고정 커밋의 코어를 함께 빌드 (설치본은 선택)

- 상태: **Accepted** · 날짜: 2026-11-23
- 관련: [ADR-0168](0168-graphics-sdk-rhi-backend.md) (Graphics SDK 단일 백엔드), [external/README.md](../../external/README.md),
  [15-BUILD](../15-BUILD.md) 5장

## 맥락

ADR-0168 로 SandboxRender 가 `GraphicsSDK::Core` 하나에 기대게 됐는데, 빌드는 `find_package(GraphicsSDK CONFIG REQUIRED)` 로
**개발자 PC 에 따로 설치한 SDK** 를 찾았다. 그 결과:

```text
1. CI 다섯 잡 (Windows · Linux 셋 · macOS) 이 구성 단계에서 모두 실패 — 러너에 SDK 가 없다 (517c914). 렌더러가 필요 없는
   서버 · 도구 빌드까지 막혔다
2. 경계 자체 시험 (arch_link_boundary_selftest) 의 중첩 구성도 SDK 를 못 찾아 실패
3. 재현성 — ADR-0168 · 리팩터링 문서의 "고정 커밋 9345c1816879c63f8c26f13fc876382b0c2ed054" 는 SDK 저장소에 없는 해시
   (실제 9345c18 = 9345c1816b06391d91d8010cf2938164eec56b01). 개발자의 설치본은 그 뒤 5 커밋 · 미커밋 파일이 있는 체크아웃에서
   빌드돼 어느 소스인지 알 수 없었다
```

받아 오기 (FetchContent) 도 시험했다: SDK 저장소는 182 MB · 18,163 파일이고 그중 17,087 개가 benchmarks/ 의 검증 기록이며
경로가 208 자까지 길어 Windows 에서 `git clone` 이 "Filename too long" 으로 실패했다 (core.longpaths 없이). 잡마다 받는 것도 느리다.

## 결정

1. **기본 = 함께 빌드.** 고정 커밋의 SDK 코어만 (`include/` · `src/` · `cmake/` · `CMakeLists.txt` · `LICENSE` — 35 파일,
   1.1 MB, MIT) 을 `external/graphics-sdk/` 에 **수정 없이** 두고 `add_subdirectory(... EXCLUDE_FROM_ALL SYSTEM)` 로 Sandbox 와
   같은 구성 · 같은 MSVC 런타임으로 빌드한다. 다른 서드파티 (external/README.md) 와 같은 규칙: 갱신은 단독 커밋.
2. **고정 커밋** `95967d2ea3c2412fa1b51ee21a2e8fcee0759a59` (SDK origin/main — ADR-0168 이 가리키려던 9345c18 뒤 5 커밋: macOS
   Metal ARC · CoreGraphics 링크 수정, 코어 소스 변경은 2 줄). `external/graphics-sdk/VENDORED_COMMIT` 이 정본이고, 구성 로그에
   나온다 ("Sandbox: Graphics SDK — external/graphics-sdk (<커밋>)").
3. **갱신 도구** `tools/vendor_graphics_sdk.py --sdk <체크아웃> --commit <40자>` — `git archive` 로 커밋 내용만 읽는다 (작업 트리
   를 건드리지 않는다). 짧은 해시 · 브랜치 이름은 받지 않는다.
4. **설치본은 선택.** `SBX_SYSTEM_GRAPHICS_SDK=ON` 이면 예전처럼 `find_package` — SDK 를 고치며 Sandbox 로 시험할 때만. 쓰지
   않는 옵션 `SBX_USE_GRAPHICS_SDK` 는 지운다 (OFF 로 바꿔도 동작하지 않던 값).
5. **백엔드는 Sandbox 옵션을 따른다**: D3D12 = Windows, Vulkan = `SBX_ENABLE_VULKAN` (헤더 = `external/vulkan-headers`), Linux
   Vulkan 은 XCB 표면 (`GFX_VULKAN_XCB` — libxcb 개발 파일), Metal = `SBX_ENABLE_METAL`.

## 결과

- CI 는 따로 할 일이 없다 (Linux 러너에 libxcb1-dev · libx11-xcb-dev · pkg-config 만 더함). 경계 자체 시험도 같은 공급을 쓴다.
- SDK 를 바꾸려면 SDK 저장소에서 고치고 다시 가져온다 — Sandbox 안에서 external/graphics-sdk 를 고치지 않는다.
- SDK 의 C++23 · 경고 설정은 SDK 의 것 (SYSTEM — Sandbox 의 SBX_WARNINGS_AS_ERRORS 밖). GCC 13 · Clang 19 (CI) 로 빌드되는지는 CI
  가 본다 (SDK 자체 CI 는 g++-14).
