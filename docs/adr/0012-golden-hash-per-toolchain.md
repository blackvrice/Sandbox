# ADR-0012. 골든 해시는 툴체인별로 기록한다

- 상태: **Accepted** · 날짜: 2026-10-05
- 관련: [04-DETERMINISM](../04-DETERMINISM.md) 2장(D4)·6장, [13-TESTING](../13-TESTING.md) 5장, [ADR-0004](0004-determinism-scope.md)

## 맥락

D4("리팩터링 커밋은 골든 해시와 같은 해시")는 골든 파일 하나에 기대 해시를 적어 두고 비교하는 방식입니다.
그런데 ADR-0004 는 결정론을 **같은 바이너리 안에서만** 보장합니다. 시뮬레이션은 float 이고, 컴파일러마다
연산 순서·축약(FMA)·수학 함수 구현이 다를 수 있습니다. 다른 컴파일러에서 해시가 다른 것은 결함이 아닙니다.

```text
선택지
  A. 기준 툴체인 하나(MSVC)의 해시만 기록, 나머지 툴체인은 D4 를 건너뛴다
  B. 툴체인별로 항목을 기록한다 — 키 = <System>-<processor>-<CompilerId>-<major>
  C. 해시를 툴체인 독립으로 만든다 (고정소수점 시뮬레이션, 또는 해시 양자화를 거칠게)
```

## 결정

```text
1. 골든 파일 = 실행 조건(scenario, seed, ticks, hashEvery) + entries[툴체인 키] = {simVersion, compiler, hashes}.
2. 툴체인 키는 빌드 정보(foundation/BuildInfo.hpp 의 kToolchainKey)에서 온다.
     "<CMAKE_SYSTEM_NAME>-<processor 소문자, AMD64→x86_64>-<CompilerId>-<major>"   예 Linux-x86_64-Clang-19
     MSVC 는 주 버전이 오래 19 로 고정이므로 major.minor (Windows-x86_64-MSVC-19.44).
3. 비교할 때 이 툴체인 항목이 없으면 실패가 아니라 SKIP (sbx_sim_check 종료 코드 3, CTest SKIP_RETURN_CODE).
   처음 보는 툴체인에서 개발자가 --record-golden 으로 기록해 커밋한다.
4. 빌드 설정(Debug/RelWithDebInfo)은 키에 넣지 않는다. 설정 간 해시가 다르면 그 자체를 조사 대상으로 본다
   (같은 소스·같은 컴파일러에서 -O 수준만으로 결과가 바뀌면 sbx_simulation_flags 가 새고 있다는 신호).
5. simVersion 규칙(04 6장)의 예외: 새 툴체인 항목 추가와 시나리오(입력) 변경은 simVersion 을 올리지 않는다.
```

## 근거

```text
- A 는 Linux/macOS 에서 리팩터링 회귀를 못 잡는다. 개발자 대부분이 쓰는 툴체인에서 안전망이 비는 셈.
- C 는 Phase 3 범위를 넘는 재설계다 (ADR-0004 가 float 을 택한 이유가 그대로 남아 있다).
- B 는 비용이 작다: 항목 하나 = 해시 10개. 항목이 없을 때 SKIP 이라 새 툴체인이 CI 를 막지 않는다.
- 2026-10-05 관찰: Clang 19 와 GCC 13 (Linux x86-64), Debug 와 RelWithDebInfo 가 random_walk_1k 600틱에서
  모두 같은 해시였다. B 는 이것이 깨져도(예: MSVC) 문제가 되지 않게 하는 보험이다.
```

## 결과

- 얻는 것: 모든 툴체인에서 D4 회귀 검사. 컴파일러 간 차이가 생겨도 오탐이 없다.
- 포기하는 것: 툴체인이 늘면 골든 갱신 때 여러 항목을 다시 기록해야 한다 (각 툴체인에서 한 번씩 실행).
- 위험: 새 툴체인에서 아무도 기록하지 않으면 그 툴체인은 계속 SKIP 이다 → CI 매트릭스의 툴체인은 항목이 있어야 한다는
  것을 CI 첫 실행 체크리스트에 둔다 (15-BUILD 8장).

## 대안

| 대안 | 기각 사유 |
|---|---|
| A. 기준 툴체인 하나 | Linux/macOS 회귀를 못 잡는다 |
| C. 툴체인 독립 해시 | ADR-0004 재검토 사안. 측정 근거(컴파일러 간 실제 불일치) 없이 비용만 크다 |
| 키에 빌드 설정 포함 | 설정 간 불일치를 정상으로 숨긴다 (결정 4) |

## 재검토 조건

세 플랫폼 툴체인의 해시가 장기간 모두 같게 유지되면 항목 하나로 합칠 수 있다. 반대로 같은 툴체인 키 안에서
부 버전 차이로 해시가 갈리는 일이 생기면 키에 부 버전을 넣는다.
