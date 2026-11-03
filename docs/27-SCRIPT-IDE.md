# 27. SandboxScript IDE — 언어 서비스 · 편집기 · 디버거 · LSP

> **설계 문서** (2026-11-03, master a1307fc). 결정은 [ADR-0091](adr/0091-script-language-service-in-sandboxscript.md). 언어 · 컴파일러 ·
> VM 의 규범은 [19-SANDBOXSCRIPT](19-SANDBOXSCRIPT.md) 그대로. 근거: [audit/ENGINE-BENCHMARK](audit/ENGINE-BENCHMARK.md) C1 ~ C4 (Roblox
> Script Editor · Godot LSP/DAP). 상태: **현행** · **프로토타입** (코드 · 시험 있음, 기능은 일부) · **[계획 vX]**.

---

## 0. 지금 (2026-11-03 소스)

```text
편집기     "콘텐츠" 창의 스크립트 종류 — ImGui 여러 줄 입력 (editor/ui/ContentPanel.cpp:184), [검사] = 팩 다시 컴파일 · 오류마다 "12 | <그 줄>",
           [적용] = 오버레이 EditContent (ADR-0066 — 다음 틱부터). 구문 색 · 자동 완성 · hover · 이동 없음
외부 IDE   sbx sdk 가 sandbox.d.ts 를 쓴다 → tsc · VS Code 가 타입 · 자동 완성 (ADR-0065). 부분집합 밖 (S3xx) · 엔진 규칙 (S404 도메인) 은
           tsc 가 모른다 → sbx check 로
컴파일러   script/lang: Lexer (토큰 · 위치) · Parser (AST) · Checker (Program { types, symbols, functions, components, systems, modules }) ·
           Codegen. 진단 = 코드 (S1xx ~ S5xx) · 파일 · 줄 · 열 (19 8장)
Host 표    script/api/HostApi kHostFunctions — { id, owner (ServerContext · ClientContext …), member, property, 서명, domain, effect, fuel, doc }
           — 검사기 · d.ts · 지문이 같이 읽는다 (ADR-0057)
VM         트랩 = 함수 번호 · pc (ScriptSystemAdapter 의 lastTrap). 바이트코드에 소스 줄 표가 없다 (M6-T4 계획)
프로토타입  script/lang/LanguageService (이번 감사) — contextMembers(domain) · completeAt(source, offset, domain): "ctx." 뒤 후보를 Host 표에서
           도메인으로 거른다 (서버 콜백에 label · print 없음, 클라이언트 콜백에 write · spawn 없음). 시험 test_language_service (unit_script)
```

## 1. 결정 요지 (ADR-0091)

```text
L1 언어 서비스는 SandboxScript 라이브러리 안 (script/lang) — 검사기의 기호 · 타입 · 진단을 그대로 쓴다. Studio · sbx lsp · sbx check 가 같은 서비스.
   TypeScript 언어 서비스 (tsserver) 를 끌어오지 않는다 — 부분집합 · 엔진 규칙 (도메인 · Capability · 결정론 금지) 을 모르고, JS 런타임이 든다
L2 자동 완성의 정본 = Host 표 + 검사기 기호 표 + 엔진 컴포넌트 접근 표 + 내장 표 (Intrinsics) — d.ts 와 같은 출처라 어긋나지 않는다
L3 분석은 Worker 에서, 결과는 버전 (문서 편집 번호) 이 맞을 때만 그린다 — 늦은 결과는 버린다 (편집 응답성)
L4 디버거는 "결정적 재생 위의 디버깅" — 라이브 서버를 멈추지 않는다 (6장)
L5 외부 IDE 는 LSP (sbx lsp — 표준 입출력) 로 v0.7. DAP 는 디버거가 선 뒤 (v0.8+)
```

## 2. 구조

```text
Script Editor (Studio — ImGui 위젯)        sbx lsp (표준 입출력 JSON-RPC) [계획 v0.7]        sbx check (현행)
            └──────────────┬─────────────────────────────┬───────────────────────────────────┘
                     LanguageService (script/lang)
                       ├─ 문서 표      경로 → (텍스트, 편집 번호), 팩 · import 그래프 (resolveImportPath · PackImports 현행)
                       ├─ 분석         Lexer → Parser → Checker (현행) — [계획] 파일 단위 캐시: 바뀐 파일과 그 파일을 import 한 파일만 다시
                       ├─ 기호 색인     Program.symbols · modules (현행 데이터) → 위치 → 기호, 기호 → 정의 · 참조 [계획 v0.6]
                       ├─ 자동 완성     ctx.(도메인 · 콜백) · 컴포넌트 필드 · import 이름 · 내장 · 지역 이름 — contextMembers 프로토타입부터
                       ├─ 진단         검사기 진단 그대로 (+ 빠른 수정 제안 — 4장)
                       ├─ hover · 서명  Host 표 doc · 서명, 사용자 함수 = 선언 줄 + 앞의 주석 블록 (Roblox 의 블록 주석 툴팁 — 벤치마크 C1)
                       └─ 이동 · 이름 바꾸기  정의로 · 참조 찾기 · 이름 바꾸기 (같은 팩 · import 경로 고침) [계획 v0.6]
```

## 3. 자동 완성 규칙

| 자리 | 후보 | 거르기 | 상태 |
|------|------|--------|------|
| `ctx.` | Host 표의 그 Context 멤버 | 콜백의 도메인 (server · client) — [계획 v0.6] 콜백 단계의 Capability (22 3.2: onRender 에는 Draw 만) | 프로토타입 (도메인만, 수신자 이름이 ctx 일 때만) |
| 식의 타입 뒤 `.` | 그 타입의 필드 · 메서드 (구조체 · 컴포넌트 데이터 · 배열 · Map · 내장 컬렉션) | 검사기의 타입 | [계획 v0.5 M6-T8] |
| `ctx.read(e, |)` · `import { | } from "sandbox"` | 컴포넌트 (스크립트 정의 · 엔진 접근 표) · SDK 이름 | 도메인 (클라이언트는 복제되는 것만 — M2-T1) | [계획 v0.5 M6-T8] |
| `import … from "|"` | 팩 안 경로 · "@<팩>/…" (직접 의존만 — ADR-0075) | 매니페스트 의존 | [계획 v0.6] |
| `defineSystem({ |` | 키 (id · domain · events · 콜백 이름 — 예약 이름은 "[계획 vX]" 표시, M2-T7) | — | [계획 v0.6] |
| `events: { |` | death · spawn · areaEnter · score · action · signal (api::kGameEventKinds) | — | [계획 v0.6] |

## 4. 진단 · 빠른 수정

```text
실시간 진단  입력 멈춤 300 ms 뒤 분석 (Worker) — 오류 밑줄 · 목록. 검사기 그대로 (새 규칙을 따로 만들지 않는다)
빠른 수정 [계획 v0.6]  S404 클라이언트에서 쓰기 → "서버 시스템으로 옮기기" 안내 / 없는 이름 → 비슷한 이름 (편집 거리 ≤ 2) / 없는 import → 경로 제안 /
           S3xx 지원 안 하는 문법 → 19 의 대안 문장 (예: Promise → onEvent 요청 · 결과)
포맷터 [계획 v0.7]  AST → 텍스트 (주석 보존이 선행 — 파서가 주석을 붙여야 한다)
스니펫     defineSystem · defineComponent · onEvent 틀 — 지금 콘텐츠 창의 "새 파일 (시스템 틀)" 의 확장 [계획 v0.6]
```

## 5. LSP (v0.7)

```text
sbx lsp — 표준 입출력 JSON-RPC (LSP 3.17 의 initialize · didOpen/didChange · completion · hover · definition · references · rename ·
publishDiagnostics). 같은 LanguageService. 프로젝트 루트 = sandbox.project.json 이 있는 폴더. 네트워크 포트를 열지 않는다 (Godot 은 포트
6005 — 벤치마크 C2. 여기서는 표준 입출력으로 보안 면을 줄인다). VS Code 확장은 얇은 클라이언트 (TypeScript 문법 색은 그대로, 의미는 sbx lsp)
```

## 6. 디버깅 — 결정론을 깨지 않는 방식

| 방식 | 내용 | 결정론 | 비용 · 위험 | 판단 |
|------|------|--------|-------------|------|
| A 라이브 서버 멈춤 | 서버 틱 안에서 VM 을 중단점에서 세운다 | 다른 플레이어 · 틱 시간이 멈춤 — 멀티에서 쓸 수 없다, 틱 예산 · 연료와 엉킨다 | 높음 | 도입 안 함 (로컬 단독 테스트에서만 [검토 v0.8]) |
| **B 결정적 재생 위 디버깅** | 리플레이 (명령 기록 — 현행) 를 디버그 프로세스에서 다시 돌려 그 틱 · 그 시스템 호출에서 VM 을 세운다. 재생은 같은 결과 (04) 라 버그가 그대로 나온다 | 깨지 않는다 (라이브와 따로) | 중간 — 줄 표 (M6-T4) · VM 중단 훅 · 세션 UI | **채택 v0.6** |
| C 로그 · 트레이스 지점 | 중단 없이 값 기록 (logpoint) | 깨지 않는다 (로그는 해시 밖) | 낮음 | v0.6 (B 와 같이) |

```text
VM 훅 [계획 v0.6]  Interpreter 에 "중단점 표 (함수 · pc)" — 비어 있으면 훅 비용 0 (분기 하나도 없는 별도 루프 경로가 이상적 — 측정해서
                   script.regen · script.stdlib 회귀 < 2 % 를 완료 기준으로)
줄 표              M6-T4 (.sbc 형식 10 — 함수마다 pc → (파일, 줄, 열)) — 트랩 위치 · 프로파일 · 디버거가 같이 쓴다
보는 것            호출 스택 (프레임 · 함수 · 줄) · 지역 (레지스터 → 기호 이름 — 코드 생성이 이름 표를 남겨야 함) · 감시 (식 평가 = 그 프레임의
                   지역 · ctx 읽기만, 쓰기 · Host 쓰기 금지 — 재생을 바꾸지 않는다)
단계 실행          Step Into · Over · Out = 줄 표 위 pc 범위
서버 · 클라이언트  서버 시스템 = 재생 디버그. 클라이언트 시스템 (onUpdate — 표시만) 은 라이브로 세워도 시뮬레이션에 닿지 않는다 [계획 v0.7]
Hot Reload 충돌     디버그 세션은 스냅숏한 바이트코드 (digest) 로 — 세션 중 소스가 바뀌면 "다시 시작" 안내 (다른 바이트코드로 재생하면 같은
                   결과가 아니다)
```

## 7. 성능 · 측정 (숫자는 측정 뒤 — 14)

```text
자동 완성 응답 (파일 1k · 5k 줄, 팩 50 파일), 전체 다시 검사 시간, 편집 중 프레임 시간 회귀 (Studio), VM 훅 비용 (중단점 0 개일 때) — sbx_bench
시나리오 [계획 v0.6]. 이번 감사의 프로토타입은 기능 시험만 (성능을 재지 않았다)
```

## 8. 상태 표

| 항목 | 상태 |
|------|------|
| 컴파일 · 진단 (줄 · 열) · 오버레이 Hot Reload · d.ts | 현행 |
| ctx. 자동 완성 (도메인 거르기) | 프로토타입 (script/lang/LanguageService · test_language_service) |
| 편집기 위젯 (구문 색 · 팝업 · 밑줄) · 타입 기반 자동 완성 · hover | [계획 v0.5 M6-T8] |
| 줄 표 · 트랩 줄 · 함수별 프로파일 | [계획 v0.5 M6-T4] (기존) |
| 정의 · 참조 · 이름 바꾸기 · 빠른 수정 · 스니펫 · 증분 분석 | [계획 v0.6] |
| 재생 디버거 (중단점 · 단계 · 스택 · 지역 · 감시 · logpoint) | [계획 v0.6] |
| LSP · 포맷터 · 클라이언트 라이브 디버그 | [계획 v0.7] |
| DAP | [계획 v0.8+] |
