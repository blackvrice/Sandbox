# Sandbox

C++23 로 처음부터 만드는 **Sandbox Simulation Engine · 게임 제작 플랫폼**입니다.

월드를 만들고 → 지형을 칠하고 → 개체를 놓고 → 컴포넌트 · 행동 · 규칙 · 트리거 · 스크립트를 정의하고 → 시뮬레이션을 실행 ·
관찰 · 수정 · 저장하고 → 여러 사람이 같은 월드에 접속해 함께 편집 · 플레이합니다. 생태계 · 콜로니 · 도시 · 전투 같은 시뮬레이션을
특정 장르 규칙에 묶이지 않고 만들고, 그것을 게임으로 묶어 배포하는 것이 목표입니다.

## 지금 상태 (2026-11-12)

제품 버전 **0.7.0**. v0.8 (Studio) · v0.9 (Script IDE) 는 코드와 자동 시험이 끝났고 창 확인 (수동 QA) 뒤 출시합니다. 지금 작업은 **v0.10**.
버전은 식별자입니다 — v0.10 은 v0.9 다음입니다.

| 버전 | 내용 | 상태 |
|------|------|------|
| v0.1 | Engine Foundation — ECS · 30 TPS 결정론 시뮬레이션 · 청크 월드 · 서버 권위 네트워크 · 멀티플레이 에디터 · DX12 / Vulkan / Metal | 🟡 구현 끝 — Linux · macOS 실기 · 50k 성능 판정 대기 |
| v0.2 | Game Creator — 게임 프로젝트 · GameMode · 플레이어 행동 · Visual Trigger · 테스트 플레이 · 게임 패키지 | ✅ 0.2.0 ([릴리스 노트](docs/releases/v0.2.0.md)) |
| v0.3 | SandboxScript — TypeScript 부분집합 · 자체 컴파일러 · 바이트코드 VM · 스크립트 컴포넌트 · 시스템 | ✅ 0.3.0 ([릴리스 노트](docs/releases/v0.3.0.md)) |
| v0.4 | Package Manager — 로컬 · Git 소스 · 잠금 파일 · 트랜잭션 설치 · 팩 사이 스크립트 import | ✅ 0.4.0 ([릴리스 노트](docs/releases/v0.4.0.md)) |
| v0.5 | Technical Alpha — 게임플레이 API · 표준 라이브러리 · 충돌 보고 · 진단 · 배포 묶음 | ✅ 0.5.0 ([릴리스 노트](docs/releases/v0.5.0.md)) |
| v0.6 | 에셋 · 패키지 · SaveGame — 프로젝트 PNG (안정 id) · 패키지 형식 3 · 세이브 마이그레이션 | ✅ 0.6.0 ([릴리스 노트](docs/releases/v0.6.0.md)) |
| v0.7 | 게임 UI Runtime · 다국어 — 선언 HUD · 메뉴 · 버튼 → 서버 신호 · 문자열 표 · 입력 컨트롤 | ✅ 0.7.0 ([릴리스 노트](docs/releases/v0.7.0.md)) |
| v0.8 | Studio 기본 환경 — 홈 · 빌드 화면 · 자동 저장 · 테스트 세션 · 리플레이 · Player | 🟡 구현 끝 — 창 QA 대기 ([체크리스트](docs/roadmap/versions/v0.8.md)) |
| v0.9 | Script IDE — 편집기 · 자동 완성 · 재생 디버거 · 정의 · 이름 바꾸기 | 🟡 태스크 끝 — 창 QA 대기 ([체크리스트](docs/roadmap/versions/v0.9.md)) |
| v0.10 | Creator Foundation — Asset Browser · UI 디자이너 · 템플릿 · 타이머 · client 수명주기 | 🟡 진행 중 ([체크리스트](docs/roadmap/versions/v0.10.md)) |
| v0.11 ~ v1.0 | 물리 단계 · 고급 제작 도구 · Closed Creator Alpha (v0.13) · 계정 · 멀티플레이 · 안정화 · RC · GA | ⬜ 계획 ([전체 로드맵](docs/roadmap/MASTER_ROADMAP.md)) |

진행률 · 남은 작업 · 막힌 것은 **[현황 대시보드](docs/overview/CURRENT_STATUS.md)** 에 있습니다 (숫자는 태스크 등록부에서 자동 집계).

## 문서 따라가기

```text
README → 현황 대시보드 → 전체 로드맵 → 버전별 체크리스트 → 기능별 설계
```

| 알고 싶은 것 | 문서 |
|---|---|
| 지금 어디까지 왔나 · 무엇이 막혔나 | [현황 대시보드](docs/overview/CURRENT_STATUS.md) |
| 무엇이 실제로 되고 무엇이 안 되나 | [기능 매트릭스](docs/overview/FEATURE_MATRIX.md) |
| 버전마다 무엇을 하나 · 얼마나 됐나 | [전체 로드맵](docs/roadmap/MASTER_ROADMAP.md) → [버전별 체크리스트](docs/roadmap/versions/) |
| 모듈마다의 역할 · 상태 · 설계 문서 | [모듈 안내](docs/overview/MODULES.md) |
| 모든 문서의 목차 | [docs/README](docs/README.md) |

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

**처음이라면 위 "문서 따라가기" 의 순서로, 그다음 [docs/README.md](docs/README.md) (전체 목차) 를 읽으십시오.**
AI 에이전트는 [AGENTS.md](AGENTS.md)를 먼저 따릅니다. 구현되지 않은 것은 문서마다 `[계획]`으로 표시합니다.

| 알고 싶은 것               | 문서                                                               |
|----------------------------|--------------------------------------------------------------------|
| 무엇을 왜 만드나           | [00-OVERVIEW](docs/00-OVERVIEW.md)                                 |
| 전체 구조와 의존성         | [01-ARCHITECTURE](docs/01-ARCHITECTURE.md)                         |
| 지금 태스크 · 다음 태스크  | [roadmap/README](docs/roadmap/README.md) 1장 · [18-PLATFORM](docs/18-PLATFORM.md) 0장 |
| 코드가 어디 있나           | [17-SOURCE-MAP](docs/17-SOURCE-MAP.md)                             |
| 스크립트 언어              | [19-SANDBOXSCRIPT](docs/19-SANDBOXSCRIPT.md)                       |
| 빌드 · 실행 옵션           | [15-BUILD](docs/15-BUILD.md)                                       |
| 문서를 어떻게 고치나       | [문서 가이드](docs/development/DOCUMENTATION_GUIDE.md)             |
| 작업 이력                  | [DEVELOPMENT_LOG](DEVELOPMENT_LOG.md)                              |

## 기원

이 프로젝트는 [blackvrice/RTS](https://github.com/blackvrice/RTS)를 **기술 참고자료**로 삼아
처음부터 다시 설계했습니다. RTS 코드를 리팩터링하거나 복사하지 않습니다.
가져온 아이디어와 버린 설계는 [design/SANDBOX_ARCHITECTURE](docs/design/SANDBOX_ARCHITECTURE.md) 2~3장에 있습니다.
