#include "editor/services/AutosaveService.hpp"

#include <charconv>
#include <chrono>
#include <format>
#include <system_error>
#include <thread>

#include <nlohmann/json.hpp>

#include "editor/services/DocumentService.hpp"
#include "foundation/hash/Fnv1a.hpp"
#include "foundation/io/FileIo.hpp"

namespace sbx::editor::services {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kInfoFile = "recovery.json";

std::string utf8(const fs::path& p) {
    const std::u8string s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}

fs::path fromUtf8(const std::string& s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}

fs::path normalized(const fs::path& dir) {
    // 없는 폴더도 같은 키 — "A" · "A/." · "A/" 가 같게 (끝 구분자를 뗀다)
    std::error_code ec;
    fs::path p = fs::weakly_canonical(dir, ec);
    p = (ec ? fs::absolute(dir, ec) : p).lexically_normal();
    if (!p.has_filename() && p.has_parent_path() && p != p.root_path()) {
        p = p.parent_path();
    }
    return p;
}

std::string utcNow() {
    // 화면 표시용 시각 (시뮬레이션 밖 — 결정론과 무관)
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);
}

} // namespace

AutosaveService::AutosaveService(fs::path root, f64 intervalSeconds)
    : m_root(std::move(root)), m_interval(intervalSeconds) {}

fs::path AutosaveService::folderFor(const fs::path& root, const fs::path& project) {
    return root / std::format("{:016x}", fnv1a64(utf8(normalized(project))));
}

std::optional<RecoveryInfo> AutosaveService::read() const {
    return m_folder.empty() ? std::nullopt : readFolder(m_folder);
}

std::optional<RecoveryInfo> AutosaveService::peek(const std::filesystem::path& project) const {
    if (m_root.empty() || project.empty()) {
        return std::nullopt;
    }
    std::optional<RecoveryInfo> r = readFolder(folderFor(m_root, project));
    if (!r || r->clean || (!r->hasWorld && r->documents.empty())) {
        return std::nullopt;
    }
    return r;
}

std::optional<RecoveryInfo> AutosaveService::readFolder(const std::filesystem::path& m_folder) {
    const Expected<std::string> text = io::readFile(m_folder / std::string(kInfoFile));
    if (!text) {
        return std::nullopt;
    }
    const nlohmann::json j = nlohmann::json::parse(*text, nullptr, false);
    if (!j.is_object()) {
        return std::nullopt;
    }
    RecoveryInfo r;
    r.folder = m_folder;
    r.project = fromUtf8(j.value("project", std::string()));
    r.savedAt = j.value("savedAt", std::string());
    r.clean = j.value("clean", true);
    // tick · 해시는 세이브 자신 (world/world.json — 세이브와 함께 원자적으로 바뀐다) 에서. recovery.json 에 따로 적으면
    // 세이브 뒤 · 정보 쓰기 전에 죽었을 때 서로 다르다
    if (const Expected<std::string> w = io::readFile(m_folder / "world" / "world.json")) {
        const nlohmann::json world = nlohmann::json::parse(*w, nullptr, false);
        if (world.is_object() && world.contains("tick") && world["tick"].is_number_unsigned()) {
            r.hasWorld = true;
            r.tick = world["tick"].get<u64>();
            std::string hash = world.value("worldHash", std::string("0"));
            if (hash.starts_with("0x")) {
                hash.erase(0, 2);
            }
            (void)std::from_chars(hash.data(), hash.data() + hash.size(), r.worldHash, 16); // 깨진 값이면 0 (예외 없음)
        }
    }
    if (const auto d = j.find("documents"); d != j.end() && d->is_array()) {
        for (const nlohmann::json& e : *d) {
            if (e.is_object() && e.contains("path") && e.contains("file") && e["path"].is_string() &&
                e["file"].is_string()) {
                r.documents.push_back(
                    {fromUtf8(e["path"].get<std::string>()), m_folder / fromUtf8(e["file"].get<std::string>())});
            }
        }
    }
    return r;
}

void AutosaveService::begin(const fs::path& project, f64 now) {
    m_pending.reset();
    m_project = normalized(project);
    m_folder = enabled() ? folderFor(m_root, m_project) : fs::path();
    m_last = now;
    m_now = false;
    m_waiting = false;
    m_hasWorld = false;
    m_documents.clear();
    if (!enabled()) {
        return;
    }
    // 앞 실행이 깨끗이 끝나지 않았고 남긴 것이 있으면 — 고를 때까지 새 자동 저장을 멈춘다 (덮지 않는다)
    if (std::optional<RecoveryInfo> r = read(); r && !r->clean && (r->hasWorld || !r->documents.empty())) {
        m_pending = std::move(r);
        return;
    }
    // 이 실행을 연다: 깨끗이 끝난 앞 실행의 복구본은 더 쓸 데가 없다 (지운다), 그리고 "깨끗하지 않음" 을 먼저 적어 둔다
    // — 첫 자동 저장 전에 죽어도 (그 뒤 세이브가 생기면) 다음 시작이 안다
    std::error_code ec;
    fs::remove_all(m_folder / "world", ec);
    fs::remove_all(m_folder / "documents", ec);
    writeInfo(false);
}

Expected<std::string> AutosaveService::restore() {
    if (!m_pending || !m_pending->hasWorld) {
        return makeError(ErrorCode::NotFound, "복구할 월드가 없다");
    }
    const fs::path worlds = m_project / "worlds";
    std::error_code ec;
    for (u32 n = 1; n < 1000; ++n) {
        const std::string name = std::format("recovered-{}", n);
        if (fs::exists(worlds / name, ec)) {
            continue; // 덮어쓰지 않는다
        }
        fs::create_directories(worlds / name, ec);
        fs::copy(m_pending->folder / "world", worlds / name, fs::copy_options::recursive, ec);
        if (ec) {
            fs::remove_all(worlds / name, ec);
            return makeError(ErrorCode::IoError, std::format("복구본을 옮기지 못했다 — {}", ec.message()),
                             utf8(worlds / name));
        }
        return name;
    }
    return makeError(ErrorCode::AlreadyExists, "recovered-1 ~ 999 가 모두 있다", utf8(worlds));
}

void AutosaveService::discard(f64 now) {
    m_pending.reset();
    m_hasWorld = false;
    m_documents.clear();
    m_last = now;
    if (m_folder.empty()) {
        return;
    }
    // 막 쓴 파일을 다른 프로그램 (백신 검사 · 색인) 이 잠깐 잡을 수 있다 — 몇 번 다시. 그래도 남으면 다시 제안하지 않게
    // 깨끗하다고 적는다 (지우지 못한 파일은 다음 자동 저장이 덮는다)
    std::error_code ec;
    for (int attempt = 0; attempt < 5; ++attempt) {
        fs::remove_all(m_folder, ec);
        if (!fs::exists(m_folder, ec)) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20 * (attempt + 1)));
    }
    m_warnings.push_back(
        std::format("복구본 폴더 {} 를 다 지우지 못했다 — 다시 제안하지 않게 표시했다", utf8(m_folder)));
    writeInfo(true);
}

bool AutosaveService::due(f64 now) const noexcept {
    if (!enabled() || m_project.empty() || m_pending || m_waiting) {
        return false;
    }
    return m_now || (m_interval > 0 && now - m_last >= m_interval);
}

void AutosaveService::markRequested(f64 now) noexcept {
    m_waiting = true;
    m_now = false;
    m_last = now;
}

void AutosaveService::onWorldSaved(bool ok, u64 tick, u64 worldHash, f64 now) {
    m_waiting = false;
    m_last = now;
    if (!ok) {
        m_warnings.push_back("자동 저장에 실패했다 — 다음 주기에 다시");
        return;
    }
    ++m_saves;
    (void)tick; // 정본은 세이브의 world.json (read) — 로그는 앱이 남긴다
    (void)worldHash;
    m_hasWorld = true;
    writeInfo(false);
}

usize AutosaveService::saveDocuments(const DocumentService& docs) {
    if (!enabled() || m_folder.empty() || m_pending) {
        return 0;
    }
    std::error_code ec;
    fs::remove_all(m_folder / "documents", ec);
    m_documents.clear();
    usize bytes = 0;
    for (const DocId id : docs.openDocuments()) {
        if (!docs.dirty(id)) {
            continue;
        }
        const std::string& text = docs.text(id);
        if (bytes + text.size() > kMaxDocumentBytes) {
            m_warnings.push_back(std::format("저장하지 않은 문서 {} 는 복구본 상한 ({} 바이트) 을 넘어 남기지 않았다",
                                             utf8(docs.path(id)), kMaxDocumentBytes));
            continue;
        }
        const fs::path file = fs::path("documents") / std::format("{}.txt", m_documents.size());
        fs::create_directories(m_folder / "documents", ec);
        if (io::writeFileAtomic(m_folder / file, text)) {
            bytes += text.size();
            m_documents.push_back({docs.path(id), file});
        }
    }
    writeInfo(false);
    return m_documents.size();
}

void AutosaveService::finishClean() {
    if (enabled() && !m_folder.empty() && !m_pending && (m_hasWorld || !m_documents.empty())) {
        writeInfo(true);
    }
}

void AutosaveService::writeInfo(bool clean) {
    nlohmann::json docs = nlohmann::json::array();
    for (const RecoveredDocument& d : m_documents) {
        docs.push_back({{"path", utf8(d.path)}, {"file", utf8(d.file)}});
    }
    // tick · 해시는 적지 않는다 — world/world.json 이 정본 (read)
    const nlohmann::json j = {
        {"project", utf8(m_project)}, {"savedAt", utcNow()}, {"clean", clean}, {"documents", std::move(docs)}};
    std::error_code ec;
    fs::create_directories(m_folder, ec);
    if (const Expected<void> w = io::writeFileAtomic(m_folder / std::string(kInfoFile), j.dump(2) + "\n"); !w) {
        m_warnings.push_back(std::format("복구 정보를 쓰지 못했다 — {}", w.error().describe()));
    }
}

std::vector<std::string> AutosaveService::takeWarnings() {
    std::vector<std::string> out;
    out.swap(m_warnings);
    return out;
}

} // namespace sbx::editor::services
