# ADR-0126. 스크립트 오류 메시지 — 코드마다 고치는 법 표 · 진단 문구 골든 · 트랩 호출 경로

- 상태: **Accepted** · 날짜: 2026-11-11
- 관련: [ADR-0124](0124-bytecode-line-table-format-11.md) (줄 표), [19-SANDBOXSCRIPT](../19-SANDBOXSCRIPT.md) 8 · 13장,
  [20-CREATOR-ALPHA](../20-CREATOR-ALPHA.md) 9장 (R3 오류 메시지), task-registry M6-T4 (조각 3 — "진단 문구 골든")

## 맥락

20 9장의 R3: S 진단마다 "고치는 법" 한 줄 (일부 문장만 대안을 말했다), 흔한 TS 습관 (class · var · any · 최상위 let) 의 안내, 트랩의
스크립트 위치와 호출 경로. 진단 문장은 시험이 코드만 봐 문구가 조용히 바뀌어도 몰랐다. export class 는 S320 ("이 export 모양은 없다") 으로
나와 TS 사용자에게 엉뚱한 안내였다.

## 결정

```text
1. script/lang/DiagnosticHelp: 코드마다 (뜻, 고치는 법 한 줄) 표 — 컴파일러가 쓰는 76 개 전부. DiagnosticList::error · warning 이
   Diagnostic.help 를 채운다. 문장 (그 자리의 사실) 과 고치는 법 (코드의 일반 안내) 을 섞지 않는다 — describe() 는 그대로, describeWithHelp()
   = describe() + "\n    고치는 법: …"
2. 표 = 소스: 시험이 script/** 의 "Sxxx" 리터럴을 모아 표와 양쪽으로 맞춘다 (빠진 코드 · 쓰지 않는 칸 모두 실패)
3. ContentIssue.help — 스크립트 진단만. sbx check (검사 · 시험의 오류 줄), sbx doctor (→ 줄), 프로젝트 열기 오류, Studio 콘텐츠 창 (스크립트
   적용), 빌드 보고, 오버레이 오류가 고치는 법을 보인다
4. 진단 문구 골든: 흔한 실수 18 가지 (class · var · == · 최상위 let · any · Math.random · console · try · ?. · 좁히지 않은 null · number → i32 ·
   타입 없는 매개변수 · 인자 수 · 없는 필드 · 없는 이름 · 수 조건 · return 빠짐 · 없는 import) 의 describeAllWithHelp 를
   tests/data/script_diagnostics.golden.txt 와 견준다. 일부러 바꾸면 SBX_UPDATE_GOLDEN=1 로 다시 쓴다 (리뷰가 차이를 본다)
5. export class · export var 는 그 낱말의 진단 (S301 · S303) — S320 은 export { } · export * 같은 export 모양에만
6. 트랩 호출 경로: CallResult.stack = (함수, pc) 안쪽부터 바깥까지 16 칸 (더 깊으면 안쪽 15 + 맨 바깥). 바깥 프레임의 pc = 부른 명령
   (CALL · INTR — 내장 콜백도). 트랩 로그 · lastTrap = "scripts/a.ts:6:10 (helper) ← :14:20 (onTick)" (같은 파일은 파일 이름을 줄인다).
   성공 호출은 비용 없음 (트랩 때만 run() 끝에서 모은다 — 트랩 뒤에도 프레임이 남아 있다. INTR 앞에 프레임 pc 하나를 적는다).
   실행 루프의 트랩 자리에서 모으면 merge sort 벤치가 약 15 % 느려졌다 (측정 — DEVELOPMENT_LOG 2026-11-11)
```

## 근거

```text
- 표 하나: 코드의 안내를 문장마다 흩어 적으면 빠지고 어긋난다. 문장은 자리의 사실 ("P 에는 필드 'y' 가 없다") 을, 표는 할 일을
- 골든: 문구는 사용자 계약 (튜토리얼 · 스크린샷 · 검색) — 바뀌면 리뷰가 보게
- 경로: 트랩이 도움 함수 안이면 안쪽 자리만으로는 어느 콜백이 불렀는지 모른다
```

## 결과

- 얻는 것: 모든 S 진단의 고치는 법, 문구 골든, 호출 경로, TS 습관 (export class) 의 맞는 진단, Math.random 문장의 낡은 "[계획]" 고침
  (ctx.random 은 v0.5 부터 있다).
- 포기하는 것: 고치는 법의 자동 수정 (빠른 수정 — ST-06-8), 다국어 문구 (진단은 한국어 — M4 다국어는 게임 UI 만).
- 위험: 골든이 문구를 묶어 작은 문장 손질도 골든 갱신이 필요하다 — 의도된 비용.

## 대안

| 대안 | 기각 사유 |
|------|-----------|
| 문장마다 "— 대안" 을 덧붙임 (지금처럼) | 76 코드 · 수백 자리에 흩어져 빠진다 |
| 고치는 법을 문서 링크로만 | 편집기 · CLI 에서 바로 읽을 수 없다 |
| 트랩 때 전체 스택 | 깊이 128 — 로그가 길어진다, 안쪽 · 맨 바깥이면 충분하다 |

## 재검토 조건

빠른 수정 (ST-06-8), 언어 서비스 hover 의 고치는 법, 진단 다국어.
