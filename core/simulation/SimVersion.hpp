#pragma once
// 시뮬레이션 규칙 버전. 엔진 버전·빌드 번호와 별개다. docs/04-DETERMINISM.md 6장.
//
// 올린다 (시뮬레이션 상태나 그 전이가 달라질 때):
//   - 틱 파이프라인 순서, System 추가·삭제·이동
//   - 엔진 내장 공식·상수, 명령 해석 방식
//   - WorldHash 의 대상·순서·알고리즘
//   - RandomService 의 시드 유도·생성기
// 올리지 않는다:
//   - 렌더링·UI·오디오·로그·도구, 동작이 같은 리팩터링 (골든 해시가 그대로여야 한다), 콘텐츠 데이터 변경
//
// ★ 골든 해시를 갱신하는 커밋은 이 숫자도 올린다. 둘은 한 쌍이다.
//
// 이력
//   v1  2026-10-05  최초 (Phase 3)
//   v2  2026-10-05  Phase 4: WorldHash 에 지형 격자 추가, Movement 가 월드 경계로 자름, 공간 색인 경계 안 격자
//   v3  2026-10-05  Phase 5B: Stage 5·6·7(Behavior)·8·10·11·16 System 추가, Movement 조향(core.movement)
//   v4  2026-10-06  Phase 5C: life.reproduce 밀도 제한(crowdRadius·crowdMax), flee 가 통행 가능한 목표를 고른다,
//                   감지는 색인의 태그 사본을 쓰고 태그 없는 엔티티를 건너뛴다

#include "foundation/types/Types.hpp"

namespace sbx::sim {

inline constexpr u32 kSimVersion = 4;

} // namespace sbx::sim
