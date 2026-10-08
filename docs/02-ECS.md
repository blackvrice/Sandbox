# 02. ECS

> **규범 문서.** Entity·Component·System의 저장 구조와 API 계약을 정합니다.
> 상태: **Phase 2에서 구현됨** (2026-10-05), Phase 3에서 보강. 0장이 문서와 구현의 차이·미구현 항목입니다.
> 결정 근거: [ADR-0002](adr/0002-custom-sparse-set-ecs.md), [ADR-0011](adr/0011-explicit-component-registration.md).

---

## 0. 구현 상태 (Phase 2 · 3)

| 절              | 구현                                                                                                                                                                                                                                                                                              | 파일                                                                     |
|-----------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------|
| 2 EntityId      | ✅ 그대로                                                                                                                                                                                                                                                                                         | `core/ecs/EntityId.hpp`                                                  |
| 3 EntityManager | ✅ LIFO 재사용, 퇴역 슬롯                                                                                                                                                                                                                                                                         | `core/ecs/EntityManager.{hpp,cpp}`                                       |
| 4 ComponentPool | ✅ 4096 슬롯 페이지, `validate()`(P1~P3)                                                                                                                                                                                                                                                          | `core/ecs/ComponentPool.{hpp,cpp}`                                       |
| 5 Registry      | ✅ — 아래 차이 참고                                                                                                                                                                                                                                                                               | `core/ecs/Registry.{hpp,cpp}`                                            |
| 6 View          | ✅ Read/Write/Exclude, 범위 for · `each` · `count`                                                                                                                                                                                                                                                | `core/ecs/View.hpp`                                                      |
| 7 ECB           | ✅ `createEmpty`/`destroy`/`emplace`/`remove` — `create(PrefabId)`는 Phase 5                                                                                                                                                                                                                      | `core/ecs/EntityCommandBuffer.{hpp,cpp}`                                 |
| 8 변경 추적     | ✅ changed/added 틱, `createdThisTick()`·`destroyedThisTick()` (Phase 3), 변경 순번 (Phase 10A) — `removedLog<T>` 는 쓰지 않음 (복제는 mask 비교)                                                                                                                                                 |                                                                          |
| 9 등록·리플렉션 | ✅ `SBX_COMPONENT`, `ComponentCatalog`(명시 등록), JSON·해시 Visitor — Binary/Bit/ImGui/Diff Visitor 와 Opaque 는 `[계획]`                                                                                                                                                                        | `core/ecs/Component*`, `core/ecs/Reflection.hpp`, `core/serialization/*` |
| 10 Resource     | ✅ `emplaceResource/resource/tryResource`                                                                                                                                                                                                                                                         | Registry                                                                 |
| 11 EventStream  | ✅ Phase 3 — `EntitySpawned`/`EntityDestroyed`/`Custom`, BeginTick 에 비움                                                                                                                                                                                                                        | `core/simulation/EventStream.hpp`                                        |
| 13 Prefab       | `[계획]` Phase 5                                                                                                                                                                                                                                                                                  |                                                                          |
| 14 카탈로그     | Phase 5A·5B (18개): `core.transform` `core.velocity` `core.lifetime` `core.tags` `core.prefab` `core.movement` `core.collider` `persist.persistence` `net.identity` `life.age` `life.energy` `life.health` `life.growth` `life.reproduce` `ai.sensor` `ai.behavior` `ai.path` `debug.random_walk` | `core/components/`                                                       |

**문서 초안과 달라진 점**

```text
- 구조 잠금 중 emplace / emplaceOrReplace 는 Release 에서도 중단한다 (참조를 돌려줘야 하므로 무시할 수 없다).
  create 는 kNullEntity + 오류 로그, destroy/remove 는 false + 오류 로그 (Debug 에서는 셋 다 단언).
- Registry 에 emplaceOrReplace<T> 추가. ECB 의 emplace 는 "있으면 교체" 의미다.
- 파괴 로그 이름: destroyedLog → destroyedThisTick() + clearTickLogs(). 로그는 EntityId 만 담는다.
  Phase 3 에서 createdThisTick() 을 추가했다 (SimulationWorld 가 새 엔티티에 saveId·netId 를 붙이는 근거).
  파괴된 엔티티의 saveId·netId 는 SimulationWorld 의 슬롯 장부가 기억한다 (컴포넌트는 이미 사라졌으므로).
- 결정적 순회 도우미: forEachEntityByIndex(fn), poolsByStableId(). 해시·저장은 반드시 이것을 쓴다 (E2).
- 동적 풀: Registry::adoptPool(info.makePool()). Registry 는 카탈로그를 모른다.
- Flags 에 NotHashed 추가: Persistent 는 자동으로 Hashed 가 되고, 빼려면 NotHashed 를 명시한다 (H3 를 구조로).
- 리플렉션 지원 필드 타입 (core/serialization/FieldCodec.hpp):
    bool · 정수 · f32/f64 · enum · Vec2 · Vec2i · EntityId · std::array<F,N> · SmallVector<F,N>
    · FixedString<N> (Phase 5A — JSON 은 문자열, 넘치면 OutOfRange. ContentId = FixedString<48>, Prefab/Behavior 참조)
  Color 타입은 첫 사용처에서 추가한다. Hint 에 PrefabRef · BehaviorRef 추가 (콘텐츠 검증기 V2 가 찾는다).
- (Phase 5A) ComponentInfo 에 getNumber · applyNumber(이름으로 수치 필드 읽기/쓰기·더하기 — FieldMeta 범위로 자르고
  정수는 반올림) 와 fields(reflect 순 필드 목록: 이름·Hint·수치 여부·FieldMeta) 를 추가했다. Rule 조건·효과와 검증기가 쓴다
  (core/serialization/FieldAccess.hpp).
- JSON 읽기: 없는 키 = 기본값, 타입 불일치·범위 밖·모르는 키 = 오류 (문맥 "<컴포넌트>.<필드>").
  부호 없는 필드는 음이 아닌 부호 있는 JSON 정수도 받는다 (코드에서 만든 Json(100) 은 signed 로 저장된다 — Phase 3 수정).
- ComponentInfo 에 patchJson(원자적 패치: 복사본에 적용 후 성공 시에만 덮어씀)과 validateJson(검사만)을 추가했다 (Phase 3,
  명령 적용기의 원자성 근거). 복사할 수 없는 컴포넌트는 patchJson 이 nullptr 이다.
- 해시: float 은 round(x×1024), NaN/무한대/범위 밖은 고정 표식, 시퀀스는 길이 먼저.
- Hint::EntityRef 필드는 지금 EntityId.raw 를 그대로 JSON·해시에 쓴다 (세이브/로드 해시 동일성(D2)이 보장되지 않는다).
  그래서 Phase 5B 의 엔티티 참조(ai.behavior.target)는 EntityId 가 아니라 **saveId(u64) 필드**로 둔다 — 저장·해시·로드가
  그대로 맞고, 쓸 때 SystemContext.saves(saveId → EntityId)로 푼다. EntityRef 의 자동 변환은 `[계획]` (복제 Phase 10).
- (Phase 5B) 리플렉션하지 않은 필드 = 틱 캐시: 매 틱 쓰기 전에 다시 계산되는 값(ai.sensor.sensed, ai.behavior.pendingAction ·
  cacheIndex)은 reflect 에 넣지 않는다 → 저장·해시·복제되지 않는다. 다음 틱에 다시 계산되지 않는 값은 반드시 reflect 한다.
- nlohmann/json 은 Core 의 PUBLIC 의존이다 (JsonVisitor 가 헤더 템플릿). JSON_USE_IMPLICIT_CONVERSIONS=0.
```

## 1. 원칙

```text
Entity    = ID. 데이터도 로직도 없다.
Component = 순수 데이터. 로직(멤버 함수)은 자명한 계산(예: ratio())까지만.
System    = 로직. 상태가 필요하면 World Resource 로 등록한다.
```

| 규칙 | 내용                                                                                                      |
|------|-----------------------------------------------------------------------------------------------------------|
| C1   | 컴포넌트는 기본적으로 `std::is_trivially_copyable_v`. 예외(가변 컨테이너 소유)는 리뷰에서 명시적으로 승인 |
| C2   | 컴포넌트 안에 포인터·참조·OS/GPU 핸들 금지. 엔티티 참조는 `EntityId` 필드 + `Hint::EntityRef`             |
| C3   | 컴포넌트는 다른 컴포넌트를 소유하지 않는다                                                                |
| C4   | 컴포넌트 크기 목표 ≤ 64바이트. 넘으면 분할 검토 (캐시 라인)                                               |
| C5   | 모든 컴포넌트는 등록(9장)되어야 한다. 미등록 타입은 `emplace` 컴파일 에러                                 |

---

## 2. EntityId

```cpp
namespace sbx::ecs {
struct EntityId {
    static constexpr std::uint64_t kInvalid = ~0ull;
    std::uint64_t raw = kInvalid;                          // [ generation:32 | index:32 ]

    constexpr std::uint32_t index() const noexcept      { return std::uint32_t(raw); }
    constexpr std::uint32_t generation() const noexcept { return std::uint32_t(raw >> 32); }
    constexpr bool valid() const noexcept { return raw != kInvalid; }
    static constexpr EntityId make(std::uint32_t idx, std::uint32_t gen) noexcept {
        return { (std::uint64_t(gen) << 32) | idx };
    }
    friend constexpr auto operator<=>(EntityId, EntityId) = default;
};
}
```

| 결정       | 값          | 이유                                        |
|------------|-------------|---------------------------------------------|
| 크기       | 64비트      | 비교·해시 1회. 컴포넌트 필드로 8바이트      |
| index      | 32비트      | 50k 목표 대비 충분                          |
| generation | 32비트      | 같은 슬롯 40억 회 재사용 전 랩어라운드 없음 |
| 무효값     | 모든 비트 1 | `index = 0xFFFFFFFF`는 할당하지 않음        |

**EntityId는 프로세스 로컬입니다.** 네트워크에는 `NetEntityId`, 세이브에는 `saveId`를 씁니다 ([08](08-NETWORK.md), [09](09-SERIALIZATION.md)).

## 3. EntityManager

```text
create()   free list 가 비어 있지 않으면 LIFO 로 꺼내 재사용, 아니면 새 index.
destroy()  alive=false, generation++, free list push.
alive(id)  index < size && alive[index] && generation[index] == id.generation()
```

- LIFO 재사용: 최근 해제된 슬롯이 캐시에 남아 있을 가능성이 높고, **같은 명령 시퀀스 → 같은 id**라 결정적입니다.
- generation이 `0xFFFFFFFF`에 도달한 슬롯은 **퇴역**(free list에 넣지 않음)합니다. 실제로는 도달하지 않지만 랩어라운드로 옛 핸들이 되살아나는 경로를 막습니다.
- `destroy`는 Registry를 통해서만, **동기화 지점에서만** 일어납니다 (7장).

---

## 4. ComponentPool — Sparse Set

```text
ComponentPool<T>
  sparse   : 페이지 배열. page[index >> 12][index & 0xFFF] = denseIndex(u32) 또는 kNone
             페이지(4096 슬롯 × 4B = 16 KB)는 처음 쓰일 때 할당. 50k 엔티티 = 13 페이지
  dense    : std::vector<EntityId>     dense[i] 의 주인
  data     : std::vector<T>            dense 와 같은 순서 (T 단위 AoS)
  changed  : std::vector<Tick>         마지막 Write 접근 틱 (8장)
  added    : std::vector<Tick>         붙은 틱
```

| 연산                  | 복잡도         | 비고                                                                |
|-----------------------|----------------|---------------------------------------------------------------------|
| `emplace(e, args...)` | O(1) 분할 상환 | dense 끝에 추가. 이미 있으면 Debug 단언 실패                        |
| `remove(e)`           | O(1)           | **swap-and-pop** — 마지막 원소가 빈자리로 이동, dense 순서가 바뀐다 |
| `contains(e)`         | O(1)           | sparse 조회 + dense[i] == e 확인 (generation 포함)                  |
| `get(e)`              | O(1)           |                                                                     |
| 순회                  | O(n)           | dense 순서                                                          |

**불변식**

```text
P1. sparse[e.index] = i  ⇔  dense[i] == e
P2. dense.size() == data.size() == changed.size() == added.size()
P3. dense 에 같은 index 가 두 번 나오지 않는다
```

Debug 빌드는 매 구조 변경 후 P1~P3을 검사하는 `validate()`를 옵션으로 호출합니다 (테스트에서는 항상).

---

## 5. Registry

```cpp
class Registry {
public:
    // 엔티티
    EntityId create();                                   // 동기화 지점 전용 (ECB 적용, 로드, 테스트)
    void     destroy(EntityId);                          // 동기화 지점 전용
    bool     alive(EntityId) const;
    std::size_t aliveCount() const;

    // 컴포넌트 — 구조 변경은 동기화 지점 전용
    template<class T, class... A> T& emplace(EntityId, A&&...);
    template<class T> void remove(EntityId);

    // 접근 — 언제나 가능
    template<class T> bool      has(EntityId) const;
    template<class T> const T&  read(EntityId) const;     // 없으면 단언 실패
    template<class T> const T*  tryRead(EntityId) const;
    template<class T> T&        write(EntityId);          // changed[] = currentTick
    template<class T> T*        tryWrite(EntityId);

    // 순회
    template<class... Ts> auto view();                    // Read<T>/Write<T>/Exclude<T>
    // 리소스 (월드 단위 싱글턴)
    template<class R, class... A> R& emplaceResource(A&&...);
    template<class R> R& resource();
    // 틱
    void setCurrentTick(Tick);                            // SimulationClock 만 호출
    Tick currentTick() const;

    // 동적(리플렉션) 접근 — Editor, Serialization, Network, Hash 용
    ComponentPoolBase* pool(ComponentTypeId);
    ComponentPoolBase* poolByStableId(StableId);
};
```

**구조 변경 가드:** System 실행 중에는 Registry가 `m_structuralLock`을 올립니다. 이때 `create/destroy/emplace/remove`를
부르면 Debug에서 단언 실패, Release에서 로그 + 무시입니다. 구조 변경은 ECB로만 합니다 (7장).

---

## 6. View

```cpp
for (auto [e, tr, vel] : reg.view<Write<Transform>, Read<Velocity>, Exclude<Frozen>>()) {
    tr.position += vel.value * kFixedDt;
}
```

| 규칙 | 내용                                                                                                 |
|------|------------------------------------------------------------------------------------------------------|
| V1   | 드라이버 = 포함 컴포넌트 중 **가장 작은 풀**. 나머지는 `contains` 후 `get`                           |
| V2   | `Read<T>`는 `const T&`, `Write<T>`는 `T&`를 준다. Write는 그 엔티티의 `changed[]`를 현재 틱으로 갱신 |
| V3   | `Exclude<T>`는 해당 컴포넌트가 있는 엔티티를 건너뛴다                                                |
| V4   | 순회 중 구조 변경 금지 (5장 가드). 값 수정만 허용                                                    |
| V5   | 순회 순서는 드라이버 풀의 dense 순서. **게임 결과가 이 순서에 의존하면 안 된다** (12장 E1)           |
| V6   | `view.each(fn)`과 범위 for 둘 다 제공. `parallelEach(jobSystem, fn)`은 Phase 15                      |
| V7   | View는 경량 값 타입. 저장해 두지 않는다 (풀 포인터가 유효한 동안만)                                  |

Write 접근을 "만지기만 하고 안 바꾸는" 경우 거짓 dirty가 생깁니다. 대역폭 낭비일 뿐 정합성 문제는 아니므로 허용합니다.

---

## 7. EntityCommandBuffer (ECB)

```cpp
class EntityCommandBuffer {
public:
    PendingEntity create(PrefabId);                 // 같은 ECB 안에서만 유효한 임시 핸들
    PendingEntity createEmpty();
    void destroy(EntityId);
    template<class T> void emplace(EntityRef, T value);   // EntityRef = EntityId | PendingEntity
    template<class T> void remove(EntityId);
    void setParentEvent(EventTag);                  // 이 ECB 가 만든 이벤트의 원인 태그 (로그용)
};
```

| 결정                       | 내용                                                                                                               | 이유                                   |
|----------------------------|--------------------------------------------------------------------------------------------------------------------|----------------------------------------|
| 소유                       | System마다 1개(병렬 시 Job마다 1개)                                                                                | 동시 쓰기 없음                         |
| 적용 시점                  | 파이프라인의 `ApplyStructuralChanges` 단계 ([03](03-SIMULATION.md) 2장)                                            | 순회 무효화 방지                       |
| 적용 순서                  | (System 실행 순서, ECB 내 기록 순서)                                                                               | 결정론                                 |
| PendingEntity              | 적용 시 실제 EntityId로 치환. 다른 ECB에서 참조 불가                                                               |                                        |
| 같은 틱 생성→파괴          | 상쇄, 복제·이벤트에 나타나지 않음                                                                                  |                                        |
| 이미 죽은 엔티티 destroy   | 무시 (중복 사망 허용)                                                                                              | 두 System이 같은 엔티티를 죽일 수 있다 |
| 이미 죽은 엔티티에 emplace | 무시 + Debug 경고                                                                                                  |                                        |
| destroy의 효과             | 모든 풀에서 제거 → generation++ → `destroyedLog`에 (EntityId, NetEntityId, saveId) 기록 → `EntityDestroyed` 이벤트 | 복제가 despawn을 안다                  |

---

## 8. 변경 추적

```text
pool<T>.changed[i]   마지막 Write 접근 틱
pool<T>.added[i]     붙은 틱
Registry.destroyedLog 이번 틱 파괴 목록 (EndTick 에 비움)
Registry.removedLog<T> 이번 틱에 T 가 제거된 엔티티 목록 (EndTick 에 비움)
```

질의:

```cpp
pool.forEachChangedSince(Tick baseline, fn);   // 복제 Delta, 증분 세이브, Inspector 갱신
```

Dirty bit가 아니라 **틱**을 기록하는 이유: 클라이언트마다 ack한 baseline이 다릅니다.

**Phase 10A — 변경 순번 (ADR-0025):** changed/added 에 들어가는 값은 `Registry::currentTick()` 이고, SimulationWorld 는
여기에 틱 번호가 아니라 `changeStamp()` 를 넣는다 — tick() 마다 `max(이전 + 1, 틱 번호)` 로 오르는 순번이라 일시정지 편집
단계(틱 번호가 그대로)의 변경도 다른 값으로 찍힌다. 일시정지가 없으면 틱 번호와 같다. 복제는 `changed > 기록의 stamp` 로
고른다 (08 6.1). 변경 기록은 해시 · 세이브에 들어가지 않는다 (결정론 골든과 무관). `removedLog<T>` 는 쓰지 않는다 — 복제는
mask(붙은 컴포넌트 비트) 비교로 떼기를 안다.

**최적화 `[계획]` (측정 후):** 50k에서 `forEachChangedSince`가 전체 dense를 훑는 비용이 문제면
틱별 변경 목록(append-only) 또는 청크별 변경 비트맵을 추가합니다.

---

## 9. 컴포넌트 등록과 리플렉션

### 9.1 선언

```cpp
// core/components/core/Transform.hpp
namespace sbx::comp {
struct Transform {
    Vec2  position;
    float rotation = 0.f;
};

template<class V> void reflect(V& v, Transform& c) {
    v.field("position", c.position, Hint::Position);
    v.field("rotation", c.rotation, Hint::Angle);
}
}
SBX_COMPONENT(sbx::comp::Transform, "core.transform", /*version*/ 1,
              Flags::Replicated | Flags::Persistent | Flags::Hashed | Flags::EditorVisible);
```

### 9.2 식별자

| 이름              | 타입   | 용도                         | 안정성                                                   |
|-------------------|--------|------------------------------|----------------------------------------------------------|
| `ComponentTypeId` | u16    | 런타임 배열 인덱스           | 등록 순서에 따름 — **저장·전송·해시에 쓰지 않는다**      |
| `stableId`        | u64    | `FNV-1a64("core.transform")` | 영구. 세이브·네트워크·해시·리플레이                      |
| `name`            | string | `"core.transform"`           | 영구. JSON·로그·에디터                                   |
| `version`         | u16    | 필드 구조 버전               | 바꾸면 마이그레이션 등록 ([09](09-SERIALIZATION.md) 6장) |

`stableId` 해시 함수는 **동결**합니다. 단위 테스트가 `stableId("core.transform") == 0x…`를 상수로 단언합니다
(RTS `TypeId::stableHash()` 동결 규칙 계승).

이름 규칙: `<namespace>.<name>`, 소문자·숫자·`_`·`.`만. 엔진 예약 네임스페이스: `core ai life society combat net persist render editor`.

### 9.3 Flags

| Flag            | 의미                                                          |
|-----------------|---------------------------------------------------------------|
| `Replicated`    | 서버→클라 복제 대상                                           |
| `Persistent`    | 세이브 대상                                                   |
| `Hashed`        | WorldHash 대상 — **Persistent면 기본 켜짐**, 끄려면 사유 주석 |
| `EditorVisible` | Inspector에 표시                                              |
| `ServerOnly`    | 서버에만 존재 (예: 경로 탐색 내부 상태). 복제 안 함           |
| `ClientOnly`    | 클라에만 존재 (예: `InterpolatedTransform`, `Selected`)       |
| `Opaque`        | 이 프로세스에 정의가 없는 컴포넌트를 바이트로 보존 (9.5)      |

### 9.4 Visitor

```cpp
struct FieldVisitorConcept {
    template<class F> void field(std::string_view name, F& value, Hint = {}, FieldMeta = {});
};
// 구현체: JsonWriter, JsonReader, BinaryWriter, BinaryReader, BitWriter(양자화), BitReader,
//         HashVisitor, ImGuiInspector, DiffVisitor(필드 단위 비교), FieldPathResolver(ChangeComponent 적용)
```

지원 필드 타입: `bool, i8~i64, u8~u64, float, double, Vec2, Vec2i, Color, EntityId, PrefabId, TagSet,
FixedString<N>, std::array<T,N>, Enum(리플렉션된 enum)`. 가변 컨테이너는 `SmallVector<T,N>`만 (C1 예외 승인 시).

`FieldMeta`: 범위(min/max), 단위, 양자화 정밀도, 에디터 툴팁. 네트워크 양자화와 Inspector 슬라이더가 같은 정보를 씁니다.

### 9.5 Opaque Component

서버나 도구가 정의를 모르는 컴포넌트(예: `render.sprite`)를 Prefab·세이브·명령에서 만나면
`OpaqueComponent{stableId, version, bytes}`로 보존합니다. 저장·복제·Inspector(원시 JSON 표시)는 되고, 시스템은 읽지 못합니다.

---

## 10. Resource (월드 싱글턴)

`SpatialIndex`, `IntentBuffer`, `EventStream`, `RandomService`, `PathfindingService`처럼 월드에 하나뿐인 상태는
엔티티가 아니라 Resource입니다. `reg.resource<R>()`로 접근합니다.

규칙: 시뮬레이션 상태를 담는 Resource는 **세이브·해시 대상인지 문서에 명시**합니다. (예: `RandomService`는 시드만, `SpatialIndex`는 파생 데이터라 대상 아님)

## 11. EventStream

```text
틱 내 이벤트: EntitySpawned, EntityDestroyed, InteractionApplied{rule, source, target}, Custom{name, payload}
수명: 한 틱. EndTick 에 비움.
소비자: ReplicationSystem(클라에 이벤트 전달 → 오디오·이펙트), 로그, 테스트
```

이벤트는 게임 상태가 아닙니다. **System이 이벤트를 읽어 게임 상태를 바꾸면 안 됩니다** — 그건 Intent나 컴포넌트로 표현합니다.

---

## 12. 순회 순서와 결정론

```text
E1. 게임 결과가 순회 순서에 의존하면 안 된다.
    → 경쟁 상호작용(먹기, 공격, 획득)은 Intent 로 모으고 Resolve 에서 (target saveId, source saveId) 오름차순으로 결정.
E2. WorldHash 와 Save 는 dense 순서가 아니라 EntityId.index 오름차순(해시) / saveId 오름차순(세이브)으로 순회한다.
E3. "가장 가까운 대상" 같은 선택의 tie-break 는 saveId 비교로 끝낸다.
E4. unordered_map/set 을 순회하면서 시뮬레이션 상태를 바꾸지 않는다. ([04](04-DETERMINISM.md))
```

---

## 13. Prefab 인스턴스화

```text
ecb.create(prefabId)
  → 적용 시: entity = create()
             for (stableId, json) in prefab.components (stableId 오름차순):
                 정의가 있으면 기본값 + JSON 덮어쓰기로 emplace
                 정의가 없으면 Opaque 로 emplace
             Tag 컴포넌트 = prefab.tags
             Persistence.saveId = world.nextSaveId++
             NetIdentity.netId  = (서버) nextNetId++
```

Prefab 스키마: [11-CONTENT-SCHEMA](11-CONTENT-SCHEMA.md) 2장.

---

## 14. 기본 컴포넌트 카탈로그 (Phase 5B 까지 구현 — 0장. society.* · combat.* · client.* 는 `[계획]`)

구현과 다른 점: `ai.path_request`·`ai.path_follow` → `ai.path` 하나(start · goal · state · submittedTick · partial · waypoints ·
cursor). `ai.sensor` 는 radius 만 (마스크는 Behavior 그래프의 감지 질의, 11 0장) · Persistent. `ai.behavior` 는
graph · state · enteredTick · target(saveId) · blackboard[8]. `core.movement` 에 goal · hasGoal. `core.collider` 의 layer·mask 는 u32 비트.

| stableId                                   | 필드(요약)                                                                                                                   | Flags                   |
|--------------------------------------------|------------------------------------------------------------------------------------------------------------------------------|-------------------------|
| `core.transform`                           | position, rotation                                                                                                           | R P H E                 |
| `core.velocity`                            | value                                                                                                                        | R P H                   |
| `core.movement`                            | maxSpeed, accel, arriveRadius                                                                                                | P H E                   |
| `core.collider`                            | radius, layer, mask                                                                                                          | P H E                   |
| `core.lifetime`                            | expireTick                                                                                                                   | P H                     |
| `core.tags`                                | TagSet(비트셋, 콘텐츠 Tag 테이블 인덱스)                                                                                     | R P H E                 |
| `life.health`                              | value, max                                                                                                                   | R P H E                 |
| `life.energy`                              | value, max, drainPerSecond                                                                                                   | R P H E                 |
| `life.growth`                              | stage, progress, rate                                                                                                        | R P H E                 |
| `life.reproduce`                           | energyCost, cooldown, cooldownLeft, offspring(PrefabId), minEnergy, chance, minStage, litter, mode, crowdRadius·crowdMax(5C) | P H E                   |
| `life.age`                                 | ageTicks, maxAgeTicks                                                                                                        | P H E                   |
| `ai.sensor`                                | radius, mask(TagSet), detected(SmallVector<EntityId,8>)                                                                      | S                       |
| `ai.behavior`                              | graph(BehaviorId), state, enteredTick, blackboard[4]                                                                         | P H E                   |
| `ai.path_request`                          | goal, requestedTick                                                                                                          | S                       |
| `ai.path_follow`                           | waypoints(SmallVector<Vec2i,16>), cursor                                                                                     | S P H                   |
| `society.faction`                          | factionId                                                                                                                    | R P H E                 |
| `society.inventory`                        | slots(SmallVector)                                                                                                           | P H E                   |
| `society.resource`                         | resourceId, amount, regenPerSecond                                                                                           | R P H E                 |
| `society.production`                       | queue, progress                                                                                                              | P H E                   |
| `combat.combat`                            | damage, range, cooldown, cooldownLeft                                                                                        | P H E                   |
| `net.identity`                             | netId, owner(ClientId)                                                                                                       | (내부)                  |
| `persist.persistence`                      | saveId                                                                                                                       | (내부)                  |
| `render.sprite` *(Client 정의)*            | material(AssetId), size, layer, tint                                                                                         | R P E · 서버에선 Opaque |
| `client.interpolated_transform` *(Client)* | prev, curr, renderPos                                                                                                        | C                       |
| `editor.selected` *(Client)*               | —                                                                                                                            | C                       |

범례: R Replicated · P Persistent · H Hashed · E EditorVisible · S ServerOnly · C ClientOnly.

---

## 15. 필수 테스트 ([13-TESTING](13-TESTING.md))

```text
- EntityId 재사용: destroy 후 같은 index 의 옛 핸들이 alive()==false
- 퇴역 슬롯: generation 최대값 슬롯이 재사용되지 않음
- Pool 불변식 P1~P3: 무작위 emplace/remove 10만 회 후 validate()
- 속성 테스트: Registry 와 단순 참조 모델(std::map 기반)을 무작위 연산으로 대조
- View: 포함/제외 조합, 가장 작은 풀 드라이버 선택
- Write 접근 시 changed[] 갱신, Read 는 갱신 안 함
- ECB: PendingEntity 치환, 생성→파괴 상쇄, 적용 순서
- 구조 변경 가드: System 실행 중 create() 호출 시 단언
- stableId 동결 상수
- 리플렉션 Visitor 왕복: JSON / Binary / Bit 각각 write→read == 원본
```

## 16. 나중에 (측정 후 결정)

```text
- Owning group / Archetype: 다중 컴포넌트 순회가 tick 예산의 30% 초과 시 ([14](14-PERFORMANCE.md))
- 병렬 순회 parallelEach: Phase 15
- 계층(Parent/Child) 컴포넌트: 콘텐츠가 요구할 때 (예: 차량-탑승자)
```
