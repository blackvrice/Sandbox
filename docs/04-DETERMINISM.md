# 04. 결정론 규약

> **이 문서는 시뮬레이션 코드를 건드리는 모든 작업의 전제입니다.**
> 다른 문서와 충돌하면 이 문서가 이깁니다. 결정 근거: [ADR-0004](adr/0004-determinism-scope.md).

---

## 1. 왜, 그리고 어디까지

이 프로젝트는 서버 권한 구조입니다. 클라이언트는 결과를 받을 뿐 계산하지 않으므로
**멀티플레이를 위해 결정론이 필요하지는 않습니다.** 결정론은 다음 네 가지를 위해 유지합니다.

```text
Replay          세이브 + 기록한 명령 → 같은 경기
Save/Load 검증   복원 후 이어서 돌리면 저장 안 한 것과 같아야 한다
버그 재현        "이 리플레이 3만 틱에서 늑대가 멈춘다"가 재현 가능해야 한다
회귀 테스트      리팩터링이 동작을 바꾸지 않았다는 증명 (골든 해시)
```

### 범위

```text
보장    같은 바이너리 + 같은 콘텐츠 + 같은 시작 세이브 + 같은 명령 로그 → 같은 WorldHash
        (스레드 수, 실행 속도, 렌더 여부, 접속 클라이언트 수와 무관)
비보장  다른 컴파일러 / OS / CPU / 빌드 설정 사이의 bit 동일성
```

결정론이 깨지면 크래시가 아니라 "가끔 리플레이가 이상하다"로 **조용히** 나타납니다.
그래서 규약 위반은 컴파일 에러처럼 취급합니다.

---

## 2. 불변식

| # | 불변식 | 자동 검사 |
|---|---|---|
| D1 | 같은 시작 + 같은 명령 → 같은 최종 WorldHash (실행 간 재현성) | `sbx_sim_check --repeat` |
| D2 | Save → Load → N틱 = 저장 안 했을 때 N틱과 같은 해시 | `--save-at` |
| D3 | 리플레이 재생 중 해시 divergence 0 | `--replay-roundtrip` |
| D4 | 리팩터링 커밋은 골든 해시와 같은 해시 | `--golden` |
| D5 | Worker 스레드 수(0, 1, N)가 결과를 바꾸지 않는다 | `--threads 0,1,8` 비교 |
| D6 | 서버에 붙은 클라이언트 수·Interest가 결과를 바꾸지 않는다 | 네트워크 통합 테스트 |

---

## 3. 고정 틱

```text
dt 는 항상 sim::kFixedDt (1/30). 측정된 벽시계 델타를 시뮬레이션에 넣지 않는다.
게임 속도는 틱 간격만 바꾼다.
렌더 보간 결과는 시뮬레이션으로 되돌아가지 않는다 (클라이언트에만 존재).
```

---

## 4. 금지 목록

### 4.1 난수

```text
금지  rand(), std::random_device, 시간 기반 시드, <random> 의 분포 객체(구현마다 다름)
허용  RandomService (03-SIMULATION 9장) — counter-based, (worldSeed, tick, purpose, saveId) 키
```

### 4.2 해시 컨테이너 순회

```text
금지  unordered_map/set 을 순회하며 시뮬레이션 상태를 바꾸거나 순서 의존 결과를 만드는 것
허용  조회 전용. 순회가 필요하면 키를 vector 로 뽑아 정렬 후 순회
```

### 4.3 정렬

```text
금지  동점이 생기는 비교자로 std::sort
허용  std::stable_sort, 또는 비교자 마지막에 saveId 를 넣어 전순서
```

### 4.4 포인터·주소

```text
금지  포인터 값으로 정렬·비교·해싱·캐시 키 (RTS PathManager 의 gridId 포인터 키 같은 것)
```

### 4.5 시간·환경·스레드

```text
금지  시뮬레이션 안에서 std::chrono, time(), 프레임 카운터
금지  스레드 완료 순서에 따라 결과를 적용 (Job 결과는 제출 순서로)
금지  환경 변수, 로케일(std::locale 에 따른 파싱), 디렉터리 나열 순서
      → 콘텐츠 파일은 항상 경로 문자열 정렬 후 로드
금지  병렬 부동소수 리덕션(합계·평균)을 스레드별 부분합으로 — 결합 순서가 바뀐다
```

### 4.6 부동소수

이 프로젝트는 시뮬레이션에 `float`을 씁니다 (RTS의 Fixed 16.16을 쓰지 않음, ADR-0004).
같은 바이너리 안에서 재현되도록 다음을 지킵니다.

```text
빌드 플래그   MSVC  /fp:precise   (/fp:fast 금지)
              Clang/GCC  -ffp-contract=off  -fno-fast-math
              → CMake 의 sbx_simulation_flags() 인터페이스 타깃이 Core 에 강제한다
코드          시뮬레이션에서 수동 SIMD intrinsic 금지 (Phase 15 에서 결정론 테스트와 함께만)
              std::sin/cos/sqrt/pow 사용 허용 (같은 바이너리 = 같은 libm)
              double 과 float 혼합 연산은 명시적 캐스트로 (암묵 승격 순서 차이 방지)
해시          float 은 정수화해서 먹인다 (5장 H1)
```

---

## 5. WorldHash

### 5.1 정의

```text
알고리즘   FNV-1a 64 (RTS 계승). 교체 시 kSimVersion++.
입력 순서  1. tick
           2. worldSeed
           3. 청크 (좌표 오름차순): terrainRevision, 지형 레이어 바이트
           4. 엔티티 (EntityId.index 오름차순):
                saveId
                Hashed 플래그 컴포넌트를 stableId 오름차순으로: stableId, version, reflect(HashVisitor)
           5. 월드 오버레이 콘텐츠(Rule/Behavior/Prefab 변경분)의 해시
           6. RandomService.worldSeed (2와 같지만 별도 리소스 상태가 생기면 여기)
제외       ClientOnly·ServerOnly 중 파생 상태, Opaque 컴포넌트, 네트워크 상태, 메트릭, EventStream
주기       기본 30틱마다 + 저장 시 + 테스트 요청 시. 디버그 옵션으로 매 틱.
```

### 5.2 규칙

```text
H1. float 은 round(x × 1024) 를 int64 로 먹인다. raw 비트를 넣지 않는다.
H2. 타입 식별자는 stableId (등록 순서 인덱스 금지).
H3. 새 시뮬레이션 상태 = 컴포넌트 + Hashed. Resource 에 상태를 두면 해시 포함 여부를 문서에 명시.
    해시에 없는 상태는 divergence 검출에서 빠진다 = 버그가 숨는다.
H4. 해시 대상·순서를 바꾸면 kSimVersion++ 과 골든 갱신.
```

H3을 구조로 막기 위해 `Persistent` 컴포넌트는 기본으로 `Hashed`입니다. 끄려면 등록 매크로 옆에 사유 주석이 필수입니다.

### 5.3 해시 진단

불일치 시 `sbx_sim_check`가 **틱 → 엔티티(saveId) → 컴포넌트(stableId) → 필드**까지 좁혀 출력합니다.
엔티티별 해시를 따로 계산해 두 실행을 이분 탐색하고, 리플렉션 `DiffVisitor`로 필드 차이를 보입니다.

---

## 6. simVersion

```cpp
namespace sbx::sim { inline constexpr std::uint32_t kSimVersion = 1; }
```

| 올린다 | 올리지 않는다 |
|---|---|
| 틱 파이프라인 순서, System 추가·삭제 | 렌더링, 에디터 UI, 오디오, 에셋 |
| 공식·밸런스 상수(엔진 내장분), Rule op·Behavior 노드 의미 | 로그·진단·도구 |
| WorldHash 대상·순서·알고리즘 | 동작이 같은 리팩터링 (골든이 그대로여야 함) |
| 명령 해석 방식 | 콘텐츠 데이터 변경 (그건 contentHash 가 구분) |
| RandomService 시드 유도·생성기 | |

**골든 해시를 갱신하는 커밋은 kSimVersion도 올립니다.** 둘은 한 쌍입니다.
리플레이·골든 파일에 simVersion을 기록하고, 다르면 divergence를 "규칙 차이"로 분류합니다.

### 이력

| 버전 | 날짜 | 변경 |
|---|---|---|
| 1 | (Phase 3) | 최초 |

---

## 7. 병렬화와 결정론 (Phase 15)

```text
- System 간 병렬: AccessSet 이 겹치지 않는 System 만 같은 웨이브. 웨이브 경계에서 ECB 를 System 순서로 병합.
- System 내 병렬: dense 범위 분할. Job 별 ECB 와 Intent 버퍼 → 범위 순서로 병합.
- 공유 쓰기 금지. 리덕션은 범위별 결과를 범위 순서로 합친다.
- D5(--threads 0,1,8 동일 해시)를 병렬화 커밋마다 CI 에서 검사.
```

---

## 8. 리뷰 체크리스트 (시뮬레이션 변경)

```text
[ ] dt 대신 측정 시간을 쓰지 않았다
[ ] unordered 컨테이너를 순회하며 상태를 바꾸지 않았다
[ ] 정렬 비교자가 전순서다 (마지막에 saveId)
[ ] 새 상태는 Hashed 컴포넌트에 있다
[ ] 난수는 RandomService 스트림으로만
[ ] Worker 결과는 제출 순서로 적용한다
[ ] 콘텐츠·파일 로드 순서가 정렬되어 있다
[ ] 동작을 바꿨다면 kSimVersion++ 과 골든 갱신을 같은 커밋에 했다
[ ] sbx_sim_check --repeat / --save-at / --replay-roundtrip / --golden 통과
```
