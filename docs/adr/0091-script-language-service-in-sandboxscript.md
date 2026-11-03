# ADR-0091. 스크립트 언어 서비스 — SandboxScript 안 (검사기 재사용) · Host 표가 자동 완성의 정본 · 재생 위 디버깅 · LSP 는 표준 입출력

- 상태: **Accepted** · 날짜: 2026-11-03
- 관련: [27-SCRIPT-IDE](../27-SCRIPT-IDE.md), [19-SANDBOXSCRIPT](../19-SANDBOXSCRIPT.md), [ADR-0056](0056-sandboxscript-language-subset-own-compiler-no-gc.md),
  [ADR-0057](0057-host-api-single-definition-cpp-table-sdk-generated.md), [ADR-0065](0065-project-script-sdk-files-tsconfig-p6-check.md),
  [ADR-0081](0081-script-execution-backend-boundary-interpreter-first.md), [ADR-0086](0086-runtime-architecture-v2.md) (Capability)

## 맥락

스크립트 편집기는 여러 줄 입력 + 컴파일 오류 줄이다. 외부 IDE 는 d.ts 로 TypeScript 타입을 보지만 부분집합 · 도메인 · Capability 규칙은 모른다.
바이트코드에 소스 줄 표가 없어 트랩 위치 · 디버거가 막혀 있다. Godot 은 엔진이 LSP · DAP 를 열고, Roblox 는 데이터 모델에 묶인 자동 완성을 준다.

## 결정

```text
1. 언어 서비스 = script/lang/LanguageService (SandboxScript 라이브러리 — Foundation 만 의존). 검사기의 기호 · 타입 · 진단을 그대로 쓴다 — 별도
   파서 · 별도 규칙을 만들지 않는다. tsserver 를 끌어오지 않는다 (JS 런타임 · 의미 차이)
2. 자동 완성 · hover 의 정본 = Host 표 (kHostFunctions — owner · member · domain · doc) + 엔진 컴포넌트 접근 표 + 내장 표 + 검사기 기호 — d.ts 와
   같은 출처. 도메인 · (v0.6) 콜백 단계 Capability 로 거른다. 프로토타입: contextMembers · completeAt (이번 감사, test_language_service)
3. 분석은 Worker, 결과는 편집 번호가 맞을 때만 반영
4. 디버깅은 리플레이 (명령 기록) 를 디버그 프로세스에서 다시 돌려 거기서 VM 을 세운다 — 라이브 서버는 멈추지 않는다 (결정론 · 멀티 보존).
   감시 식은 읽기만. 중단점이 없을 때 VM 비용 회귀 < 2 % (script.regen · script.stdlib)
5. 줄 표는 .sbc 형식 10 (M6-T4) — 트랩 · 프로파일 · 디버거가 같이 쓴다
6. 외부 IDE = sbx lsp (표준 입출력 JSON-RPC — 포트를 열지 않는다) v0.7, DAP 는 디버거 뒤 (v0.8+)
```

## 선택지

| 안 | 내용 | 판단 |
|----|------|------|
| A (채택) | 자체 언어 서비스 (검사기 재사용) | 규칙이 하나 (컴파일 = 편집기 진단), 도메인 · Capability 를 안다 |
| B | tsserver + d.ts | 부분집합 밖 문법 · 엔진 규칙을 놓친다, Node 의존 |
| C | 라이브 VM 중단 디버거 | 멀티 · 틱 예산 · 결정론과 충돌 |
| D | LSP 를 TCP 포트로 (Godot 식) | 로컬 포트 노출 — 표준 입출력이 더 좁다 |

## 결과

- v0.5: 편집기 자동 완성 · hover 기본 (M6-T9), 줄 표 (M6-T4). v0.6: 색인 · 이동 · 이름 바꾸기 · 빠른 수정 · 재생 디버거. v0.7: LSP.
- 비용: 검사기에 "위치 → 기호" 질의 · 오류가 있어도 계속 분석 (지금도 진단을 모으며 계속한다 — 확인 필요는 27 3장 구현 때).
