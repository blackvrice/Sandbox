# ADR-0102. 키 다시 묶기의 사용자 파일은 <설정 폴더>/input.json — 기본과 다른 액션만 (ADR-0083 5 의 bindings.json 대신)

- 상태: **Accepted** · 날짜: 2026-11-09
- 관련: [ADR-0083](0083-i18n-accessibility-foundation.md) 5 (키 다시 묶기), [ADR-0017](0017-physical-keys-event-queue-headless-window.md) (ActionMap),
  [20-CREATOR-ALPHA](../20-CREATOR-ALPHA.md) 5장 · 7.5 (사용자 설정 폴더), [07-PLATFORM](../07-PLATFORM.md) 5장

## 맥락

ADR-0083 5 는 키 덮어쓰기를 `<사용자 설정>/sandbox/bindings.json` 에 두기로 했다. 그 뒤 v0.5 M1-T3 (사용자 설정 · 안전 모드) 가 같은
폴더의 `input.json` 을 이미 읽는다 — 형식은 `settings/input.json` · `--input` · 팩 `presentation/input.json` 과 같은 `sandbox.input`
이고, 깨지면 옆으로 옮기고 알리며, 안전 모드는 읽지 않는다. v0.7 M4-T3 의 "키" 패널이 저장할 파일을 정해야 한다.

## 결정

```text
1. 키 다시 묶기의 사용자 파일 = <설정 폴더>/input.json (M1-T3 의 것 그대로). bindings.json 은 만들지 않는다
2. 저장하는 내용 = 엔진 기본 바인딩 (DefaultInput) 과 다른 액션만 (ActionMap::overridesFrom) — 엔진 액션의 바뀐 바인딩 · 사용자
   play.* 덮어쓰기. 읽을 때는 기본 위에 merge (M1-T3 그대로) 라 저장 → 다시 시작 = 같은 맵
3. 패널의 지금 맵은 기본 + input.json + --input + 패널에서 바꾼 것 — 저장하면 --input 의 값도 들어간다 (--input 은 시험 · QA 용)
4. 안전 모드 (--safe-mode) 는 저장하지 않는다 (이 실행에만)
```

## 근거

```text
- 파일 하나 · 형식 하나: 사람이 손으로 고쳐도, 패널이 써도, --input 으로 넘겨도 같은 sandbox.input
- 깨진 파일 처리 · 안전 모드 · 설정 폴더가 M1-T3 에 이미 있다 — 두 번째 파일은 같은 규칙을 다시 만든다
- 기본과 다른 것만 쓰면 엔진이 기본 키를 바꿨을 때 사용자 파일이 옛 기본을 붙잡지 않는다
```

## 결과

- 얻는 것: 파일 · 규칙 하나, 저장 → 다시 시작 왕복이 시험된다 (test_key_bindings).
- 포기하는 것: ADR-0083 5 의 파일 이름 (bindings.json — 만든 적 없다).
- 위험: --input 을 쓴 실행에서 저장하면 그 값이 사용자 파일에 남는다 (패널이 지금 맵을 저장한다 — 3).

## 대안

| 대안                                  | 기각 사유                                                      |
|---------------------------------------|----------------------------------------------------------------|
| bindings.json (ADR-0083 5 그대로)     | input.json 과 같은 형식의 파일이 둘 — 어느 것이 이기는지 규칙이 는다 |
| 지금 맵 전부를 저장                   | 엔진 기본 키가 바뀌어도 사용자 파일이 옛 기본을 붙잡는다          |

## 재검토 조건

게임마다 다른 키 설정 (프로젝트별 덮어쓰기) 이 필요할 때, 게임패드 바인딩 (I5).
