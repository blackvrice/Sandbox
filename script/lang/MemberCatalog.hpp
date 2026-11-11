#pragma once
// 내장 타입의 멤버 표 — 자동 완성 · hover 용 (v0.9 M6-T8 — docs/27-SCRIPT-IDE.md 3장, ADR-0129). 검사기는 메서드를 이름
// 비교로 푼다 (표가 없다) — 이 표가 그 이름들을 사람에게 보일 서명 · 설명과 함께 적는다. 시험 (test_language_service)
// 이 표의 모든 이름을 검사기가 받는지 (S406 없음) · 내장 표 (script/vm/Intrinsics) 의 받는 쪽 메서드가 모두 표에 있는지
// 본다. 사용자 타입 (interface · 컴포넌트 데이터) · Host 멤버 (ctx · Entity …) 는 검사기 · Host 표에서 바로 (여기
// 없다).

#include <span>
#include <string_view>

#include "foundation/types/Types.hpp"

namespace sbx::script {

enum class BuiltinReceiver : u8 {
    Array,
    String,
    Map,
    Set,
    Stack,
    Queue,
    Deque,
    PriorityQueue,
    MinHeap,
    MaxHeap,
    BitSet,
    Graph,
    Math, // Math.* (값 이름공간)
    Vec2, // vec2.* (값 이름공간)
};

struct MemberDoc {
    std::string_view name;
    std::string_view signature; // "pop(): T | undefined"
    std::string_view doc;
    bool property = false;
};

// 이름 순
[[nodiscard]] std::span<const MemberDoc> builtinMembers(BuiltinReceiver r);
[[nodiscard]] std::string_view builtinReceiverName(BuiltinReceiver r) noexcept; // "Array" …

} // namespace sbx::script
