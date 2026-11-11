#include "editor/ui/ScriptEditor.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>

#include "editor/ui/StudioTheme.hpp"
#include "foundation/text/Utf8.hpp"

namespace sbx::editor {

namespace {

constexpr f64 kHoverDelay = 0.35; // 초 — 마우스를 멈춘 뒤 hover
constexpr usize kPopupRows = 10;

[[nodiscard]] f32 dpi() {
    return ImGui::GetStyle().FontScaleDpi;
}

// ImGui 입력 칸과 같은 측정 (ImFontCalcTextSizeEx — 반올림 없음)
[[nodiscard]] f32 widthOf(std::string_view s) {
    if (s.empty()) {
        return 0.f;
    }
    return ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.f, s.data(), s.data() + s.size()).x;
}

[[nodiscard]] std::vector<usize> lineStartsOf(std::string_view text) {
    std::vector<usize> out{0};
    for (usize i = text.find('\n'); i != std::string_view::npos; i = text.find('\n', i + 1)) {
        out.push_back(i + 1);
    }
    return out;
}

// 글 원점 기준 자리 — starts 는 그 글의 줄 시작 (lineStartsOf)
[[nodiscard]] ImVec2 posIn(std::string_view text, const std::vector<usize>& starts, usize offset) {
    offset = std::min(offset, text.size());
    const auto it = std::upper_bound(starts.begin(), starts.end(), offset);
    const auto line = static_cast<usize>(it - starts.begin()) - 1;
    return {widthOf(text.substr(starts[line], offset - starts[line])), static_cast<f32>(line) * ImGui::GetFontSize()};
}

[[nodiscard]] std::optional<usize> offsetIn(std::string_view text, const std::vector<usize>& starts, ImVec2 pos) {
    if (pos.x < 0.f || pos.y < 0.f) {
        return std::nullopt;
    }
    const auto line = static_cast<usize>(pos.y / ImGui::GetFontSize());
    if (line >= starts.size()) {
        return std::nullopt;
    }
    f32 x = 0.f;
    for (usize i = starts[line]; i < text.size() && text[i] != '\n';) {
        usize next = i;
        (void)utf8::decodeNext(text, next);
        x += widthOf(text.substr(i, next - i));
        if (pos.x < x) {
            return i;
        }
        i = next;
    }
    return std::nullopt;
}

// 줄 시작 · 가장 긴 줄의 폭 (입력 칸 크기)
void relayout(ScriptEditorState& st, std::string_view text) {
    st.layoutDirty = false;
    st.lineStarts = lineStartsOf(text);
    st.contentWidth = 0.f;
    for (usize l = 0; l < st.lineStarts.size(); ++l) {
        const usize end = l + 1 < st.lineStarts.size() ? st.lineStarts[l + 1] - 1 : text.size();
        st.contentWidth = std::max(st.contentWidth, widthOf(text.substr(st.lineStarts[l], end - st.lineStarts[l])));
    }
}

[[nodiscard]] usize snapToChar(std::string_view text, usize offset) {
    offset = std::min(offset, text.size());
    while (offset > 0 && offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0xC0) == 0x80) {
        --offset; // UTF-8 글자 가운데가 아니게
    }
    return offset;
}

[[nodiscard]] const char* kindLabel(script::CompletionKind k) {
    switch (k) {
    case script::CompletionKind::Property:
        return "속성";
    case script::CompletionKind::Method:
        return "메서드";
    case script::CompletionKind::Field:
        return "필드";
    case script::CompletionKind::Function:
        return "함수";
    case script::CompletionKind::Variable:
        return "변수";
    case script::CompletionKind::Constant:
        return "상수";
    case script::CompletionKind::Type:
        return "타입";
    case script::CompletionKind::Enum:
        return "enum";
    case script::CompletionKind::EnumMember:
        return "enum 값";
    case script::CompletionKind::Component:
        return "컴포넌트";
    case script::CompletionKind::Namespace:
        return "이름공간";
    }
    return "";
}

// 콜백과 위젯 사이 (이번 프레임)
struct Frame {
    std::string* text = nullptr;
    ScriptEditorState* st = nullptr;
    bool acceptKey = false; // 팝업이 열린 채 Enter · Tab
    bool dot = false;       // '.' 를 쳤다
};

int editorCallback(ImGuiInputTextCallbackData* data) {
    auto* f = static_cast<Frame*>(data->UserData);
    ScriptEditorState& st = *f->st;
    switch (data->EventFlag) {
    case ImGuiInputTextFlags_CallbackResize:
        f->text->resize(static_cast<usize>(data->BufTextLen));
        data->Buf = f->text->data();
        return 0;
    case ImGuiInputTextFlags_CallbackCharFilter:
        if (st.assist.completionOpen() && (data->EventChar == '\n' || data->EventChar == '\t')) {
            f->acceptKey = true;
            return 1; // 줄 · 탭을 넣지 않고 후보를 넣는다 (아래 Always)
        }
        if (data->EventChar == '.') {
            f->dot = true;
        }
        return 0;
    case ImGuiInputTextFlags_CallbackAlways: {
        st.active = true;
        const std::string_view buf(data->Buf, static_cast<usize>(data->BufTextLen));
        const auto place = [&](usize at) {
            data->CursorPos = data->SelectionStart = data->SelectionEnd = static_cast<int>(at);
        };
        if (st.gotoOffset) {
            place(snapToChar(buf, *st.gotoOffset));
            st.gotoOffset.reset();
        } else if (f->acceptKey) {
            if (const auto r = st.assist.acceptCompletion(static_cast<usize>(data->CursorPos))) {
                data->DeleteChars(static_cast<int>(r->begin), static_cast<int>(r->end - r->begin));
                data->InsertChars(static_cast<int>(r->begin), r->text.data(), r->text.data() + r->text.size());
                place(r->begin + r->text.size());
            }
        } else if (st.nav != 0 && st.assist.completionOpen()) {
            // 위아래는 팝업의 것 — 입력 칸이 옮긴 커서를 되돌린다
            data->CursorPos = static_cast<int>(st.cursor);
            data->SelectionStart = static_cast<int>(st.selStart);
            data->SelectionEnd = static_cast<int>(st.selEnd);
            st.assist.moveSelection(st.nav);
        }
        st.cursor = static_cast<usize>(data->CursorPos);
        st.selStart = static_cast<usize>(data->SelectionStart);
        st.selEnd = static_cast<usize>(data->SelectionEnd);
        return 0;
    }
    default:
        return 0;
    }
}

// 물결 밑줄
void squiggle(ImDrawList* dl, ImVec2 a, f32 x1, ImU32 col) {
    const f32 step = 2.f * dpi();
    f32 x = a.x;
    bool up = true;
    while (x < x1) {
        const f32 nx = std::min(x + step, x1);
        dl->AddLine({x, a.y + (up ? 0.f : step)}, {nx, a.y + (up ? step : 0.f)}, col, std::max(1.f, dpi()));
        x = nx;
        up = !up;
    }
}

void drawCompletion(const ScriptEditorState& st, ImVec2 at) {
    const auto items = st.assist.completionItems();
    if (items.empty()) {
        return;
    }
    ImGui::SetNextWindowPos(at);
    if (!ImGui::BeginTooltip()) {
        return;
    }
    const usize sel = st.assist.selected();
    const usize first = sel < kPopupRows ? 0 : sel + 1 - kPopupRows;
    const usize last = std::min(items.size(), first + kPopupRows);
    const ImVec4 muted = theme::tokens().muted;
    for (usize i = first; i < last; ++i) {
        const script::CompletionItem& c = items[i];
        ImGui::PushID(static_cast<int>(i));
        (void)ImGui::Selectable(c.label.c_str(), i == sel, ImGuiSelectableFlags_None,
                                {std::max(160.f * dpi(), widthOf(c.label) + 8.f * dpi()), 0.f});
        ImGui::SameLine();
        ImGui::TextColored(muted, "%s  %s", kindLabel(c.kind), c.detail.c_str());
        ImGui::PopID();
    }
    if (items.size() > kPopupRows) {
        ImGui::TextDisabled("%zu / %zu", sel + 1, items.size());
    }
    if (sel < items.size() && !items[sel].doc.empty()) {
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 420.f * dpi());
        ImGui::TextUnformatted(items[sel].doc.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndTooltip();
}

} // namespace

ImVec2 scriptTextPos(std::string_view text, usize offset) {
    return posIn(text, lineStartsOf(text), offset);
}

std::optional<usize> scriptTextOffsetAt(std::string_view text, ImVec2 pos) {
    return offsetIn(text, lineStartsOf(text), pos);
}

bool drawScriptEditor(const char* id, std::string& text, ImVec2 size, bool readOnly, ScriptEditorState& st,
                      const ScriptWorkspaceFn& workspace) {
    const f64 now = ImGui::GetTime();
    // 바깥에서 글을 바꿨다 (다른 파일을 골랐다 · 버렸다 · 서버 값으로 다시 불렀다)
    if (!st.seen || text != st.shadow) {
        if (st.seen) {
            // 활성 입력 칸은 제 버퍼를 다시 써 바깥 변경을 덮는다 — 다른 id 의 새 입력 칸으로 (커서 자리는 이어서)
            ++st.generation;
            if (st.active) {
                st.gotoOffset = std::min(st.cursor, text.size());
            }
        }
        st.shadow = text;
        st.seen = true;
        st.assist.reset();
        st.layoutDirty = true;
        st.followed = static_cast<usize>(-1);
    }
    // 진단 — 입력이 300 ms 멈춘 뒤 (UI 스레드에서 검사기 한 번)
    if (st.assist.analysisDue(now)) {
        const u64 version = st.assist.version();
        const std::optional<ScriptWorkspace> ws = workspace ? workspace(text) : std::nullopt;
        (void)st.assist.analyzed(version, ws ? analyzeScript(*ws) : std::vector<ScriptMark>{});
    }
    if (st.layoutDirty) {
        relayout(st, text);
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 lineH = ImGui::GetFontSize();
    const theme::Tokens& tk = theme::tokens();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, style.Colors[ImGuiCol_FrameBg]);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    const bool visible = ImGui::BeginChild(id, size, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (!visible) {
        ImGui::EndChild();
        return false;
    }
    const ImVec2 view = ImGui::GetContentRegionAvail();
    const f32 gutter = widthOf(std::to_string(std::max<usize>(st.lineStarts.size(), 999))) + 14.f * dpi();
    const ImVec2 base = ImGui::GetCursorScreenPos(); // 내용 원점 (스크롤 반영)
    const ImVec2 origin{base.x + gutter + style.FramePadding.x, base.y + style.FramePadding.y}; // 글 원점
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // 줄 번호 · 진단 점 (보이는 줄만)
    const f32 scrollY = ImGui::GetScrollY();
    const usize firstLine = static_cast<usize>(std::max(0.f, (scrollY - style.FramePadding.y) / lineH));
    const usize lastLine = std::min(st.lineStarts.size(), firstLine + static_cast<usize>(view.y / lineH) + 2);
    std::vector<i8> lineMark(lastLine > firstLine ? lastLine - firstLine : 0, 0); // 1 경고 · 2 오류
    const auto& marks = st.assist.marks();
    for (const ScriptMark& m : marks) {
        if (m.begin > text.size()) {
            continue; // 옛 결과 (그사이 줄었다) — 다음 분석이 고친다
        }
        const auto line = static_cast<usize>(posIn(text, st.lineStarts, m.begin).y / lineH + 0.5f);
        if (line >= firstLine && line < lastLine) {
            i8& v = lineMark[line - firstLine];
            v = std::max<i8>(v, m.error ? 2 : 1);
        }
    }
    for (usize l = firstLine; l < lastLine; ++l) {
        const std::string n = std::to_string(l + 1);
        const f32 y = origin.y + static_cast<f32>(l) * lineH;
        dl->AddText({base.x + gutter - 8.f * dpi() - widthOf(n), y}, theme::toU32(tk.muted), n.c_str());
        if (const i8 v = lineMark[l - firstLine]; v != 0) {
            dl->AddCircleFilled({base.x + 4.f * dpi(), y + lineH * 0.5f}, 2.5f * dpi(),
                                theme::toU32(v == 2 ? tk.error : tk.warning));
        }
    }
    // 밑줄 — 입력 칸 아래에 먼저 (입력 칸 바탕은 투명). 보이는 줄만 (진단이 많은 큰 파일 — 정점 수)
    for (const ScriptMark& m : marks) {
        if (m.end > text.size()) {
            continue;
        }
        const ImVec2 a = posIn(text, st.lineStarts, m.begin);
        const auto line = static_cast<usize>(a.y / lineH + 0.5f);
        if (line < firstLine || line >= lastLine) {
            continue;
        }
        const ImVec2 b = posIn(text, st.lineStarts, m.end);
        const f32 x1 = b.y == a.y && m.end > m.begin ? b.x : a.x + std::max(widthOf("m"), 4.f * dpi());
        squiggle(dl, {origin.x + a.x, origin.y + a.y + lineH - 2.f * dpi()}, origin.x + x1,
                 theme::toU32(m.error ? tk.error : tk.warning));
    }

    // 키 — 팝업이 열린 채 위아래 (입력 칸이 옮긴 커서는 콜백이 되돌린다), 붙여넣기는 팝업을 먼저 닫는다 (붙인 줄바꿈을
    // 넣기로 먹지 않게)
    st.nav = 0;
    if (st.assist.completionOpen() && st.active) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            st.nav = 1;
        } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            st.nav = -1;
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V) ||
            ImGui::IsKeyChordPressed(ImGuiMod_Shift | ImGuiKey_Insert)) {
            st.assist.closeCompletion();
        }
    }
    // Esc 를 입력 칸보다 먼저 받는다 (ImGui 는 Esc 에 고친 것을 모두 되돌린다) — 팝업만 닫는다
    constexpr ImGuiInputFlags kOver = ImGuiInputFlags_RouteFocused | ImGuiInputFlags_RouteOverActive;
    if (ImGui::Shortcut(ImGuiKey_Escape, kOver)) {
        st.assist.closeCompletion();
    }
    const bool ctrlSpace = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Space, kOver);

    ImGui::SetCursorScreenPos({base.x + gutter, base.y});
    if (st.focusRequest || (st.gotoOffset && !st.active)) {
        ImGui::SetKeyboardFocusHere();
        st.focusRequest = false;
    }
    const ImVec2 inputSize{
        std::max(view.x - gutter, st.contentWidth + style.FramePadding.x * 2.f + widthOf("mmmm")),
        std::max(view.y, static_cast<f32>(st.lineStarts.size() + 1) * lineH + style.FramePadding.y * 2.f)};
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CallbackAlways |
                                ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_AllowTabInput;
    if (readOnly) {
        flags |= ImGuiInputTextFlags_ReadOnly;
    }
    Frame frame{&text, &st};
    st.active = false;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0.f, 0.f, 0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    ImGui::PushID(static_cast<int>(st.generation));
    const bool edited =
        ImGui::InputTextMultiline("##code", text.data(), text.capacity() + 1, inputSize, flags, editorCallback, &frame);
    ImGui::PopID();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (edited) {
        st.shadow = text;
        st.assist.edited(now);
        relayout(st, text); // 아래 자리 계산이 새 글로
    }

    // 완성 — '.' · Ctrl+Space 에 묻고, 그 뒤 글 · 커서가 바뀌면 다시 거른다
    if (!st.active || readOnly) {
        st.assist.closeCompletion();
    } else if (frame.dot || ctrlSpace) {
        if (const auto ws = workspace ? workspace(text) : std::nullopt) {
            st.assist.openCompletion(*ws, st.cursor);
        }
    } else if (st.assist.completionOpen() && st.assist.refreshCompletion(text, st.cursor)) {
        if (const auto ws = workspace ? workspace(text) : std::nullopt) {
            st.assist.openCompletion(*ws, st.cursor); // 접두가 처음보다 짧다 — 다시 묻는다
        }
    }

    // 커서가 움직였으면 보이게 (바깥 자식 창이 스크롤한다)
    if (st.active && st.cursor != st.followed) {
        st.followed = st.cursor;
        const ImVec2 p = posIn(text, st.lineStarts, st.cursor);
        const f32 y = style.FramePadding.y + p.y;
        const f32 x = gutter + style.FramePadding.x + p.x;
        if (y < ImGui::GetScrollY()) {
            ImGui::SetScrollY(std::max(0.f, y - style.FramePadding.y));
        } else if (y + lineH > ImGui::GetScrollY() + view.y) {
            ImGui::SetScrollY(y + lineH + style.FramePadding.y - view.y);
        }
        if (x < ImGui::GetScrollX() + gutter) {
            ImGui::SetScrollX(std::max(0.f, x - gutter - widthOf("mmmm")));
        } else if (x > ImGui::GetScrollX() + view.x - widthOf("mm")) {
            ImGui::SetScrollX(x - view.x + widthOf("mmmm"));
        }
    }

    if (st.assist.completionOpen()) {
        const ImVec2 a = posIn(text, st.lineStarts, st.assist.completionAnchor());
        drawCompletion(st, {origin.x + a.x, origin.y + a.y + lineH});
    } else if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::IsAnyMouseDown()) {
        // hover — 마우스를 멈추면 그 자리의 진단 · 서명
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (mouse.x != st.hoverMouse.x || mouse.y != st.hoverMouse.y) {
            st.hoverMouse = mouse;
            st.hoverSince = now;
        } else if (now - st.hoverSince >= kHoverDelay) {
            if (const auto at = offsetIn(text, st.lineStarts, {mouse.x - origin.x, mouse.y - origin.y})) {
                const ScriptMark* mark = st.assist.markAt(*at);
                const std::optional<script::HoverInfo>* info = st.assist.cachedHover(*at);
                if (info == nullptr) {
                    const auto ws = workspace ? workspace(text) : std::nullopt;
                    st.assist.storeHover(*at, ws ? script::hover(ws->query(*at)) : std::nullopt);
                    info = st.assist.cachedHover(*at);
                }
                if (mark != nullptr || (info != nullptr && info->has_value())) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 480.f * dpi());
                    if (mark != nullptr) {
                        ImGui::TextColored(mark->error ? tk.error : tk.warning, "%s", mark->text.c_str());
                    }
                    if (info != nullptr && info->has_value()) {
                        if (mark != nullptr) {
                            ImGui::Separator();
                        }
                        ImGui::TextUnformatted((*info)->title.c_str());
                        if (!(*info)->doc.empty()) {
                            ImGui::TextColored(tk.muted, "%s", (*info)->doc.c_str());
                        }
                    }
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            }
        }
    } else {
        st.hoverMouse = {-1.f, -1.f};
    }
    ImGui::EndChild();
    return edited;
}

} // namespace sbx::editor
