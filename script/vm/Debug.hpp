#pragma once
// SandboxScript 디버거 — VM 쪽 (v0.9 ST-06-7 — docs/27-SCRIPT-IDE.md 6장, ADR-0132). 결정적 재생 위의 디버깅 (방식 B):
// 디버그 세션은 모듈의 사본을 갖고, 멈출 자리의 명령을 BRK 로 바꾼다 (원래 명령은 CodePatches). 인터프리터는 BRK 에서
// IDebugHook 을 부르고 원래 명령을 돌린다 — 중단점이 없으면 실행 비용은 0 (BRK 는 switch 의 칸 하나).
//
// 중단점: 파일 · 줄 → 그 줄에 코드가 있는 함수마다 그 줄의 첫 명령 (줄 표 — 형식 11). logpoint 는 멈추지 않고 글을
// 남긴다 ("{식}" 은 감시 식으로 채운다).
// 단계: Into = 모든 함수의 줄 시작, Over = 스택의 함수들의 줄 시작 (더 깊은 프레임은 지나간다), Out = 바깥 프레임들의
// 줄 시작 (지금 깊이 이상은 지나간다). 멈추면 단계 BRK 를 모두 거둔다.
// 보기: 프레임 (함수 · 파일:줄:열), 지역 (형식 12 의 지역 이름 표 — 그 pc 에서 보이는 것), 감시 식 (지역 · .필드 ·
// [첨자] · .length — 읽기만, 쓰기 · Host 호출은 없다: 재생 결과를 바꾸지 않는다).
//
// 훅은 읽기만 하므로 디버거를 붙여도 호출 결과 · 연료 · 프로파일이 같다 (시험이 본다 — 재생의 WorldHash 가 같은 근거).
// 차례: 인터프리터를 모듈 사본으로 만든 뒤에 (생성자는 검증된 모듈을 요구한다 — BRK 는 검증이 거절) BRK 를 놓는다.

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "script/vm/Bytecode.hpp"
#include "script/vm/Interpreter.hpp"

namespace sbx::script::vm {

// BRK 자리 표 — 세션의 모듈 사본을 고친다 (원본 · 파일은 그대로). 자리마다 주인 비트 (사용자 중단점 · 단계): 모두
// 거두면 원래 명령으로 되돌린다
class CodePatches {
public:
    enum Owner : u8 { User = 1, Step = 2 };

    explicit CodePatches(bc::Module& module) noexcept : m_module(&module) {}

    // 자리 (함수 · pc) 에 BRK — 함수 · pc 가 없으면 false
    bool set(u32 function, u32 pc, u8 owner);
    void clear(u32 function, u32 pc, u8 owner);
    void clearOwner(u8 owner);
    [[nodiscard]] const bc::Insn* original(u32 function, u32 pc) const noexcept;
    [[nodiscard]] u8 owners(u32 function, u32 pc) const noexcept;
    [[nodiscard]] usize size() const noexcept { return m_sites.size(); }

private:
    struct Site {
        bc::Insn original;
        u8 owners = 0;
    };
    bc::Module* m_module;
    std::map<std::pair<u32, u32>, Site> m_sites;
};

// 줄이 시작하는 명령 — 줄 표에서 줄이 앞 칸과 다른 칸의 pc (단계 실행이 멈출 자리)
[[nodiscard]] std::vector<u32> lineStarts(const bc::Function& f);
// 파일 · 줄 → (함수, 그 줄의 첫 pc) — 그 줄에 코드가 있는 함수마다 (같은 줄의 화살표 함수도)
[[nodiscard]] std::vector<std::pair<u32, u32>> resolveLine(const bc::Module& m, std::string_view file, u32 line);

struct DebugVariable {
    std::string name;
    std::string type;  // "number" · "Vec2" · "Entity | null" …
    std::string value; // 사람이 읽는 값 ("12" · "\"abc\"" · "{ x: 1, y: 2 }" · "[1, 2, 3]" · "null")
};

// 값 → 글 (타입 번호로 해석 — 배열 · 구조체는 depth 단계까지 펼친다, 긴 것은 자른다)
[[nodiscard]] std::string describeValue(const Interpreter& vm, Value v, u32 type, u32 depth = 2);
// 그 프레임에서 보이는 지역 (선언 차례 — 매개변수 먼저)
[[nodiscard]] std::vector<DebugVariable> frameLocals(const Interpreter& vm, usize frame);

struct WatchResult {
    bool ok = false;
    DebugVariable var; // ok 가 아니면 value = 이유
};
// 감시 식: 지역 이름 뒤에 .필드 · [정수] · .length 를 잇는다 ("m.current" · "xs[2].pos.x" · "names.length")
[[nodiscard]] WatchResult evaluateWatch(const Interpreter& vm, usize frame, std::string_view expr);

enum class StopReason : u8 { Breakpoint, Step };
enum class StepAction : u8 { Continue, Into, Over, Out };

struct DebugStop {
    StopReason reason = StopReason::Breakpoint;
    u32 function = 0;
    u32 pc = 0;
    std::string file; // "scripts/regen.ts"
    u32 line = 0;     // 0 = 줄 표 없음
    u32 column = 0;
    usize depth = 0; // 프레임 수 (1 = 부른 콜백)
    const Interpreter* vm = nullptr;
};

struct Breakpoint {
    std::string file;
    u32 line = 0;
    std::string log; // 비면 멈춘다, 아니면 logpoint
    usize sites = 0; // 놓인 BRK 수
    u64 hits = 0;
};

class Debugger final : public IDebugHook {
public:
    explicit Debugger(bc::Module& module) noexcept : m_module(&module), m_patches(module) {}

    [[nodiscard]] const CodePatches& patches() const noexcept { return m_patches; }
    // 중단점 — 놓인 자리 수 (0 = 그 줄에 코드가 없다 — 표에는 남지 않는다). 같은 줄이면 바꾼다
    usize setBreakpoint(std::string_view file, u32 line, std::string log = {});
    bool clearBreakpoint(std::string_view file, u32 line);
    void clearBreakpoints();
    [[nodiscard]] const std::vector<Breakpoint>& breakpoints() const noexcept { return m_breakpoints; }

    // 멈출 때마다 부른다 — 돌려준 행동으로 이어간다. 없으면 멈추지 않는다 (logpoint · 단계 없이 계속)
    using StopHandler = std::function<StepAction(const DebugStop&)>;
    void setStopHandler(StopHandler h) { m_handler = std::move(h); }
    // 다음에 BRK 를 만나기 전에 단계를 건다 (예: 첫 줄에서 멈추기 = Into) — 깊이 0 에서 시작한 것으로 본다
    void armStep(StepAction a);

    [[nodiscard]] const std::vector<std::string>& logs() const noexcept { return m_logs; }
    void clearLogs() { m_logs.clear(); }
    [[nodiscard]] u64 brkHits() const noexcept { return m_brkHits; } // BRK 를 만난 수 (자리 · 단계 — 비용)
    [[nodiscard]] u64 stops() const noexcept { return m_stops; }

    void onBreak(const Interpreter& vm, u32 function, u32 pc) override;

private:
    void beginStep(StepAction a, const Interpreter* vm, u32 function, u32 line);
    void endStep();
    [[nodiscard]] const std::vector<u32>& starts(u32 function);
    [[nodiscard]] std::string formatLog(const Interpreter& vm, std::string_view text) const;

    bc::Module* m_module;
    CodePatches m_patches;
    std::vector<Breakpoint> m_breakpoints;
    std::map<std::pair<u32, u32>, usize> m_siteBreakpoint; // 자리 → m_breakpoints 의 자리
    StopHandler m_handler;
    std::vector<std::string> m_logs;
    std::map<u32, std::vector<u32>> m_starts; // 함수 → 줄 시작 (한 번 구한다)
    struct StepState {
        StepAction action = StepAction::Continue;
        usize depth = 0;
        u32 function = 0;
        u32 line = 0;
    } m_step;
    u64 m_brkHits = 0;
    u64 m_stops = 0;
};

} // namespace sbx::script::vm
