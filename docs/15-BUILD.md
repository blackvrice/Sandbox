# 15. 빌드

> **참조 문서.** 툴체인, CMake 구조, 의존성, 실행 옵션, CI를 설명합니다. 근거: [ADR-0009](adr/0009-msvc-toolchain.md).
> 상태: 1~4장·7장 일부·8~9장은 **Phase 1에서 구현됨** (2026-10-05). `[계획]` 표시가 붙은 것만 아직 없습니다.

---

## 1. 툴체인

| 플랫폼  | 컴파일러                                                                      | 필수                                                         | 비고                                                                                   |
|---------|-------------------------------------------------------------------------------|--------------------------------------------------------------|----------------------------------------------------------------------------------------|
| Windows | MSVC (Visual Studio 2022 17.x 이상 또는 Build Tools, 최신 툴셋) 또는 clang-cl | Windows 10/11 SDK, CMake 3.28+, Ninja, Python 3(경계 테스트) | **MinGW 미지원**                                                                       |
| Linux   | **Clang 19+** 또는 GCC 13+                                                    | CMake 3.28+, Ninja, Python 3                                 | Clang 18 + libstdc++ 13 조합은 `std::expected`가 비활성화되어 구성 단계에서 거부됩니다 |
| macOS   | Apple Clang (Xcode 16+)                                                       | Xcode Command Line Tools, CMake, Ninja                       | Apple Silicon, macOS 13+                                                               |

구성 단계가 `std::expected`·`std::format`·`std::source_location`을 실제로 컴파일해 보고, 안 되면 원인 한 줄로 중단합니다 (`cmake/SbxCompilerSettings.cmake`).

### 1.1 CLion 설정 (Windows)

```text
Settings → Build, Execution, Deployment → Toolchains → + → Visual Studio
  (RTS 에서 쓰던 MinGW 툴체인을 기본값으로 두지 않는다)
Settings → … → CMake → 프로필 목록에서 "Enable profile" 로 CMakePresets.json 의
  windows-msvc - Debug / RelWithDebInfo 를 켠다. 툴체인은 위의 Visual Studio.
```

명령줄은 **x64 Native Tools Command Prompt for VS**(또는 `vcvars64.bat`를 실행한 셸)에서 실행합니다.
Ninja 생성기는 `cl.exe`·Windows SDK 경로를 환경 변수에서 찾기 때문입니다.

## 2. CMake 프리셋

| configure 프리셋   | 대상                                                                             |
|--------------------|----------------------------------------------------------------------------------|
| `windows-msvc`     | Windows · MSVC (Developer 환경 필요)                                             |
| `windows-clangcl`  | Windows · clang-cl                                                               |
| `linux-clang`      | Linux · `clang++` (19 이상이어야 함 — 아니면 `-D CMAKE_CXX_COMPILER=clang++-19`) |
| `linux-gcc`        | Linux · GCC (보조)                                                               |
| `linux-clang-asan` | Linux · Clang + AddressSanitizer + UBSan                                         |
| `linux-clang-tsan` | Linux · Clang + ThreadSanitizer                                                  |
| `macos`            | macOS · Apple Clang                                                              |

생성기는 **Ninja Multi-Config**, 빌드 폴더는 `build/<프리셋>/`. 빌드·테스트 프리셋은 `<configure>-debug`,
`<configure>-relwithdebinfo`, `<configure>-release`(빌드만)입니다.

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

```bash
cmake --preset linux-clang -D CMAKE_CXX_COMPILER=clang++-19
cmake --build --preset linux-clang-debug && ctest --preset linux-clang-debug
```

실행 파일은 `build/<프리셋>/bin/<Config>/`에 모입니다 (예: `build/windows-msvc/bin/Debug/SandboxServer.exe`).

## 3. 옵션

| 옵션                           | 기본        | 의미                                                                               | 상태                  |
|--------------------------------|-------------|------------------------------------------------------------------------------------|-----------------------|
| `SBX_BUILD_SERVER`             | ON          | SandboxServer                                                                      | 구현                  |
| `SBX_BUILD_TESTS`              | ON          | SandboxTests + CTest 등록                                                          | 구현                  |
| `SBX_BUILD_TOOLS`              | ON          | 개발 도구 `sbx_sim_check` (+ CTest `det_*`) · `sbx_net_probe` (9)                  | 구현 (Phase 3)        |
| `SBX_WARNINGS_AS_ERRORS`       | OFF (CI ON) | 경고 = 에러                                                                        | 구현                  |
| `SBX_BOUNDARY_SELFTEST`        | OFF         | 내부: 경계 검사 실패 경로 시험 (테스트만 켬)                                       | 구현                  |
| `SBX_BUILD_CLIENT`             | ON          | SandboxClient (+ CTest `client_*`, 스위트 client). Platform 은 항상 빌드           | 구현 (Phase 6)        |
| `SBX_BUILD_BENCH`              | OFF         | sbx_bench + CTest `bench_quick` (EnTT 기준선은 `[계획]`)                           | 구현                  |
| `SBX_ENABLE_VULKAN_ON_WINDOWS` | OFF         | Windows 에서 Vulkan 백엔드도 빌드                                                  | `[계획]` Phase 13     |
| `SBX_ENABLE_TRACY`             | OFF         | Tracy 계측                                                                         | `[계획]`              |
| `SBX_BUILD_SHADERS`            | ON(Windows) | HLSL 을 DXC 로 빌드해 SandboxRender 에 내장, 정의 `SBX_HAS_SHADERS`. Python 3 필요 | 구현 (Phase 7B)       |
| `SBX_DXC`                      | (빔)        | dxc 경로. 비면 Windows 는 고정 버전을 `.cache/dxc/` 로 내려받는다                  | 구현 (Phase 7B)       |
| `SBX_SHADER_HOT_RELOAD`        | ON(Debug)   | 런타임 DXC 호출 허용                                                               | `[계획]` Phase 8 이후 |

옵션은 기능이 실제로 생기는 Phase 에 추가합니다. 쓰이지 않는 옵션을 미리 만들지 않습니다.

## 4. 타깃 구조

현재 (Phase 7B):

```text
sbx_warnings              INTERFACE  경고 수준. MSVC: /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor
sbx_strict_conversions    INTERFACE  -Wconversion -Wsign-conversion (Foundation·Core 만)
sbx_simulation_flags      INTERFACE  /fp:precise · -ffp-contract=off -fno-fast-math  (04-DETERMINISM 4.6)
SandboxFoundation         STATIC     include 루트 = 저장소 루트 + build/<preset>/generated. PUBLIC Threads::Threads (JobSystem, 5B)
sbx_nlohmann_json         INTERFACE  external/nlohmann_json (SYSTEM include, JSON_USE_IMPLICIT_CONVERSIONS=0)
sbx_doctest               INTERFACE  external/doctest (SYSTEM include), DOCTEST_CONFIG_USE_STD_HEADERS (5A, MSVC)
SandboxCore               STATIC     PUBLIC Foundation, PUBLIC sbx_nlohmann_json, PUBLIC sbx_simulation_flags
SandboxPlatform           STATIC     PUBLIC Foundation, PRIVATE sbx_nlohmann_json (ActionMap). 항상 빌드 (입력·오디오 단위 테스트)
                                     WIN32: platform/windows/* + user32 imm32, UNICODE, _WIN32_WINNT=0x0A00
                                     그 밖: platform/stub (createWindow = Unsupported, Phase 13·14 전)
SandboxRender             STATIC     PUBLIC Platform, PRIVATE sbx_stb. 항상 빌드 (RHI 순수 로직·이미지 비교 단위 테스트). Core 를 모른다
                                     WIN32: render/dx12/* + d3d12 dxgi dxguid. 그 밖: render/stub (createRenderDevice = Unsupported)
                                     SBX_BUILD_SHADERS: sbx_add_shader 가 만든 generated/render/generated/*Shader.cpp 를 소스에 더하고
                                     PUBLIC SBX_HAS_SHADERS=1 (7B, cmake/SbxShaders.cmake)
sbx_stb_impl              OBJECT     render/asset/StbImpl.cpp — stb 구현 TU (sbx_warnings 밖)
SandboxNetwork            STATIC     PUBLIC Core, PRIVATE sbx_enet (Phase 9). Transport · Protocol · ServerHost · ClientSession
SandboxEditor             STATIC     PUBLIC Core Render, PRIVATE sbx_imgui (Phase 12A). 항상 빌드 (툴 단위 테스트). Network 를 모른다
sbx_enet                  STATIC     external/enet (C — 루트가 C 언어를 켠다). WIN32: ws2_32 winmm. 그 밖: 업스트림과 같은 configure 검사
SandboxServer             EXE        Network Core
SandboxClient             EXE        Editor Network Render Platform Core (SBX_BUILD_CLIENT=ON — 10B: 로컬 · 원격 서버의 복제본). WIN32: GUI 서브시스템 +
                                     /ENTRY:mainCRTStartup (MSVC), SandboxClient.manifest (Per-Monitor DPI v2). 정의 SBX_DEFAULT_CONTENT_DIR ·
                                     SBX_DEFAULT_ASSETS_DIR (저장소의 content/ · assets/). Editor 는 Phase 12
SandboxTests              EXE        Foundation Core Network Platform Render Editor (+ ServerOptions.cpp · SimCheckOptions.cpp · NetProbeOptions.cpp · apps/client 의 Application ·
                                     ClientOptions · DefaultInput 직접 컴파일), sbx_doctest
sbx_sim_check             EXE        Core   (SBX_BUILD_TOOLS=ON)
sbx_net_probe             EXE        Network Core   (SBX_BUILD_TOOLS=ON, Phase 9 — 서버 접속 확인)
sbx_bench                 EXE        Core Network Render   (SBX_BUILD_BENCH=ON 일 때만 — 기본 OFF. Network 은 net.* 벤치, Phase 11)
sbx_render_tests          EXE        Render, sbx_doctest — WIN32 + SBX_BUILD_TESTS 만 (tests/render). CTest render_tests_warp

생성 헤더 build/<preset>/generated/foundation/BuildInfo.hpp
  kVersion*, kGitCommit, kCompilerId, kSystemName,
  kToolchainKey = "<CMAKE_SYSTEM_NAME>-<processor>-<CompilerId>-<major>"  (골든 해시 키, ADR-0012)
    processor 는 소문자, AMD64 → x86_64. MSVC 는 major.minor (19.44). 교차 컴파일에서 비어 있으면 호스트 값.
```

`sbx_simulation_flags`는 Core 가 **PUBLIC**으로 전파합니다. ECS·시스템 코드 상당수가 헤더 템플릿이라 Core 를 쓰는
모든 번역 단위(Network, Server, Tests, Client)에 같은 규칙이 걸려야 하기 때문입니다. Render 는 Core 를 링크하지 않으므로 자유롭습니다.

전역 설정: C++23, 확장 끔, `CMAKE_CXX_SCAN_FOR_MODULES OFF`(모듈 미사용 — 켜 두면 clang-scan-deps 가 없는 환경에서 전부 실패),
`compile_commands.json` 생성.

계획 (Phase 6 이후):

```cmake
add_library(SandboxEditor   STATIC …)   # PUBLIC Core Render, PRIVATE imgui  — Phase 12A 에 생겼다 (위 목록)
add_executable(SandboxClient …)         # Editor Network Render Platform Core
if(WIN32) platform/windows + render/dx12   elseif(APPLE) OBJCXX + platform/macos + render/metal   elseif(UNIX) platform/linux + render/vulkan
```

경계 규칙은 아직 없는 타깃까지 루트 `CMakeLists.txt`에 미리 선언되어 있어, 타깃이 생기는 순간부터 검사됩니다
([01-ARCHITECTURE](01-ARCHITECTURE.md) 2.2).

## 5. 외부 의존성 (버전 고정)

| 라이브러리                             | 용도                 | 대상                        | 방식                                                                             |
|----------------------------------------|----------------------|-----------------------------|----------------------------------------------------------------------------------|
| nlohmann/json                          | 콘텐츠·세이브        | Core (PUBLIC — 헤더 템플릿) | vendored — **v3.12.0** (`external/nlohmann_json/`)                               |
| doctest                                | 테스트               | Tests                       | vendored — **v2.5.0** (`external/doctest/`)                                      |
| ENet                                   | Transport            | Network                     | vendored — **v1.3.18** (`external/enet/`, C, ADR-0024)                           |
| zstd                                   | 청크·스냅샷 압축     | Core/Network                | FetchContent (해시 고정)                                                         |
| Dear ImGui (docking)                   | Editor UI            | Editor/Render               | vendored, 백엔드 파일 미사용                                                     |
| stb_image, stb_truetype, stb_rect_pack | 디코드·폰트·아틀라스 | Render                      | vendored                                                                         |
| miniaudio                              | 오디오               | Platform                    | vendored                                                                         |
| D3D12 Memory Allocator                 | D3D12 메모리         | Render/dx12                 | vendored                                                                         |
| volk, Vulkan-Headers, VMA              | Vulkan               | Render/vulkan               | vendored                                                                         |
| DXC                                    | 셰이더 컴파일러      | 빌드 타임                   | NuGet Microsoft.Direct3D.DXC **1.9.2609.5** (SHA256 고정, 구성 때 `.cache/dxc/`) |
| SPIRV-Cross                            | MSL 변환 (Metal)     | 빌드 타임                   | `[계획]` Phase 14. 리플렉션은 자체 파서 (ADR-0019)                               |
| WinPixEventRuntime                     | PIX 마커             | Render/dx12                 | NuGet 패키지 버전 고정                                                           |
| Tracy                                  | 프로파일러           | 옵션                        | FetchContent                                                                     |
| EnTT                                   | 벤치 기준선만        | sbx_bench                   | FetchContent, `SBX_BUILD_BENCH`                                                  |

현재 vendored 목록과 버전: [external/README.md](../external/README.md).

```text
규칙: 의존성 추가 = ADR 또는 이 표 갱신 + 라이선스 확인(external/<lib>/LICENSE 보존).
      버전 업그레이드는 단독 커밋.
```

## 6. 셰이더 빌드

```cmake
sbx_add_shader(TARGET SandboxRender SOURCE shaders/sprite.hlsl STAGES vs:VSMain ps:PSMain
               OUTPUT_HEADER render/generated/SpriteShader.hpp)
```

플랫폼별 산출물과 명령은 [06-RENDERING](06-RENDERING.md) 6장. 산출물은 `build/<preset>/shaders/`에 생기고 실행 파일 옆으로 복사됩니다.

## 7. 실행

현재 구현 (Phase 1 + 3):

```text
SandboxServer --help | --version | --log-level <trace|debug|info|warn|error|off>
SandboxServer --scenario <random_walk_1k|random_walk_10k> [--ticks N] [--seed N] [--realtime]
    헤드리스로 시나리오를 돌리고 "tick N hash 0x… entities … avg tick … ms" 한 줄을 출력한다.
    --realtime: 30 Hz × speed 로 페이싱, 3틱 넘게 밀리면 기준점을 다시 잡고 overruns 에 센다 (03-SIMULATION 3장).
    네트워크 없이 시나리오만 — 결정론 · 성능 확인용으로 남긴다.
SandboxServer --world <시나리오|세이브 폴더> [--port 7777] [--bind addr] [--max-clients 16] [--default-role editor]
              [--threads N] [--seed N] [--content-root d] [--snapshot-kbps 256] [--ticks N --exit]  (Phase 9 · 10A)
    네트워크 서버: ServerHost (Simulation 스레드 + Net IO 스레드) + ENet UDP. 시작하면 "listening udp *:<port> world …"
    (--port 0 = 빈 포트), 끝나면 "tick N hash 0x… entities … accepted … rejected … overruns … replication X ms"
    (replication = 스냅숏 만들기 평균, Phase 10A). 세이브 폴더는 world.json 의 팩을 읽는다.
    --snapshot-kbps: 클라이언트당 복제 예산 KB/s (기본 256, 0 = 제한 없음 — 08 6.3, ADR-0025). 넘는 엔티티는 다음 스냅숏으로.
    0 은 측정용 — 10k 월드의 스냅숏이 1 MB 를 넘어 UDP 받기 버퍼에서 조각을 잃고 클라이언트가 거의 못 받는다 (10B 측정).
    역할: 일시정지 · 한 틱 · 속도는 admin 부터 (10-EDITOR 7장) — 혼자 시험할 때는 --default-role admin.
    Ctrl+C: 접속자에게 Disconnect{ServerShutdown} 을 보내고 끝낸다. 암호화 없음 — LAN · 신뢰하는 환경 전용 (R6).
    Windows 방화벽이 처음 실행 때 UDP 허용을 묻는다 (같은 PC 안 127.0.0.1 은 묻지 않아도 된다).
종료 코드: 0 정상 · 1 실행 오류 · 2 잘못된 인자 (모르는 월드 포함)

sbx_net_probe [--connect host:port] [--name n] [--content-root d] [--seconds s] [--timeout s]
              [--pause] [--resume] [--step n] [--speed x] [--create x,y]       (Phase 9 — 화면 없는 접속 확인 도구)
    접속 → "접속 client #1 role … world … tick …" → 명령 결과 "결과 #n <명령> 수락 tick … | 거절 <ErrorCode> — 사유" →
    "서버 tick … · 개체 … · 틱 … ms · TPS · 접속 …" → (Phase 10A) "복제 개체 N · 스냅숏 M (다시 맞춤 R) · 지형 청크 C · 받은 X KB"
    → "probe 끝: 명령 N 수락 a 거절 r". 복제 개체 수는 --seconds 동안 따라온 만큼 (예산이 걸리면 큰 월드는 늦게 다 온다).
    콘텐츠가 다르다고 거절되면 서버가 알려 준 팩을 --content-root 에서 읽어 한 번 다시 접속한다.
    종료 코드: 0 · 1 접속 실패/거절 · 2 잘못된 인자 · 3 거절된 명령 있음

sbx_sim_check --help      결정론 하네스. 옵션은 13-TESTING 4장

sbx_bench [--quick] [--out result.json] [--machine name] [--threads n] [--only <이름 접두사>]   (14-PERFORMANCE 2장)
    기본 구성에는 없다: -DSBX_BUILD_BENCH=ON 으로 구성해야 타깃이 생긴다 (CLion 은 프로필의 CMake options 에).
    예: --only net. → net.snapshot (50k, 클라이언트 1 · 4 · 16, 관심 = 월드 전체 · 화면) · net.late_join (Phase 11).
    --quick 은 10k · 클라이언트 4 까지. 숫자는 Release 로

SandboxClient (Phase 6 — 빈 창 + 앱 상태기계, --help 에 전체 목록)
    [--console] [--headless] [--frames N] [--fps HZ] [--width W] [--height H] [--input settings.json] [--log-input]
    [--log-level L] [--version]
    (Phase 7A) [--no-render] [--rhi-debug] [--rhi-gbv] [--rhi-warp] [--rhi-fl11] [--vsync on|off] [--frames-in-flight 2|3]
    렌더러가 있으면 화면을 천천히 색이 바뀌는 어두운 색으로 지우고, 제목 줄 끝에 "D3D12 60 fps VSync 켬" 이 붙는다.
    (Phase 10B) [--world <시나리오|세이브 폴더>] [--connect host:port] [--name <이름>] [--seed N] [--threads N]
                [--content <dir>] [--assets <dir>]       (8A 의 --direct-sim 은 지웠다 — ADR-0026)
    --world: 싱글플레이 — 같은 프로세스의 서버(LocalServerHost: ServerHost + Loopback, 역할 owner)로 시나리오 또는 세이브를
    돌리고 접속한다. 창에서는 서버의 Simulation · Net IO 스레드, --headless 는 프레임 안에서 (틱 수 고정).
    --threads 는 그 Worker 수 (기본 코어 수 - 2 를 1~4, 결과는 같다 — D5).
    --connect: SandboxServer 에 접속 (포트 기본 7777). 콘텐츠가 다르다고 거절되면 서버 팩을 --content 에서 읽어 다시.
    일시정지 · 한 틱 · 속도는 서버 명령 — 원격 서버의 기본 역할 editor 는 거절된다 (서버 --default-role admin).
    접속하고 첫 스냅숏을 받을 때까지 Connecting, 거절 · 끊김이면 오류를 남기고 종료 코드 1.
    (Phase 11) 화면에 보이는 영역 + 1 청크만 받는다 (관심 영역 — 줌 아웃해 월드 전체가 보이면 전부). --connect 에서 네트워크가
    끊기면(시간 초과) 같은 세션 토큰으로 2 초마다 다시 접속하고 60 초 넘으면 끝낸다 — 그동안 마지막 화면이 남고 상태 줄에
    "다시 접속 중 N 초". 서버가 Ctrl+C 로 끝났거나 쫓아냈으면 바로 끝낸다.
    제목 줄에 "<월드> tick · 개체 · TPS · 틱 ms (· RTT)" 와 "fps · 월드 · 추출 · 렌더 ms". 끝날 때
    "world <이름> tick N 개체 M 스냅숏 S (다시 맞춤 R) · 받기 · 적용 X ms" + 프레임 평균 (+ 로컬 서버 틱 · 스냅숏 만들기).
    성능을 볼 때는 RelWithDebInfo/Release 로 (Debug 는 틱 · 적용이 5 ~ 10 배 느리다 — 14-PERFORMANCE 7.7 · 7.10).
    월드 조작: WASD/화살표 이동 · 휠 확대(커서 기준) · 가운데 끌기 · Home 맞춤 · Space 일시정지 · . 한 틱 · = / - 속도
    (8B) 왼쪽 클릭 선택 · 왼쪽 끌기 박스 선택 · Shift 더하기/빼기 · Esc 해제 · G 격자 · V 선택한 개체의 감지 반경 · 경로.
    (12A) 에디터 툴: 1 선택 · 2 이동 · 3 배치 · 4 지형 · 5 지우기 (왼쪽 = 툴, 지형은 오른쪽 = 바탕으로 지우기), Delete = 선택
    지우기. 패널 "편집"(오른쪽 아래): Prefab 목록 · 머티리얼 · 브러시 · 격자 맞춤. 제목 줄에 "툴 …" (10-EDITOR 3장, ADR-0028)
    제목 줄 끝에 "GPU … ms (지형 · 스프라이트 · 격자 · 선)" — 패스별 GPU 시간 (타임스탬프, ADR-0022)
    (Phase 8C) [--font <ttf|ttc>] [--no-ui]
    ImGui 패널 "시뮬레이션" · "통계" (F1 로 숨김). 폰트: --font → Windows 맑은 고딕 → 내장 영문 (ADR-0023).
    (10B) 패널 "네트워크": 서버 · 역할 · RTT · 스냅숏(epoch · 다시 맞춤) · 받은 KB/s · 적용 ms · 보간 지연 · 거절된 명령.
    (11) + "관심 영역" (월드 전체 | 청크 x0..x1 × y0..y1) · "다시 접속" (횟수 | 중…).
    Linux · Wine 시험에서 한글: --font /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc (Wine 은 Z:/usr/…)

sbx_render_tests [--warp] [--debug] [--gbv] [--fl11] [--update-references] [--references <dir>] [--out <dir>] [doctest 옵션]
    기준 이미지 tests/render/references/*.png. 실패하면 --out 에 <name>.actual.png · <name>.diff.png
    Windows 는 GUI 서브시스템 실행 파일이라 PowerShell 에서 실행해도 로그가 안 보인다 → --console (부모 콘솔에 붙는다).
    CLion 실행 창·CTest 처럼 출력이 이미 연결된 곳에서는 그대로 보인다.
    창이 아직 없는 OS(Linux·macOS)에서는 --headless 로만 돈다. 끝날 때 "SandboxClient 끝: frames N state Shutdown".
    창 안 단축키(수동 QA): F2 글자 입력 · F3 마우스 캡처 · F4 커서 모양 · Ctrl+C/V · Esc · Ctrl+Q (qa/MANUAL-QA Phase 6)
종료 코드: 0 정상 · 1 실행 오류(창 생성·설정 파일 · 서버 접속 실패 · 끊김) · 2 잘못된 인자 (없는 월드 포함)

(Phase 5A) 콘텐츠 루트: SandboxServer --content-root · sbx_sim_check --content-root. 생략하면 빌드 때 컴파일된
저장소의 content/ (SBX_DEFAULT_CONTENT_DIR — 개발용 기본값, 배포 경로 정책은 Phase 9)
```

계획:

```text
SandboxServer … [--autosave 300] [--record-replay] [--metrics-csv path]      (Phase 9 에서 나머지는 구현)

SandboxClient (--world · --connect · --name 은 Phase 10B 구현)
              [--rhi dx12|vulkan|metal] [--rhi-debug] [--rhi-gbv] [--rhi-warp] [--rhi-capture N]
              [--frames-in-flight 2|3] [--vsync on|off] [--platform x11|wayland]
```

## 8. CI

[13-TESTING](13-TESTING.md) 8장의 매트릭스. 워크플로: `.github/workflows/ci.yml` — Phase 1 범위(Windows MSVC, Linux clang-19/gcc-13/asan, macOS)로 작성됨.
원격 저장소에 push 하면 첫 실행됩니다 (아직 실행된 적 없음).

```text
- Core·Server·Tests 는 Phase 1 부터 Windows/Linux/macOS 세 OS 에서 빌드.
- 아티팩트: 실패 시 sbx_sim_check 진단 로그, 렌더 diff 이미지.
- 캐시: FetchContent 다운로드, DXC 바이너리.
```

## 9. 알려진 함정

```text
- MinGW 로 구성된 기존 CLion 프로필(RTS)을 재사용하지 말 것. 새 프로필을 Visual Studio 툴체인으로.
  MinGW 로 구성하면 CMake 가 경고를 낸다 (막지는 않는다). Phase 2 까지의 코드는 MinGW GCC 13·15 에서도 빌드된다.
- IDE(CLion)는 Ninja 를 PATH 가 아니라 CMAKE_MAKE_PROGRAM 으로 넘긴다. CMake 를 중첩 호출하는 테스트
  (arch_link_boundary_selftest)는 CMAKE_MAKE_PROGRAM·CMAKE_TOOLCHAIN_FILE 을 그대로 전달해야 한다 (Phase 3 에서 수정).
  같은 이유로 CLion 은 vcvars 환경(LIB·INCLUDE·LIBPATH)을 CTest 에 주지 않아 중첩 구성의 링크가 LNK1104(kernel32.lib)로
  실패했다 → MSVC 면 구성 시점의 값을 테스트 환경으로 넘기고, 그래도 컴파일러를 못 쓰면 SKIP (Phase 5A).
- Windows 콘솔·CTest 의 기본 인코딩은 cp949 다. Python 도구는 출력 인코딩을 UTF-8 로 직접 고정한다 (check_includes.py,
  테스트는 -X utf8 도 붙인다). C++ 도구(sbx_sim_check · SandboxServer)는 시작할 때 콘솔 출력 코드 페이지를 UTF-8 로 바꾼다 (foundation/io/Console, 5B 후속). CTest 로그 파일은 여전히 UTF-8 바이트다.
- doctest 는 MSVC 에서 std 앞선언 때문에 std::string_view 를 문자열화하지 못한다 → sbx_doctest 가
  DOCTEST_CONFIG_USE_STD_HEADERS 를 켠다 (Phase 5A, 사용자 PC 에서 발견).
- Python 3 이 없으면 arch_include_lint 가 등록되지 않는다 (구성 경고). Windows 에서는 python.org 설치본을 PATH 에 두면 잡힌다.
- 골든 해시는 툴체인별이다. 새 컴파일러에서 det_*_golden 이 SKIP 이면 tests/golden/ 의 각 파일에 대해
  sbx_sim_check --record-golden tests/golden/<이름>.json 으로 기록해 커밋한다 (ADR-0012).
  simVersion 이 오른 커밋 뒤에는 기존 항목도 다시 기록해야 한다 (Phase 4 에서 1 → 2).
  지금 골든 4개: random_walk_1k · world_save_load · eco_lifecycle · ecosystem_small (kSimVersion 4).
  상대 경로는 현재 폴더 기준이다 — 저장소 루트에서 실행한다 (예: .\cmake-build-debug\bin\sbx_sim_check.exe --record-golden tests\golden\eco_lifecycle.json).
  골든 파일이 없으면 --record-golden 은 --scenario 없이 실행을 거절한다 (기본 시나리오를 엉뚱한 파일에 기록하지 않게).
- tests/data/saves/ 의 청크 파일(*.chunk)은 바이너리다 (.gitattributes). 샘플 세이브는 고치지 않는다 (09 3.5).
- GCC 13 -O2 의 -Warray-bounds 는 인라인 버퍼(SmallVector)에 대한 memmove 를 오진한다. 우회는 SmallVector::relocate 의 명시적 루프.
- Windows 경로 길이: 저장소를 짧은 경로(D:\Game\Sandbox)에 둔다. FetchContent 깊은 경로 주의.
- sbx_simulation_flags 는 Core 가 PUBLIC 으로 전파한다(4장). Render 는 Core 를 모르므로 /fp:fast 허용.
- MSVC 는 /utf-8 이 필수다. 소스에 한국어 주석이 있어 빠지면 C4819 와 잘못된 컴파일이 난다 (sbx_warnings 가 넣는다).
- (Phase 5B) MinGW 교차 빌드는 std::thread 때문에 posix 스레드 모델 컴파일러(x86_64-w64-mingw32-g++-posix)를 쓴다.
- 메모리가 작은 머신(8 GB 이하)에서는 -j 를 코어 수 이하로. nlohmann/json 을 쓰는 번역 단위는 컴파일러 하나가 0.5 GB 안팎을
  쓴다 (클라우드 2코어 컨테이너에서 -j16 이 OOM 으로 실패한 적이 있다).
- Ubuntu 24.04 의 기본 clang++ 는 18 이다. linux-clang 프리셋에는 -D CMAKE_CXX_COMPILER=clang++-19 를 붙인다.
- 열거형 → 문자열 함수를 toString 이라 부르지 않는다 (doctest 가 ADL 로 잡아 컴파일 오류). 12-CODING-STANDARDS 2장.
- 셰이더 산출물을 저장소에 커밋하지 않는다.
- (Phase 7B) Windows 의 첫 구성은 DXC 패키지(약 53 MB)를 내려받는다 — 인터넷이 필요하다. 이후에는 `<저장소>/.cache/dxc/`
  를 프리셋끼리 공유한다. 오프라인이면 -D SBX_DXC=<dxc.exe> (Windows SDK 의 bin\<버전>\x64\dxc.exe 도 된다 — 단 같은 폴더에
  dxil.dll 이 있어야 한다. 없으면 DXIL 이 서명되지 않아 생성기가 빌드를 멈춘다. 서명 없는 DXIL 은 실제 D3D12 가 거부한다).
- (Phase 7B) SBX_BUILD_SHADERS=ON 이면 Python 3 이 필수다 (없으면 구성 오류). 셰이더 없이 빌드하려면 -D SBX_BUILD_SHADERS=OFF
  — 그러면 sbx_render_tests 의 그리기 케이스와 SandboxClient 의 삼각형이 빠진다.
- (Phase 7B) 클라우드에서 DXIL 그리기 시험: Ubuntu 24.04 의 wine 9.0(vkd3d 1.10)은 DXIL 을 컴파일하지 못한다
  ("vkd3d result -4"). WineHQ 의 wine-devel(11.19 에서 확인)을 dpkg -x 로 풀고 tools/wine/run.sh 에 WINE=<풀어 둔 곳>/opt/wine-devel/bin/wine
  을 준다. 새 Wine 은 접두사를 따로 둔다 (WINEPREFIX). 셰이더 컴파일(tools/wine/dxc.sh 로 Windows dxc.exe)은 wine 9 로도 된다.
- (Phase 6) SandboxClient 는 Windows GUI 서브시스템이다. PowerShell 에서 그냥 실행하면 로그가 보이지 않는다 → --console.
  MSVC 는 /ENTRY:mainCRTStartup 으로 main 을 진입점으로 쓴다. Windows CRT 는 파이프로 넘긴 stderr 를 버퍼링해 강제 종료 때
  마지막 로그를 잃었다 → 기본 로그 싱크가 줄마다 flush 한다.
- (Phase 7A) D3D12 Debug Layer(--rhi-debug, sbx_render_tests --debug)는 Windows 선택적 기능 "그래픽 도구" 가 있어야 켜진다.
  없으면 경고만 하고 Debug Layer 없이 돈다 (render_tests_warp 도 통과하지만 Debug Layer 검사는 빠진다).
- (Phase 7A) 클라우드의 D3D12 시험: MinGW 교차 빌드 → tools/wine/run.sh (Wine d3d12 = vkd3d → lavapipe). vkd3d 는 FL 11_1 까지라
  --fl11 / --rhi-fl11 이 필요하고, lavapipe 의 viewportSubPixelBits 를 올리는 시험용 Vulkan 레이어를 함께 쓴다 (ADR-0018).
  Wine 이 보고하는 어댑터 이름(예: "NVIDIA GeForce GTX 470")은 가짜다.
- (Phase 6) Win32 코드는 클라우드에서 MinGW 교차 빌드 + Wine(Xvfb) + xdotool 로 실행 시험을 한다. Wine 은 Per-Monitor DPI 를
  지원하지 않아 "DPI 를 켜지 못했습니다" 경고가 나온다 — 실제 Windows 에서는 나오지 않아야 한다.
```
