# ADR-0096. 로드맵 버전 체계 — 작은 MINOR 마일스톤 (v0.5 ~ v0.21+), 성숙 단계와 버전 분리, v0.5 = Technical Alpha, 태스크 등록부

- 상태: **Accepted** · 날짜: 2026-11-03
- 관련: [roadmap/versioning](../roadmap/versioning.md) · [roadmap/task-registry](../roadmap/task-registry.md) · [roadmap/releases](../roadmap/releases.md) ·
  [roadmap/release-gates](../roadmap/release-gates.md), [audit/SECOND-PASS-GAP-ANALYSIS](../audit/SECOND-PASS-GAP-ANALYSIS.md),
  [ADR-0039](0039-platform-scope-version-line-after-phase-15.md) (버전 줄 v0.1 ~ v1.0), [ADR-0078](0078-v05-creator-alpha-scope-expansion.md) (v0.5 범위),
  [ADR-0082](0082-v06-v10-feature-placement.md) · [ADR-0088](0088-v06-v10-placement-update-runtime-v2.md) (v0.6 ~ v1.0 배치),
  [ADR-0092](0092-three-tracks-creator-studio-placement.md) (세 트랙 · 버전 이름 유지), [ADR-0095](0095-audio-v05-events-null-output-v06.md) (오디오 출력 v0.6)

## 맥락

옛 줄은 v0.5 Creator Alpha · v0.6 Hub · v0.7 Multiplayer · v0.8 Marketplace · v0.9 Business · v1.0 GA 의 여섯 칸이었다. Studio 감사
(ADR-0089 ~ 0092) 가 v0.5 에 M6 태스크를 더하고, 2차 감사가 패키지 무결성 · 배포 수명주기 공백 (NG-01 ~ NG-17) 을 더 찾자 v0.5 한 칸에 M3 ~ M8 ·
Studio 기반 · 데모 · 외부 시험이 들어가 끝낼 수 없는 크기가 됐다 (끝난 M1 · M2 · M8 이 다섯 마일스톤 중 셋). 또 "v1.0 = v0.9 다음" 이라는 줄은
기준을 통과하지 못해도 번호가 1.0 으로 가게 만든다. ADR-0092 는 버전 이름을 유지했지만 (요청의 "v0.5 Technical Alpha" 안을 받지 않음), 그 뒤
범위가 더 늘어 그 판단의 전제 (v0.5 가 끝날 수 있는 크기) 가 무너졌다.

## 결정

```text
1. 제품 버전 = SemVer, 자리마다 정수 (foundation/version/SemVer 그대로 — v0.10 > v0.9). MINOR 하나 = 사용자 목표 하나 · P0 대략 10 개 이하.
   목표가 크면 다음 MINOR 로 나눈다 — 번호를 아끼지 않는다. 버전 목록은 versioning 3장 (v0.5 ~ v0.21 · v1.0 · v1.x)
2. v1.0.0 은 GA 기준 (release-gates 4장) 을 모두 통과한 빌드에 붙인다. 직전 버전 · 날짜를 미리 정하지 않는다 — v0.21 · v0.22 … 로 계속될 수 있다
3. 성숙 단계 (Technical Alpha · Closed Creator Alpha · Public Creator Alpha · Beta · GA) 는 버전 번호와 따로, 진입 기준 (release-gates 3장) 으로 붙인다
4. v0.5 = Technical Alpha — 끝난 M1 · M2 · M8 + M7-T6 (CLI 로 만든 게임의 다른 폴더 · 빈 캐시 E2E). "Creator Alpha" 의 사용자 목표
   (외부 제작자가 Studio 만으로 — 옛 20 13장 C1 ~ C4 · X1 ~ X5) 는 v0.11 Closed Creator Alpha
5. 옛 태스크 ID 는 바꾸지 않는다. 목표 버전은 태스크 등록부 (원래 · 목표 두 칸) 가 정본. ID 는 다시 쓰지 않는다 (옛 번호 표)
6. 세 트랙 (ADR-0092) 은 그대로 — 서로 기다리지 않고, 늦은 트랙의 태스크만 다음 버전으로 옮긴다
7. 호환 번호 (Host API · .sbc · 프로토콜 · 시뮬레이션 · 세이브 · 패키지 · 매니페스트 · 리플레이 · Studio Document · Editor Plugin API) 는
   제품 버전과 따로 (versioning 5장) — 제품 버전이 올라도 저절로 올리지 않는다
8. 일관성은 검사기로 지킨다: tools/check_docs.py (CTest docs_consistency — 링크 · ADR · 태스크 ID · 목표 버전 · 상태)
```

## 선택지

| 안 | 내용 | 판단 |
|----|------|------|
| A (채택) | 작은 MINOR + 성숙 단계 분리 + 등록부 | 버전마다 끝낼 수 있는 크기, 1.0 이 기준에 묶임, 옛 ID 추적 |
| B | 옛 여섯 칸 유지 · v0.5 를 길게 | v0.5 가 끝나지 않고 그동안 아무것도 출시하지 않는다 — 버전이 진척을 보여 주지 못한다 |
| C | v0.5 를 0.5.1 · 0.5.2 … 패치로 쪼갬 | PATCH 는 고침 전용 (SemVer) — 기능을 패치에 넣으면 릴리스 피드 · 호환 판단이 틀린다 |
| D | 태스크 ID 를 새 버전으로 다시 붙임 (RT-06-3 → RT-10-3) | 커밋 · ADR · DEVELOPMENT_LOG 의 옛 ID 추적이 끊긴다 |

## 결과

- ADR-0039 (버전 줄 표 — v0.6 ~ v1.0 의 칸), ADR-0078 (v0.5 = Creator Alpha 의 범위), ADR-0082 · ADR-0088 (v0.6 ~ v1.0 배치 버전), ADR-0092 (버전 이름 유지 ·
  M6 태스크를 v0.5 에), ADR-0095 (오디오 출력 "v0.6" → NG-17 v0.10) 의 **버전 배치만** 이 ADR 이 대신한다 (일부 Superseded). 기능 결정 (무엇을 · 어떻게)
  은 그대로 유효하다.
- 2026-11-03 이전 문서의 "[계획 v0.x]" 는 고쳐 쓰지 않고 versioning 4장 변환표로 읽는다. 18 · 20 · 23 의 머리말 · 지금 · 다음 줄은 이 커밋에서 고친다.
- 0.5.0 출시는 M7-T6 · M7-T5 뒤. 프로젝트의 engine 범위 (<1.0.0) 는 0.x 를 잘게 나눠도 안 깨진다 — 1.0 전환은 NG-16.
