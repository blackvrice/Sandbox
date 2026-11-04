#include "ui/binding/UiBinding.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace sbx::ui {

std::string UiScalar::display() const {
    switch (kind) {
    case Kind::None:
        return "-";
    case Kind::Text:
        return text;
    case Kind::Bool:
        return flag ? "true" : "false";
    case Kind::Number:
        break;
    }
    if (!std::isfinite(number)) {
        return "-";
    }
    // 정수 범위의 정수는 소수점 없이 (점수 · 개수), 그 밖은 최단 표현
    if (number == std::floor(number) && std::abs(number) < 1e15) {
        return std::format("{}", static_cast<i64>(number));
    }
    return std::format("{}", number);
}

std::optional<f64> UiScalar::asNumber() const {
    if (kind == Kind::Number && std::isfinite(number)) {
        return number;
    }
    if (kind == Kind::Bool) {
        return flag ? 1.0 : 0.0;
    }
    return std::nullopt;
}

UiScalar evaluate(std::string_view bind, const IUiDataSource& source) {
    if (bind.starts_with("var:")) {
        return source.var(bind.substr(4));
    }
    if (bind.starts_with("session:")) {
        return source.session(bind.substr(8));
    }
    if (bind.starts_with("component:")) {
        const std::string_view rest = bind.substr(10);
        const usize at = rest.find('@');
        if (at == std::string_view::npos) {
            return {};
        }
        const std::string_view target = rest.substr(at + 1);
        if (target == "owned") {
            return source.component(rest.substr(0, at), UiTarget::Owned);
        }
        if (target == "selected") {
            return source.component(rest.substr(0, at), UiTarget::Selected);
        }
    }
    return {};
}

UiScalar evaluate(const UiValue& value, const IUiDataSource* source) {
    if (value.bind.empty()) {
        return UiScalar::of(static_cast<f64>(value.number));
    }
    return source != nullptr ? evaluate(value.bind, *source) : UiScalar{};
}

std::string formatText(const UiNode& node, const IUiDataSource* source) {
    const std::string& t = node.text;
    std::string out;
    out.reserve(t.size());
    for (usize i = 0; i < t.size(); ++i) {
        if (t[i] == '{' && i + 2 < t.size() && t[i + 1] >= '0' && t[i + 1] <= '9' && t[i + 2] == '}') {
            const usize index = static_cast<usize>(t[i + 1] - '0');
            if (index < node.args.size()) {
                out += evaluate(node.args[index], source).display();
            }
            i += 2;
            continue;
        }
        out += t[i];
    }
    return out;
}

f32 progressOf(const UiNode& node, const IUiDataSource* source) {
    const std::optional<f64> v = evaluate(node.value, source).asNumber();
    const std::optional<f64> m = evaluate(node.max, source).asNumber();
    if (!v || !m || !(*m > 0)) {
        return 0;
    }
    return static_cast<f32>(std::clamp(*v / *m, 0.0, 1.0));
}

usize listCountOf(const UiNode& node, const IUiDataSource* source) {
    if (node.items.empty() || source == nullptr) {
        return 0;
    }
    const std::optional<f64> n = evaluate(node.items, *source).asNumber();
    if (!n || !(*n > 0)) {
        return 0;
    }
    return static_cast<usize>(std::min(*n, static_cast<f64>(kMaxListItems)));
}

} // namespace sbx::ui
