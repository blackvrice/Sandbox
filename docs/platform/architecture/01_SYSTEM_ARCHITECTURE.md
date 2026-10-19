# 02. 전체 시스템 및 모듈 아키텍처

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 현행 기반 [현행]
2026-10-13 GitHub 기준: SandboxFoundation → SandboxCore(ECS/Simulation) → SandboxNetwork, 별도의 SandboxRender 및 SandboxEditor, 헤드리스 SandboxServer, SandboxClient가 있다. `ComponentCatalog`/Reflection, Sparse Set, 30 TPS, 서버 권위 `SimCommand`, WorldHash/Replay, ENet 및 Delta Replication, Phase 12 멀티플레이 Editor, Windows DX12, 초기 Vulkan RHI가 존재한다. **SandboxScript, Hub, 범용 패키지 Registry, 상용 호스팅은 미구현 설계 대상**이다. 현재 문서 `docs/00-OVERVIEW.md`의 초기 비목표(스크립트, UGC 공유 등)는 제품 범위 변경 ADR로 갱신해야 한다.

## 2. 논리 서비스 구성
```text
[SandboxClient / Editor] ─────── [Hub Web/API]
  Renderer · Game UI              Account · Repo · Package · Marketplace
        │                                   │
  GamePackage Loader                     Metadata DB / Object Storage
        │                                   │
        └── [Directory/Match/Session Service] ── Hosting Controller
                           │                       │
                     ┌─────┴─────┐              Worker Nodes
                     │           │                  │
                Self-host    Managed Host ──── SandboxServer
                     └───────────┴── Gameplay UDP ── Client
```
- **Control Plane**(Hub/Cloud): 계정, 서버 검색, 게임 패키지, 결제, 방/세션 배정. HTTP/TLS·DB와 작업 큐.
- **Data Plane**(SandboxServer): 실시간 ECS 시뮬레이션, 스냅샷 복제, 게임 이벤트. 제어면 장애 시 이미 시작한 세션은 정책에 따라 지속.
- **Authoring Plane**(Editor/CI): 프로젝트, Package Manager, Script Compiler, 로컬 테스트, 빌드/배포.

## 3. C++ 의존성 원칙
```text
SandboxFoundation (type, allocation, job, error)
   ├─ SandboxCore (ECS, simulation, component reflection)
   ├─ SandboxScriptRuntime (bytecode verifier / VM; Core 의존 금지)
   └─ SandboxScriptCompilerTools (빌드/에디터 프로세스)
SandboxScriptBindings → SandboxCore + SandboxScriptRuntime
SandboxNetwork → SandboxCore
SandboxServer → Core + Network + ScriptBindings
SandboxClient → Render + Network + ScriptBindings(client domain)
```
- 기존 CMake 모듈 경계 검사를 유지한다. ScriptVM에 Core 포인터를 직접 넣지 않고 Host Function 인터페이스로 역전.
- SandboxRender는 계속 ECS 타입에 직접 의존하지 않는다. Render Extraction으로 RenderWorld 구성.
- Editor 로직은 Network 직접 참조 없이 기존 CommandSink 계약을 유지한다.

## 4. 저장 모델 / 식별자
| 항목 | 목적 | 안정 식별자 |
| --- | --- | --- |
| GameProject | 편집 원본과 개발 설정 | projectId |
| GamePackage | 실행/배포 스냅샷 | gameId + semver + contentHash |
| World | 게임 맵/월드 정의 | worldId + schemaVersion |
| SaveGame | 실행 상태 스냅샷 | saveId + worldVersion |
| Component/Message | 컴파일·런타임 연계 | namespaced stableId + version |
| Server/Room | 실행 서버/실제 플레이 세션 | serverId / roomId / sessionId |
| Repository/Package | 개발자 소유 저장소/재사용 모듈 | owner/name + version + digest |

## 5. 데이터 흐름과 경계
1. Editor가 GameProject를 변경하고 검증한다.
2. Script Compiler가 TypeScript 부분집합을 Typed IR → `.sbc` Bytecode로 컴파일한다.
3. Package Builder가 선언·메타데이터·리소스·해시·잠금 파일을 고정한다.
4. GamePackage는 Self-host 또는 공식 SandboxServer에서 검증 후 로드한다.
5. Directory는 접속 위치와 권한 토큰만 제공하고 Gameplay 트래픽은 서버로 직접 전송한다(릴레이는 예외).
6. 서버 ECS 변경은 기존 명령과 System/ECB 규칙을 따르며, 클라이언트는 복제 결과를 렌더링한다.

## 6. 비기능 요구
- 결정론 재현 / Replay / WorldHash 테스트는 Script 도입 후에도 유지.
- 공개 인터넷 서비스는 ENet 평문 그대로 배포하지 않는다. 인증·암호화·속도 제한이 출시 조건.
- 모든 패키지에 버전과 서명/해시, 장애 복구 및 롤백 가능한 릴리스 모델 적용.
- 운영 서비스 데이터는 프로젝트/조직 단위 권한, 감사 로그, 백업 정책 필요.

## 7. 변경 관리
신규 모듈은 설계 ADR → 스키마/API 계약 → 통합 테스트 → 작은 단위 CTest → 성능 비교 → 문서 갱신 순으로 반영한다.
