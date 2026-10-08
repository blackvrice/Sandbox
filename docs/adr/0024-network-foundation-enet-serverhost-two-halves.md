# ADR-0024. 네트워크 기초: ENet 1.3.18 을 벤더링하고, ServerHost 는 Net 절반 · Sim 절반을 큐로 잇고, 권한 · 속도 제한은 Net 쪽 CommandValidator 한 곳에서

- 상태: **Accepted** · 날짜: 2026-10-08
- 관련: [ADR-0003](0003-server-authoritative.md), [ADR-0010](0010-enet-transport.md) (구체화),
  [ADR-0021](0021-direct-sim-simulation-thread-snapshot.md) (같은 Inline/Threaded 두 방식),
  [08-NETWORK](../08-NETWORK.md) 2 ~ 5 · 10 · 11장, [03-SIMULATION](../03-SIMULATION.md) 4.1, [10-EDITOR](../10-EDITOR.md) 7장,
  [16-ROADMAP](../16-ROADMAP.md) Phase 9

## 맥락

Phase 9 는 9.1 Transport(Loopback · Simulated) · 9.2 EnetTransport · 9.3 비트스트림 · 메시지 · 9.4 핸드셰이크 · 9.5 ServerHost
(Simulation 스레드 + Net IO 스레드) · CommandValidator · 9.6 SandboxServer `--world --ticks N --exit` 이다. 완료 기준: Loopback 핸드셰이크
· 명령 · 거절 테스트, 헤드리스 서버 CI 실행. 스냅숏 복제는 Phase 10. 정할 것:

```text
1. ENet 을 어디서 가져오나 (이 세션은 GitHub 에 닿지 않는다 — 패키지 저장소만)
2. ServerHost 의 스레드 구조 · 테스트 방법
3. 검증(권한 · 속도 제한)을 어느 스레드에서, 어떤 순서로
4. 시나리오가 넣는 명령과 클라이언트 명령의 결과를 어떻게 가리나 (random_walk 는 issuer 1 로 넣는다)
5. SimCommand 의 와이어 형식 (컴포넌트 값은 JSON 이다)
6. 콘텐츠 해시가 다른 클라이언트
7. 서버를 멈출 때 끊기가 상대에게 닿게
```

## 결정

```text
1. ENet v1.3.18 (2024-04 최신 릴리스) 의 C 소스를 external/enet 에 수정 없이 (callbacks · compress · host · list · packet · peer ·
   protocol · unix · win32 .c, include/enet/*.h, LICENSE). 출처 = Debian enet_1.3.18+ds.orig.tar.xz (업스트림 태그 v1.3.18 을 다시
   묶은 것) — SHA256 이 Debian 의 서명된 .dsc 와 같다. 정적 라이브러리 sbx_enet: 이 때문에 프로젝트에 C 언어를 켠다.
   Linux 는 업스트림 CMakeLists 와 같은 configure 검사(HAS_POLL 등), Windows 는 ws2_32 · winmm. enet.h 는
   network/transport/EnetTransport.cpp 안에서만 (pimpl — 헤더는 ENet 을 모른다).
2. ServerHost = Net 절반(Transport · 클라이언트 표 · 핸드셰이크 · CommandValidator · 인코딩) + Sim 절반(ScenarioRunner).
   둘 사이는 복사한 값만: inbox(검사 통과한 명령) · outbox(보낼 메시지) · status(Welcome 에 실을 tick · 일시정지 · 속도),
   뮤텍스 + vector 교환 (01 5.3). 진행 방식은 ADR-0021 과 같은 두 가지 — Threaded(SandboxServer: Simulation 스레드 +
   Net IO 스레드) · Inline(update(dt) 가 Net → 틱(최대 3 개) → Net, 단위 테스트가 시간을 손으로 넣는다).
3. CommandValidator 는 Net 절반에서, 월드를 보지 않고: 순번(클라이언트마다 단조 증가) → 속도 제한(토큰 버킷 초당 120,
   몰아서 120) → 권한(역할 × 명령 종류, 10 7장 표). 통과한 명령만 Sim 으로; Sim 이 executeTick = currentTick + 1 을 찍는다
   (M4). 대상 · 콘텐츠 · 값 검사는 SimulationWorld 가 적용하며 하고, 그 거절도 같은 CommandResult 길로 간다 (M6).
   역할: Observer < Player < Editor < Admin < Owner. 엔티티 · 지형 편집 = Editor 부터, 시뮬레이션 제어 = Admin 부터.
   기본 역할 = --default-role (기본 editor). ErrorCode::RateLimited 를 추가한다.
4. Sim 절반이 "네트워크에서 넣은 (issuer, sequence)" 집합을 들고, 결과 중 그 집합에 있는 것만 돌려준다 — 시나리오 명령이
   같은 issuer 를 써도 클라이언트에게 가지 않는다.
5. 명령 페이로드 = 종류 태그 u8 (값 고정, PayloadTag) + 필드. 엔티티 참조 = NetEntityId varint, 좌표 · 속도 = f32 (유한해야
   한다), 타일 = zigzag varint, 컴포넌트 값 · 패치 = JSON 텍스트 (16 KB 상한 — 리플렉션 JsonReader 가 그대로 적용).
   비트스트림: LSB 우선, LEB128 varint, 오류는 플래그 (예외 없음), 문자열은 상한 + 엄격한 UTF-8. 메시지 하나 = 패킷 하나,
   남는 바이트 · 예약된 id · 상한 초과 = 형식 오류 → 연결을 끊는다 (ProtocolError).
6. Reject{ContentMismatch} 가 서버의 팩 목록과 contentHash 를 싣는다. 클라이언트는 그 팩을 자기 content 루트에서 읽어 해시를
   맞추고 다시 접속한다 (sbx_net_probe 가 그렇게 한다). 콘텐츠 자체를 보내는 것은 [계획 — ContentOverlay, Phase 12].
7. INetworkTransport::drain(ms) — 끊는 중인 연결이 상대에게 닿을 시간을 준다. ENet 끊기는 enet_peer_disconnect_later
   (쌓인 신뢰 패킷이 확인된 뒤 disconnect) 라 상대의 ack 가 있어야 끝난다. ServerHost::stop 은 모두에게
   Disconnect{ServerShutdown} 을 보내고 끊은 뒤 drain(300 ms). 끊긴 쪽만 Disconnected 이벤트를 받는다 (끊은 쪽은 이미 안다).
```

그 밖에:

```text
- Transport 계약: 한 객체 = 한 스레드. ConnectionId 는 재사용하지 않는다 (늦은 이벤트가 새 연결로 가지 않게). wait(ms) 로
  Net IO 스레드가 잔다 (ENet = enet_host_service 대기, Loopback = 조건 변수). LoopbackNetwork 허브는 스레드 사이를 잇는다.
- SimulatedTransport: 보내는 쪽에서 지연 · 지터 · 대역폭 · 손실(Snapshot 만, 신뢰 채널은 재전송 지연) · 순서 바꿈(Snapshot 은
  옛것 버림). 시계 · 난수(splitmix64) 를 주입해 결정적이다.
- 핸드셰이크 거절: 버전 · 콘텐츠 · 가득 참(Transport 상한은 maxClients + 4 — 넘친 접속에도 Reject 로 말해 준다) · 순서 ·
  nonce · 이름(1 ~ 32 글자, 제어 문자 없음) · 시간 초과(10 초). buildId 가 다르면 경고만.
- ClientSession (network/client): 핸드셰이크 · 명령 · 결과 · ServerStats. Phase 10 의 NetworkSession 이 이 위에 선다.
- SandboxServer --world <시나리오 | 세이브 폴더> [--port --bind --max-clients --default-role --threads] [--ticks N --exit].
  실제 포트를 "listening udp *:<port>" 로 출력 (--port 0). Ctrl+C → 서버 종료 알림. SIGPIPE 무시.
- sbx_net_probe: 접속 · 명령(--pause --resume --step --speed --create) · 결과 · 통계를 출력하는 도구.
- 원래 08 5장 표의 CommandResult · ServerStats · Reject 필드를 구현에 맞췄다 (appliedTick · created, 실제 지표, packs ·
  contentHash). 아직 내보낸 적 없는 프로토콜이라 kProtocolVersion 은 1 그대로.
```

## 근거

```text
- Debian orig 묶음은 서명된 .dsc 의 해시로 출처를 확인할 수 있다. crates.io enet-sys 의 사본은 1.3.17 이었다.
- 두 절반 + 큐는 01 의 T1 · T2 를 그대로 따른다. Inline 이 있어 핸드셰이크 · 시간 초과 · 속도 제한 테스트가 실제 시간 없이
  결정적으로 돈다 (DirectSim 과 같은 이유).
- 검증을 Net 쪽에 두면 거절된 명령이 Simulation 스레드에 닿지 않는다 — 속도 제한이 시뮬레이션을 보호한다. 월드가 필요한
  검사만 적용 단계에 남긴다.
- JSON 텍스트는 SimCommand 가 이미 JSON 값을 싣고 리플렉션이 적용한다 — 필드별 비트 인코딩은 복제(Phase 10)에서 측정 뒤.
```

## 결과

- 얻는 것: 실제 UDP 로 서버 · 클라이언트가 핸드셰이크 · 명령 · 결과 · 통계를 주고받는다. 서버 실행 파일이 Network + Core 만.
  나쁜 네트워크(지연 · 손실)를 결정적으로 시험한다.
- 포기하는 것: 아직 스냅숏이 없어 클라이언트는 월드를 볼 수 없다 (Phase 10). 재접속 토큰은 발급만 (11.4), 역할 바꾸기 ·
  Chat 은 Phase 12. 암호화 없음 (R6).
- 위험: 같은 issuer · sequence 를 시나리오와 클라이언트가 같은 틱에 쓰면 결과가 하나 섞일 수 있다 (시나리오 issuer 는 1 뿐,
  클라이언트 순번은 1 부터 — 같은 틱에 같은 번호일 때만). 세이브를 일시정지 상태로 띄우면 --ticks N --exit 는 누군가
  재개할 때까지 기다린다.

## 대안

| 대안                                                  | 기각 사유                                                                      |
|-------------------------------------------------------|--------------------------------------------------------------------------------|
| crates.io enet-sys 0.x 의 vendor/enet                 | 1.3.17 — 1.3.18 의 MTU · 체크섬 수정이 빠졌다                                  |
| 서버 스레드 하나 (틱 사이에 소켓 처리)                | 틱이 느리면 핸드셰이크 · 결과가 같이 늦는다. 01 5장의 Net IO 스레드 계획과 다름 |
| 검증을 Sim 스레드 Stage 1 에서                        | 속도 제한에 걸릴 명령까지 Simulation 스레드가 받는다                           |
| 컴포넌트 값을 리플렉션 비트 인코딩                    | 명령은 드물다 (초당 120 상한) — 이득이 작고 복제 설계(Phase 10)와 겹친다       |
| enet_peer_disconnect (즉시)                           | 쌓인 송신 큐를 지운다 — 마지막 CommandResult · Disconnect 메시지를 잃는다      |

## 재검토 조건

Phase 10 복제(Snapshot · Bulk 가 Net 절반의 큐 구조를 다시 본다), GNS · 암호화 요구, 서버 틱이 Net IO 를 막는 측정,
LocalServerHost(싱글플레이)에서 Loopback 비용이 보일 때.
