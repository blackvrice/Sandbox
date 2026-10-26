# ADR-0059. 스크립트 컴포넌트 — defineComponent 상수 스키마, 바이트 배치 동적 풀, 필드 이름 순, 같은 방문자 → 같은 형식, 콘텐츠마다 카탈로그

- 상태: **Accepted** · 날짜: 2026-10-26 (D-03 결정)
- 관련: [ADR-0011](0011-explicit-component-registration.md) (명시 등록 — 이 ADR 이 동적 등록을 더한다), [ADR-0015](0015-ai-tick-caches-saveid-refs-path-jobs.md)
  (개체 참조 = saveId), [ADR-0056](0056-sandboxscript-language-subset-own-compiler-no-gc.md) (최상위는 선언만 · 틱을 넘는 상태는 컴포넌트),
  [ADR-0057](0057-host-api-single-definition-cpp-table-sdk-generated.md) (Host 표 · 쓰기 버퍼), [ADR-0058](0058-bytecode-register-machine-typed-registers-verifier.md)
  (구조체 = 필드 이름 순), [02-ECS](../02-ECS.md) 9장, [04-DETERMINISM](../04-DETERMINISM.md) 5장, [19-SANDBOXSCRIPT](../19-SANDBOXSCRIPT.md) 15장,
  [18-PLATFORM](../18-PLATFORM.md) 6.2 (v0.3 M3), 초안 [architecture/05](../platform/architecture/05_COMPONENT_SYSTEMS.md) 3 · 6장

## 맥락

v0.3 의 완료 기준은 "스크립트로 Mana 컴포넌트 + 재생 시스템" 이다. 지금 컴포넌트는 C++ 타입뿐이다 — `SBX_COMPONENT` 특수화와
`reflect` 함수 하나가 JSON · 해시 · 바이트 형식 · Inspector 필드 목록이 되고, 카탈로그 (ADR-0011) 의 `ComponentInfo` 는 그 함수들의
**함수 포인터** 표다 (타입마다 람다 하나 — 문맥이 없다). 스크립트가 정의하는 컴포넌트는 C++ 타입이 없고, 로드 때에야 모양을 안다.
정할 것 (D-03): 스크립트가 컴포넌트를 어떻게 적나, 정규 스키마의 모양, 저장소, 세이브 · 해시 · 복제 형식, 카탈로그에 넣는 법, 콘텐츠
로드 순서.

## 결정

```text
1. 선언 = 최상위 const 의 defineComponent 호출, 인자는 모두 상수 (검사기가 스키마로 바꾼다 — 실행하지 않는다):
     import { defineComponent, field } from "sandbox";
     export const Mana = defineComponent({
       id: "rpg.mana", version: 1,
       fields: { current: field.f32(100, { min: 0, max: 1000 }), maximum: field.f32(100), state: field.enumOf(ManaState.Idle) },
     });
   필드 종류 7 = field.i32 · f32 · f64 (기본값?, { min, max }?) · bool (기본값?) · vec2 (x?, y?) · entity () · enumOf (E.멤버).
   선택 키 persistent · replicated · editorVisible (기본 true) · serverOnly (기본 false — 켜면 replicated 기본 false, 둘 다
   켜면 오류). 함수 안 · 다른 식 안의 defineComponent · field 는 오류. 컴포넌트 const 는 지금 값으로 쓸 수 없다 — Host 의
   ctx.read · ctx.query 인자가 첫 자리 (v0.3 M3-T3)
2. 정규 스키마 (script/api/ComponentSchema — Foundation 만, 엔진 쪽은 core/script 가 ecs 설명으로 옮긴다):
   id        컴포넌트 이름 규칙 (02 9.2 — 소문자 · 숫자 · _ · 점, 점 하나 이상) + 엔진 이름공간 금지 (core · ai · life · game ·
             net · persist · debug · render · editor · client · server · script · sandbox · sbx). 한 프로그램 안에서 하나
   version   1 ~ 65535. 세이브의 컴포넌트 버전 표 · 해시에 들어간다 (내장과 같다)
   fields    ≤ 64, 이름 = 식별자, **이름 순으로 정렬** (선언 순서를 바꿔도 같은 컴포넌트 — ADR-0058 의 구조체와 같은 규칙).
             기본값은 범위 안, i32 기본값 · 범위는 정수, f32 기본값은 f32 로 한 번 반올림 (검사기 · 엔진이 같은 규칙)
   값 타입   i32 → i32, f32 · f64 → number (f32 는 읽을 때 넓히고 쓸 때 반올림 — ADR-0056 3), bool → boolean,
             vec2 → Vec2 { x, y: number }, entity → Entity | null, enumOf → 그 enum
3. 저장소 = DynamicComponentPool (core/ecs): ComponentPoolBase 의 sparse set (P1~P3 · changed · added 그대로) + 바이트 배열
   (칸 하나 = stride 바이트). 배치 = 이름 순, 종류마다 자연 정렬 (i32 · f32 · enum 4, f64 · entity 8, bool 1, vec2 = f32 둘),
   stride = 8 의 배수. entity 칸 = saveId (u64, 0 = null — ADR-0015 의 ai.behavior.target 과 같다: 세이브 · 로드 뒤에도 같은 개체)
4. 형식 = 같은 방문자: 필드마다 칸을 타입 있는 임시값 (i32 · f32 · f64 · bool · Vec2 · u64) 으로 읽어 내장 컴포넌트가 쓰는 방문자
   (JsonWriter · JsonReader · HashVisitor · BinaryWrite/ReadVisitor · FieldListVisitor · NumberGet/SetVisitor) 의 field() 를
   부르고 되쓴다 (reflectDynamic). → JSON · WorldHash · 복제 바이트 · Rule 의 field.add/set · Inspector 필드 목록이 같은 필드를
   이름 순으로 가진 C++ 컴포넌트와 **바이트 단위로 같다** (시험이 지킨다). 범위 밖 · 모르는 키 · 타입 오류도 같은 규칙
5. 카탈로그: ComponentInfo 의 함수 칸을 함수 포인터에서 std::function 으로 (문맥 — 배치 — 을 잡는다. 부르는 쪽은 그대로).
   ComponentInfo::dynamicLayout (shared_ptr<const DynamicComponentLayout>) 이 이름 · 필드 이름 문자열을 살려 두고 Host 가 배치를
   찾는다 (내장은 null). catalog.addDynamic(설명) = add<T>() 와 같은 넣기 규칙 (같은 이름 AlreadyExists · stableId 충돌
   ValidationFailed). 스크립트 컴포넌트의 ComponentTypeId = 프로세스 표 (이름 → 번호, 같은 이름 = 같은 번호 — 카탈로그 여럿이어도
   Registry 의 풀 칸이 하나). 번호는 여전히 영속하지 않는다 (stableId = fnv1a64(id) 가 영속)
6. 콘텐츠마다 카탈로그 [계획 v0.3 M3-T2]: 월드가 쓰는 카탈로그 = 엔진 카탈로그 (registerCoreComponents) 의 복사 + 그 콘텐츠
   팩들의 스크립트 컴포넌트. 로드 차례: 팩 순서로 scripts/ 를 모아 컴파일 (팩마다 프로그램 하나) → 컴포넌트 등록 (두 팩의 같은 id
   = 오류, 덮어쓰기 없음) → Prefab · 규칙 · 트리거 검사 (V3 가 스크립트 컴포넌트를 안다) → 월드. 카탈로그는 로드 뒤 불변이고
   콘텐츠와 같이 산다. 스크립트가 없는 콘텐츠의 카탈로그 = 엔진 카탈로그와 같은 내용 → 골든 · kSimVersion 그대로
7. 이 ADR 밖 (계획): Host 의 읽기 · 질의 · 쓰기 버퍼 (ADR-0057 5 — M3-T3), 스키마 version 을 올린 세이브의 필드 마이그레이션
   (더하기 = 기본값, 빼기 · 종류 바꾸기 = 로드 오류나 Opaque — M3-T4), sandbox.d.ts 의 팩별 컴포넌트 타입 (SDK 는 일반 선언만 —
   팩의 타입은 그 팩의 소스가 TS 에 준다)
```

## 근거

```text
- 상수 스키마: 스크립트 최상위는 선언뿐이다 (ADR-0056 4). defineComponent 를 실행하지 않고 검사기가 읽으면 VM 없이 sbx check ·
  에디터 · 서버 로드가 같은 스키마를 얻는다. TS 쪽은 d.ts 의 제네릭 선언 하나로 ctx.read 의 결과 타입이 나온다
- 이름 순: 필드 순서가 해시 · 바이트 형식의 구조다 (HashVisitor — "순서가 곧 구조"). 사용자가 선언 순서를 바꾸는 일은 흔하고, 그때
  세이브 · 해시가 바뀌면 놀랍다. VM 의 구조체 배치도 이름 순이라 규칙이 하나다
- 같은 방문자: 형식 규칙 (float 해시 = round(x × 1024), JSON 정수 범위, f32 비트, zigzag) 을 두 번 쓰지 않는다. 새 규칙이 생기면
  내장과 스크립트가 같이 바뀐다. 비용은 필드마다 임시값 복사 하나
- std::function: 함수 포인터 표에 문맥을 더하는 다른 길 (모든 함수에 const ComponentInfo& 를 더하기) 은 부르는 곳 ~30 군데를
  바꾼다. std::function 은 부르는 모양이 같고, 비용은 간접 호출 하나 (해시 · 세이브는 개체 × 풀마다 한 번 — 벤치로 확인)
- saveId 칸: EntityId 는 세이브 · 로드 뒤 달라진다. 엔진은 이미 개체 참조를 saveId 로 둔다 (ADR-0015) — 같은 규칙이면 세이브 ·
  해시 · 복제가 손대지 않고 맞는다 (복제는 saveId 를 그대로 보낸다 — 클라이언트 표시용 변환은 M3-T3 의 읽기에서)
- 콘텐츠마다 카탈로그: 전역 카탈로그에 넣으면 한 프로세스의 두 월드 (에디터의 편집 · 테스트 플레이, 도구) 가 섞인다
```

## 결과

- 얻는 것: 내장과 같은 세이브 · 해시 · 복제 · Inspector 경로, 선언 순서에 견디는 형식, VM 없이 아는 스키마, 스크립트 없는 월드는 그대로.
- 포기하는 것: 문자열 · 배열 필드 (v0.3 은 고정 크기만 — 문자열 · 배열은 [v0.4 뒤 검토]), 필드별 권한 (초안 3장 permissions —
  [v0.4 뒤 검토]), 계산된 기본값 (상수만).
- 위험: ComponentInfo 를 std::function 으로 바꾸어 해시 · 세이브가 느려질 수 있다 — 완화: sbx_bench 의 해시 · 세이브 측정을 남긴다.
  프로세스 표의 번호가 이름마다 늘어난다 (상한 65535 — 같은 이름을 다시 쓰므로 실제로는 팩의 컴포넌트 수).

## 대안

| 대안                                              | 기각 사유                                                                              |
|---------------------------------------------------|----------------------------------------------------------------------------------------|
| 스키마를 JSON 파일 (components/*.json) 로          | 스크립트와 두 곳에 같은 모양을 적는다 — TS 타입이 스키마에서 나오지 않는다             |
| defineComponent 를 VM 이 실행해 스키마를 만든다    | 로드에 VM 이 필요하고, 최상위 실행 (전역 상태) 을 허락하게 된다                         |
| 선언 순서 배치                                    | 순서를 바꾸면 세이브 · 해시가 바뀐다 (4 근거)                                           |
| 필드마다 따로 쓴 JSON · 해시 · 바이트 코드         | 형식 규칙을 두 벌 유지한다 — 어긋나면 세이브 · 복제가 조용히 다르다                    |
| 필드를 f64 칸 하나씩 (모두 number)                 | f32 · i32 의 형식이 내장과 달라지고, 메모리가 두 배                                     |
| 전역 카탈로그에 등록                              | 한 프로세스의 월드들이 섞인다 (6 근거), 카탈로그가 실행 중에 바뀐다 (ADR-0011 의 결정성) |

## 재검토 조건

문자열 · 배열 · 중첩 구조 필드가 필요한 데모가 생기면 (가변 크기 칸 — 해시 · 바이트 형식은 SmallVector · FixedString 의 규칙으로),
필드 권한 (읽기 전용 · 서버만) 이 요구되면, sbx_bench 에서 std::function 비용이 해시 · 세이브 시간의 의미 있는 부분이면 (함수
포인터 + 문맥 포인터로).
