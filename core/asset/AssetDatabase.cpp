#include "core/asset/AssetDatabase.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <random>
#include <set>

#include <nlohmann/json.hpp>

#include "core/project/GameProject.hpp"
#include "foundation/hash/Sha256.hpp"
#include "foundation/io/FileIo.hpp"

namespace sbx::asset {

namespace fs = std::filesystem;
using json = nlohmann::json;
using content::ContentIssue;
using Severity = ContentIssue::Severity;

namespace {

void add(std::vector<ContentIssue>& out, Severity s, std::string rule, std::string file, std::string message) {
    out.push_back({s, std::move(rule), std::move(file), "", std::move(message)});
}

bool endsWithMeta(std::string_view rel) noexcept {
    return rel.size() > kMetaSuffix.size() && rel.ends_with(kMetaSuffix);
}

// 훑기 결과 + 이 프로젝트에 이미 쓰인 모든 id (충돌한 것까지 — 새 id 가 그것과 겹치지 않게)
struct Scanned {
    AssetScan scan;
    std::set<std::string> usedIds;
};

Scanned scanImpl(const fs::path& projectDir) {
    Scanned out;
    AssetScan& r = out.scan;
    const fs::path root = projectDir / std::string(kAssetDir);
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        return out;
    }
    std::vector<std::string> sources;
    std::set<std::string> metas;
    std::vector<std::string> all;
    for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        const fs::directory_entry& e = *it;
        // 글자로만 상대 경로를 만든다 — fs::relative 는 링크를 풀어 밖의 경로가 되고, 반복자의 ec 를 덮어 훑기를 멈출
        // 수 있다
        const std::string rel = std::string(kAssetDir) + "/" + e.path().lexically_relative(root).generic_string();
        // 링크는 따라가지 않는다 — 프로젝트 밖 파일이 에셋이 되지 않게 (NG-13 · 24 P6)
        if (e.is_symlink(ec)) {
            add(r.issues, Severity::Error, "A5", rel, "심볼릭 링크는 에셋이 될 수 없다 (밖을 가리킬 수 있다)");
            continue;
        }
        if (e.is_directory(ec)) {
            continue;
        }
        if (!e.is_regular_file(ec)) {
            add(r.issues, Severity::Error, "A5", rel, "일반 파일이 아니다");
            continue;
        }
        if (all.size() >= kMaxAssets * 2) { // 원본 + .meta
            add(r.issues, Severity::Error, "A5", std::string(kAssetDir),
                std::format("assets/ 의 파일이 {} 개를 넘는다", kMaxAssets * 2));
            break;
        }
        if (const auto why = project::checkRelativePath(rel)) {
            add(r.issues, Severity::Error, "A5", rel, std::format("에셋 경로: {}", *why));
            continue;
        }
        all.push_back(rel);
        if (endsWithMeta(rel)) {
            metas.insert(rel);
        } else {
            sources.push_back(rel);
        }
    }
    if (ec) {
        add(r.issues, Severity::Error, "A5", std::string(kAssetDir), std::format("훑지 못했다 — {}", ec.message()));
    }
    std::sort(all.begin(), all.end());
    if (const auto clash = project::findCaseConflict(all)) {
        add(r.issues, Severity::Error, "A5", std::string(kAssetDir),
            std::format("대소문자만 다른 경로 {} — Windows · macOS 에서 한 파일이 된다", *clash));
    }
    std::sort(sources.begin(), sources.end());

    struct Found {
        std::string path;
        std::string sha256;
        u64 size = 0;
    };
    std::map<std::string, std::vector<Found>> byId;
    for (const std::string& rel : sources) {
        const fs::path file = projectDir / fs::path(rel);
        const u64 size = fs::file_size(file, ec);
        if (ec) {
            add(r.issues, Severity::Error, "A5", rel, ec.message());
            continue;
        }
        if (size > kMaxAssetBytes) {
            add(r.issues, Severity::Error, "A5", rel,
                std::format("원본이 {} MiB 를 넘는다 ({} 바이트)", kMaxAssetBytes >> 20, size));
            continue;
        }
        auto bytes = io::readFile(file);
        if (!bytes) {
            add(r.issues, Severity::Error, "A5", rel, bytes.error().describe());
            continue;
        }
        const std::string metaRel = rel + std::string(kMetaSuffix);
        if (!metas.contains(metaRel)) {
            add(r.issues, Severity::Warning, "A2", rel,
                "id 가 없다 (.meta 없음) — 가져오기가 새 id 를 쓴다. 경로로 부른 참조는 옮기면 끊긴다");
            r.unidentified.push_back(rel);
            continue;
        }
        const auto metaText = io::readFile(projectDir / fs::path(metaRel));
        auto meta = metaText ? parseAssetMeta(*metaText) : Expected<AssetMeta>(std::unexpected(metaText.error()));
        if (!meta) {
            add(r.issues, Severity::Warning, "A1", metaRel,
                std::format(".meta 를 읽을 수 없다 — {} (id 없는 에셋으로 본다. 가져오기가 새 id 를 쓴다)",
                            meta.error().message));
            r.unidentified.push_back(rel);
            continue;
        }
        out.usedIds.insert(meta->id);
        byId[meta->id].push_back({rel, toHex(sha256(*bytes)), size});
    }
    // 짝 없는 .meta — 원본을 밖에서 옮기거나 지우면서 남은 것
    for (const std::string& m : metas) {
        const std::string source = m.substr(0, m.size() - kMetaSuffix.size());
        if (!std::binary_search(sources.begin(), sources.end(), source)) {
            add(r.issues, Severity::Warning, "A4", m, "원본이 없는 .meta — 원본과 함께 옮기거나 지운다");
            if (const auto text = io::readFile(projectDir / fs::path(m))) {
                if (const auto meta = parseAssetMeta(*text)) {
                    out.usedIds.insert(meta->id); // 다시 나타날 원본의 id 를 새 id 로 쓰지 않게
                }
            }
        }
    }
    for (auto& [id, found] : byId) {
        // found 는 경로 순 (sources 가 정렬됐다)
        const bool sameContent =
            std::all_of(found.begin(), found.end(), [&](const Found& f) { return f.sha256 == found.front().sha256; });
        if (!sameContent) {
            const auto other = std::find_if(found.begin(), found.end(),
                                            [&](const Found& f) { return f.sha256 != found.front().sha256; });
            add(r.issues, Severity::Error, "A3", found.front().path,
                std::format(
                    "id {} 가 내용이 다른 두 파일에 있다 — '{}' · '{}'. 어느 쪽도 고르지 않는다: 복사한 쪽에 새 id "
                    "(assignNewAssetId)",
                    id, found.front().path, other->path));
            continue;
        }
        AssetEntry entry{id, found.front().path, found.front().sha256, found.front().size, {}};
        for (usize i = 1; i < found.size(); ++i) {
            entry.copies.push_back(found[i].path);
        }
        r.assets.push_back(std::move(entry));
    }
    return out;
}

// 쓰인 id 와 겹치지 않는 새 id (생성기가 같은 값을 거듭 주면 오류 — 시험의 고정 생성기)
Expected<std::string> freshId(const IdGenerator& newId, std::set<std::string>& used) {
    for (int attempt = 0; attempt < 16; ++attempt) {
        std::string id = newId();
        if (!isAssetId(id)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("새 id '{}' 가 32 글자 소문자 16 진수가 아니다", id));
        }
        if (used.insert(id).second) {
            return id;
        }
    }
    return makeError(ErrorCode::AlreadyExists, "이미 쓰인 id 와 겹치지 않는 새 id 를 만들지 못했다");
}

} // namespace

bool isAssetId(std::string_view text) noexcept {
    return text.size() == 32 && std::all_of(text.begin(), text.end(),
                                            [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

Expected<AssetMeta> parseAssetMeta(std::string_view jsonText) {
    const json j = json::parse(jsonText, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return makeError(ErrorCode::ParseError, "JSON 객체가 아니다");
    }
    for (const auto& [key, value] : j.items()) {
        if (key != "id" && key != "importer" && key != "settings") {
            return makeError(ErrorCode::ValidationFailed, std::format("모르는 키 '{}'", key));
        }
    }
    AssetMeta m;
    if (!j.contains("id") || !j["id"].is_string() || !isAssetId(j["id"].get<std::string>())) {
        return makeError(ErrorCode::ValidationFailed, "\"id\" 는 32 글자 소문자 16 진수");
    }
    m.id = j["id"].get<std::string>();
    if (!j.contains("importer") || !j["importer"].is_string() || j["importer"].get<std::string>().empty()) {
        return makeError(ErrorCode::ValidationFailed, "\"importer\" 는 비지 않은 문자열 (\"image@1\")");
    }
    m.importer = j["importer"].get<std::string>();
    if (j.contains("settings") && !j["settings"].is_object()) {
        return makeError(ErrorCode::ValidationFailed, "\"settings\" 는 객체");
    }
    m.settings = j.contains("settings") ? j["settings"].dump() : "{}"; // nlohmann 객체는 키 순 — 정규형
    return m;
}

std::string writeAssetMeta(const AssetMeta& meta) {
    return std::format("{{\n  \"id\": {},\n  \"importer\": {},\n  \"settings\": {}\n}}\n", json(meta.id).dump(),
                       json(meta.importer).dump(), meta.settings.empty() ? std::string("{}") : meta.settings);
}

std::string randomAssetId() {
    // 128 비트 — 프로젝트 사이에서 겹칠 걱정이 없는 크기 (Unity GUID · Godot uid 와 같은 방향). 시뮬레이션 밖
    std::random_device rd;
    std::string id;
    for (int i = 0; i < 4; ++i) {
        id += std::format("{:08x}", static_cast<u32>(rd()));
    }
    return id;
}

std::string importerFor(std::string_view relPath) {
    std::string ext(relPath.substr(relPath.find_last_of('.') == std::string_view::npos ? relPath.size()
                                                                                       : relPath.find_last_of('.')));
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    return ext == ".png" ? "image@1" : "file@1";
}

bool AssetScan::ok() const noexcept {
    return std::none_of(issues.begin(), issues.end(),
                        [](const ContentIssue& i) { return i.severity == Severity::Error; });
}

const AssetEntry* AssetScan::find(std::string_view id) const noexcept {
    const auto it = std::lower_bound(assets.begin(), assets.end(), id,
                                     [](const AssetEntry& e, std::string_view key) { return e.id < key; });
    return it != assets.end() && it->id == id ? &*it : nullptr;
}

AssetScan scanProjectAssets(const fs::path& projectDir) {
    return scanImpl(projectDir).scan;
}

Expected<usize> importAssets(const fs::path& projectDir, const IdGenerator& newId) {
    Scanned s = scanImpl(projectDir);
    for (const ContentIssue& i : s.scan.issues) {
        if (i.severity == Severity::Error) {
            return makeError(ErrorCode::ValidationFailed, std::format("에셋 문제를 먼저 고친다 — {}", i.describe()));
        }
    }
    usize written = 0;
    for (const std::string& rel : s.scan.unidentified) {
        auto id = freshId(newId, s.usedIds);
        if (!id) {
            return std::unexpected(id.error());
        }
        const AssetMeta meta{*id, importerFor(rel), "{}"};
        if (auto w = io::writeFileAtomic(projectDir / fs::path(rel + std::string(kMetaSuffix)), writeAssetMeta(meta));
            !w) {
            return std::unexpected(w.error());
        }
        ++written;
    }
    return written;
}

Expected<std::string> assignNewAssetId(const fs::path& projectDir, std::string_view relPath, const IdGenerator& newId) {
    const std::string rel(relPath);
    if (const auto why = project::checkRelativePath(rel)) {
        return makeError(ErrorCode::InvalidArgument, std::format("에셋 경로: {}", *why), rel);
    }
    if (!rel.starts_with(std::string(kAssetDir) + "/") || endsWithMeta(rel)) {
        return makeError(ErrorCode::InvalidArgument, "assets/ 아래의 원본 파일 (.meta 아님) 을 준다", rel);
    }
    std::error_code ec;
    const fs::path file = projectDir / fs::path(rel);
    if (fs::is_symlink(file, ec) || !fs::is_regular_file(file, ec)) {
        return makeError(ErrorCode::NotFound, "원본 파일이 없다", io::displayPath(file));
    }
    Scanned s = scanImpl(projectDir);
    const fs::path metaFile = projectDir / fs::path(rel + std::string(kMetaSuffix));
    AssetMeta meta{"", importerFor(rel), "{}"};
    if (const auto text = io::readFile(metaFile)) {
        if (auto old = parseAssetMeta(*text)) {
            meta = std::move(*old); // importer · settings 는 그대로
        }
    }
    auto id = freshId(newId, s.usedIds);
    if (!id) {
        return std::unexpected(id.error());
    }
    meta.id = *id;
    if (auto w = io::writeFileAtomic(metaFile, writeAssetMeta(meta)); !w) {
        return std::unexpected(w.error());
    }
    return *id;
}

} // namespace sbx::asset
