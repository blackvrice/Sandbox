# ADR-0067. 클라이언트 스크립트 최소 — domain "client" · onUpdate(ClientContext), 복제 월드 읽기 · 화면 글자만, .sbc 형식 5

- 상태: **Accepted** · 날짜: 2026-10-26
- 관련: [ADR-0057](0057-host-api-single-definition-cpp-table-sdk-generated.md) (Host 표 · 도메인 · 바인딩 계약),
  [ADR-0058](0058-bytecode-register-machine-typed-registers-verifier.md) (.sbc), [ADR-0060](0060-script-component-host-read-query-write-buffer-entity-handles.md)
  (컴포넌트 읽기), [ADR-0062](0062-script-systems-define-system-stage-16b-pack-id-order.md) · [ADR-0064](0064-script-budgets-tick-fuel-disable-after-traps-saved-state.md)
  (시스템 · 예산), [ADR-0066](0066-script-overlay-kind-recompile-same-components-protocol-13.md) (오버레이), [19-SANDBOXSCRIPT](../19-SANDBOXSCRIPT.md) 21장,
  [18-PLATFORM](../18-PLATFORM.md) 6.2 (v0.3 M5-T3)

## 맥락

Host 표는 처음부터 도메인 (Server · Client) 칸이 있었고 (ADR-0057), defineSystem 은 `domain` 키를 받되 "server" 만 허용했다.
게임 제작자는 서버 규칙과 함께 화면 쪽 표시 (개체 위 이름 · 상태 · 작은 HUD) 를 스크립트로 쓰고 싶다. 정할 것: 클라이언트
스크립트가 무엇을 보고 무엇을 할 수 있나, 결정론 · 서버 권위와 어떻게 갈라 두나, 바이트코드 · 검사기 · 바인딩 계약을 어떻게 넓히나.
M5-T3 은 계획에서 "IClientContext 최소 + 프로파일" 이었다 — 둘은 서로 기대지 않아 둘로 나눈다 (프로파일은 M5-T4).

## 결정

```text
1. defineSystem({ domain: "client", id, onUpdate(ctx) { … } }) — client 시스템. 콜백은 onUpdate 하나 (ctx = ClientContext,
   적지 않아도 · 매개변수 없이도), intervalTicks · events · eventsPerTick · onTick · onStart · onStop · onEvent 는 S426.
   server 시스템의 onUpdate 도 S426. domain 은 문자열 리터럴로 (메서드의 ctx 타입을 먼저 정해야 해서 — 상수 이름은 S426).
   시스템 id 는 도메인과 무관하게 팩들 사이에 하나 (V1)
2. Host 표 항목 (주인 ClientContext · 도메인 Client — 더하기만이라 kHostApiVersion 1 그대로, 지문만 바뀐다):
     client.tick: i32 (복제 월드의 서버 틱) · client.frame: i32 (1 부터) · client.dt: number (앞 프레임 뒤 실제 초 — 표시용)
     client.read(entity, ref): T | null · client.has(entity, ref): boolean · client.query(ref): Entity[] — 복제 월드만,
       질의 = netId 오름차순 (comp::NetIdentity), 스크립트 컴포넌트만, 개체 참조 필드는 null [계획]
     client.label(entity, text) · client.print(text) — 효과 Display (새 효과 종류): 이번 프레임 개체 위 글자 · "스크립트" 칸 한 줄
       (줄마다 256 바이트, 프레임마다 글자 1024 · 줄 64 — 넘으면 버리고 센다)
   ServerContext 와 ClientContext 는 서로의 항목을 부를 수 없다 (검사기가 주인 타입으로 찾는다 — ctx.write 는 클라이언트에 없다)
3. .sbc 형식 5: 시스템 표에 도메인 (u8) · onUpdate. 검증기 — client 시스템은 onUpdate 하나 · (ClientContext?) → void · 주기 1 ·
   구독 없음, server 시스템은 onUpdate 없음. 옛 형식은 VersionMismatch (소스에서 다시 컴파일)
4. 바인딩 계약은 도메인마다: hostBindings (ScriptHost) = Server 항목, clientHostBindings (ClientScriptHost) = Client 항목 —
   checkHostBindingShapes(…, 도메인) 가 "그 도메인 항목마다 구현이 정확히 하나 · 다른 도메인 항목의 구현은 문제" 를 본다
5. 서버: ScriptSystemAdapter 는 client 시스템을 건너뛰고, server 시스템이 없으면 어댑터를 만들지 않는다 (client 시스템만 있는
   콘텐츠의 시뮬레이션 · 해시 · 세이브는 스크립트 없는 콘텐츠와 같다)
6. 클라이언트: core/script/ClientScriptSystems — 팩마다 인터프리터, (팩 순서, id) 차례로 프레임마다 onUpdate. 예산: 호출 연료
   1,000,000 · 프레임 연료 2,000,000 (kFrameFuel — 넘으면 "예산" · "건너뜀"), 트랩이면 그 호출의 표시를 버리고, 3 번 이어지면
   그 시스템을 끈다 (이 접속의 메모리에만 — 세이브 · 서버와 무관). NetworkSession 이 콘텐츠 (오버레이 포함) 에 client 시스템이
   있을 때만 두고, 스냅숏을 적용한 뒤 돌린다. 오버레이로 바이트코드가 바뀌면 다시 만든다
7. 화면: IWorldSession::scriptOverlay → Application 이 글자를 개체의 그린 자리 (보간 위치) 위에 (ImGui 배경 목록), 줄을
   "스크립트" 칸에 그린다 — 게임 화면이라 패널을 숨겨도. 헤드리스 클라이언트는 끝날 때 시스템 통계 · 마지막 줄을 찍는다
```

## 근거

```text
- 읽기 · 표시만: 클라이언트는 서버 권위의 복제본을 본다 — 쓰기 · 명령은 서버 검증 (역할 · 명령 표) 을 다시 거쳐야 해서 v0.3 의
  최소 범위 밖이다. 표시는 그 클라이언트 화면에만 남아 결정론 (04) · 해시 · 세이브에 닿지 않는다
- 같은 언어 · 같은 VM · 같은 Host 표: d.ts · 검사기 · 검증기 · 예산 · 트랩 정책을 그대로 쓰고, 도메인은 주인 타입으로 갈린다
  (서버 항목을 클라이언트에서 부르는 일이 타입 검사에서 막힌다)
- netId 순 질의: 복제 월드의 풀 순서는 받은 차례라 클라이언트마다 다르다. saveId 는 복제하지 않는다 (persist.persistence)
- 프레임 연료: 클라이언트 프레임은 시뮬레이션 틱과 따로 돈다 — 화면을 멈추지 않게 틱 연료의 절반
- 끔이 메모리에만: 클라이언트 스크립트 상태는 시뮬레이션 상태가 아니다 (다시 접속하면 새로)
```

## 결과

- 얻는 것: 스크립트로 개체 위 글자 · HUD 줄 (예: 마나 값, 남은 시간) — 서버 · 결정론에 닿지 않고, 오버레이로 고치면 바로 바뀐다.
- 포기하는 것: 클라이언트 스크립트의 쓰기 · 명령 보내기 · 입력 · 그리기 (도형 · 스프라이트) · 개체 참조 필드 · 엔진 컴포넌트 읽기
  — [계획] (M6 데모가 요구하면 18 6.2 에 태스크로).
- 위험: .sbc 형식 5 — 옛 패키지 바이트코드는 다시 컴파일한다 (소스가 있다). 복제되지 않는 (serverOnly) 컴포넌트는 null · 빈 질의다.

## 대안

| 대안                                       | 기각 사유                                                                        |
|--------------------------------------------|----------------------------------------------------------------------------------|
| ServerContext 를 클라이언트에서도          | 쓰기 · 로그의 뜻이 달라진다 — 서버 항목을 부르는 클라이언트 코드가 타입 검사를 지난다 |
| 클라이언트 스크립트를 별도 파일 종류로     | 모듈 · import · 컴포넌트 정의를 서버 스크립트와 나눠 쓸 수 없다                    |
| 질의를 saveId 순으로                       | saveId 는 복제하지 않는다 — 클라이언트에는 없다                                    |
| 끔 상태를 세이브에                         | 클라이언트 표시는 월드 상태가 아니다                                               |
| 프로파일 (M5-T4) 과 한 태스크              | 둘은 서로 기대지 않는다 — 한 커밋이 두 가지를 섞는다                              |

## 재검토 조건

클라이언트 스크립트가 명령을 보내야 할 때 (서버 검증 표와 함께 새 ADR), 입력 · 그리기 API 가 필요할 때, 프레임 연료가 측정으로
모자랄 때 (M6 벤치).
