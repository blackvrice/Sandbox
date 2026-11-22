# Sandbox Graphics SDK 기반 RHI 리팩토링 문서 지도

이 디렉터리(`docs/graphics-sdk-refactor/`)는 Sandbox의 자체 그래픽 백엔드(`render/dx12/`, `render/vulkan/`, `render/metal/`)를 독립적인 **Graphics SDK**(`GraphicsSDK::Core`, 네임스페이스 `gfx`) 기반으로 전환하기 위한 설계, 호환성 분석, 로드맵, 벤치마크, 검증 및 인수인계 문서의 단일 진실원(Single Source of Truth)입니다.

---

## 1. 작업 공간 및 고정 버전

| 구분 | 경로 / 식별자 | 브랜치 / 고정 SHA | 역할 |
|---|---|---|---|
| **Sandbox (원본)** | `D:/Game/Sandbox` | `master` (`189ac37`) | 기존 Sandbox 기능 개발 (변경 금지) |
| **Sandbox-GraphicsSDK** | `D:/Game/Sandbox-GraphicsSDK` | `refactor/graphics-sdk` | Graphics SDK 기반 RHI 리팩토링 전용 Worktree |
| **GraphicsSDK** | `D:/source/GraphicsSDK` | `main` (`9345c1816879c63f8c26f13fc876382b0c2ed054`) | 독립 그래픽 SDK (`GraphicsSDK::Core`, C++23) |
| **GraphicsUI** | `D:/source/GraphicsUI` | `main` | 독립 Graphics UI 프레임워크 (Phase 8 연동 대상) |

---

## 2. 문서 목록

| 문서 | 설명 |
|---|---|
| [AI_HANDOFF.md](AI_HANDOFF.md) | 다음 AI/개발자가 5분 안에 현재 진행 상태와 재개 지점을 파악하기 위한 인수인계서 |
| [AI_WORK_LOG.md](AI_WORK_LOG.md) | 세션별 작업 내용, 변경 파일, 실행 명령, 테스트 결과 및 남은 작업 기록 |
| [DEVELOPMENT_DASHBOARD.md](DEVELOPMENT_DASHBOARD.md) | 전체 진행률, Phase별 상태, 백엔드/플랫폼 검증 현황, 성능 요약 대시보드 |
| [REFACTORING_ROADMAP.md](REFACTORING_ROADMAP.md) | `R0.1` ~ `R0.35` 세부 버전 및 하위 단계(`R0.x.1` ~ `R0.x.3`) 로드맵 |
| [VERSION_HISTORY.md](VERSION_HISTORY.md) | 완료된 리팩토링 버전(`R0.1` ~ `R0.11`) 이력 및 검증 증적 |
| [ARCHITECTURE.md](ARCHITECTURE.md) | 현재 구조, 전환기 공존 구조(`SBX_USE_GRAPHICS_SDK`), 최종 목표 아키텍처 및 동기화 모델 |
| [API_COMPATIBILITY.md](API_COMPATIBILITY.md) | Sandbox RHI 공개 인터페이스와 `GraphicsSDK::Core` 간의 1:1 호환성 매트릭스 |
| [FEATURE_MATRIX.md](FEATURE_MATRIX.md) | 기능별 Legacy 백엔드 vs Graphics SDK Adapter 지원 및 검증 상태 매트릭스 |
| [SDK_REQUIREMENTS.md](SDK_REQUIREMENTS.md) | Sandbox 전체 렌더 패스 전환을 위해 식별된 Graphics SDK 기능 확장 요구사항 (`SDK-REQ-001` ~ `SDK-REQ-008`) |
| [TEST_PLAN.md](TEST_PLAN.md) | RHI 단위 테스트, GPU 골든 이미지 테스트, 헤드리스/창 스모크, 결정론 보존 테스트 계획 |
| [TEST_REPORT.md](TEST_REPORT.md) | 실제 실행된 테스트 명령, 환경, 통과/실패 결과 및 로그 요약 보고서 |
| [BENCHMARK_BASELINE.md](BENCHMARK_BASELINE.md) | Phase 0 기존 Legacy 백엔드(D3D12 WARP, D3D12 GTX 1660 SUPER, Vulkan GTX 1660 SUPER) 실측 기준선 |
| [BENCHMARK_COMPARISON.md](BENCHMARK_COMPARISON.md) | Legacy 백엔드 대비 Graphics SDK 어댑터의 단계별 성능 비교표 |
| [KNOWN_ISSUES.md](KNOWN_ISSUES.md) | 현재 알려진 제약, 드라이버/플랫폼 특이사항 및 우회 방법 |
| [PLATFORM_SUPPORT.md](PLATFORM_SUPPORT.md) | Windows(D3D12/Vulkan), Linux(Vulkan), macOS(Metal) 플랫폼별 지원 및 실측 현황 |
| [MERGE_PLAN.md](MERGE_PLAN.md) | 기존 개발 브랜치(`master`)와의 비침투적 동기화 및 단계적 통합 계획 |
| [phases/PHASE0_ANALYSIS.md](phases/PHASE0_ANALYSIS.md) | Phase 0(`R0.1` ~ `R0.2`) 기존 그래픽 시스템 정밀 분석 및 기준선 보고서 |
| [phases/PHASE1_ADAPTER.md](phases/PHASE1_ADAPTER.md) | Phase 1(`R0.3` ~ `R0.11`) CMake 패키지 연동 및 `render/graphics_sdk/` 어댑터 구현 보고서 |

---

## 3. 상위 Sandbox 문서 연결

- 전체 문서 지도: [../README.md](../README.md)
- 아키텍처 및 경계 규칙: [../01-ARCHITECTURE.md](../01-ARCHITECTURE.md)
- 결정론 계약: [../04-DETERMINISM.md](../04-DETERMINISM.md)
- 렌더링 아키텍처: [../06-RENDERING.md](../06-RENDERING.md)
- 소스 트리 지도: [../17-SOURCE-MAP.md](../17-SOURCE-MAP.md)
