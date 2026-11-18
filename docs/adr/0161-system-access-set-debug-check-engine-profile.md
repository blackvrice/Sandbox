# ADR-0161. 시스템 AccessSet 선언 · Debug 선언 밖 단언 · 엔진 시스템 시간 표 (ServerStats, 프로토콜 22)

- 상태: **Accepted** · 날짜: 2026-11-18
- 관련: [03-SIMULATION](../03-SIMULATION.md) 2.2 (ISystem), [22-RUNTIME-V2](../22-RUNTIME-V2.md) 8장 (스케줄러 · 병렬화 계획 ① ②),
  [04-DETERMINISM](../04-DETERMINISM.md), [08-NETWORK](../08-NETWORK.md) (ServerStats), task-registry RT-06-11 · RT-09-3

## 맥락

22 8장의 병렬화 계획은 ① 시스템마다 읽기 · 쓰기 집합 선언 + Debug 검사, ② 단계별 시간 프로파일, ③ (측정이 필요를 보이면) 같은 단계 안
병렬 이다. RT-06-11 은 ① ② — 완료 기준 "선언 밖 단언". 03 2.2 의 ISystem::access 는 초안에만 있었다.

## 결정

```text
1. 선언       ecs::AccessSet { restricted, reads, writes } — AccessSet::declare().read<A, B>().write<C>(). 쓰기는 읽기도 허락.
              ISystem::access() 기본 = 선언 없음 (무엇이든). 기본 파이프라인 열 중 여덟이 선언하고, Rule 의 조건 · 효과가 리플렉션으로
              아무 컴포넌트를 만지는 Interaction · ResolveIntents 는 선언 없음 (명시 주석). 스크립트 시스템은 스케줄러 밖 (VM)
2. 검사       SystemScheduler 가 System 을 돌리는 동안 Registry 에 선언을 건다. Registry 의 read · tryRead · has (읽기), write ·
              tryWrite (쓰기), view 의 Read · Write · Exclude, poolByStableId (const = 읽기, 아니면 쓰기) 가 선언 밖이면 로그 (시스템 ·
              컴포넌트) + SBX_ASSERT — Debug (SBX_ENABLE_ASSERTS) 만. Release 는 검사 코드가 없다. 결과 · 해시는 바뀌지 않는다
3. 프로파일    서버가 스케줄러에 ISystemProfiler (steady 시계 — 진단용, 시뮬레이션과 무관) 를 넘겨 시스템마다 창 안의 호출 · 시간 합 ·
              최대를 모은다. ServerStats 끝에 엔진 시스템 표 (이름 · 단계 · 호출 · 합 · 최대, 32 행) — 프로토콜 22. Studio 의 "스크립트
              프로파일" 창이 "엔진 시스템" 표 (ms/틱 · 최대 · 몫) 를 보인다 (스크립트가 없어도)
```

## 결과

- 시스템이 선언하지 않은 컴포넌트를 만지면 Debug 시험이 바로 멈춘다 — 병렬화 (RT-09-3) 의 겹침 판단 근거가 생긴다.
- 단계 하나가 틱의 몇 % 인지 Studio 에서 본다 (22 8장 ③ 의 시작 조건 — 30 %).
- 리소스 (SpatialIndex · Intent · Event) 의 읽기 · 쓰기 선언 · 선언으로 자동 병렬은 [계획] (RT-09-3).

## 고른 것 · 버린 것

- 컴파일 시간 검사 (System 이 받는 Registry 보기를 타입으로 좁힘): 모든 System 의 서명 · 도우미를 바꿔야 한다 — 실행 시간 Debug 검사로
  같은 결함을 시험에서 잡는다.
- 단계마다 합친 표만: 시스템 이름이 있어야 어디를 고칠지 안다 — 시스템 행에 단계를 붙인다.
