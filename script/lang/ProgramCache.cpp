#include "script/lang/ProgramCache.hpp"

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include "foundation/hash/Fnv1a.hpp"

namespace sbx::script {

namespace {

constexpr usize kEntries = 4; // 편집 중인 글 · 완성의 자리표 글 · 이름 바꾸기의 시험 글 …

struct Entry {
    u64 key = 0;
    std::vector<SourceFile> files;
    std::vector<PackImports> imports;
    std::shared_ptr<const AnalyzedProgram> result;
};

struct Cache {
    std::mutex mutex;
    std::vector<Entry> entries; // 앞이 최근
    ProgramCacheStats stats;
};

Cache& cache() {
    static Cache c;
    return c;
}

[[nodiscard]] u64 keyOf(std::span<const SourceFile> files, std::span<const PackImports> imports) {
    Fnv1a64 h;
    // 길이를 먼저 — 파일 경계가 어긋난 같은 바이트열이 같은 열쇠가 되지 않게
    const auto str = [&](std::string_view s) { h.u64le(s.size()).string(s); };
    h.u64le(files.size());
    for (const SourceFile& f : files) {
        str(f.path);
        str(f.text);
    }
    h.u64le(imports.size());
    for (const PackImports& p : imports) {
        str(p.pack);
        h.u64le(p.allowed.size());
        for (const std::string& a : p.allowed) {
            str(a);
        }
    }
    return h.value();
}

[[nodiscard]] bool same(const Entry& e, std::span<const SourceFile> files, std::span<const PackImports> imports) {
    if (e.files.size() != files.size() || e.imports.size() != imports.size()) {
        return false;
    }
    for (usize i = 0; i < files.size(); ++i) {
        if (e.files[i].path != files[i].path || e.files[i].text != files[i].text) {
            return false;
        }
    }
    for (usize i = 0; i < imports.size(); ++i) {
        if (e.imports[i].pack != imports[i].pack || e.imports[i].allowed != imports[i].allowed) {
            return false;
        }
    }
    return true;
}

} // namespace

std::shared_ptr<const AnalyzedProgram> analyzeCached(std::span<const SourceFile> files,
                                                     std::span<const PackImports> imports) {
    const u64 key = keyOf(files, imports);
    Cache& c = cache();
    {
        const std::lock_guard lock(c.mutex);
        for (usize i = 0; i < c.entries.size(); ++i) {
            if (c.entries[i].key == key && same(c.entries[i], files, imports)) {
                ++c.stats.hits;
                std::rotate(c.entries.begin(), c.entries.begin() + static_cast<std::ptrdiff_t>(i),
                            c.entries.begin() + static_cast<std::ptrdiff_t>(i) + 1); // 앞으로
                return c.entries.front().result;
            }
        }
        ++c.stats.misses;
    }
    // 잠금 밖에서 검사 (오래 걸린다)
    auto result = std::make_shared<AnalyzedProgram>();
    result->program = checkProgram(files, result->diagnostics, imports);
    const std::lock_guard lock(c.mutex);
    Entry e{key, std::vector<SourceFile>(files.begin(), files.end()),
            std::vector<PackImports>(imports.begin(), imports.end()), result};
    c.entries.insert(c.entries.begin(), std::move(e));
    if (c.entries.size() > kEntries) {
        c.entries.pop_back();
    }
    return result;
}

ProgramCacheStats programCacheStats() {
    Cache& c = cache();
    const std::lock_guard lock(c.mutex);
    return c.stats;
}

void clearProgramCache() {
    Cache& c = cache();
    const std::lock_guard lock(c.mutex);
    c.entries.clear();
    c.stats = {};
}

} // namespace sbx::script
