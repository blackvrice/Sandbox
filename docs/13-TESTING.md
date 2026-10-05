# 13. 테스트 전략

> **규범 문서.** 변경을 검증하는 방법과 골든 해시 절차를 정합니다. 상태: 전부 `[계획]`.

---

## 1. 원칙

```text
안전망이 기능 코드보다 먼저다. (RTS ADR-0005 계승)
"동작이 같다"를 명령 하나로 증명할 수 없으면 리팩터링이 아니라 재작성이다.
```

## 2. 레벨

| 레벨 | 대상 | 도구 | CTest 라벨 | 언제 |
|---|---|---|---|---|
| L1 단위 | Foundation, ECS, 리플렉션, 직렬화, BitStream, Rule 매칭, FSM, Input | doctest `SandboxTests` | `unit` | 매 빌드 |
| L1′ 속성 | Registry vs 참조 모델, Spatial vs brute-force, Delta 적용 vs 전체 스냅샷 | 시드 고정 무작위 | `property` | 매 빌드 |
| L2 헤드리스 시나리오 | Ecosystem 생존, 편집 명령, 권한, 콘텐츠 검증 | `SandboxTests`, `sbx_sim_check` | `sim`, `content` | 매 빌드 |
| L3 결정론 | D1~D5 ([04](04-DETERMINISM.md) 2장) | `sbx_sim_check` | `det` | 매 커밋 |
| L3′ 복제 수렴 | 서버 상태 == 클라 복제본 | Loopback + SimulatedTransport | `net` | 매 커밋 |
| L4 성능 예산 | [14](14-PERFORMANCE.md) 시나리오 단축판 | `sbx_bench --budget --quick` | `bench` | 매 커밋(짧은) / 야간(전체) |
| L5 렌더 이미지 | 기준 이미지 비교, 백엔드별 | `sbx_render_tests` | `render` | GPU 가능 CI |
| L6 아키텍처 | 링크 의존성, include 린트 | CMake + `tools/check_includes.py` | `arch` | 매 구성/빌드 |
| L7 수동 QA | 조작감, 에디터 UX, 실제 GPU·네트워크 | [qa/MANUAL-QA](qa/MANUAL-QA.md) | — | 마일스톤 |

현재 등록된 테스트 (Phase 1):

| CTest 이름 | 라벨 | 내용 |
|---|---|---|
| `unit_foundation` · `unit_core` · `unit_server` | unit | doctest 스위트별 실행 (`--test-suite=`) |
| `server_version` | unit;server | `SandboxServer --version` 출력 확인 |
| `server_rejects_unknown_option` | unit;server | 잘못된 인자 → 실패 종료 |
| `arch_include_lint` | arch | 실제 소스 트리 include 경계 0건 |
| `arch_include_lint_selftest` | arch | 픽스처(`tests/arch/fixtures`)에서 정확히 8건 검출 |
| `arch_link_boundary_selftest` | arch | 금지 링크를 넣은 구성이 "SBX boundary violation"으로 실패 |

```powershell
ctest --preset windows-msvc-debug                       # 전부
ctest --preset windows-msvc-debug -L "unit|property"    # 빠른 것만
ctest --preset windows-msvc-debug -L det                # 결정론
```

## 3. 디렉터리

```text
tests/
  unit/        foundation/ ecs/ simulation/ world/ serialization/ network/ editor/ platform/
  property/    무작위 대조 테스트
  sim/         헤드리스 시나리오
  determinism/ CTest 정의 + 시나리오 명령 로그
  network/     Loopback / Simulated 통합
  render/      기준 이미지 (golden/<backend>/*.png)
  golden/      골든 해시 JSON
  data/        테스트용 콘텐츠 팩, 샘플 세이브(마이그레이션용)
```

테스트 이름: `TEST_CASE("<모듈>: <행동> <조건>")` 예 `"ecs: remove swaps last element into hole"`.
각 파일은 `TEST_SUITE("<스위트>")`로 감싸고, 새 스위트는 `tests/CMakeLists.txt`의 스위트 목록에 추가합니다 (라벨별 실행).
앱의 순수 로직(예: `apps/server/ServerOptions.cpp`)은 실행 파일을 링크할 수 없으므로 SandboxTests 에 소스로 직접 넣습니다.

## 4. sbx_sim_check (결정론 하네스)

RTS `rts_sim_check`의 인터페이스를 계승합니다.

```text
sbx_sim_check --world <path|scenario> [--content <pack>] --ticks N
              [--hash-at 0,30,90 | --hash-every 30] [--print-hash]
              [--golden tests/golden/<name>.json]          D4
              [--repeat]                                    D1: 같은 입력 2회 실행 비교
              [--save-at T]                                 D2: T 에서 저장→로드→이어서 N 까지, 비교
              [--replay-roundtrip]                          D3: 기록→재생 divergence 검사
              [--threads 0,1,8]                             D5: 워커 수별 비교
              [--commands <log>]                            명령 로그 주입
              [--validate-content <pack>]                   콘텐츠 검증만
              [--verbose]
실패 출력: 첫 불일치 틱 → 엔티티(saveId, prefab) → 컴포넌트(stableId) → 필드(경로, 기대값, 실제값)
           + 의심 범주 (순회 순서 / 난수 / 부동소수 / 저장 누락)
종료 코드: 0 통과, 1 불일치, 2 입력 오류
```

## 5. 골든 해시

```jsonc
// tests/golden/ecosystem_small.json
{
  "scenario": "tests/data/worlds/ecosystem_small",
  "content": "tests/data/content/ecosystem",
  "ticks": 900,
  "simVersion": 1,
  "hashes": { "0": "0x…", "300": "0x…", "900": "0x…" },
  "recordedAt": "YYYY-MM-DD",
  "recordedBy": "<commit>",
  "note": "의도적으로 시뮬레이션 동작을 바꾼 커밋에서만 갱신한다."
}
```

```text
기록 절차
  1. --repeat 통과 확인 (비결정적인 코드를 골든으로 박지 않는다)
  2. --print-hash 출력의 hashes 를 옮겨 적고 recordedAt/recordedBy 기입
갱신 규칙
  리팩터링 커밋        해시 동일 필수. 다르면 리팩터링이 동작을 바꾼 것 → 그 커밋에서 해결
  규칙 변경 커밋       kSimVersion++ 와 골든 갱신을 같은 커밋에, 메시지에 이유
  골든 파일의 simVersion 과 코드 kSimVersion 이 다르면 det_golden_* 은 "갱신 필요"로 실패한다
  (RTS 에서 simVersion 2 전환 후 골든이 1로 남았던 상태를 구조적으로 막는다)
```

## 6. 시나리오 목록 `[계획]`

| 이름 | Phase | 내용 | 검사 |
|---|---|---|---|
| `ecs_churn` | 2 | 무작위 생성·파괴·추가·제거 10만 | P1~P3, 참조 모델 일치 |
| `random_walk_1k` | 3 | 1,000 엔티티 무작위 이동 | D1 |
| `world_save_load` | 4 | 지형 편집 + 엔티티 + 저장/로드 | D2 |
| `ecosystem_small` | 5 | 64×64 타일, 300 엔티티, 900틱 | D1~D5, 골든 |
| `ecosystem_survival` | 5 | 512×512, 시드 3개, 18,000틱 | 세 종 공존 |
| `edit_session` | 12 | 명령 로그(배치·이동·지형·Rule 변경) | D3, 권한 |
| `net_convergence` | 10 | 2클라 + Simulated(100ms, 5%) | L3′ |

## 7. 렌더 기준 이미지

```text
sbx_render_tests --rhi <backend> --case <name> [--update-golden]
비교: 픽셀별 |Δ| ≤ 2 (8비트) 인 비율 ≥ 99.9%, 백엔드별 기준 이미지
케이스: clear, triangle, texture, sprite, batch_1k, coord_convention, culling_ccw, imgui_basic
CI: Windows WARP, Linux lavapipe, macOS Apple Silicon 러너
```

## 8. CI 매트릭스 (Phase 1부터 단계적으로)

| 잡 | OS | 빌드 | 테스트 |
|---|---|---|---|
| win-msvc-debug | Windows | Debug | unit property sim det arch content |
| win-msvc-release | Windows | RelWithDebInfo | det bench(quick) render(WARP, Phase 7+) |
| linux-clang | Ubuntu | Debug + ASan/UBSan | unit property sim det arch net |
| linux-clang-tsan | Ubuntu | TSan | 스레드 관련 테스트 |
| linux-render | Ubuntu | Release | render(lavapipe, Phase 13+) |
| macos | macOS arm64 | Debug | unit det (Phase 1부터 Core만), render(Phase 14+) |

Core·Server는 **Phase 1부터** 세 OS에서 빌드합니다. 플랫폼 이식성 문제를 Phase 13·14까지 숨기지 않기 위해서입니다.
