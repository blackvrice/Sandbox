# ADR-0057. Host API 단일 정의 — C++ 표 하나에서 타입 검사 · 로드 묶기 · 바인딩 검사 · sandbox.d.ts 를 만든다, 쓰기는 호출 단위 버퍼

- 상태: **Accepted** · 날짜: 2026-10-21 (D-04 결정) — 4 의 Query 반복자 · 초안의 setField 는 [ADR-0060](0060-script-component-host-read-query-write-buffer-entity-handles.md) 이 대신한다 (Entity[] · 값 통째 쓰기)
- 관련: [ADR-0056](0056-sandboxscript-language-subset-own-compiler-no-gc.md) (언어 — D-01), [ADR-0011](0011-explicit-component-registration.md)
  (명시 등록), [ADR-0050](0050-visual-trigger-schema-tree-graph-flat-program.md) (노드 종류 표 — 같은 방식), [04-DETERMINISM](../04-DETERMINISM.md),
  [03-SIMULATION](../03-SIMULATION.md) 2 · 2.2장, [18-PLATFORM](../18-PLATFORM.md) 6.2, 초안 [architecture/04](../platform/architecture/04_SCRIPT_VM_AND_SDK.md)
  3 · 5 · 6장 · [architecture/05](../platform/architecture/05_COMPONENT_SYSTEMS.md) 4 · 6장

## 맥락

스크립트가 엔진을 부르는 함수 (ctx.read · ctx.query · ctx.commands.setField …) 를 네 곳이 안다:

```text
1. 컴파일러의 타입 검사 (이름 · 매개변수 · 반환 타입 · 어느 콜백에서 부를 수 있나)
2. 바이트코드 로드 · 검증기 (이 이름의 함수가 있나, 인자 수 · 타입이 맞나)
3. C++ 바인딩 (실제 구현 — Registry · ECB · RandomService 를 만진다)
4. SDK 의 sandbox.d.ts (IDE 자동 완성 — ADR-0056 2)
```

초안 요구: "VM 과 C++ Native 모듈 간 API 스키마 계약 자동 검사", ".d.ts 만으로 VM 이 생기지 않으니 함수 ID · ABI · 검증 규칙 ·
도메인을 담은 API Registry 필수". 넷이 따로 쓰이면 어긋난다. D-04 = 무엇이 단일 정의이고 나머지를 어떻게 맞추나.

## 결정

```text
1. 단일 정의 = C++ constexpr 표 (script/api/HostApi — v0.2 TriggerSchema 와 같은 방식, 의존 = Foundation 만):
   항목마다 이름 ("entity.read" — 점 마디, 영속), 매개변수 · 반환 타입 (ABI 타입 — 4), 도메인 (server · client), 콜백 범위
   (onTick · onEvent · onStart · onStop · onUpdate), 효과 종류 (read · write · structural · random), 연료 비용 (기본 + 항목당),
   d.ts 이름 · 한 줄 설명. 별도 IDL 파일 · 생성 단계는 없다 — 표가 곧 IDL 이다
2. 나머지 셋은 표를 읽는다:
   컴파일러   타입 검사가 표에서 시그니처를 찾는다 (d.ts 를 읽지 않는다)
   로드       바이트코드는 Host 함수를 이름으로 부른다 (import 표). 로드 때 이름 → 표 인덱스로 묶는다 — 없거나 시그니처가 다르면
              검증기 오류. 인덱스는 영속하지 않는다 (세이브 · 와이어 · 패키지에 없다). .sbc 머리에 표 지문 (항목 정규형의 FNV-1a 64)
              — 다르면 소스에서 다시 컴파일 (패키지에 소스가 있다 — ADR-0056 1)
   바인딩     core/script 가 항목마다 구현을 하나씩 등록한다 (명시 등록 — ADR-0011 과 같은 정신). 시작 때 · 단위 시험이
              "표의 모든 항목에 구현이 정확히 하나, 등록한 시그니처 = 표" 를 검사한다
   d.ts       생성기가 표 + 컴포넌트 카탈로그 (내장 · 스크립트 정의) 로 sandbox.d.ts 를 쓴다 (sbx sdk [계획]). 저장소에 커밋한
              sdk/sandbox.d.ts 와 생성 결과가 같은지 시험한다 (골든 파일 — 표를 바꾸면 같은 커밋에서 d.ts 를 갱신)
3. 호환: 표의 항목을 지우거나 시그니처를 바꾸면 hostApiVersion++ (정수, 릴리스 노트에 적는다). 더하기만이면 그대로.
   스크립트가 쓰는 항목이 이 엔진에 없으면 로드 오류 (이름을 적는다)
4. ABI 타입: i32 · number(f64) · bool · string · Vec2 {x, y: number} (값) · Entity (불투명 — 로컬 EntityId, 산술 없음, 같음 비교만,
   틱을 넘겨 두려면 EntityRef 필드에) · ComponentRef<T> (로드 때 stableId 로 묶인다) · Query (호출 안에서만 유효한 반복자) ·
   T | null. 포인터 · ECS 메모리는 VM 에 나가지 않는다 — 읽기는 값 복사, 핸들은 호출이 끝나면 무효 (VM 이 세대로 확인)
5. 읽기 · 쓰기 · 순서 (결정론)
   읽기    그 단계의 Registry 현재 상태를 읽는다 (같은 호출의 앞선 쓰기는 보인다 — 6)
   질의    ctx.query(Ref …) 의 순서 = saveId 오름차순 (풀 순서는 세이브 → 로드 뒤에 다를 수 있다 — 04 D2). 질의당 개체 상한 · 연료
   쓰기    필드 쓰기 · 컴포넌트 붙이기/떼기 · 생성 · 파괴 · 이벤트 · 점수는 **호출 단위 버퍼**에 쌓는다. 호출이 끝나면 쌓인 순서대로
           적용 (필드는 바로 — 다음 호출 · 다음 시스템이 본다, 생성 · 파괴는 ECB → 단계 17), 트랩이면 통째로 버린다 (ADR-0056 5).
           같은 호출 안의 읽기는 자기 버퍼를 먼저 본다 (read-your-writes)
   검사    필드 범위 (FieldMeta.range) 밖 쓰기 · 죽은 개체 쓰기 · 엔진 관리 컴포넌트 (net.identity · persist.persistence ·
           game.session · game.score — 02 14장의 "(내부)" · "엔진 관리") 쓰기 = 트랩. 점수는 승인된 함수 game.addScore 로만 (효과 score.add 와 같은 규칙)
   난수    ctx.random — RandomService 새 목적 Purpose::Script, 키 = (시스템 순서 · 호출 번호 · 그 호출의 n 번째)
6. 도메인: server 컨텍스트 (IServerContext — onTick 등, 시뮬레이션 단계 안) 가 v0.3 의 본체. client 컨텍스트 (IClientContext —
   onUpdate, 권위 상태 쓰기 없음, 표시만) 는 표에 도메인 표시로 들어가고 구현은 v0.3 M5 [계획]. 한 시스템이 두 도메인을 섞지 않는다
7. 단계 · 순서는 v0.3 M4 의 ADR 이 정한다 — 조건만 여기: 고정 단계 하나, 시스템 순서 = (팩 순서, 시스템 id), 스크립트가 없는
   월드는 아무것도 하지 않는다 (골든 · kSimVersion 그대로 — 17b · 17c 와 같다)
```

## 근거

```text
- C++ 표: 구현 · 검사 · 생성이 같은 언어 · 같은 빌드에 있다. IDL 파일 + 코드 생성은 빌드 단계와 생성물 커밋 규칙이 하나 더 생긴다.
  이 저장소는 이미 TriggerSchema 표 하나로 검사기 · 폼 · 실행을 맞췄다 (ADR-0050)
- 이름으로 import · 로드 때 묶기: 표에 항목을 더해도 기존 바이트코드 · 패키지가 깨지지 않는다. 인덱스를 영속하면 순서가 계약이 된다
- d.ts 골든 시험: "SDK 타입과 Host Registry 계약 자동 비교" (초안 9장) 를 파일 비교 하나로
- 호출 단위 버퍼: 트랩 되돌리기 (ADR-0056) 가 쉽고, 쓰기 순서가 호출 순서 하나로 정해진다. 필드를 바로 적용해 같은 틱의 다음
  시스템이 보게 하는 것은 Native System 사이의 규칙 (03 2장 — 컴포넌트로 소통) 과 같다
- saveId 순 질의: 비용 (정렬) 이 있지만 세이브 → 로드 · 늦게 합류한 서버에서 같은 결과가 먼저다. 비용은 v0.3 M6 벤치로 잰다
```

## 결과

- 얻는 것: 어긋날 수 없는 네 쪽 (검사 · 로드 · 바인딩 · d.ts), 자동 계약 검사, 트랩 되돌리기, 저장 · 로드에 견디는 순서.
- 포기하는 것: 엔진 메모리 직접 접근 (모든 읽기가 복사), 질의 순서의 정렬 비용, 표 밖의 Host 기능.
- 위험: 표 항목이 늘어나면 비용 · 권한 검토가 커진다 — 완화: 항목마다 효과 종류 · 비용이 필수 칸 (빈 칸이면 컴파일 오류).

## 대안

| 대안                                        | 기각 사유                                                                       |
|---------------------------------------------|---------------------------------------------------------------------------------|
| sandbox.d.ts 를 단일 정의로 (C++ 가 파싱)   | 비용 · 도메인 · 효과 종류를 d.ts 로 표현하려면 주석 문법을 만들어야 한다        |
| 별도 IDL 파일 + C++ · d.ts 코드 생성         | 생성 단계 · 생성물 규칙이 늘어난다 — 표가 같은 일을 빌드 없이 한다              |
| 바이트코드에 Host 함수 인덱스                | 표의 순서가 영속 계약이 된다 (항목을 끼워 넣을 수 없다)                         |
| 쓰기를 바로 Registry 에                      | 트랩 되돌리기가 없고, 반쯤 적용된 상태가 남는다                                 |
| 질의 순서 = 풀 순서                          | 세이브 → 로드 뒤 순서가 달라질 수 있다 (04 D2 위반 위험)                         |

## 재검토 조건

saveId 순 정렬이 v0.3 M6 벤치 (10k 대상) 에서 틱 예산의 상당 부분이면 (정렬을 캐시하거나 풀 순서를 결정적으로 고정하는 방법을
결정론 시험과 함께), client 도메인이 서버와 다른 표를 요구하면, v0.4 패키지가 팩 사이 Host 확장 (사용자 Host 함수) 을 요구하면.
