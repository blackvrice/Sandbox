#include "editor/services/ProjectFiles.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <system_error>

#include "core/asset/AssetDatabase.hpp"
#include "core/asset/AssetRefs.hpp"
#include "core/project/GameProject.hpp"
#include "foundation/io/FileIo.hpp"

namespace sbx::editor::services {

namespace fs = std::filesystem;

namespace {

char lower(char c) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool iless(std::string_view a, std::string_view b) noexcept {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                        [](char x, char y) { return lower(x) < lower(y); });
}

bool icontains(std::string_view hay, std::string_view needle) noexcept {
    if (needle.empty()) {
        return true;
    }
    const auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                                [](char x, char y) { return lower(x) == lower(y); });
    return it != hay.end();
}

std::vector<std::string_view> segments(std::string_view rel) {
    std::vector<std::string_view> out;
    while (!rel.empty()) {
        const usize slash = rel.find('/');
        out.push_back(rel.substr(0, slash));
        if (slash == std::string_view::npos) {
            break;
        }
        rel.remove_prefix(slash + 1);
    }
    return out;
}

// 숨기는 마디 — 어느 깊이든 (.git · .vs · .sandbox), 맨 위만 (build — 빌드 출력)
bool hiddenSegment(std::string_view seg, u32 depth) noexcept {
    return seg == ".sandbox" || seg == ".git" || seg == ".vs" || (depth == 0 && seg == "build");
}

bool isMeta(std::string_view name) noexcept {
    return name.ends_with(asset::kMetaSuffix);
}

bool underAssets(std::string_view rel) noexcept {
    return rel.starts_with(std::string(asset::kAssetDir) + "/");
}

std::string parentOf(std::string_view rel) {
    const usize slash = rel.rfind('/');
    return slash == std::string_view::npos ? std::string() : std::string(rel.substr(0, slash));
}

std::string nameOf(std::string_view rel) {
    const usize slash = rel.rfind('/');
    return std::string(slash == std::string_view::npos ? rel : rel.substr(slash + 1));
}

std::string join(std::string_view parent, std::string_view name) {
    return parent.empty() ? std::string(name) : std::format("{}/{}", parent, name);
}

fs::path absOf(const fs::path& projectDir, std::string_view rel) {
    return rel.empty() ? projectDir : projectDir / fs::path(std::string(rel));
}

// 프로젝트 폴더에서 rel 까지 심볼릭 링크를 지나나 (마지막 마디 포함 — 없는 마디는 거기서 멈춘다)
bool crossesSymlink(const fs::path& projectDir, std::string_view rel) {
    fs::path cur = projectDir;
    std::error_code ec;
    for (const std::string_view seg : segments(rel)) {
        cur /= fs::path(std::string(seg));
        const fs::file_status st = fs::symlink_status(cur, ec);
        if (ec || st.type() == fs::file_type::not_found) {
            return false;
        }
        if (st.type() == fs::file_type::symlink) {
            return true;
        }
    }
    return false;
}

// 있는 항목을 고칠 때 — 이름 규칙은 보지 않는다 (밖에서 만든 한글 이름 파일도 옮기고 지울 수 있게), 경로 탈출 · 숨김 ·
// 심볼릭 링크 · 지킴만
std::optional<std::string> checkSource(const fs::path& projectDir, std::string_view rel, bool protectRoots) {
    if (rel.empty()) {
        return "프로젝트 폴더 자체는 고칠 수 없다";
    }
    if (rel.find('\\') != std::string_view::npos || rel.front() == '/' || (rel.size() >= 2 && rel[1] == ':')) {
        return "프로젝트 기준 '/' 상대 경로여야 한다";
    }
    const std::vector<std::string_view> segs = segments(rel);
    for (usize i = 0; i < segs.size(); ++i) {
        if (segs[i].empty() || segs[i] == "." || segs[i] == "..") {
            return "'.' · '..' · 빈 마디는 쓸 수 없다 (프로젝트 밖을 가리키지 않는다)";
        }
        if (hiddenSegment(segs[i], static_cast<u32>(i))) {
            return std::format("'{}' 는 Studio 가 관리하는 폴더 — 고치지 않는다", segs[i]);
        }
    }
    if (protectRoots && (rel == project::kManifestFile || rel == "content" || rel == asset::kAssetDir ||
                         rel == "worlds")) {
        return std::format("'{}' 는 프로젝트 구조 — 이름 바꾸기 · 옮기기 · 지우기를 하지 않는다", rel);
    }
    if (crossesSymlink(projectDir, rel)) {
        return "심볼릭 링크를 지나는 경로는 고치지 않는다 (프로젝트 밖일 수 있다)";
    }
    return std::nullopt;
}

// 새로 생길 경로 — 이름 규칙 (P4) 까지
std::optional<std::string> checkTarget(const fs::path& projectDir, std::string_view rel) {
    if (const auto bad = project::checkRelativePath(rel)) {
        return std::format("'{}' — {}", rel, *bad);
    }
    if (const auto bad = checkSource(projectDir, rel, false)) {
        return bad;
    }
    std::error_code ec;
    if (fs::exists(fs::symlink_status(absOf(projectDir, rel), ec))) {
        return std::format("'{}' 가 이미 있다 — 덮어쓰지 않는다", rel);
    }
    return std::nullopt;
}

Error editError(std::string message, const fs::path& projectDir) {
    return Error{ErrorCode::InvalidArgument, std::move(message), io::displayPath(projectDir)};
}

// 폴더 안의 원본 파일 (.meta · 심볼릭 링크 빼기) — 프로젝트 기준 rel
std::vector<std::string> filesUnder(const fs::path& projectDir, std::string_view folderRel) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(absOf(projectDir, folderRel), ec), end; !ec && it != end;
         it.increment(ec)) {
        if (it->is_symlink(ec)) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec) && !isMeta(it->path().filename().string())) {
            out.push_back(fs::relative(it->path(), projectDir, ec).generic_string());
        }
    }
    std::ranges::sort(out);
    return out;
}

fs::path freeTrashSlot(const fs::path& projectDir) {
    std::error_code ec;
    for (u32 n = 1;; ++n) {
        fs::path slot = projectDir / ".sandbox" / "trash" / std::to_string(n);
        if (!fs::exists(slot, ec)) {
            return slot;
        }
    }
}

// 에셋 폴더를 새 접두로 — 파일마다 moveAsset (.meta · 경로 참조), 그 뒤 빈 옛 폴더를 지운다
Expected<void> moveAssetFolder(const fs::path& projectDir, std::string_view fromRel, std::string_view toRel) {
    for (const std::string& f : filesUnder(projectDir, fromRel)) {
        const std::string dest = std::format("{}{}", toRel, std::string_view(f).substr(fromRel.size()));
        if (auto r = asset::moveAsset(projectDir, f, dest); !r) {
            return std::unexpected(r.error());
        }
    }
    std::error_code ec;
    fs::create_directories(absOf(projectDir, toRel), ec); // 빈 폴더도 옮긴 것으로
    if (filesUnder(projectDir, fromRel).empty()) {
        fs::remove_all(absOf(projectDir, fromRel), ec); // 남은 것은 빈 폴더 · 짝 없는 .meta 뿐
    }
    return {};
}

Expected<std::string> relocate(const fs::path& projectDir, std::string_view rel, const std::string& to) {
    if (const auto bad = checkTarget(projectDir, to)) {
        return std::unexpected(editError(*bad, projectDir));
    }
    std::error_code ec;
    const fs::path from = absOf(projectDir, rel);
    const bool folder = fs::is_directory(from, ec);
    if (underAssets(rel) != underAssets(to) && underAssets(rel)) {
        return std::unexpected(editError("에셋은 assets/ 안에서만 옮긴다 (참조 · .meta 가 따라가는 곳)", projectDir));
    }
    if (underAssets(rel)) {
        if (folder) {
            if (auto r = moveAssetFolder(projectDir, rel, to); !r) {
                return std::unexpected(r.error());
            }
        } else if (auto r = asset::moveAsset(projectDir, rel, to); !r) {
            return std::unexpected(r.error());
        }
        return to;
    }
    fs::create_directories(absOf(projectDir, parentOf(to)), ec);
    if (ec = io::renameRetrying(from, absOf(projectDir, to)); ec) {
        return makeError(ErrorCode::IoError, std::format("옮기지 못했다: {}", ec.message()), io::displayPath(from));
    }
    return to;
}

void listInto(const fs::path& dir, const std::string& relDir, u32 depth, std::vector<ProjectFileEntry>& out) {
    std::vector<ProjectFileEntry> children;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().generic_string();
        if (it->is_symlink(ec) || hiddenSegment(name, depth) || isMeta(name)) {
            continue;
        }
        ProjectFileEntry e;
        e.rel = join(relDir, name);
        e.name = name;
        e.depth = depth;
        if (it->is_directory(ec)) {
            e.kind = ProjectFileKind::Folder;
        } else if (it->is_regular_file(ec)) {
            e.kind = projectFileKind(name);
            e.size = it->file_size(ec);
        } else {
            continue; // 장치 · 소켓 …
        }
        children.push_back(std::move(e));
    }
    std::ranges::sort(children, [](const ProjectFileEntry& a, const ProjectFileEntry& b) {
        const bool fa = a.kind == ProjectFileKind::Folder;
        const bool fb = b.kind == ProjectFileKind::Folder;
        if (fa != fb) {
            return fa;
        }
        return iless(a.name, b.name) || (!iless(b.name, a.name) && a.name < b.name);
    });
    for (ProjectFileEntry& e : children) {
        if (out.size() >= kMaxProjectFiles) {
            return;
        }
        const bool folder = e.kind == ProjectFileKind::Folder;
        const std::string rel = e.rel;
        const fs::path sub = dir / fs::path(e.name);
        out.push_back(std::move(e));
        if (folder) {
            listInto(sub, rel, depth + 1, out);
        }
    }
}

bool isScriptSegment(std::string_view seg) noexcept {
    return !seg.empty() && seg.front() >= 'a' && seg.front() <= 'z' &&
           std::ranges::all_of(seg, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

} // namespace

ProjectFileKind projectFileKind(std::string_view name) noexcept {
    const usize dot = name.rfind('.');
    if (dot == std::string_view::npos) {
        return ProjectFileKind::Other;
    }
    std::string ext(name.substr(dot + 1));
    std::ranges::transform(ext, ext.begin(), lower);
    if (ext == "ts") {
        return ProjectFileKind::Script;
    }
    if (ext == "png" || ext == "jpg" || ext == "jpeg") {
        return ProjectFileKind::Image;
    }
    if (ext == "wav" || ext == "ogg") {
        return ProjectFileKind::Audio;
    }
    if (ext == "json") {
        return ProjectFileKind::Json;
    }
    return ProjectFileKind::Other;
}

std::vector<ProjectFileEntry> listProjectFiles(const fs::path& projectDir) {
    std::vector<ProjectFileEntry> out;
    std::error_code ec;
    if (projectDir.empty() || !fs::is_directory(projectDir, ec)) {
        return out;
    }
    listInto(projectDir, std::string(), 0, out);
    return out;
}

std::vector<ProjectFileEntry> filterProjectFiles(const std::vector<ProjectFileEntry>& entries,
                                                 std::string_view query) {
    if (query.empty()) {
        return entries;
    }
    // 맞는 항목의 rel 과 그 위 폴더들을 남긴다 (목록은 깊이 우선 — 차례 그대로)
    std::vector<bool> keep(entries.size(), false);
    std::vector<std::string> folders;
    for (usize i = 0; i < entries.size(); ++i) {
        if (!icontains(entries[i].rel, query)) {
            continue;
        }
        keep[i] = true;
        for (std::string p = parentOf(entries[i].rel); !p.empty(); p = parentOf(p)) {
            folders.push_back(p);
        }
    }
    std::ranges::sort(folders);
    std::vector<ProjectFileEntry> out;
    for (usize i = 0; i < entries.size(); ++i) {
        if (keep[i] || (entries[i].kind == ProjectFileKind::Folder && std::ranges::binary_search(folders, entries[i].rel))) {
            out.push_back(entries[i]);
        }
    }
    return out;
}

std::optional<std::string> checkProjectEdit(const fs::path& projectDir, std::string_view rel, bool protectRoots) {
    return checkSource(projectDir, rel, protectRoots);
}

Expected<std::string> createProjectFolder(const fs::path& projectDir, std::string_view parentRel,
                                          std::string_view name) {
    if (name.find('/') != std::string_view::npos) {
        return std::unexpected(editError("이름에 '/' 는 쓸 수 없다", projectDir));
    }
    if (!parentRel.empty()) {
        if (const auto bad = checkSource(projectDir, parentRel, false)) {
            return std::unexpected(editError(*bad, projectDir));
        }
    }
    std::error_code ec;
    if (!fs::is_directory(absOf(projectDir, parentRel), ec)) {
        return makeError(ErrorCode::NotFound, std::format("폴더 '{}' 가 없다", parentRel), io::displayPath(projectDir));
    }
    const std::string rel = join(parentRel, name);
    if (const auto bad = checkTarget(projectDir, rel)) {
        return std::unexpected(editError(*bad, projectDir));
    }
    if (!fs::create_directory(absOf(projectDir, rel), ec) || ec) {
        return makeError(ErrorCode::IoError, std::format("폴더를 만들지 못했다: {}", ec.message()),
                         io::displayPath(absOf(projectDir, rel)));
    }
    return rel;
}

Expected<std::string> renameProjectEntry(const fs::path& projectDir, std::string_view rel, std::string_view newName) {
    if (const auto bad = checkSource(projectDir, rel, true)) {
        return std::unexpected(editError(*bad, projectDir));
    }
    if (newName.empty() || newName.find('/') != std::string_view::npos) {
        return std::unexpected(editError("새 이름이 비었거나 '/' 가 있다", projectDir));
    }
    std::error_code ec;
    if (!fs::exists(absOf(projectDir, rel), ec)) {
        return makeError(ErrorCode::NotFound, std::format("'{}' 가 없다", rel), io::displayPath(projectDir));
    }
    if (newName == nameOf(rel)) {
        return std::string(rel);
    }
    return relocate(projectDir, rel, join(parentOf(rel), newName));
}

Expected<std::string> moveProjectEntry(const fs::path& projectDir, std::string_view rel, std::string_view destFolderRel) {
    if (const auto bad = checkSource(projectDir, rel, true)) {
        return std::unexpected(editError(*bad, projectDir));
    }
    if (!destFolderRel.empty()) {
        if (const auto bad = checkSource(projectDir, destFolderRel, false)) {
            return std::unexpected(editError(*bad, projectDir));
        }
    }
    std::error_code ec;
    if (!fs::exists(absOf(projectDir, rel), ec)) {
        return makeError(ErrorCode::NotFound, std::format("'{}' 가 없다", rel), io::displayPath(projectDir));
    }
    if (!fs::is_directory(absOf(projectDir, destFolderRel), ec)) {
        return makeError(ErrorCode::NotFound, std::format("폴더 '{}' 가 없다", destFolderRel),
                         io::displayPath(projectDir));
    }
    if (destFolderRel == rel || std::string_view(destFolderRel).starts_with(std::string(rel) + "/")) {
        return std::unexpected(editError("폴더를 자기 안으로 옮길 수 없다", projectDir));
    }
    if (parentOf(rel) == destFolderRel) {
        return std::string(rel); // 같은 곳
    }
    return relocate(projectDir, rel, join(destFolderRel, nameOf(rel)));
}

Expected<std::string> duplicateProjectEntry(const fs::path& projectDir, std::string_view rel) {
    if (const auto bad = checkSource(projectDir, rel, true)) {
        return std::unexpected(editError(*bad, projectDir));
    }
    std::error_code ec;
    const fs::path from = absOf(projectDir, rel);
    const bool folder = fs::is_directory(from, ec);
    if (!folder && !fs::is_regular_file(from, ec)) {
        return makeError(ErrorCode::NotFound, std::format("'{}' 가 없다", rel), io::displayPath(projectDir));
    }
    const std::string name = nameOf(rel);
    const usize dot = folder ? std::string::npos : name.rfind('.');
    const std::string stem = dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
    const std::string ext = dot == std::string::npos || dot == 0 ? std::string() : name.substr(dot);
    std::string to;
    for (u32 n = 1; n < 1000; ++n) {
        const std::string candidate =
            join(parentOf(rel), std::format("{}_copy{}{}", stem, n == 1 ? std::string() : std::to_string(n), ext));
        if (!fs::exists(fs::symlink_status(absOf(projectDir, candidate), ec))) {
            to = candidate;
            break;
        }
    }
    if (to.empty()) {
        return std::unexpected(editError("복제 이름을 찾지 못했다 (사본이 너무 많다)", projectDir));
    }
    if (const auto bad = checkTarget(projectDir, to)) {
        return std::unexpected(editError(*bad, projectDir));
    }
    const fs::path dest = absOf(projectDir, to);
    if (!folder) {
        if (!fs::copy_file(from, dest, ec) || ec) {
            return makeError(ErrorCode::IoError, std::format("복제하지 못했다: {}", ec.message()), io::displayPath(from));
        }
        return to; // .meta 는 복사하지 않는다 — 같은 id 둘은 충돌 (NG-12)
    }
    fs::create_directories(dest, ec);
    for (fs::recursive_directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_symlink(ec)) {
            it.disable_recursion_pending();
            continue;
        }
        const fs::path target = dest / fs::relative(it->path(), from, ec);
        if (it->is_directory(ec)) {
            fs::create_directories(target, ec);
        } else if (it->is_regular_file(ec) && !(underAssets(rel) && isMeta(it->path().filename().string()))) {
            fs::copy_file(it->path(), target, ec);
        }
        if (ec) {
            return makeError(ErrorCode::IoError, std::format("복제하지 못했다: {}", ec.message()),
                             io::displayPath(it->path()));
        }
    }
    if (ec) {
        return makeError(ErrorCode::IoError, std::format("복제하지 못했다: {}", ec.message()), io::displayPath(from));
    }
    return to;
}

Expected<fs::path> trashProjectEntry(const fs::path& projectDir, std::string_view rel, bool force) {
    if (const auto bad = checkSource(projectDir, rel, true)) {
        return std::unexpected(editError(*bad, projectDir));
    }
    std::error_code ec;
    const fs::path from = absOf(projectDir, rel);
    const bool folder = fs::is_directory(from, ec);
    if (!folder && !fs::is_regular_file(from, ec)) {
        return makeError(ErrorCode::NotFound, std::format("'{}' 가 없다", rel), io::displayPath(projectDir));
    }
    if (underAssets(rel) && !folder && !isMeta(rel)) {
        auto r = asset::deleteAsset(projectDir, rel, force); // 참조 검사 · .meta 동행
        if (!r) {
            return std::unexpected(r.error());
        }
        return r->trashed;
    }
    if (underAssets(rel) && folder && !force) {
        const std::vector<asset::AssetRef> refs =
            asset::findAssetReferences(projectDir, asset::scanProjectAssets(projectDir));
        usize used = 0;
        std::string first;
        for (const std::string& f : filesUnder(projectDir, rel)) {
            const std::vector<asset::AssetRef> to = asset::referencesTo(refs, f);
            if (!to.empty() && first.empty()) {
                first = std::format("{} ← {}{}", f, to.front().file, to.front().pointer);
            }
            used += to.size();
        }
        if (used > 0) {
            return std::unexpected(editError(
                std::format("폴더 안의 에셋을 {} 곳이 쓴다 — 지우지 않는다 (참조를 먼저 고친다)\n  {}", used, first),
                projectDir));
        }
    }
    const fs::path dest = freeTrashSlot(projectDir) / fs::path(std::string(rel));
    fs::create_directories(dest.parent_path(), ec);
    if (ec = io::renameRetrying(from, dest); ec) {
        return makeError(ErrorCode::IoError, std::format("휴지통으로 옮기지 못했다: {}", ec.message()),
                         io::displayPath(from));
    }
    return dest;
}

Expected<std::string> newScriptPath(const fs::path& projectDir, std::string_view parentRel, std::string_view name) {
    if (const auto bad = checkSource(projectDir, parentRel, false)) {
        return std::unexpected(editError(*bad, projectDir));
    }
    const std::vector<std::string_view> segs = segments(parentRel);
    if (segs.size() < 2 || segs[0] != "content" || (segs.size() >= 3 && segs[2] != "scripts")) {
        return std::unexpected(
            editError("스크립트는 팩의 scripts 폴더에 만든다 (content/<팩>/ · content/<팩>/scripts/…)", projectDir));
    }
    std::error_code ec;
    if (!fs::is_directory(absOf(projectDir, std::format("content/{}", segs[1])), ec)) {
        return makeError(ErrorCode::NotFound, std::format("팩 폴더 content/{} 가 없다", segs[1]),
                         io::displayPath(projectDir));
    }
    for (usize i = 3; i < segs.size(); ++i) {
        if (!isScriptSegment(segs[i])) {
            return std::unexpected(editError(
                std::format("폴더 '{}' — 스크립트 id 마디는 소문자 · 숫자 · '_' (소문자로 시작)", segs[i]), projectDir));
        }
    }
    std::string_view stem = name;
    if (stem.ends_with(".ts")) {
        stem.remove_suffix(3);
    }
    if (!isScriptSegment(stem)) {
        return std::unexpected(editError(
            std::format("이름 '{}' — 스크립트 이름은 소문자 · 숫자 · '_' (소문자로 시작, 예: enemy_ai)", name), projectDir));
    }
    const std::string base = segs.size() == 2 ? std::format("{}/scripts", parentRel) : std::string(parentRel);
    const std::string rel = std::format("{}/{}.ts", base, stem);
    if (fs::exists(fs::symlink_status(absOf(projectDir, rel), ec))) {
        return makeError(ErrorCode::AlreadyExists, std::format("'{}' 가 이미 있다", rel), io::displayPath(projectDir));
    }
    if (const auto bad = project::checkRelativePath(rel)) {
        return std::unexpected(editError(std::format("'{}' — {}", rel, *bad), projectDir));
    }
    return rel;
}

} // namespace sbx::editor::services
