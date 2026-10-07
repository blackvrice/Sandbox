#pragma once
// 에셋 경로 → id. docs/06-RENDERING.md 7.2.
//   정규화: '\' → '/', 소문자(ASCII), 앞의 "./" · "/" 와 겹친 '/' 제거, 에셋 루트 상대
//   id = FNV-1a64(정규화한 경로)  — 같은 파일을 다른 표기로 불러도 한 번만 읽는다. [계획] .meta GUID

#include <string>
#include <string_view>

#include "foundation/hash/Fnv1a.hpp"
#include "foundation/types/Types.hpp"

namespace sbx::render {

using AssetId = u64;

[[nodiscard]] inline std::string normalizeAssetPath(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    for (char c : path) {
        if (c == '\\') {
            c = '/';
        }
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
        if (c == '/' && (out.empty() || out.back() == '/')) {
            continue; // 앞의 '/' · 겹친 '/'
        }
        out.push_back(c);
    }
    while (out.starts_with("./")) {
        out.erase(0, 2);
    }
    return out;
}

[[nodiscard]] inline AssetId assetIdOf(std::string_view path) {
    return Fnv1a64().string(normalizeAssetPath(path)).value();
}

} // namespace sbx::render
