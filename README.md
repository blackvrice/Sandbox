# Sandbox

C++23 로 처음부터 만드는 **Sandbox Simulation Engine · 게임 제작 플랫폼**입니다.

월드를 만들고 → 지형을 칠하고 → 개체를 놓고 → 컴포넌트 · 행동 · 규칙 · 트리거 · 스크립트를 정의하고 → 시뮬레이션을 실행 ·
관찰 · 수정 · 저장하고 → 여러 사람이 같은 월드에 접속해 함께 편집 · 플레이합니다. 생태계 · 콜로니 · 도시 · 전투 같은 시뮬레이션을
특정 장르 규칙에 묶이지 않고 만들고, 그것을 게임으로 묶어 배포하는 것이 목표입니다.

## 지금 상태 (2026-10-26)

| 버전 | 내용                                                                                       | 상태                                                     |
|------|--------------------------------------------------------------------------------------------|----------------------------------------------------------|
| v0.1 | Engine Foundation — ECS · 30 TPS 결정론 시뮬레이션 · 청크 월드 · 서버 권위 네트워크 · 멀티플레이 에디터 · DX12 / Vulkan / Metal | 구현 끝 (Phase 0~15) — 실기 · CI · macOS 검증 대기         |
| v0.2 | Game Creator — 게임 프로젝트 · GameMode · 플레이어 행동 · Visual Trigger · 테스트 플레이 · 게임 패키지 | ✅ 0.2.0 ([릴리스 노트](docs/releases/v0.2.0.md))          |
| v0.3 | SandboxScript — TypeScript 부분집합 · 자체 컴파일러 · 바이트코드 VM · 스크립트 컴포넌트 · 시스템 | ✅ 0.3.0 ([릴리스 노트](docs/releases/v0.3.0.md)) |
| v0.4 | Package Manager — 콘텐츠 팩 = 패키지 · 로컬 · Git · 캐시 소스 · 잠금 파일 · 트랜잭션 설치 · 팩 사이 스크립트 import · 라이선스 요약 | ✅ 0.4.0 ([릴리스 노트](docs/releases/v0.4.0.md)) |
| v0.5~ | Creator Alpha · Hub · 방 · 마켓 · Business · GA                                            | [계획] ([18-PLATFORM](docs/18-PLATFORM.md))               |

지금 무엇을 하고 다음에 무엇을 하는지는 **[docs/18-PLATFORM.md](docs/18-PLATFORM.md) 0장**에 있습니다 (Phase 0~15 이력은
[docs/16-ROADMAP.md](docs/16-ROADMAP.md)). 구현되지 않은 것은 문서마다 `[계획]`으로 표시합니다.

## 핵심 기술

```text
엔진        Custom ECS (Sparse Set) · 30 TPS 고정 틱 · 결정론 (WorldHash · 골든 · 리플레이 · 세이브 왕복)
            청크 월드 · 공간 색인 · Behavior FSM · Rule · Job System · 비동기 A*
네트워크    서버 권위 · 클라이언트별 차분 스냅숏 · Interest · 역할 · 멀티플레이 에디터 (싱글플레이도 같은 길 — Loopback)
렌더 · OS   자체 RHI — D3D12 (Windows) · Vulkan (Linux · Windows) · Metal (macOS), HLSL → DXIL / SPIR-V / MSL, Dear ImGui
            Win32 · X11 · Wayland · Cocoa 창, 헤드리스 전용 서버
게임 제작   게임 프로젝트 · GameMode · 점수 · 팀 · 플레이어 행동 · Visual Trigger · 편집 ↔ 테스트 플레이 · .sbpkg 패키지 · 자동 플레이
스크립트    SandboxScript (TS 부분집합) → 자체 검사기 · 코드 생성 → 검증한 바이트코드 → VM (연료 · 메모리 예산, GC 없음)
            defineComponent (세이브 · 복제 · Inspector) · defineSystem (onTick · onStart · onEvent · onStop) · 실행 중 편집
```

## 구조 한눈에

```text
SandboxClient ── Editor · Presentation · Renderer ── RHI ─┬─ D3D12  (Windows)
     │                                                     ├─ Vulkan (Linux · Windows)
     │ Command ↑   ↓ Snapshot                              └─ Metal  (macOS)
SandboxServer ── Authoritative SimulationWorld (SandboxCore ← SandboxScript) ── 창 · GPU 없이 실행
sbx           ── 게임 프로젝트 만들기 · 검사 · 패키지 · SDK
```

싱글플레이도 같은 프로세스 안에서 서버를 띄우고 Loopback 으로 연결합니다. 코드 경로는 하나입니다.

## 빌드

Windows (Visual Studio 2022 Build Tools — MSVC. MinGW 는 지원하지 않습니다):

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug -E render_tests_warp
```

Linux (Clang 19+ 또는 GCC 13+):

```bash
cmake --preset linux-clang -D CMAKE_CXX_COMPILER=clang++-19
cmake --build --preset linux-clang-debug && ctest --preset linux-clang-debug
```

Windows 첫 구성은 셰이더 컴파일러 DXC (NuGet, 약 53 MB) 를 `.cache\dxc\` 로 한 번 내려받고, 셰이더 코드 생성에 Python 3 을
씁니다. 툴체인 · 옵션 · CI 는 [docs/15-BUILD.md](docs/15-BUILD.md).

## 해 보기

실행 파일은 `build\windows-msvc\bin\Debug\` 에 있습니다. 옵션 전체는 [15-BUILD](docs/15-BUILD.md) 7장.

```text
SandboxClient --world ecosystem_survival                 싱글플레이 생태계 (WASD · 휠 · Space · 클릭 선택 · 1~5 편집 툴)
SandboxClient --world samples/rabbit_guard               v0.2 데모 미니게임 "토끼 지키기" ("게임" 패널의 시작 → Q · E)
SandboxClient --world samples/mana_regen                 v0.3 스크립트 데모 "마나 재생" (토끼 위 마나 · R 기운 주기 · F1 프로파일)
sbx install samples/stash && SandboxClient --world samples/stash   v0.4 패키지 데모 "보관함" (설치한 inventory 패키지를 쓰는 게임)
SandboxServer --world ecosystem_survival                 헤드리스 전용 서버
SandboxClient --connect 127.0.0.1:7777 --name 철수       서버 접속 (함께 편집)

sbx new MyGame --id studio.mygame --from ecosystem_small 새 게임 프로젝트 (sdk/sandbox.d.ts · tsconfig.json 포함)
sbx check MyGame                                         프로젝트 검사 (콘텐츠 · 스크립트 진단 · 시작 월드)
sbx build MyGame                                         게임 패키지 (.sbpkg)
sbx_sim_check --repeat 2 --save-at 250                   결정론 확인 (재현 · 세이브 왕복)
```

스크립트는 팩의 `scripts/*.ts` 에 씁니다 ([docs/19-SANDBOXSCRIPT.md](docs/19-SANDBOXSCRIPT.md)):

```ts
import { defineComponent, defineSystem, field } from "sandbox";
export const Mana = defineComponent({ id: "rpg.mana", fields: { current: field.f32(0, { min: 0, max: 100 }) } });
export default defineSystem({
  id: "rpg.mana.regen",
  intervalTicks: 30,
  onTick(ctx) {
    for (const e of ctx.query(Mana)) {
      const m = ctx.read(e, Mana);
      if (m !== null) { m.current = Math.min(m.current + 1, 100); ctx.write(e, Mana, m); }
    }
  },
});
```

## 문서

**처음이라면 [docs/README.md](docs/README.md)부터 읽으십시오.** 모든 문서의 지도와 읽는 순서가 있습니다.
AI 에이전트는 [AGENTS.md](AGENTS.md)를 먼저 따릅니다.

| 알고 싶은 것               | 문서                                                               |
|----------------------------|--------------------------------------------------------------------|
| 무엇을 왜 만드나           | [00-OVERVIEW](docs/00-OVERVIEW.md)                                 |
| 전체 구조와 의존성         | [01-ARCHITECTURE](docs/01-ARCHITECTURE.md)                         |
| 지금 상태 · 다음 할 일     | [18-PLATFORM](docs/18-PLATFORM.md)                                 |
| 코드가 어디 있나           | [17-SOURCE-MAP](docs/17-SOURCE-MAP.md)                             |
| 스크립트 언어              | [19-SANDBOXSCRIPT](docs/19-SANDBOXSCRIPT.md)                       |
| 빌드 · 실행 옵션           | [15-BUILD](docs/15-BUILD.md)                                       |
| 작업 이력                  | [DEVELOPMENT_LOG](DEVELOPMENT_LOG.md)                              |

## 기원

이 프로젝트는 [blackvrice/RTS](https://github.com/blackvrice/RTS)를 **기술 참고자료**로 삼아
처음부터 다시 설계했습니다. RTS 코드를 리팩터링하거나 복사하지 않습니다.
가져온 아이디어와 버린 설계는 [design/SANDBOX_ARCHITECTURE](docs/design/SANDBOX_ARCHITECTURE.md) 2~3장에 있습니다.
