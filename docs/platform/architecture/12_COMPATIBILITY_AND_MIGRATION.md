# 13. 버전·호환성·마이그레이션 정책

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 버전 번호는 여러 축
| 구분 | 목적 | 호환성 규칙 |
| --- | --- | --- |
| Product Version | Sandbox 앱 릴리스 | UI/기능 마일스톤 |
| Engine Version | C++ Native ECS/Render/Server | 바이너리·기능 호환 |
| Script API Version | `ctx`, SDK, Native host ABI | 강제된 API 버전 범위 |
| Bytecode Version | `.sbc` Instruction Set | Verifier/Loader 엄격 검사 |
| Package Format Version | `.sbpkg` manifest/asset | Loader 마이그레이션 또는 거부 |
| Component Schema Version | 사용자 데이터 | Save/Network migration |
| Network Protocol Version | 클라이언트-서버 wire 계약 | handshake 및 호환 window |
| Simulation Version | 결정론 로직/순서 | Replay/WorldHash 기준 동결 |
| Hub API Version | 서비스 제어면 | 버전 경로와 최소 지원 범위 |

## 2. SemVer 정책
- Package·GameVersion은 기본 SemVer (`major.minor.patch`)를 따르되 엔진 호환 범위를 명시.
- Breaking API/schema 변경은 Major, 기능 추가는 Minor, 버그 수정은 Patch. 단, Simulation 동작 변화는 패치라도 해시/재생 경고와 별도 version bump 필요.
- `sandbox.lock`에는 정확 버전·digest를 고정. 개발자는 명시적으로 업데이트.

## 3. 호환성 매트릭스 예시
```text
GamePackage 0.7.2
  engineRange: >=0.7 <0.9
  scriptApi: 1
  bytecode: 2
  netProtocolMin: 8
  componentSchemas: [core.transform@1, rpg.mana@2]
```
숫자는 예시; 현행 Protocol 번호와 제품 버전은 1:1 대응시키지 않는다.

## 4. 단계적 변경 절차
1. Deprecation 경고와 Migration Guide.
2. Compat Adapter와 자동 변환 가능 여부 검사.
3. 신규 GamePackage 빌드·TestWorld·Save migration dry-run.
4. 별도 Staging 서버/채널에서 정합성·Replay 확인.
5. Release + 기존 롤백 세이브·패키지 확보.
6. 서버와 클라이언트의 실제 지원 버전을 확인한 뒤 강제 전환.

## 5. 플랫폼 별 고려
Bytecode 배포는 CPU 독립, Native compiled cache는 CPU/OS/compiler-specific. Windows DX12 / Linux Vulkan / macOS Metal은 Renderer 백엔드가 다를 수 있지만 게임 규칙과 패키지 계약은 공통을 지향.

## 6. 검증 기준
구버전 패키지 로딩/거부 메시지 정확, 자동 변환 후 Save/WorldHash 보존, Lockfile Reproducibility, 서버/클라 Protocol Negotiation, Bytecode ABI mismatch 차단.
