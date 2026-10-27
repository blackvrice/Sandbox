#pragma once
// 스크립트 프로파일의 진단 시계 (v0.3 M5-T4, ADR-0068). docs/19-SANDBOXSCRIPT.md 22장.
//
// 시뮬레이션은 벽시계를 보지 않는다 (04) — 엔진 (core) 은 시계를 모르고, 부르는 쪽 (서버 · 클라이언트) 이 단조 나노초를
// 돌려주는 함수를 넣으면 스크립트 호출마다 재어 통계에만 더한다. 잰 값은 결과 · 해시 · 세이브 · 복제 월드에 없다
// (없어도 같은 결과).

#include "foundation/types/Types.hpp"

namespace sbx::script {

using ScriptClock = u64 (*)() noexcept;

} // namespace sbx::script
