#include "core/project/Diagnostics.hpp"

#include <algorithm>
#include <format>

#include <nlohmann/json.hpp>

#include "core/package/GamePackage.hpp"
#include "core/project/GameProject.hpp"
#include "core/replay/WorldHash.hpp"
#include "foundation/diag/Report.hpp"
#include "foundation/io/FileIo.hpp"

namespace sbx::project {

namespace fs = std::filesystem;

namespace {

constexpr usize kMaxContentItems = 8;

std::string_view levelName(DiagLevel level) {
    switch (level) {
    case DiagLevel::Info:
        return "정보";
    case DiagLevel::Ok:
        return "통과";
    case DiagLevel::Warning:
        return "경고";
    case DiagLevel::Error:
        return "오류";
    }
    return "?";
}

DiagLevel levelOf(const content::ContentIssue& i) {
    return i.severity == content::ContentIssue::Severity::Error ? DiagLevel::Error : DiagLevel::Warning;
}

struct Builder {
    DiagnosticReport& r;
    void add(std::string area, DiagLevel level, std::string text, std::string hint = {}) {
        r.items.push_back({std::move(area), level, std::move(text), std::move(hint)});
    }
};

// 지원 매트릭스 (docs/20-CREATOR-ALPHA.md 7.8) — 이 개발 PC 에서 시험한 것만 "지원"
void diagnoseSystem(Builder& b, std::string_view system) {
    if (system == "Windows") {
        b.add("환경", DiagLevel::Ok, "Windows x64 — 지원 (D3D12, 시험한 환경)");
    } else if (system == "Linux") {
        b.add("환경", DiagLevel::Warning,
              "Linux — 검증 안 됨 (빌드 경로 · Vulkan 은 있으나 이 릴리스에서 시험하지 않았다)",
              "문제가 나면 sbx report 의 묶음과 함께 알린다");
    } else if (system == "Darwin") {
        b.add("환경", DiagLevel::Warning,
              "macOS — 검증 안 됨 (빌드 경로 · Metal 은 있으나 이 릴리스에서 시험하지 않았다)",
              "문제가 나면 sbx report 의 묶음과 함께 알린다");
    } else {
        b.add("환경", DiagLevel::Warning, std::format("{} — 지원 매트릭스에 없는 OS", system.empty() ? "?" : system));
    }
}

// 범위가 왜 이 엔진을 받지 않나 — 아래 경계 (>= · > · =) 가 엔진보다 크면 더 새 엔진이, 위 경계 (< · <=) 를 넘었으면
// 더 옛 엔진 (또는 게임을 고칠 일) 이다
void diagnoseEngineRange(Builder& b, const VersionRange& range, const SemVer& engine) {
    if (range.contains(engine)) {
        b.add("엔진 범위", DiagLevel::Ok,
              std::format("이 엔진 {} 는 게임의 범위 \"{}\" 안이다", engine.toString(), range.text));
        return;
    }
    bool needNewer = false;
    std::string lower;
    for (const VersionComparator& c : range.all) {
        const auto order = compareSemVer(engine, c.version);
        const bool below = (c.op == VersionComparator::Op::Ge && order < 0) ||
                           (c.op == VersionComparator::Op::Gt && order <= 0) ||
                           (c.op == VersionComparator::Op::Eq && order < 0);
        if (below) {
            needNewer = true;
            lower = c.version.toString();
        }
    }
    const std::string text =
        std::format("이 게임은 엔진 \"{}\" 이 필요한데 이 엔진은 {} 이다", range.text, engine.toString());
    if (needNewer) {
        b.add("엔진 범위", DiagLevel::Error, text,
              std::format("엔진 {} 이상을 받는다 (게임이 이 엔진보다 새 기능을 쓴다)", lower));
    } else {
        b.add("엔진 범위", DiagLevel::Error, text,
              "이 게임은 옛 엔진용이다 — 범위 안의 엔진을 쓰거나, 만든 사람이 이 엔진으로 시험한 뒤 매니페스트 "
              "engine 범위를 넓힌다");
    }
}

enum class Area : u8 { Manifest, Engine, Dependencies, Content, Tools };

Area areaOf(const content::ContentIssue& i) {
    if (i.rule == "P1" || i.rule == "P2") {
        return Area::Manifest;
    }
    if (i.rule == "P3") {
        return i.pointer == "/engine" && i.file == kManifestFile ? Area::Engine : Area::Manifest;
    }
    if (i.rule == "P7" || i.rule == "P8") {
        return Area::Dependencies;
    }
    if (i.rule == "P6") {
        return Area::Tools;
    }
    return Area::Content; // V* · S* · P4 · P5 · IO · JSON
}

void diagnoseProject(Builder& b, const fs::path& dir, bool fromPackage, const DiagnoseOptions& o,
                     const ecs::ComponentCatalog& catalog) {
    const ProjectReport r = validateProject(dir, o.engine, catalog);
    std::vector<const content::ContentIssue*> manifest;
    std::vector<const content::ContentIssue*> deps;
    std::vector<const content::ContentIssue*> contentIssues;
    std::vector<const content::ContentIssue*> tools;
    for (const content::ContentIssue& i : r.issues) {
        switch (areaOf(i)) {
        case Area::Manifest:
            manifest.push_back(&i);
            break;
        case Area::Engine: // 문장은 아래에서 범위로 다시 만든다
            break;
        case Area::Dependencies:
            deps.push_back(&i);
            break;
        case Area::Content:
            contentIssues.push_back(&i);
            break;
        case Area::Tools:
            tools.push_back(&i);
            break;
        }
    }

    // 매니페스트
    for (const content::ContentIssue* i : manifest) {
        b.add("매니페스트", levelOf(*i), i->describe(),
              fromPackage ? "패키지를 만든 사람에게 알린다"
                          : "sandbox.project.json 을 고친다 (docs/18-PLATFORM.md 5.2)");
    }
    if (!r.manifestRead) {
        if (manifest.empty()) {
            b.add("매니페스트", DiagLevel::Error, "매니페스트를 읽지 못했다");
        }
        return; // 나머지는 매니페스트가 있어야 본다
    }
    if (manifest.empty()) {
        b.add("매니페스트", DiagLevel::Ok,
              std::format("게임 {} {} (\"{}\"){}", r.manifest.gameId, r.manifest.version.toString(), r.manifest.name,
                          r.manifest.gameMode.empty() ? "" : " · 게임 모드 " + r.manifest.gameMode));
    }

    // 엔진 범위 — 문장은 범위에서 다시 만든다 (P3 /engine 의 문구 대신 — 어느 쪽 엔진이 필요한지)
    diagnoseEngineRange(b, r.manifest.engine, o.engine);

    // 의존 · 설치
    for (const content::ContentIssue* i : deps) {
        b.add("의존 · 설치", levelOf(*i), i->describe(),
              fromPackage       ? "패키지를 만든 사람에게 알린다 (패키지에 설치한 팩이 빠졌거나 다르다)"
              : i->rule == "P8" ? std::format("sbx install {} (잠금 · .sandbox/packages 를 매니페스트에 맞춘다)",
                                              dir.generic_string())
                                : "매니페스트 dependencies 의 범위 · 팩의 version 을 맞춘다");
    }
    if (deps.empty()) {
        b.add("의존 · 설치", DiagLevel::Ok,
              r.dependencies.empty() ? std::string("의존 없음")
                                     : std::format("의존 {} — 범위 · 잠금 · 설치가 맞다", r.dependencies.size()));
    }

    // 콘텐츠
    for (usize k = 0; k < contentIssues.size() && k < kMaxContentItems; ++k) {
        b.add("콘텐츠", levelOf(*contentIssues[k]), contentIssues[k]->describe());
    }
    if (contentIssues.size() > kMaxContentItems) {
        b.add("콘텐츠", DiagLevel::Info, std::format("그 밖 {} 개", contentIssues.size() - kMaxContentItems),
              std::format("sbx check {} 로 전부 본다", dir.generic_string()));
    }
    if (contentIssues.empty()) {
        b.add("콘텐츠", DiagLevel::Ok,
              std::format("팩 {} · Prefab {} · 스크립트 시스템 {} · contentHash {}", loadedPacks(r.manifest).size(),
                          r.prefabs, r.scriptSystems.size(), replay::formatHash(r.contentHash)));
    }

    // 시작 월드
    if (r.worldOpened) {
        b.add("시작 월드", DiagLevel::Ok,
              std::format("{} 열림 — tick {}, 엔티티 {}",
                          r.manifest.entryWorld.empty() ? "(빈 월드)" : r.manifest.entryWorld, r.worldTick,
                          r.worldEntities));
    } else if (r.errorCount() > 0) {
        b.add("시작 월드", DiagLevel::Info, "열지 않았다 — 위 오류를 먼저 고친다");
    } else {
        b.add("시작 월드", DiagLevel::Error, "시작 월드를 열지 못했다");
    }

    // 도구
    for (const content::ContentIssue* i : tools) {
        b.add("도구", levelOf(*i), i->describe(),
              fromPackage ? "" : std::format("sbx sdk {} 로 SDK 선언을 다시 쓴다", dir.generic_string()));
    }
}

void diagnosePackage(Builder& b, const fs::path& file, const DiagnoseOptions& o, const ecs::ComponentCatalog& catalog) {
    constexpr std::string_view kRebuild = "다시 받거나, 만든 사람에게 sbx build 로 다시 만들어 달라고 한다";
    auto index = package::readPackageIndex(file);
    if (!index) {
        b.add("패키지", DiagLevel::Error,
              std::format("패키지 머리 · 파일 목록을 읽을 수 없다 — 잘렸거나 .sbpkg 가 아니다 ({})",
                          index.error().message),
              std::string(kRebuild));
        return;
    }
    auto extracted = package::extractPackage(file, o.packageCache);
    if (!extracted) {
        b.add("패키지", DiagLevel::Error,
              std::format("패키지 {} {} 의 내용이 목록과 다르다 — 만든 뒤 깨졌거나 바뀌었다 ({})", index->gameId,
                          index->version, extracted.error().message),
              std::string(kRebuild));
        // 풀지 못해도 엔진 범위는 목록에서 본다 — 깨진 패키지가 다른 엔진용이기도 한지
        if (auto range = parseVersionRange(index->engine)) {
            diagnoseEngineRange(b, *range, o.engine);
        }
        return;
    }
    const package::PackageIndex& ix = extracted->index;
    b.add("패키지", DiagLevel::Ok,
          std::format("패키지 {} {} — 형식 {} · 파일 {} · digest sha256:{:.16}… (파일마다 SHA-256 확인함)", ix.gameId,
                      ix.version, ix.format, ix.files.size(), ix.digest));
    if (ix.format < 2) {
        b.add("패키지", DiagLevel::Warning, std::format("형식 {} 패키지 — 라이선스 요약 (팩 목록) 이 없다", ix.format),
              "sbx build 로 다시 만들면 형식 2 (팩마다 라이선스 · digest)");
    }
    for (const package::PackageIndex::Pack& p : ix.packs) {
        if (p.license.empty()) {
            b.add("패키지", DiagLevel::Warning, std::format("팩 {} 의 라이선스가 없다", p.id),
                  "팩을 만든 사람에게 pack.json license (SPDX) 를 묻는다");
        }
    }
    diagnoseProject(b, extracted->dir, true, o, catalog);
}

// '/' 로만 (Windows 에서 root / 이름 이 섞이지 않게)
std::string genericPath(const fs::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

std::string jsonString(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

} // namespace

usize DiagnosticReport::errorCount() const noexcept {
    return static_cast<usize>(
        std::ranges::count_if(items, [](const DiagItem& i) { return i.level == DiagLevel::Error; }));
}

usize DiagnosticReport::warningCount() const noexcept {
    return static_cast<usize>(
        std::ranges::count_if(items, [](const DiagItem& i) { return i.level == DiagLevel::Warning; }));
}

bool DiagnosticReport::ok() const noexcept {
    return errorCount() == 0;
}

DiagnosticReport diagnose(const fs::path& target, const DiagnoseOptions& o, const ecs::ComponentCatalog& catalog) {
    DiagnosticReport r;
    r.target = genericPath(target);
    Builder b{r};
    b.add("엔진", DiagLevel::Info,
          o.engineBuild.empty() ? std::format("엔진 {}", o.engine.toString())
                                : std::format("엔진 {} ({})", o.engine.toString(), o.engineBuild));
    diagnoseSystem(b, o.system);
    std::error_code ec;
    if (!fs::exists(target, ec)) {
        b.add("대상", DiagLevel::Error, std::format("{} 가 없다", r.target),
              "게임 프로젝트 폴더 (sandbox.project.json 이 있는) 나 패키지 (.sbpkg) 를 준다");
        return r;
    }
    if (package::isPackageFile(target)) {
        diagnosePackage(b, target, o, catalog);
    } else if (isProjectFolder(target)) {
        diagnoseProject(b, target, false, o, catalog);
    } else {
        b.add(
            "대상", DiagLevel::Error,
            std::format("{} 는 게임 프로젝트도 패키지도 아니다 — {} 이 없고 .sbpkg 도 아니다", r.target, kManifestFile),
            "sbx new 로 만든 폴더나 sbx build 로 만든 .sbpkg 를 준다");
    }
    return r;
}

std::string formatDiagnosticReport(const DiagnosticReport& report) {
    std::string out = std::format("진단 {}\n", report.target);
    for (const DiagItem& i : report.items) {
        out += std::format("  [{}] {}  {}\n", i.area, levelName(i.level), i.text);
        if (!i.hint.empty()) {
            out += std::format("        → {}\n", i.hint);
        }
    }
    out += std::format("진단: 오류 {} · 경고 {}\n", report.errorCount(), report.warningCount());
    return out;
}

Expected<ReportBundle> readReportBundle(const fs::path& dir) {
    const auto text = io::readFile(dir / "report.json");
    if (!text) {
        return makeError(ErrorCode::NotFound, "보고 묶음이 아니다 — report.json 을 읽을 수 없다", io::displayPath(dir));
    }
    const nlohmann::json j = nlohmann::json::parse(*text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return makeError(ErrorCode::ParseError, "report.json 이 JSON 객체가 아니다", io::displayPath(dir));
    }
    ReportBundle b;
    b.dir = dir;
    const auto format = j.find("format");
    if (format == j.end() || !format->is_number_unsigned()) {
        return makeError(ErrorCode::ParseError, "report.json 에 format 이 없다", io::displayPath(dir));
    }
    b.format = format->get<u32>();
    if (b.format > diag::kReportFormat) {
        return makeError(ErrorCode::VersionMismatch,
                         std::format("더 새 보고 형식 {} 이다 (이 sbx 는 {} 까지 읽는다) — 새 sbx 로 본다", b.format,
                                     diag::kReportFormat),
                         io::displayPath(dir));
    }
    b.app = jsonString(j, "app");
    b.reason = jsonString(j, "reason");
    b.time = jsonString(j, "time");
    b.version = jsonString(j, "version");
    b.commit = jsonString(j, "commit");
    b.toolchain = jsonString(j, "toolchain");
    b.system = jsonString(j, "system");
    const auto strings = [&](const char* key, std::vector<std::string>& out) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_array()) {
            for (const auto& v : *it) {
                if (v.is_string()) {
                    out.push_back(v.get<std::string>());
                }
            }
        }
    };
    strings("args", b.args);
    strings("stack", b.stack);
    if (const auto it = j.find("context"); it != j.end() && it->is_object()) {
        for (const auto& [k, v] : it->items()) {
            b.context.emplace_back(k, v.is_string() ? v.get<std::string>() : v.dump());
        }
    }
    std::vector<std::string> files;
    strings("files", files);
    for (const std::string& f : files) {
        std::error_code ec;
        const u64 size = fs::file_size(dir / f, ec);
        if (ec) {
            b.missingFiles.push_back(f);
        } else {
            b.files.emplace_back(f, size);
        }
    }
    if (const auto log = io::readFile(dir / "log.txt")) {
        usize start = 0;
        while (start < log->size()) {
            const usize end = log->find('\n', start);
            const usize stop = end == std::string::npos ? log->size() : end;
            b.logLines.push_back(log->substr(start, stop - start));
            start = stop + 1;
        }
    }
    return b;
}

std::vector<fs::path> listReportBundles(const fs::path& root) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        std::error_code ec2;
        if (e.is_directory(ec2) && fs::exists(e.path() / "report.json", ec2)) {
            out.push_back(e.path());
        }
    }
    std::ranges::sort(out, [](const fs::path& a, const fs::path& b) { return a.filename() > b.filename(); });
    return out;
}

std::string formatReportBundle(const ReportBundle& b, usize logLines, std::string_view engineVersion) {
    std::string out = std::format("보고 {}\n", genericPath(b.dir));
    out += std::format("  앱 {} · 시각 {} · 버전 {} (commit {}) · {} · {}\n", b.app, b.time, b.version, b.commit,
                       b.toolchain, b.system);
    out += std::format("  이유 {}\n", b.reason);
    if (!b.args.empty()) {
        std::string args;
        for (const std::string& a : b.args) {
            args += (args.empty() ? "" : " ") + a;
        }
        out += std::format("  명령줄 {}\n", args);
    }
    for (const auto& [k, v] : b.context) {
        out += std::format("  문맥 {} = {}\n", k, v);
    }
    if (b.stack.empty()) {
        out += "  콜스택 없음 (이 종류의 충돌 · 이 OS 는 콜스택을 남기지 않았다)\n";
    } else {
        out += std::format("  콜스택 ({} 프레임):\n", b.stack.size());
        for (const std::string& f : b.stack) {
            out += std::format("    {}\n", f);
        }
    }
    const usize from = b.logLines.size() > logLines ? b.logLines.size() - logLines : 0;
    out += std::format("  마지막 로그 ({} / {} 줄):\n", b.logLines.size() - from, b.logLines.size());
    for (usize i = from; i < b.logLines.size(); ++i) {
        out += std::format("    {}\n", b.logLines[i]);
    }
    std::string files;
    bool dump = false;
    for (const auto& [name, size] : b.files) {
        files += std::format("{}{} ({} 바이트)", files.empty() ? "" : " · ", name, size);
        dump = dump || name == "crash.dmp";
    }
    out += std::format("  파일 {}\n", files.empty() ? std::string("(없음)") : files);
    for (const std::string& m : b.missingFiles) {
        out += std::format("  빠진 파일 {} (report.json 에 적혔는데 없다)\n", m);
    }
    out += "  report.json · log.txt 는 민감정보 (비밀 · 사용자 폴더 · 사용자 이름 · IP · 전자우편) 를 걸렀다\n";
    if (dump) {
        out += "  주의: crash.dmp 에는 프로세스 메모리 (스택 · 월드 · 채팅 · 열쇠) 가 걸러지지 않고 들어 있다 — 믿는 "
               "사람에게만 보낸다\n";
        out +=
            "  덤프 열기: Visual Studio 나 WinDbg 로 crash.dmp (같은 커밋의 실행 파일 · PDB 가 있어야 기호가 보인다)\n";
    }
    if (!b.version.empty() && b.version != engineVersion) {
        out += std::format("  이 sbx 는 {} 이다 — 묶음은 {} 에서 났다 (기호 · 줄 번호는 그 버전의 빌드로 본다)\n",
                           engineVersion, b.version);
    }
    return out;
}

} // namespace sbx::project
