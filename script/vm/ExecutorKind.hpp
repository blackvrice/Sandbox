#pragma once
// 스크립트 실행자 종류 (v0.9 M6-T4 — ADR-0127). 콘텐츠 (팩마다 고른다) 가 인터프리터 헤더 없이 쓴다

#include "foundation/types/Types.hpp"

namespace sbx::script::vm {

enum class ExecutorKind : u8 { Interpreter };

} // namespace sbx::script::vm
