#include "script/lang/MemberCatalog.hpp"

#include <array>

namespace sbx::script {

namespace {

using M = MemberDoc;

// 표마다 이름 순 (시험이 본다). 서명의 T 는 원소 타입, 콜백은 바깥 이름을 잡지 않는 화살표 · 최상위 함수 (19 25.1)
constexpr std::array kArray{
    M{"concat", "concat(other: T[]): T[]", "두 배열을 이은 새 배열"},
    M{"copyWithin", "copyWithin(target: number, start: number, end?: number): T[]",
      "배열 안에서 구간을 옮겨 쓴다 (같은 배열)"},
    M{"every", "every(fn: (x: T, i: number) => boolean): boolean", "모든 원소가 참이면 true (빈 배열은 true)"},
    M{"fill", "fill(x: T, start?: number, end?: number): T[]", "구간을 x 로 채운다 (같은 배열)"},
    M{"filter", "filter(fn: (x: T, i: number) => boolean): T[]", "참인 원소만 새 배열로"},
    M{"find", "find(fn: (x: T, i: number) => boolean): T | undefined", "처음으로 참인 원소"},
    M{"findIndex", "findIndex(fn: (x: T, i: number) => boolean): i32", "처음으로 참인 자리 (없으면 -1)"},
    M{"forEach", "forEach(fn: (x: T, i: number) => void): void", "원소마다 부른다"},
    M{"includes", "includes(x: T, from?: number): boolean", "있으면 true (NaN 도 찾는다 — SameValueZero)"},
    M{"indexOf", "indexOf(x: T, from?: number): i32", "처음 자리 (없으면 -1, ===)"},
    M{"join", "join(sep?: string): string", "글로 잇는다 (기본 쉼표)"},
    M{"lastIndexOf", "lastIndexOf(x: T, from?: number): i32", "마지막 자리 (없으면 -1)"},
    M{"length", "length: i32", "원소 수", true},
    M{"map", "map(fn: (x: T, i: number) => U): U[]", "원소마다 바꾼 새 배열"},
    M{"pop", "pop(): T | undefined", "끝 원소를 빼서 돌려준다 (비면 undefined)"},
    M{"push", "push(x: T): i32", "끝에 넣는다 — 새 길이"},
    M{"reduce", "reduce(fn: (acc: U, x: T, i: number) => U, init: U): U", "앞에서부터 접는다"},
    M{"reverse", "reverse(): T[]", "차례를 뒤집는다 (같은 배열)"},
    M{"shift", "shift(): T | undefined", "첫 원소를 빼서 돌려준다"},
    M{"slice", "slice(start?: number, end?: number): T[]", "구간의 새 배열"},
    M{"some", "some(fn: (x: T, i: number) => boolean): boolean", "참인 원소가 하나라도 있으면 true"},
    M{"sort", "sort(cmp?: (a: T, b: T) => number): T[]", "안정 정렬 (cmp 없으면 문자열 배열만 — 수는 sortNumbers)"},
    M{"splice", "splice(start: number, count?: number): T[]", "구간을 빼서 새 배열로 (같은 배열이 줄어든다)"},
    M{"unshift", "unshift(x: T): i32", "앞에 넣는다 — 새 길이"},
};
constexpr std::array kString{
    M{"length", "length: i32", "UTF-16 단위 수 (JS 와 같다)", true},
};
constexpr std::array kMap{
    M{"clear", "clear(): void", "모두 지운다"},
    M{"delete", "delete(k: K): boolean", "지웠으면 true"},
    M{"get", "get(k: K): V | undefined", "값 (없으면 undefined)"},
    M{"has", "has(k: K): boolean", "키가 있으면 true"},
    M{"keys", "keys(): K[]", "넣은 차례의 키 — for-of · Array.from 에서만"},
    M{"set", "set(k: K, v: V): Map<K, V>", "넣거나 바꾼다 (차례는 처음 넣은 때)"},
    M{"size", "size: i32", "항목 수", true},
    M{"values", "values(): V[]", "넣은 차례의 값 — for-of · Array.from 에서만"},
};
constexpr std::array kSet{
    M{"add", "add(x: T): Set<T>", "넣는다 (이미 있으면 그대로)"},
    M{"clear", "clear(): void", "모두 지운다"},
    M{"delete", "delete(x: T): boolean", "지웠으면 true"},
    M{"has", "has(x: T): boolean", "있으면 true"},
    M{"size", "size: i32", "원소 수", true},
    M{"values", "values(): T[]", "넣은 차례 — for-of · Array.from 에서만"},
};
constexpr std::array kStack{
    M{"clear", "clear(): void", "모두 지운다"},       M{"peek", "peek(): T | undefined", "맨 위 (빼지 않는다)"},
    M{"pop", "pop(): T | undefined", "맨 위를 뺀다"}, M{"push", "push(x: T): void", "위에 넣는다"},
    M{"size", "size: i32", "원소 수", true},
};
constexpr std::array kQueue{
    M{"clear", "clear(): void", "모두 지운다"},
    M{"dequeue", "dequeue(): T | undefined", "앞에서 뺀다"},
    M{"enqueue", "enqueue(x: T): void", "뒤에 넣는다"},
    M{"peek", "peek(): T | undefined", "앞 (빼지 않는다)"},
    M{"size", "size: i32", "원소 수", true},
};
constexpr std::array kDeque{
    M{"clear", "clear(): void", "모두 지운다"},
    M{"peekBack", "peekBack(): T | undefined", "뒤 (빼지 않는다)"},
    M{"peekFront", "peekFront(): T | undefined", "앞 (빼지 않는다)"},
    M{"popBack", "popBack(): T | undefined", "뒤에서 뺀다"},
    M{"popFront", "popFront(): T | undefined", "앞에서 뺀다"},
    M{"pushBack", "pushBack(x: T): void", "뒤에 넣는다"},
    M{"pushFront", "pushFront(x: T): void", "앞에 넣는다"},
    M{"size", "size: i32", "원소 수", true},
};
constexpr std::array kPriorityQueue{
    M{"clear", "clear(): void", "모두 지운다"},
    M{"peek", "peek(): T | undefined", "가장 작은 우선순위 (빼지 않는다)"},
    M{"peekPriority", "peekPriority(): number | undefined", "그 우선순위"},
    M{"pop", "pop(): T | undefined", "가장 작은 우선순위를 뺀다 (같으면 넣은 차례)"},
    M{"push", "push(x: T, priority: number): boolean", "넣는다 (NaN 우선순위는 넣지 않는다 — false)"},
    M{"size", "size: i32", "원소 수", true},
};
constexpr std::array kMinHeap{
    M{"clear", "clear(): void", "모두 지운다"},
    M{"extractMin", "extractMin(): number | undefined", "가장 작은 수를 뺀다"},
    M{"insert", "insert(x: number): void", "넣는다"},
    M{"peek", "peek(): number | undefined", "가장 작은 수"},
    M{"size", "size: i32", "원소 수", true},
};
constexpr std::array kMaxHeap{
    M{"clear", "clear(): void", "모두 지운다"},
    M{"extractMax", "extractMax(): number | undefined", "가장 큰 수를 뺀다"},
    M{"insert", "insert(x: number): void", "넣는다"},
    M{"peek", "peek(): number | undefined", "가장 큰 수"},
    M{"size", "size: i32", "원소 수", true},
};
constexpr std::array kBitSet{
    M{"clear", "clear(i: i32): void", "끈다 (범위 밖은 트랩)"}, M{"count", "count(): i32", "켜진 비트 수"},
    M{"set", "set(i: i32): void", "켠다 (범위 밖은 트랩)"},     M{"size", "size: i32", "비트 수", true},
    M{"test", "test(i: i32): boolean", "켜졌으면 true"},
};
constexpr std::array kGraph{
    M{"addEdge", "addEdge(a: i32, b: i32, weight?: number): boolean",
      "간선 (무방향이면 양쪽) — 음수 · NaN · 무한 가중치는 false"},
    M{"astar", "astar(start: i32, goal: i32, xs: number[], ys: number[]): i32[] | undefined", "A* (유클리드 휴리스틱)"},
    M{"bfs", "bfs(start: i32): i32[]", "너비 우선 방문 차례"},
    M{"components", "components(): i32[]", "노드마다 연결 성분 번호"},
    M{"dfs", "dfs(start: i32): i32[]", "깊이 우선 방문 차례"},
    M{"dijkstra", "dijkstra(start: i32): number[]", "최단 거리 (닿지 않으면 Infinity)"},
    M{"neighbors", "neighbors(a: i32): i32[]", "이웃 (넣은 차례)"},
    M{"pathTo", "pathTo(start: i32, goal: i32): i32[] | undefined", "최단 경로 (같은 거리 = 작은 노드 번호)"},
    M{"size", "size: i32", "노드 수", true},
    M{"topoSort", "topoSort(): i32[] | undefined", "위상 정렬 (순환이면 undefined)"},
};
constexpr std::array kMath{
    M{"E", "E: number", "자연로그의 밑", true},
    M{"LN10", "LN10: number", "ln 10", true},
    M{"LN2", "LN2: number", "ln 2", true},
    M{"LOG10E", "LOG10E: number", "log10 e", true},
    M{"LOG2E", "LOG2E: number", "log2 e", true},
    M{"PI", "PI: number", "원주율", true},
    M{"SQRT1_2", "SQRT1_2: number", "√½", true},
    M{"SQRT2", "SQRT2: number", "√2", true},
    M{"abs", "abs(x: number): number", "절댓값"},
    M{"atan2", "atan2(y: number, x: number): number", "라디안 각"},
    M{"ceil", "ceil(x: number): number", "올림"},
    M{"cos", "cos(x: number): number", "코사인 (라디안)"},
    M{"exp", "exp(x: number): number", "e^x"},
    M{"floor", "floor(x: number): number", "내림"},
    M{"hypot", "hypot(...xs: number[]): number", "√(합 제곱) — 둘씩 잇는다"},
    M{"log", "log(x: number): number", "자연로그"},
    M{"max", "max(...xs: number[]): number", "가장 큰 값 (모두 i32 면 i32)"},
    M{"min", "min(...xs: number[]): number", "가장 작은 값 (모두 i32 면 i32)"},
    M{"pow", "pow(x: number, y: number): number", "x^y (JS 가장자리 그대로)"},
    M{"round", "round(x: number): number", "반올림 (JS — .5 는 위로)"},
    M{"sign", "sign(x: number): number", "-1 · 0 · 1"},
    M{"sin", "sin(x: number): number", "사인 (라디안)"},
    M{"sqrt", "sqrt(x: number): number", "제곱근"},
    M{"tan", "tan(x: number): number", "탄젠트 (라디안)"},
    M{"trunc", "trunc(x: number): number", "0 쪽으로 자른다"},
};
constexpr std::array kVec2{
    M{"add", "add(a: Vec2, b: Vec2): Vec2", "더하기"},
    M{"angle", "angle(a: Vec2): number", "x 축과의 각 (라디안)"},
    M{"cross", "cross(a: Vec2, b: Vec2): number", "2D 외적 (z 값)"},
    M{"distance", "distance(a: Vec2, b: Vec2): number", "거리"},
    M{"distanceSq", "distanceSq(a: Vec2, b: Vec2): number", "거리의 제곱"},
    M{"div", "div(a: Vec2, s: number): Vec2", "나누기"},
    M{"dot", "dot(a: Vec2, b: Vec2): number", "내적"},
    M{"length", "length(a: Vec2): number", "길이"},
    M{"lengthSq", "lengthSq(a: Vec2): number", "길이의 제곱"},
    M{"lerp", "lerp(a: Vec2, b: Vec2, t: number): Vec2", "선형 보간"},
    M{"mul", "mul(a: Vec2, s: number): Vec2", "곱하기"},
    M{"normalize", "normalize(a: Vec2): Vec2", "단위 벡터 (0 이면 0)"},
    M{"reflect", "reflect(a: Vec2, n: Vec2): Vec2", "법선 n 에 반사"},
    M{"rotate", "rotate(a: Vec2, rad: number): Vec2", "회전 (라디안)"},
    M{"sub", "sub(a: Vec2, b: Vec2): Vec2", "빼기"},
};

} // namespace

std::span<const MemberDoc> builtinMembers(BuiltinReceiver r) {
    switch (r) {
    case BuiltinReceiver::Array:
        return kArray;
    case BuiltinReceiver::String:
        return kString;
    case BuiltinReceiver::Map:
        return kMap;
    case BuiltinReceiver::Set:
        return kSet;
    case BuiltinReceiver::Stack:
        return kStack;
    case BuiltinReceiver::Queue:
        return kQueue;
    case BuiltinReceiver::Deque:
        return kDeque;
    case BuiltinReceiver::PriorityQueue:
        return kPriorityQueue;
    case BuiltinReceiver::MinHeap:
        return kMinHeap;
    case BuiltinReceiver::MaxHeap:
        return kMaxHeap;
    case BuiltinReceiver::BitSet:
        return kBitSet;
    case BuiltinReceiver::Graph:
        return kGraph;
    case BuiltinReceiver::Math:
        return kMath;
    case BuiltinReceiver::Vec2:
        return kVec2;
    }
    return {};
}

std::string_view builtinReceiverName(BuiltinReceiver r) noexcept {
    switch (r) {
    case BuiltinReceiver::Array:
        return "Array";
    case BuiltinReceiver::String:
        return "string";
    case BuiltinReceiver::Map:
        return "Map";
    case BuiltinReceiver::Set:
        return "Set";
    case BuiltinReceiver::Stack:
        return "Stack";
    case BuiltinReceiver::Queue:
        return "Queue";
    case BuiltinReceiver::Deque:
        return "Deque";
    case BuiltinReceiver::PriorityQueue:
        return "PriorityQueue";
    case BuiltinReceiver::MinHeap:
        return "MinHeap";
    case BuiltinReceiver::MaxHeap:
        return "MaxHeap";
    case BuiltinReceiver::BitSet:
        return "BitSet";
    case BuiltinReceiver::Graph:
        return "Graph";
    case BuiltinReceiver::Math:
        return "Math";
    case BuiltinReceiver::Vec2:
        return "vec2";
    }
    return "?";
}

} // namespace sbx::script
