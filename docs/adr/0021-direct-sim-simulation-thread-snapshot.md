# ADR-0021. --direct-sim 은 창에서 Simulation 스레드로 돌리고, 렌더 쪽에는 틱마다 만든 불변 스냅숏만 넘긴다

- 상태: **Superseded by [ADR-0026](0026-network-session-local-server-inspect.md)** (2026-10-08 — --direct-sim 삭제, Simulation 스레드는 LocalServerHost 로) · 날짜: 2026-10-07
- 관련: [ADR-0020](0020-sprite-atlas-instancing-direct-sim-presentation.md) 결정 5 · 6 (이 ADR 이 대체),
  [01-ARCHITECTURE](../01-ARCHITECTURE.md) 5장 (T1 · T2), [06-RENDERING](../06-RENDERING.md) 9장,
  [14-PERFORMANCE](../14-PERFORMANCE.md) 7.7

## 맥락

사용자 PC 에서 `SandboxClient --direct-sim ecosystem_10k --vsync off` 가 **3 ~ 6 fps** 였다 (CLion `cmake-build-debug`,
MSVC Debug). Release 로 바꾸니 fps 가 올랐다 — 그래픽이 아니라 시뮬레이션 쪽 문제다. 같은 시나리오를 클라우드에서 쟀다
(2코어 컨테이너, 헤드리스, 14-PERFORMANCE 7.7):

```text
                     틱 하나      프레임당 월드 진행   추출(ECS → 스프라이트)   배치 10k
clang Debug (-O0)    82.6 ms      40.9 ms             4.2 ms                  4.9 ms
clang RelWithDebInfo 14.4 ms       7.2 ms             0.4 ms                  0.4 ms
```

ADR-0020 의 --direct-sim 은 **렌더 스레드 안에서** ScenarioRunner.step 을 불렀고, 밀리면 한 프레임에 4 틱까지 따라잡았다.
틱이 33 ms(30 TPS 간격)보다 오래 걸리면 프레임마다 밀린 시간이 생겨 늘 4 틱을 돈다 → 프레임 = 틱 × 4. Debug 에서 틱
50 ~ 80 ms 면 200 ~ 320 ms = 3 ~ 5 fps — 보고와 맞는다. 화면 · 카메라까지 같이 멈추므로 Debug 빌드로는 월드를 볼 수 없다.

01-ARCHITECTURE 5장은 이미 Simulation 을 따로 된 스레드로 두고 (T1: 월드는 Simulation 스레드만, T2: 스레드 사이는 복사본)
있다. 8A 의 인라인 진행이 예외였다.

## 결정

```text
1. DirectSim 은 두 방식. 창 = Threaded, --headless · 단위 테스트 = Inline (지금과 같은 프레임 안 진행 — 틱 수가 고정된다).
2. Threaded: Simulation 스레드가 월드를 소유하고 30 TPS × 속도의 시각표대로 틱을 돈다. 틱이 간격보다 오래 걸려
   0.25 초 넘게 밀리면 밀린 몫은 버린다 (몰아 돌지 않는다) — 시뮬레이션만 실시간보다 느려지고 화면은 제 속도.
   일시정지 · 한 틱 · 속도는 mutex + condition_variable 로 스레드에 알린다 (월드에 명령을 넣지 않는 것은 그대로).
3. 추출을 둘로: capture (Simulation 스레드, 틱마다) — 월드 → WorldSnapshot{틱, 배경, 개체마다 직전 · 지금 위치 · 크기 ·
   회전 · 스프라이트 · 색 · 레이어}. emit (Main 스레드, 프레임마다) — 스냅숏 + alpha → SpriteDraw (보간 · depth = -y).
   스냅숏은 shared_ptr<const> 로 내놓고, 아무도 안 잡은 이전 것은 다음 capture 가 다시 쓴다 (할당 없음).
4. 보간 alpha (Threaded) = 스냅숏이 나온 뒤 흐른 시간 / max(틱 간격, 최근 틱 실제 시간). 일시정지면 1.
5. 시뮬레이션에 Worker 를 붙인다 (경로 · 감지 Job). 기본 = 코어 수 - 2 를 1 ~ 4 로, --threads n. 헤드리스 0.
   결과는 Worker 수와 무관하다 (D5) — 단위 테스트가 Threaded(Worker 2) 와 Inline(0) 의 같은 틱 위치를 비교한다.
6. 제목 줄에 구간 시간: "… · 29.8/30 TPS · 틱 12.3 ms | … | 144 fps · 월드 0.0 · 추출 0.3 · 렌더 1.2 ms".
   헤드리스 끝에 프레임 · 틱 평균. 어디가 느린지 사용자가 바로 적어 보낼 수 있게.
```

## 근거

```text
- 문제는 "틱이 느리다" 가 아니라 "느린 틱이 화면을 막는다" 다. Debug 의 틱을 빠르게 하는 것(최적화 플래그, 반복자 검사 끄기)은
  디버깅을 어렵게 하고 다른 Debug 비용은 그대로다. 스레드를 나누면 틱 비용과 무관하게 카메라가 움직인다.
- 01 5장의 스레드 모델(Simulation 스레드 · T1 · T2)과 같은 모양이라 Phase 10 의 LocalServerHost 로 옮길 때 그대로 간다.
  스냅숏은 Phase 10 의 ClientWorld 스냅숏 자리와 같은 위치다.
- capture 를 틱마다(30 Hz) 하면 프레임마다 ECS 를 훑던 추출(Debug 4.2 ms)이 렌더 쪽에서 빠지고, emit 은 배열을 한 번
  보간할 뿐이다. 보간용 직전 위치도 capture 가 함께 담아 틱 전에 위치를 따로 모으던 패스가 없어졌다.
- 헤드리스는 Inline 으로 남긴다: CTest client_direct_sim_headless 가 틱 수(28)를 정확히 본다 — 시간에 따라 달라지면 안 된다.
```

## 결과

- 얻는 것: Debug 빌드에서도 화면 · 카메라가 렌더 속도로 움직이고, 시뮬레이션이 실시간을 못 따라가면 제목 줄 TPS 로 보인다.
  Release 에서는 렌더 스레드가 틱을 기다리지 않아 fps 가 오른다.
- 포기하는 것: Threaded 에서 world() 직접 접근 (단언으로 막는다 — Inline 에서만). 스냅숏 한 벌 복사(10k × 40 바이트, 30 Hz).
- 위험: 락 순서 · 수명. 락은 하나(m_mutex)뿐이고 step · capture 는 락 밖, 소멸자는 m_stop → notify → join 뒤에 월드를 지운다.
  TSan 단위 테스트(linux-clang-tsan)로 본다. MaterialLibrary::find(경고 기록이 바뀐다)는 capture 에서만 부른다.

## 대안

| 대안                                         | 기각 사유                                                                                  |
|----------------------------------------------|--------------------------------------------------------------------------------------------|
| 따라잡기를 4 → 1 틱으로                      | 프레임 = 틱 하나. Debug 10k 에서 여전히 10 fps 안팎이고 카메라가 시뮬레이션에 묶인다       |
| 프레임당 시뮬레이션 시간 예산 (ms)           | 틱 하나가 예산보다 길면 같은 문제. 틱을 쪼갤 수 없다                                       |
| Debug 에서도 Core 만 최적화 (/O2 + IDL 0)    | 디버깅이 어려워지고 ABI(_ITERATOR_DEBUG_LEVEL)가 타깃마다 달라 링크가 깨진다               |
| 월드를 락으로 감싸 렌더 쪽에서 직접 추출     | T1 위반. 추출하는 동안 Simulation 이 멈추고, 렌더가 틱 시간만큼 기다린다                   |

## 재검토 조건

Phase 10 LocalServerHost (Simulation 스레드 · 스냅숏이 네트워크 경로로 바뀐다 — --direct-sim 삭제), 스냅숏 복사가 프레임
예산에 보일 때(50k 개체 이상), Render 스레드 분리(01 5.1 — CPU 렌더 > 6 ms).
