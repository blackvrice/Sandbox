# ADR-0060. 스크립트의 컴포넌트 Host 항목 — 제네릭 ABI · ComponentRef = stableId (LOADK) · 질의 = Entity[] (saveId 순) · 값 통째 쓰기 + 호출 단위 버퍼 · 호출마다 개체 핸들 표

- 상태: **Accepted** · 날짜: 2026-10-26
- 관련: [ADR-0057](0057-host-api-single-definition-cpp-table-sdk-generated.md) (Host 표 · 쓰기 버퍼 — 이 ADR 이 4 의 Query 반복자와 초안의
  setField 를 바꾼다), [ADR-0058](0058-bytecode-register-machine-typed-registers-verifier.md) (바이트코드 — 형식 2),
  [ADR-0059](0059-script-components-schema-dynamic-pool-name-order-catalog.md) (스크립트 컴포넌트), [04-DETERMINISM](../04-DETERMINISM.md),
  [19-SANDBOXSCRIPT](../19-SANDBOXSCRIPT.md) 10 · 12 · 13 · 14 · 15장, [18-PLATFORM](../18-PLATFORM.md) 6.2 (v0.3 M3-T3)

## 맥락

스크립트 컴포넌트 (ADR-0059) 는 정의 · 저장 · 콘텐츠 로드까지 되었다. 스크립트가 그것을 읽고 써야 Mana 재생 시스템 (v0.3 완료 기준) 이
된다. ADR-0057 은 원칙 (읽기 = 값 복사, 질의 = saveId 순, 쓰기 = 호출 단위 버퍼 · 트랩이면 버림, 핸들은 호출 안에서만) 만 정했다.
정할 것: Host 표는 고정된 ABI 타입의 표인데 컴포넌트마다 값 모양이 다르다 (제네릭), 컴포넌트 참조를 바이트코드에 어떻게 싣나,
구조체 값을 VM 경계에서 어떻게 넘기나 (믿지 않는 바이트코드), 질의 결과의 모양, 쓰기의 단위, 개체 핸들의 표현.

## 결정

```text
1. 항목 넷 (서버 도메인): ctx.read(entity, ref): T | null · ctx.has(entity, ref): boolean · ctx.query(ref): Entity[] ·
   ctx.write(entity, ref, value: T): void. ref = defineComponent 의 const (ComponentRef<T>)
2. 제네릭 ABI: 새 ABI 타입 Component (ComponentRef<T>) · ComponentData (T) · ComponentDataOrNull (T | null) · EntityList
   (Entity[]). 한 항목 안의 T 는 Component 인자 하나가 정한다 — 검사기가 그 인자의 타입 (TypeKind::Component, elem = 값 구조체)
   으로 나머지를 푼다. d.ts 는 메서드에 <T> 를 붙인다. 표 항목을 더했을 뿐이라 kHostApiVersion 은 1 그대로 (지문은 바뀐다)
3. ComponentRef 값 = stableId (fnv1a64(id)). 바이트코드에 컴포넌트 표 (id 문자열 — 처음 쓰는 차례) 와 명령 LOADK a ← components[b]
   를 더한다 → .sbc 형식 2 (형식 1 은 VersionMismatch — 소스에서 다시 컴파일, 패키지에 소스가 있다). 검증기: 표의 id 는 이름 규칙 ·
   겹침 없음, LOADK 의 대상은 불투명 Component · 번호는 표 안. Host 는 그 stableId 를 월드의 카탈로그에서 찾고 스크립트 컴포넌트
   (dynamicLayout) 만 받는다 — 엔진 컴포넌트의 ComponentRef 는 [계획] (권한 · 엔진 관리 컴포넌트 검사와 함께)
4. 구조체 값 = 칸 목록 (VM 이 펼치고 만든다): 필드 이름 순, 중첩 구조체 (Vec2) 는 그 자리에서, Entity | null 상자는 null = 0 ·
   아니면 핸들. VM 은 레지스터 타입을 따라 펼치고 만든다 — 잎은 i32 · f64 · bool · 불투명 · 상자 (불투명) · 중첩 구조체 (깊이 4)
   만, 그 밖 (문자열 · 배열 · 재귀) 이나 칸 수가 다르면 HostError. 믿지 않는 바이트코드가 Host 값을 참조로 읽게 하지 않는다
   (메모리 안전은 VM 이, 뜻 (종류 · 범위) 은 Host 가 컴포넌트 배치로 본다)
5. 질의 = Entity[] (배열): ADR-0057 4 의 "호출 안에서만 유효한 반복자 Query" 대신. for-of · length · 첨자가 그대로 되고 새 타입 ·
   명령이 없다. 순서 = saveId 오름차순, 상한 65,536 개 (넘으면 HostError), 연료 = 16 + 개체마다 1 (HostResult::extraFuel)
6. 쓰기 = 값 통째 (ctx.write(e, ref, T)): 초안의 ctx.commands.setField(e, ref, "필드", 값) 대신 — 필드 이름 문자열이 없어 타입
   검사 · d.ts 가 일반 규칙 그대로다. 읽기 → 고치기 → 쓰기가 기본 모양. 값은 쓸 때 바이트로 검사한다: 범위 (FieldMeta) ·
   유한 (NaN · ±Infinity 는 JSON 세이브에 없다) · f32 범위와 한 번 반올림 · 개체 참조 (이 호출의 핸들 → saveId). 있는 컴포넌트의
   값만 (붙이기 · 떼기 · 생성 · 파괴는 [계획 M4] ECB 와 함께)
7. 호출 단위 버퍼 (ADR-0057 5): (개체, 컴포넌트) 마다 마지막 값 하나, 처음 쓴 차례. 같은 호출의 read 는 버퍼를 먼저 본다
   (read-your-writes). endCall(true) — 트랩 없이 끝남 — 가 차례대로 풀에 쓰고 (changed = Registry 의 지금 표시), endCall(false) 는
   버린다. 호출 안에는 구조 변경이 없으므로 적용할 자리는 그대로 있다. 상한 65,536 개
8. 개체 핸들 = 호출마다 새 표: 핸들 = 표지 0x5E00 | 호출 번호 아래 16 비트 | 자리 + 1. 같은 개체 = 같은 핸들 (같음 비교가
   된다), 지난 호출 · 위조 · 죽은 개체의 핸들은 HostError. 개체 참조 필드는 saveId 로 저장하고 (ADR-0059 3) 읽을 때 핸들로
   (없는 · 죽은 개체 = null). ScriptHost::entityHandle 로 시스템 단계 (M4) 가 인자 개체를 넘긴다
```

## 근거

```text
- 제네릭 ABI: 컴포넌트마다 표 항목을 만들면 표가 콘텐츠에 묶인다 (표는 엔진 빌드의 것). T 를 인자 하나로 정하면 검사기 규칙이
  짧고 tsc 의 제네릭과 같은 뜻이다
- stableId 를 값으로: 이미 세이브 · 복제 · 카탈로그의 영속 열쇠다. 바이트코드에는 id 문자열을 두어 읽는 사람 · 역어셈블이 알아보고,
  값은 로드 때 한 번 해시한다
- 칸 목록 + VM 쪽 모양 검사: 검증기는 구조체가 어떤 컴포넌트인지 모른다 (T 는 검사기만 안다). 잎 종류를 제한하면 위조한 레지스터
  타입이 메모리 안전을 깨지 못하고, 최악은 같은 칸 수의 다른 해석 — 결정적이다
- 배열 질의: 반복자 타입은 VM 에 새 객체 종류 · 명령 · 무효화 규칙이 필요하다. 호출이 짧고 (연료) 결과가 상한 안이라 배열 복사로 충분하다.
  비용은 v0.3 M6 벤치 (10k) 로 잰다
- 통째 쓰기: 필드 이름 문자열은 오타가 실행 때에야 드러나고 d.ts 로 표현하려면 keyof 가 필요하다. 값 복사 의미 (쓴 뒤 고친 지역 값은
  버퍼에 들지 않는다) 가 트랩 되돌리기와 맞다
- 호출마다 핸들 표: EntityId 는 64 비트 (세대 · 자리) 라 표지를 붙일 자리가 없고, 핸들을 그대로 내보내면 세이브 · 로드 뒤 달라지는 값이
  스크립트에 보인다. 표는 호출 안에서 결정적이고 비교가 된다
```

## 결과

- 얻는 것: Mana 재생 같은 시스템을 쓸 수 있는 읽기 · 질의 · 쓰기, 트랩 되돌리기, 저장 · 로드에 견디는 순서 · 참조, VM 경계의 메모리 안전.
- 포기하는 것: 필드 하나만 쓰기 (통째 — 읽기가 하나 더), 질의의 지연 반복 (배열 복사), 여러 컴포넌트 질의 (하나씩 — [검토 M6]),
  엔진 컴포넌트 접근 ([계획]).
- 위험: 질의마다 saveId 정렬 — 완화: M6 벤치, 재검토 조건. 버퍼 적용이 틱 밖 (시험) 에서 일어나면 복제의 changed 표시가 이미 보낸
  기준과 같을 수 있다 — M4 의 시스템 단계 (틱 안) 에서 부르면 해당 없다.

## 대안

| 대안                                         | 기각 사유                                                                          |
|----------------------------------------------|------------------------------------------------------------------------------------|
| 컴포넌트마다 Host 표 항목                    | 표가 콘텐츠에 묶이고 지문이 콘텐츠마다 다르다                                      |
| ComponentRef = 로드 때 묶는 번호              | 번호가 영속하지 않아 바이트코드 · 디버깅에서 알아볼 수 없다 — id 문자열 + stableId 가 같은 일을 한다 |
| 질의 반복자 (Query 객체)                     | VM 객체 종류 · 명령 · 무효화 규칙이 는다 (5 근거)                                   |
| setField(e, ref, "필드", 값)                 | 필드 이름 문자열 — 검사 · d.ts 가 특별 규칙 (6 근거)                                |
| 쓰기를 바로 풀에                             | 트랩 되돌리기가 없다 (ADR-0057 5)                                                  |
| EntityId 를 핸들로 그대로                    | 세이브 · 로드 뒤 다른 값, 위조 · 지난 핸들을 가릴 수 없다                           |

## 재검토 조건

질의 정렬 · 배열 복사가 M6 벤치 (10k) 에서 틱 예산의 의미 있는 부분이면 (saveId 순 색인 · 지연 반복), 필드 하나 쓰기가 자주 필요하면
(ctx.set(e, ref, { 부분 }) — Partial 타입), 엔진 컴포넌트 (core.transform 등) 를 스크립트가 읽어야 하면 (내장 ComponentRef · 권한 표).
