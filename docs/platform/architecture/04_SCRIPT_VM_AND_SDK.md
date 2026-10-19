# 05. SandboxScript TypeScript / Compiler / VM 설계

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 핵심 결정
**TypeScript 문법의 제한된 부분집합**을 공식 게임 스크립트 언어로 사용한다. TS Compiler API는 편집/빌드 시 Parsing·AST·Type Checking을 돕는다. 최종 실행은 JavaScript가 아니라 자체 Typed IR → Sandbox Bytecode `.sbc` → C++ SandboxVM. Node.js는 편집/빌드 체인에서 필요할 수 있지만 게임 플레이어에서 JavaScript 런타임을 필수로 하지 않는다.

## 2. 지원 부분집합 (MVP 제안)
- 지원: 함수, 제어 흐름, `const`/제한적 `let`, 정적 타입, struct-like 데이터, 배열(상한), `enum` 유사 타입, 제한된 모듈 import, `defineComponent`, `defineSystem`, Host API.
- 불허: `eval`, `Function`, 동적 `import`, 프로토타입 재정의, 무제한 reflection, Node/DOM API, OS 파일/소켓 직접 접근, 임의 Native FFI.
- `number`를 그대로 정수/부동소수 타입으로 간주하지 말고 명시적 `i32/f32/f64` 대응 정책 결정. 정수 overflow, NaN, 소수 정밀도, PRNG, 컬렉션 순서도 정의.
- GC/메모리 모델은 VM의 수명과 복제 제약으로 정의. MVP에서는 bounded heap과 값 타입을 우선 고려.

## 3. SDK와 실제 실행 분리
```typescript
export interface ComponentRef<T> { readonly stableId: string; }
export interface IEntityAPI {
  read<T>(entity: Entity, ref: ComponentRef<T>): Readonly<T> | null;
  has<T>(entity: Entity, ref: ComponentRef<T>): boolean;
  query<T>(ref: ComponentRef<T>): EntityQuery;
}
export interface IServerContext extends IEntityAPI {
  readonly tick: number;
  readonly commands: ICommandAPI;
  readonly network: IServerNetworkAPI;
}
export interface IClientContext extends IEntityAPI {
  readonly deltaTime: number;
  readonly ui: IUIAPI;
  readonly audio: IAudioAPI;
}
```
- `.d.ts`와 `ComponentRef<T>`는 IDE 자동완성·빌드 타입 검사 계약.
- 런타임 `ctx.read()`는 Bytecode의 `CALL_HOST`를 통해 C++ `ScriptHostBindings`로 이어짐.
- `ctx`는 시스템 호출 범위에 묶인 불투명 Context; ECS 포인터·메모리를 외부에 노출하지 않음.
- `.d.ts`만으로 VM이 자동 생성되는 것이 아니므로 함수 ID, ABI, 검증 규칙, 도메인을 포함하는 API Registry 필수.

## 4. 실행 모델
| Callback | 실행 도메인 | 일반 정책 |
| --- | --- | --- |
| `onStart` | 서버/클라이언트 | 시작 시 1회, VM 인터프리터 |
| `onTick` | 서버 | 고정 30 TPS; 순서/Stage 고정 |
| `onUpdate` | 클라이언트 | 렌더 프레임; 권위 상태 변경 금지 |
| `onEvent` | 각 도메인 이벤트 | 명시적 구독, 재진입/폭주 제한 |
| `onStop` | 각 도메인 | 정리 시 실행 |
같은 Script System에서 서버 `onTick`과 클라이언트 `onUpdate`의 혼재를 기본적으로 금지한다. 메타데이터(`id`, `domain`, `stage`, `intervalTicks`)를 컴파일러가 추출하고 Scheduler는 패키지 로딩 시 등록한다.

## 5. Bytecode 및 VM
```text
TS Parser/Type Checker → Sandbox Semantic Validation → Typed IR
→ Optimize (constant folding, dead code) → .sbc + metadata
→ Bytecode Verifier → SandboxVM (register/fuel/memory/call stack)
→ HostCall ABI → ScriptBindings → ECS / SimCommand / SystemContext
```
- Bytecode에 명시적 타입의 `ADD_F32`, `ADD_I32` 등의 opcode.
- load-time은 symbol/stableId binding과 유효성·권한·스키마 검사 수행.
- HostCall은 ID 기반이며 반환 값은 읽기 전용 Snapshot/불투명 Handle로 복사; 임의 pointer 반환 금지.
- `queryBatch()` 등 Native 집계 API 지원, Entity마다 VM을 재진입하는 구조 지양.

## 6. VM 보안과 스케줄링
- 제한: fuel(명령 실행량), 메모리, 스택, spawn 수, 이벤트 수, query당 entity 수, HostCall 횟수.
- 서버 Tick의 메모리/시간 상한, 사용자별/시스템별 통계, 실행 중단 시 정책(시스템 정지/세션 종료/오류 전달).
- Script가 발생시킨 구조적 변경은 기존 ECB 또는 승인된 Gameplay Command를 통해 동일한 결정적 지점에 반영.
- `ctx.commands`는 게임 규칙 검증 후 권위 서버에서만 처리.
- OS/Network/HTTP는 별도 권한의 서버 비동기 외부 API로 격리. Simulation Tick은 외부 응답 대기하지 않는다.

## 7. Native 컴파일 최적화는 별도 단계
- v0.3 MVP는 Bytecode interpreter로 정합성을 우선 확보.
- 후보: 자주 호출되는 `onTick`/`onUpdate`, 빈번하고 무거운 `onEvent`와 그 호출 그래프.
- load-time JIT/AOT 캐시 설계 시 CPU 아키텍처, Bytecode digest, Compiler·API 버전을 캐시 키에 포함.
- Native로 컴파일하더라도 메모리 격리, 연산 예산, syscall 제한, Host 검증, Replay 동일성 필수.
- 기본 VM과 Native 결과의 WorldHash/Replay 동등성 테스트 통과 전에는 운영 서버에 활성화하지 않는다.

## 8. 정의 예시 (설계용 API)
```typescript
export const Mana = defineComponent({
  id: "rpg.mana", version: 1,
  fields: { current: f32(100), maximum: f32(100) }
});
export default defineSystem({
  id: "rpg.mana.regen", domain: "server", intervalTicks: 3,
  onTick(ctx) {
    for (const entity of ctx.query(Mana)) {
      const mana = ctx.read(entity, Mana);
      if (mana && mana.current < mana.maximum)
        ctx.commands.setField(entity, Mana, "current", Math.min(mana.current + 1, mana.maximum));
    }
  }
});
```
위의 `Math.min`은 실제 지원 부분집합에 편입·컴파일러 내장 함수로 매핑해야 한다. 이 코드와 함수명은 모두 설계 예시다.

## 9. 완료 기준
단일 게임의 Script가 Local/Remote에서 같은 결과/Hash. 권한 위반·무한 루프·불법 Bytecode·핸들 위조가 VM 경계에서 거부. SDK 타입과 Host Registry 계약 자동 비교. 10k 대상 스크립트/Native batch 벤치마크 기록.
