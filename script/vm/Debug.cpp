#include "script/vm/Debug.hpp"

#include <algorithm>
#include <format>

#include "script/vm/Numbers.hpp"

namespace sbx::script::vm {

namespace {

constexpr usize kMaxShownElements = 8; // 배열 · 구조체를 펼칠 때
constexpr usize kMaxShownStringBytes = 120;

[[nodiscard]] bool identStart(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || u >= 0x80;
}
[[nodiscard]] bool identChar(char c) noexcept {
    return identStart(c) || (c >= '0' && c <= '9');
}

[[nodiscard]] std::string quote(std::string_view s) {
    usize cut = std::min(s.size(), kMaxShownStringBytes);
    while (cut > 0 && cut < s.size() && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) {
        --cut; // UTF-8 글자 가운데를 자르지 않는다
    }
    std::string out = "\"";
    for (const char c : s.substr(0, cut)) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
        }
    }
    out += cut < s.size() ? "…\"" : "\"";
    return out;
}

// 타입 이름 — 바이트코드는 구조체 이름을 싣지 않는다 (구조적 타입): 구조체는 필드로 ("{ hp: i32, pos: {…} }")
[[nodiscard]] std::string debugTypeName(const bc::Module& m, u32 type, u32 depth = 1) {
    if (type >= m.types.size()) {
        return "?";
    }
    const bc::TypeDesc& t = m.types[type];
    switch (t.kind) {
    case bc::TypeKind::Struct: {
        if (depth == 0) {
            return "{…}";
        }
        std::string out = "{ ";
        for (usize i = 0; i < t.fieldNames.size(); ++i) {
            out += std::format("{}{}: {}", i == 0 ? "" : ", ", t.fieldNames[i],
                               debugTypeName(m, t.fieldTypes[i], depth - 1));
        }
        return out + " }";
    }
    case bc::TypeKind::Array:
        return debugTypeName(m, t.elem, depth) + "[]";
    case bc::TypeKind::Box:
        return debugTypeName(m, t.elem, depth) + " | null";
    default:
        return bc::typeName(m, type);
    }
}

} // namespace

// --- CodePatches ---

bool CodePatches::set(u32 function, u32 pc, u8 owner) {
    if (function >= m_module->functions.size() || pc >= m_module->functions[function].code.size()) {
        return false;
    }
    bc::Insn& at = m_module->functions[function].code[pc];
    auto [it, fresh] = m_sites.try_emplace({function, pc});
    if (fresh) {
        it->second.original = at;
        at = bc::Insn{bc::Op::BRK, 0, 0, 0, 0};
    }
    it->second.owners = static_cast<u8>(it->second.owners | owner);
    return true;
}

void CodePatches::clear(u32 function, u32 pc, u8 owner) {
    const auto it = m_sites.find({function, pc});
    if (it == m_sites.end()) {
        return;
    }
    it->second.owners = static_cast<u8>(it->second.owners & ~owner);
    if (it->second.owners == 0) {
        m_module->functions[function].code[pc] = it->second.original;
        m_sites.erase(it);
    }
}

void CodePatches::clearOwner(u8 owner) {
    for (auto it = m_sites.begin(); it != m_sites.end();) {
        it->second.owners = static_cast<u8>(it->second.owners & ~owner);
        if (it->second.owners == 0) {
            m_module->functions[it->first.first].code[it->first.second] = it->second.original;
            it = m_sites.erase(it);
        } else {
            ++it;
        }
    }
}

const bc::Insn* CodePatches::original(u32 function, u32 pc) const noexcept {
    const auto it = m_sites.find({function, pc});
    return it != m_sites.end() ? &it->second.original : nullptr;
}

u8 CodePatches::owners(u32 function, u32 pc) const noexcept {
    const auto it = m_sites.find({function, pc});
    return it != m_sites.end() ? it->second.owners : u8{0};
}

// --- 줄 ---

std::vector<u32> lineStarts(const bc::Function& f) {
    std::vector<u32> out;
    u32 last = 0;
    for (const bc::LineEntry& l : f.lines) {
        if (l.line != last) {
            out.push_back(l.pc);
            last = l.line;
        }
    }
    return out;
}

std::vector<std::pair<u32, u32>> resolveLine(const bc::Module& m, std::string_view file, u32 line) {
    std::vector<std::pair<u32, u32>> out;
    for (u32 fi = 0; fi < m.functions.size(); ++fi) {
        const bc::Function& f = m.functions[fi];
        if (bc::functionFile(f.name) != file) {
            continue;
        }
        for (const bc::LineEntry& l : f.lines) {
            if (l.line == line) {
                out.emplace_back(fi, l.pc); // 그 줄의 첫 명령 (pc 오름차순)
                break;
            }
        }
    }
    return out;
}

// --- 값 ---

std::string describeValue(const Interpreter& vm, Value v, u32 type, u32 depth) {
    const bc::Module& m = vm.module();
    if (type >= m.types.size()) {
        return "?";
    }
    const bc::TypeDesc& t = m.types[type];
    using K = bc::TypeKind;
    switch (t.kind) {
    case K::Void:
        return "undefined";
    case K::I32:
        return int32ToString(v.asI32());
    case K::F64:
        return numberToString(v.asF64());
    case K::Bool:
        return v.asBool() ? "true" : "false";
    case K::String:
        return quote(vm.stringOf(v));
    case K::Opaque:
        return v.isNull() ? std::string("null") : std::format("{}({})", bc::typeName(m, type), v.bits);
    case K::Box:
        return v.isNull() ? std::string("null") : describeValue(vm, vm.unbox(v), t.elem, depth);
    case K::Array: {
        const usize n = vm.arrayLength(v);
        if (depth == 0) {
            return std::format("[…] ({})", n);
        }
        std::string out = "[";
        for (usize i = 0; i < std::min(n, kMaxShownElements); ++i) {
            out += (i == 0 ? "" : ", ") + describeValue(vm, vm.arrayAt(v, i), t.elem, depth - 1);
        }
        out += n > kMaxShownElements ? std::format(", … ({})]", n) : std::string("]");
        return out;
    }
    case K::Struct: {
        if (depth == 0) {
            return "{…}";
        }
        std::string out = "{ ";
        for (usize i = 0; i < t.fieldNames.size(); ++i) {
            if (i == kMaxShownElements) {
                out += ", …";
                break;
            }
            out += std::format("{}{}: {}", i == 0 ? "" : ", ", t.fieldNames[i],
                               describeValue(vm, vm.fieldOf(v, i), t.fieldTypes[i], depth - 1));
        }
        return out + " }";
    }
    case K::Func:
        return v.bits == 0 || v.bits > m.functions.size()
                   ? std::string("null")
                   : std::format("함수 {}", bc::functionShortName(m.functions[v.bits - 1].name));
    case K::Map:
    case K::Set:
    case K::Coll:
        return std::format("{} ({} 개)", bc::typeName(m, type), vm.containerSize(v));
    }
    return "?";
}

std::vector<DebugVariable> frameLocals(const Interpreter& vm, usize frame) {
    std::vector<DebugVariable> out;
    if (frame >= vm.frameCount()) {
        return out;
    }
    const FrameInfo fi = vm.frameAt(frame);
    const bc::Module& m = vm.module();
    if (fi.function >= m.functions.size()) {
        return out;
    }
    const bc::Function& f = m.functions[fi.function];
    for (const bc::LocalName& l : f.locals) {
        if (l.begin <= fi.pc && fi.pc < l.end && l.reg < f.regs.size()) {
            const u32 type = f.regs[l.reg];
            out.push_back({l.name, debugTypeName(m, type), describeValue(vm, vm.regAt(frame, l.reg), type)});
        }
    }
    return out;
}

WatchResult evaluateWatch(const Interpreter& vm, usize frame, std::string_view expr) {
    WatchResult r;
    r.var.name = std::string(expr);
    const auto fail = [&](std::string why) {
        r.var.value = std::move(why);
        return r;
    };
    if (frame >= vm.frameCount()) {
        return fail("프레임이 없다");
    }
    const bc::Module& m = vm.module();
    const FrameInfo fi = vm.frameAt(frame);
    const bc::Function& f = m.functions[fi.function];
    usize i = 0;
    while (i < expr.size() && expr[i] == ' ') {
        ++i;
    }
    const usize nameBegin = i;
    if (i == expr.size() || !identStart(expr[i])) {
        return fail("지역 이름으로 시작해야 한다");
    }
    while (i < expr.size() && identChar(expr[i])) {
        ++i;
    }
    const std::string_view name = expr.substr(nameBegin, i - nameBegin);
    const bc::LocalName* local = nullptr;
    for (const bc::LocalName& l : f.locals) {
        if (l.name == name && l.begin <= fi.pc && fi.pc < l.end) {
            local = &l; // 같은 이름이 여럿이면 (가린 이름) 안쪽 — 나중에 연 것
        }
    }
    if (local == nullptr) {
        return fail(std::format("'{}' — 이 자리에서 보이는 지역이 아니다", name));
    }
    u32 type = f.regs[local->reg];
    Value v = vm.regAt(frame, local->reg);
    while (i < expr.size()) {
        const auto& t = m.types[type];
        if (t.kind == bc::TypeKind::Box) { // 좁히지 않은 null 가능 값 — null 이면 더 읽을 수 없다
            if (v.isNull()) {
                return fail("null 의 멤버");
            }
            v = vm.unbox(v);
            type = t.elem;
            continue;
        }
        if (expr[i] == '.') {
            const usize b = ++i;
            while (i < expr.size() && identChar(expr[i])) {
                ++i;
            }
            const std::string_view field = expr.substr(b, i - b);
            if (field == "length" && (t.kind == bc::TypeKind::Array || t.kind == bc::TypeKind::String)) {
                const auto n =
                    t.kind == bc::TypeKind::Array ? vm.arrayLength(v) : static_cast<usize>(utf16Length(vm.stringOf(v)));
                v = Value::i32v(static_cast<i32>(n));
                type = 0;
                r.var = {std::string(expr), "number", int32ToString(static_cast<i32>(n))};
                if (i != expr.size()) {
                    return fail(".length 뒤에는 읽을 수 없다");
                }
                r.ok = true;
                return r;
            }
            if (t.kind != bc::TypeKind::Struct) {
                return fail(std::format("'{}' — {} 에는 필드가 없다", field, debugTypeName(m, type)));
            }
            const auto at = std::find(t.fieldNames.begin(), t.fieldNames.end(), field);
            if (at == t.fieldNames.end()) {
                return fail(std::format("'{}' — {} 의 필드가 아니다", field, debugTypeName(m, type)));
            }
            const auto slot = static_cast<usize>(at - t.fieldNames.begin());
            v = vm.fieldOf(v, slot);
            type = t.fieldTypes[slot];
        } else if (expr[i] == '[') {
            const usize b = ++i;
            while (i < expr.size() && expr[i] >= '0' && expr[i] <= '9') {
                ++i;
            }
            if (i == b || i >= expr.size() || expr[i] != ']' || i - b > 9) {
                return fail("첨자는 [정수]");
            }
            usize index = 0;
            for (usize k = b; k < i; ++k) {
                index = index * 10 + static_cast<usize>(expr[k] - '0');
            }
            ++i;
            if (t.kind != bc::TypeKind::Array) {
                return fail(std::format("{} 은 배열이 아니다", debugTypeName(m, type)));
            }
            if (index >= vm.arrayLength(v)) {
                return fail(std::format("첨자 {} — 길이 {}", index, vm.arrayLength(v)));
            }
            v = vm.arrayAt(v, index);
            type = t.elem;
        } else if (expr[i] == ' ') {
            ++i;
        } else {
            return fail(std::format("'{}' 를 읽을 수 없다 (.필드 · [정수] · .length 만)", expr.substr(i, 1)));
        }
    }
    r.ok = true;
    r.var.type = debugTypeName(m, type);
    r.var.value = describeValue(vm, v, type);
    return r;
}

// --- Debugger ---

usize Debugger::setBreakpoint(std::string_view file, u32 line, std::string log) {
    (void)clearBreakpoint(file, line);
    const auto sites = resolveLine(*m_module, file, line);
    if (sites.empty()) {
        return 0;
    }
    const usize index = m_breakpoints.size();
    m_breakpoints.push_back({std::string(file), line, std::move(log), sites.size(), 0});
    for (const auto& [fn, pc] : sites) {
        m_patches.set(fn, pc, CodePatches::User);
        m_siteBreakpoint[{fn, pc}] = index;
    }
    return sites.size();
}

bool Debugger::clearBreakpoint(std::string_view file, u32 line) {
    const auto it = std::find_if(m_breakpoints.begin(), m_breakpoints.end(),
                                 [&](const Breakpoint& b) { return b.file == file && b.line == line; });
    if (it == m_breakpoints.end()) {
        return false;
    }
    for (const auto& [fn, pc] : resolveLine(*m_module, file, line)) {
        m_patches.clear(fn, pc, CodePatches::User);
    }
    m_breakpoints.erase(it);
    // 자리 → 중단점 색인을 다시 (지운 뒤의 자리가 당겨졌다)
    m_siteBreakpoint.clear();
    for (usize i = 0; i < m_breakpoints.size(); ++i) {
        for (const auto& site : resolveLine(*m_module, m_breakpoints[i].file, m_breakpoints[i].line)) {
            m_siteBreakpoint[site] = i;
        }
    }
    return true;
}

void Debugger::clearBreakpoints() {
    m_patches.clearOwner(CodePatches::User);
    m_breakpoints.clear();
    m_siteBreakpoint.clear();
}

const std::vector<u32>& Debugger::starts(u32 function) {
    auto it = m_starts.find(function);
    if (it == m_starts.end()) {
        it = m_starts.emplace(function, lineStarts(m_module->functions[function])).first;
    }
    return it->second;
}

void Debugger::armStep(StepAction a) {
    endStep();
    if (a != StepAction::Continue) {
        beginStep(StepAction::Into, nullptr, 0, 0); // 깊이 0 — 처음 만나는 줄에서 멈춘다
    }
}

void Debugger::beginStep(StepAction a, const Interpreter* vm, u32 function, u32 line) {
    m_step = {a, vm != nullptr ? vm->frameCount() : 0, function, line};
    const auto patchFunction = [&](u32 fn) {
        for (const u32 pc : starts(fn)) {
            m_patches.set(fn, pc, CodePatches::Step);
        }
    };
    if (a == StepAction::Into || vm == nullptr) {
        for (u32 fn = 0; fn < m_module->functions.size(); ++fn) {
            patchFunction(fn);
        }
        return;
    }
    // Over = 스택의 함수들 (더 깊은 호출은 멈추지 않고 지나간다 — 멈춤 조건이 깊이를 본다), Out = 바깥 프레임들
    const usize top = vm->frameCount();
    for (usize i = 0; i < top; ++i) {
        if (a == StepAction::Out && i + 1 == top) {
            continue;
        }
        patchFunction(vm->frameAt(i).function);
    }
}

void Debugger::endStep() {
    m_patches.clearOwner(CodePatches::Step);
    m_step = {};
}

std::string Debugger::formatLog(const Interpreter& vm, std::string_view text) const {
    std::string out;
    const usize top = vm.frameCount() - 1;
    for (usize i = 0; i < text.size();) {
        if (text[i] == '{') {
            const usize close = text.find('}', i);
            if (close != std::string_view::npos) {
                const WatchResult w = evaluateWatch(vm, top, text.substr(i + 1, close - i - 1));
                out += w.ok ? w.var.value : "<" + w.var.value + ">";
                i = close + 1;
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

void Debugger::onBreak(const Interpreter& vm, u32 function, u32 pc) {
    ++m_brkHits;
    const bc::Function& fn = m_module->functions[function];
    const bc::LineEntry* where = bc::findLine(fn, pc);
    const u32 line = where != nullptr ? where->line : 0;
    const usize depth = vm.frameCount();

    bool stop = false;
    StopReason reason = StopReason::Step;
    if (m_step.action != StepAction::Continue && (m_patches.owners(function, pc) & CodePatches::Step) != 0) {
        switch (m_step.action) {
        case StepAction::Into:
            stop = depth != m_step.depth || function != m_step.function || line != m_step.line;
            break;
        case StepAction::Over:
            stop =
                depth < m_step.depth || (depth == m_step.depth && (function != m_step.function || line != m_step.line));
            break;
        case StepAction::Out:
            stop = depth < m_step.depth;
            break;
        case StepAction::Continue:
            break;
        }
    }
    if ((m_patches.owners(function, pc) & CodePatches::User) != 0) {
        if (const auto it = m_siteBreakpoint.find({function, pc}); it != m_siteBreakpoint.end()) {
            Breakpoint& bp = m_breakpoints[it->second];
            ++bp.hits;
            if (!bp.log.empty()) {
                m_logs.push_back(std::format("{}:{}: {}", bp.file, bp.line, formatLog(vm, bp.log)));
            } else {
                stop = true;
                reason = StopReason::Breakpoint;
            }
        }
    }
    if (!stop || !m_handler) {
        return;
    }
    ++m_stops;
    endStep();
    DebugStop s;
    s.reason = reason;
    s.function = function;
    s.pc = pc;
    s.file = std::string(bc::functionFile(fn.name));
    s.line = line;
    s.column = where != nullptr ? where->column : 0;
    s.depth = depth;
    s.vm = &vm;
    const StepAction next = m_handler(s);
    if (next != StepAction::Continue) {
        beginStep(next, &vm, function, line);
    }
}

} // namespace sbx::script::vm
