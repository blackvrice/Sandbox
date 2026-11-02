# ADR-0085. 스크립트 표준 라이브러리 — VM 내장 (intrinsic) 표 · 내장 컬렉션 · 함수 참조와 캡처 없는 화살표 · T | undefined

- 상태: **Accepted** · 날짜: 2026-11-02
- 관련: [19-SANDBOXSCRIPT](../19-SANDBOXSCRIPT.md) 25장 (지원 매트릭스 · 의미), [20-CREATOR-ALPHA](../20-CREATOR-ALPHA.md) 10장 Phase 8 (M8),
  [ADR-0056](0056-sandboxscript-language-subset-own-compiler-no-gc.md) (부분집합 — 이 ADR 이 S304 · S315 · S327 · undefined 의 일부를 연다),
  [ADR-0057](0057-host-api-single-table-sdk-generation.md) (Host 표), [ADR-0058](0058-bytecode-register-machine-typed-registers-verifier.md)
  (바이트코드 · 검증기), [ADR-0080](0080-gameplay-api-engine-component-access-structural-ops.md) (게임플레이 API),
  [ADR-0081](0081-script-execution-backend-boundary-interpreter-first.md) (실행 경계), 04 (결정론 — 이 ADR 보다 앞선다)

## 맥락

v0.3 의 SandboxScript 는 배열에 length · push · 첨자 · for-of 만 있고, Map · Set · 콜백 · 컬렉션 · 정렬 · 탐색이 없다. 제작자는
인벤토리 (Map · Set), RTS 유닛 AI (우선순위 큐 · 반경 질의 · 경로 요청), 보드게임 AI (그래프 · BFS) 를 TS 처럼 쓰기를 원한다.
조건: JS 엔진을 넣지 않는다, 서버 권한 · 결정론, 호출마다 아레나 (GC 없음), 영속 상태는 ECS, 연료 · 메모리 · 깊이 · Host 호출 예산을
어떤 내장도 우회하지 않는다, 바이트코드 · Host API 호환, "tsc --strict 가 받으면 같은 뜻" (ADR-0056 1).

## 선택지 — 어디에 구현하나

```text
(A) VM 내장 (intrinsic) — C++ 로 VM 안에. 빠르다 (명령 하나가 원소 루프), 원소 타입에 일반적 (검증기가 레지스터 타입으로 본다),
    연료 · 메모리를 원소 · 비교 · 이동마다 정확히 매길 수 있다. 비용: 검증기 · 검사기에 서명 규칙, 바이트코드 형식 한 번
(B) 스크립트 표준 라이브러리 — SandboxScript 로 쓴 모듈을 같이 컴파일. VM 을 안 고친다. 그러나 일반 (제네릭) 함수가 없어 (S327)
    원소 타입마다 복사해야 하고 인터프리터 속도 (명령마다 연료 · 분기) 로 돈다
(C) Host API — 엔진 상태 (경로 · 공간 색인 · 물리) 에 닿는 것. 배열 · 컬렉션 같은 순수 계산을 Host 로 보내면 VM 참조를 밖으로 내보내야
    한다 (ADR-0057 4 가 막는다)
```

## 결정

```text
1. 자리
   - 언어 수준 내장 (Array 메서드 · Map · Set · "sandbox" 의 컬렉션 · 정렬 · 탐색 · 집계) = (A) VM 내장 표 script/vm/Intrinsics —
     이름 · 받는 쪽 · 서명 틀 · 연료 모형을 한 곳에 두고 검사기 · 코드 생성 · 검증기 · VM · d.ts 문서가 같이 읽는다 (MathBuiltins 와
     같은 방식). 새 명령 INTR a b c (c = 표 자리, 인자 = b … 연속 레지스터, a = 결과 · kNoReg) — 표의 끝에 더하기만이면 형식 그대로,
     자리를 바꾸면 bc::kFormatVersion 을 올린다
   - 순수 수치 도움 함수 (clamp · lerp · smoothstep · Vec2 함수 …) = (A) 와 같은 표 (f64 산술 — 같은 바이너리 = 같은 결과, 04 4.6)
   - 엔진 상태 (경로 요청 · 공간 질의 · 물리) = (C) Host 표 (지문이 바뀐다 — kHostApiVersion 그대로, 더하기만)
   - (B) 는 쓰지 않는다 (v0.5). 사용자 제네릭이 생기면 다시 본다
2. 꾸러미: Array · Map · Set 은 TS 전역 (lib ES2020 — import 없이). 나머지 (Stack · Queue · Deque · PriorityQueue · MinHeap ·
   MaxHeap · BitSet · Graph, 알고리즘 · 수학 · vec2) 는 "sandbox" 의 이름 내보내기 하나. "sandbox/collections" 같은 하위 경로는
   만들지 않는다 — import 해석 (ADR-0075 의 "@팩/경로") 에 새 규칙이 생기고 이름이 둘로 갈린다. 필요하면 뒤에 같은 이름을 다시 내보내는
   별칭으로 더한다
3. 만들기: 내장 컬렉션만 new — new Map<K, V>() · new Set<T>() · new Set<T>(배열) · new Queue<T>() … (d.ts 는 declare class —
   사용자 class 는 여전히 S301). 사용자 타입 · 함수의 new 는 S315 그대로. 타입 인자는 new 에 쓰거나 선언의 타입에서 받는다
   (const m: Map<string, i32> = new Map()). 팩토리 함수 (queue<T>()) 는 고르지 않았다 — Map · Set 은 new 가 TS 의 유일한 길이다
4. 타입: Map<K, V> · Set<T> · Queue<T> … 는 내장 제네릭 타입 이름 (사용자 제네릭 S327 은 그대로). 키 타입 = i32 · number ·
   string · boolean · enum (Entity · 구조체 · 배열 키는 [계획] — 핸들 수명 · 모양 같음을 따로 정한다)
5. T | undefined: TS 의 Array.pop · Map.get · find 는 T | undefined 를 돌려준다. tsc --strict 와 같은 뜻을 위해 undefined 를
   "그 결과의 없음" 으로 연다 — 타입 T | undefined (null 과 다른 타입 — 서로 넣을 수 없고, x !== undefined 가 좁힌다), 값 undefined
   (비교 · 대입 · 반환). 표현은 T | null 과 같다 (값 칸 0). T | null | undefined 는 S331. 그 밖의 undefined 금지 (S413) 는 그대로
6. 콜백 (우선순위 순):
   (1) 최상위 함수 참조 — xs.sort(byHp) · xs.filter(isAlive). 새 타입 종류 Func (매개변수 · 결과) 와 명령 LOADFN a ← functions[b]
       (검증기: 함수 서명 = 레지스터 타입). 함수 값은 내장의 콜백 인자로만 — 변수 · 필드 · 배열 · 반환에 둘 수 없다 (S411)
   (2) 캡처 없는 화살표 — xs.sort((a, b) => a.hp - b.hp). 숨은 최상위 함수로 컴파일한다. 몸이 쓸 수 있는 이름 = 자기 매개변수 ·
       최상위 const · 함수 · enum · import. 바깥 지역 변수 · 매개변수를 쓰면 S304 "캡처하는 화살표는 없다" (대안을 말한다)
   (3) 제한된 함수 값 · (4) 클로저 — [계획] 따로 ADR (아레나 수명 · 캡처 복사 규칙)
   콜백 안의 Host 호출: 콜백은 ctx 를 받지 않고 캡처도 못 하므로 Host 를 부를 수 없다 — 정렬 · 걸러내기 중의 부수 효과는 인자 원소의
   필드 쓰기뿐 (결정적)
7. 반복 순서 (결정론): Map · Set = 넣은 차례 (JS). 이미 있는 키를 다시 넣으면 자리는 그대로 (값만 바뀜), 지웠다 다시 넣으면 끝.
   키 같음 = SameValueZero (NaN = NaN, -0 = +0 — 키는 +0 으로 저장). 해시는 고정 (FNV-1a · 정수 섞기 — 주소 · 무작위 씨앗 없음).
   반복 중 넣기는 끝에 보이고 (JS), 지우기는 건너뛴다, clear 는 반복을 끝낸다. 지운 칸은 호출 안에서 다시 쓰지 않는다 (아레나 —
   호출이 끝나면 통째로 버린다)
8. 정렬: sort() · sort(cmp) 는 안정 (ES2019) — 병합 + 삽입 (길이 16 이하 덩어리) 고정 알고리즘, 최악 O(n log n), 비교 횟수 = 입력의
   함수 (같은 입력 = 같은 비교 차례). number[] · i32[] 의 sort() (비교자 없음) 는 JS 가 문자열로 비교하므로 S4xx — 비교자를 쓰거나
   sortNumbers. string[] 의 sort() = UTF-16 단위 순서 (JS). 비교자 결과 NaN = 0 (JS ToNumber · +0). 일관 없는 비교자도 결과는 결정적
   (같은 알고리즘). PriorityQueue 의 같은 우선순위 = 넣은 차례 (일련번호)
9. 연료 · 메모리: 내장마다 "기본 + 원소 · 비교 · 이동 · 해시 탐색 · 방문 · 확장마다" 연료 (표에 적는다 — 25장). 콜백 호출은 그 함수의
   명령 연료를 그대로 낸다 (내장 안에서 VM 이 같은 실행 루프로 부른다 — 깊이 · 칸 예산 그대로). 새 객체 · 늘림은 아레나 바이트로 센다
   (용량을 늘릴 때 늘어난 만큼), 원소 상한 = Limits::maxArrayLength. 트랩은 그 호출을 멈추고 쓰기를 버린다 (ADR-0064 그대로)
10. 그래프 (Graph): 노드 = 0 … n-1 정수, 방향 · 무방향, 간선 가중치 number. 음수 · NaN · 무한 가중치는 addEdge 가 받지 않는다
    (false 를 돌려준다 — 그래서 Dijkstra · A* 는 음수 간선을 만나지 않는다), 없는 노드 번호는 트랩 IndexOutOfRange. 이웃 순서 =
    넣은 차례, 같은 거리 = 작은 노드 번호. 월드 A* (core/path) 와 따로 — 스크립트 그래프는 호출 아레나 안
11. 엔진 API: 경로는 core/path PathfindingService 를 다시 쓰는 비동기 요청 (요청 id · 상태 · 결과 · 만료 · 취소, 틱마다 상한) —
    틱 안의 무제한 동기 A* 는 없다. 공간 질의는 ctx.near 의 규칙 (단계 4 색인 · saveId 순) 을 지키며 nearest · kNearest · inBox 를
    더한다. 물리 (raycast · overlap) 는 지금 충돌 구조 (core.collider · SpatialIndex) 가 받치는 만큼만 — 나머지는 따로 태스크
```

## 결과

- 좋다: TS 처럼 쓰는 컬렉션 · 콜백 · 정렬이 생기고, 원소 루프가 C++ 이라 빠르며, 연료 · 메모리 규칙이 내장 하나하나에 적힌다.
  "sandbox" 하나의 이름공간이라 import 해석이 그대로다.
- 비용: 바이트코드 형식 5 → 6 (INTR · LOADFN · Func · Map · Set · 컬렉션 타입 종류) — 옛 .sbc 는 다시 컴파일 (패키지는 소스만 담는다 —
  ADR-0081 4, 설치가 다시 컴파일한다). 검증기에 서명 틀. undefined 의 일부 허용 (null 과 섞으면 오류).
- 지키는 것: 서버 권한 · 결정론 (반복 · 동점 · 해시 고정), 호출마다 아레나, 예산 우회 없음, 스크립트 없는 콘텐츠의 골든 그대로.
- 다시 볼 때: 사용자 제네릭 · 클로저 · 튜플 (entries · 구조 분해) 이 들어오면 (B) 와 6 (3)(4) 를 새 ADR 로.
