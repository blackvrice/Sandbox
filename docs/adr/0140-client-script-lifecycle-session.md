# ADR-0140. client 수명주기 — 세션 = 복제 월드 하나, onStart → onUpdate → onLateUpdate (프레임 안의 차례), onStop 은 월드 없이

- 상태: **Accepted** · 날짜: 2026-11-16
- 관련: [ADR-0067](0067-client-scripts-domain-client-onupdate-read-only-display.md) (client 시스템 · onUpdate), [ADR-0137](0137-call-token-capability-runtime-check.md)
  (호출 토큰 · 실행 검사), [ADR-0086](0086-runtime-architecture-v2.md) 6 (예약 콜백), [22-RUNTIME-V2](../22-RUNTIME-V2.md) 3.1 · 3.2,
  [24-SECURITY-GATES](../24-SECURITY-GATES.md) C2, [19-SANDBOXSCRIPT](../19-SANDBOXSCRIPT.md) 21 · 26장, task-registry RT-06-7 (조각 1)

## 맥락

client 시스템은 프레임마다 onUpdate 하나만 받는다 (ADR-0067). 그래서 HUD 의 처음 값을 정하려면 onUpdate 가 "처음인가" 를 스스로
알아야 하고 (상태를 둘 곳이 UI 변수뿐), 서버 월드가 바뀌거나 (불러오기 · 테스트 플레이) 콘텐츠가 바뀔 때 정리할 자리가 없다.
카메라 · 따라가기는 모든 시스템의 onUpdate 가 끝난 뒤의 값을 봐야 하는데 시스템 차례 (팩 · id) 에 묶인다. v0.5 M2-T7 이 이름만 예약했다
(client onStart · onStop · onLateUpdate — [계획 v0.6]). 정할 것은 세션이 무엇인가 · 프레임 안의 차례 · 끝날 때 무엇을 보나 ·
허락 Capability 다. onEvent (복제 이벤트 큐) 는 조각 2 로 미룬다 (프로토콜 · 큐 상한 — 24 Q1).

## 결정

```text
1. 세션      client 스크립트 세션 = 복제 월드 하나. 앱 (NetworkSession) 이 정한다:
               시작  그 복제 월드에 첫 스냅숏이 적용된 프레임 (onStart 가 빈 월드를 보지 않는다)
               끝    복제 월드가 바뀜 (다시 접속 · WorldChanged — 불러오기 · 재시작 · 테스트 플레이) · 콘텐츠가 바뀜 (오버레이로 바이트코드가
                     바뀜 · client 시스템이 없어짐) · 접속이 끝남 (끊김 · NetworkSession 을 없앰)
             같은 콘텐츠로 월드만 바뀌면 실행기 (ClientScriptSystems) 를 다시 만들지 않고 새 세션을 연다 (꺼짐 · 통계는 접속의 메모리 —
             ADR-0067 그대로). 콘텐츠가 바뀌면 옛 실행기의 세션을 끝내고 새로 만든다
2. 차례      run (프레임):  세션의 첫 프레임이면 모든 시스템의 onStart → 모든 시스템의 onUpdate → 모든 시스템의 onLateUpdate
             stop (세션 끝): 모든 시스템의 onStop — 시작한 세션만, 한 번. 다음 run 이 새 세션 (onStart 다시)
             시스템 차례는 (팩 순서, id) 그대로. 시스템 하나의 콜백을 묶지 않는다 — onLateUpdate 는 다른 시스템의 onUpdate 결과까지
             본 뒤 (Unity LateUpdate 와 같은 자리, 22 12장 5)
3. onStop    월드 없이 부른다 (frame.registry = null) — 끝을 알아챈 때 옛 복제 월드는 이미 없다 (ClientSession 이 새 월드를 만든 뒤).
             ctx.query 는 빈 목록 (트랩이 아니다 — onUpdate 에 월드가 없을 때도 같다. 전에는 HostError), tick 은 0, frame 은 마지막 프레임.
             접속 끝 · 콘텐츠 바뀜처럼 월드가 아직 있으면 그 월드를 준다. 내보낸 UI 값은 적용한다 (HUD 를 숨기는 자리)
4. Capability onStart · onStop · onLateUpdate = EcsRead · Time · Display · Ui (읽기 · 표시 · UI 값) — 입력 · 신호 · 소리는 onUpdate
             (· onAction — RT-06-8). 검사는 두 겹: 검사기 — 콜백 메서드의 몸에서 ctx 로 바로 부른 Host 함수의 Capability 가 그 콜백의
             허락 밖이면 S404 ("client.send 는 onLateUpdate 에서 부를 수 없다 — Capability ClientSend"). 도우미 함수 (ctx 를 받는
             최상위 function) · 화살표 함수 안은 어느 콜백인지 모른다 — 실행 검사 (호출 토큰, ADR-0137) 가 HostError 로 막는다
5. 예산      새 콜백도 같은 프레임 연료 (kFrameFuel — 프레임의 모든 콜백 합) · 호출 연료 · 트랩 정책 (이어진 트랩 3 번 = 끔 — 콜백을
             가리지 않고 센다). 꺼진 시스템은 onStop 도 부르지 않는다. stop 은 프레임 연료 하나 몫
6. 바이트코드 .sbc 형식 13 — 시스템 표에 onLateUpdate (onMigrate 뒤). client onStart · onStop 은 기존 칸 (도메인이 가른다). 검증기:
             client = onUpdate · onLateUpdate · onStart · onStop 중 하나 이상, onTick · onEvent · onMigrate · 주기 · 구독 없음 —
             server 의 onUpdate · onLateUpdate 거절. Host 표 · 지문은 그대로 (콜백 표는 지문 밖 — 형식 번호가 옛 파일을 막는다)
7. d.ts      SystemDefinition = ServerSystemDefinition | ClientSystemDefinition, defineSystem 오버로드 둘 (domain: "client" 면 client
             정의 — onStart · onStop 의 ctx 가 도메인마다 다르다)
```

## 결과

- HUD 처음 값 · 정리 · 카메라가 제자리를 얻는다. 기존 client 시스템 (onUpdate 만) 의 차례 · 결과는 같다 — 첫 스냅숏 전 프레임에는 이제
  돌지 않는다 (전에는 빈 월드로 돌았다).
- 형식 12 의 .sbc 는 "다시 컴파일" 로 거절된다 (패키지에 소스가 있다 — 다른 형식 바꿈과 같다).
- 시뮬레이션 밖 — WorldHash · 세이브 · kSimVersion 무관.
- 하다가 찾은 것: (a) 복제 월드를 다시 만들면 같은 주소에 놓일 수 있어 NetworkSession 의 주소 비교가 바뀜을 놓쳤다 (선택 비우기 ·
  추출 초기화도) — ClientWorld::serial (프로세스 안에서 겹치지 않는 차례) 로 같이 본다. (b) Script 오버레이가 있는 월드는 세이브의
  contentHash 가 서버의 기본 콘텐츠와 달라 불러오기 · 테스트 플레이가 거절된다 (오버레이 소스가 해시에 든다 — 이 ADR 밖, DEVELOPMENT_LOG).

## 고른 것 · 버린 것

- 세션 = 접속 하나 (복제 월드가 바뀌어도 이어짐): 불러오기 · 재시작 뒤 HUD 가 옛 월드의 값을 들고 있다 — 버림.
- onStop 을 옛 월드로: ClientSession 안에 "월드를 없애기 전" 콜백을 두어야 한다 (net → 앱 의존 · 복잡) — 버림. 끝에서 읽을 값은 onUpdate 에서
  UI 변수에 둔다.
- 시스템마다 onStart → onUpdate 를 묶기: onLateUpdate 와 같은 "모두 뒤" 를 줄 수 없다 — 버림.
- 검사기가 호출 그래프를 따라 도우미 함수까지 보기: 함수 하나를 여러 콜백이 부를 수 있다 (맥락 민감) — 실행 검사가 있으니 몸만 본다.
