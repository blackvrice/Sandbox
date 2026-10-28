#include "core/package/PackTemplate.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <utility>

#include "core/content/ContentLoader.hpp"
#include "core/content/PackManifest.hpp"
#include "core/package/PackSource.hpp"
#include "foundation/io/FileIo.hpp"

namespace sbx::package {
namespace {

namespace fs = std::filesystem;

bool isPackId(std::string_view s) noexcept {
    return !s.empty() && s.size() <= 64 && std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

// "{id}" 를 팩 id 로
std::string fill(std::string_view text, std::string_view id) {
    std::string out;
    for (usize i = 0; i < text.size();) {
        if (text.substr(i, 4) == "{id}") {
            out += id;
            i += 4;
        } else {
            out += text[i++];
        }
    }
    return out;
}

constexpr std::string_view kMain =
    R"(// {id} — 다른 팩은 pack.json dependencies 에 "{id}" 를 적고 import { Counter, bump } from "@{id}/main" 으로 쓴다
import { defineComponent, field } from "sandbox"

export const Counter = defineComponent({
  id: "{id}.counter",
  fields: {
    value: field.f32(0, { min: 0, max: 1000000 }),
  },
})

export function bump(value: number, by: number): number {
  return Math.min(value + by, 1000000)
}
)";

constexpr std::string_view kTick = R"(// server 시스템: 30 틱 (1 초) 마다 counter 를 1 올린다
import { defineSystem } from "sandbox"
import { Counter, bump } from "./main"

export default defineSystem({
  id: "{id}.tick",
  intervalTicks: 30,
  onTick(ctx) {
    for (const e of ctx.query(Counter)) {
      const c = ctx.read(e, Counter)
      if (c !== null) {
        c.value = bump(c.value, 1)
        ctx.write(e, Counter, c)
      }
    }
  },
})
)";

constexpr std::string_view kThing = R"({"id": "{id}.thing", "components": {"core.transform": {}, "{id}.counter": {}}}
)";

} // namespace

Expected<NewPackResult> createPack(const fs::path& dir, std::string_view id, std::string_view license,
                                   const ecs::ComponentCatalog& catalog) {
    if (!isPackId(id) || id == "core") {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("팩 id '{}' — 소문자 · 숫자 · '_' (64 바이트까지), core 는 엔진의 것", id));
    }
    const std::string lic(license.empty() ? kDefaultPackLicense : license);
    if (auto bad = content::checkSpdxExpression(lic)) {
        return makeError(ErrorCode::InvalidArgument, std::format("license '{}' — {}", lic, *bad));
    }
    const fs::path target = dir.filename().empty() ? dir.parent_path() : dir;
    std::error_code ec;
    if (fs::exists(target, ec) && !fs::is_empty(target, ec)) {
        return makeError(ErrorCode::AlreadyExists, "폴더가 비어 있지 않다 (덮어쓰지 않는다)", io::displayPath(target));
    }
    // 옆의 임시 폴더에서 만들고 검사한다 — 통과해야 제자리로
    const fs::path staging = target.parent_path() / (target.filename().string() + ".sbx-new");
    fs::remove_all(staging, ec);
    const fs::path pack = staging / target.filename();
    const std::string packJson = std::format(
        "{{\n  \"id\": \"{}\",\n  \"version\": \"0.1.0\",\n  \"license\": \"{}\",\n  \"description\": \"{} — "
        "sbx new --pack 의 틀 (컴포넌트 · 함수 · 시스템 · Prefab)\"\n}}\n",
        id, lic, id);
    const std::array<std::pair<const char*, std::string>, 4> files{{
        {"pack.json", packJson},
        {"scripts/main.ts", fill(kMain, id)},
        {"scripts/tick.ts", fill(kTick, id)},
        {"prefabs/thing.json", fill(kThing, id)},
    }};
    for (const auto& [rel, text] : files) {
        const fs::path p = pack / rel;
        if (auto made = io::createDirectories(p.parent_path()); !made) {
            fs::remove_all(staging, ec);
            return std::unexpected(made.error());
        }
        if (auto w = io::writeFileAtomic(p, text); !w) {
            fs::remove_all(staging, ec);
            return std::unexpected(w.error());
        }
    }
    const std::string ids[] = {std::string(id)};
    const content::ContentLoadResult loaded = content::ContentLoader::load(staging, ids, catalog);
    if (!loaded.issues.empty()) {
        std::string msg = std::format("틀이 콘텐츠 검사를 통과하지 못했다 ({}개)", loaded.issues.size());
        for (usize i = 0; i < loaded.issues.size() && i < 5; ++i) {
            msg += "\n  " + loaded.issues[i].describe();
        }
        fs::remove_all(staging, ec);
        return makeError(ErrorCode::ValidationFailed, msg);
    }
    auto snap = snapshotPack(pack);
    if (!snap) {
        fs::remove_all(staging, ec);
        return std::unexpected(snap.error());
    }
    fs::remove(target, ec); // 빈 폴더였으면 (위에서 비었음을 보았다)
    fs::rename(pack, target, ec);
    const std::string why = ec ? ec.message() : std::string();
    std::error_code ignored;
    fs::remove_all(staging, ignored);
    if (!why.empty()) {
        return makeError(ErrorCode::IoError, std::format("제자리로 옮길 수 없다 — {}", why), io::displayPath(target));
    }
    return NewPackResult{std::string(id), lic, snap->digest, snap->files.size()};
}

} // namespace sbx::package
