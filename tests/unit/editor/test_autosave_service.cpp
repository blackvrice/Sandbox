// Studio 자동 저장 · 크래시 복구 (v0.8 M6-T7 — editor/services/AutosaveService, ADR-0113): 복구본 폴더 (프로젝트마다 ·
// 사용자 폴더), 주기 · 지금 저장 · 기다리는 동안 멈춤, recovery.json (tick · 해시 · clean), 깨끗이 끝남 = 다음에 제안
// 없음, 깨끗이 끝나지 않음 = 복구 제안 (그동안 자동 저장 멈춤) · 복구 (worlds/recovered-<n> — 있는 이름 건너뜀) ·
// 버리기, 저장하지 않은 문서 (상한)
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <string>

#include "editor/services/AutosaveService.hpp"
#include "editor/services/DocumentService.hpp"
#include "foundation/io/FileIo.hpp"

using namespace sbx;
using namespace sbx::editor::services;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view name) {
        std::random_device rd;
        path = fs::temp_directory_path() / std::format("sbx_autosave_{}_{:08x}", name, rd());
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

// 세션 (로컬 서버) 이 세이브한 것처럼 — world/world.json 하나 (tick · worldHash 는 세이브 형식 그대로)
void fakeWorldSave(const AutosaveService& a, u64 tick, u64 hash, std::string_view marker = "") {
    fs::create_directories(a.worldDir());
    std::ofstream(a.worldDir() / "world.json", std::ios::binary)
        << std::format(R"({{"tick": {}, "worldHash": "0x{:016x}", "marker": "{}"}})", tick, hash, marker);
}

} // namespace

TEST_SUITE("editor") {
    TEST_CASE("autosave: 복구본 폴더 — 프로젝트마다 · 같은 프로젝트는 같은 폴더, 꺼진 서비스는 아무것도 하지 않는다") {
        TempDir t("folder");
        const fs::path a = AutosaveService::folderFor(t.path / "rec", t.path / "A");
        CHECK(a == AutosaveService::folderFor(t.path / "rec", t.path / "A" / "."));
        CHECK(a != AutosaveService::folderFor(t.path / "rec", t.path / "B"));
        CHECK(a.parent_path() == t.path / "rec");
        CHECK(a.filename().string().size() == 16);

        AutosaveService off({});
        CHECK_FALSE(off.enabled());
        off.begin(t.path / "A", 0);
        CHECK_FALSE(off.due(1e9));
        off.requestNow();
        CHECK_FALSE(off.due(0));
        off.finishClean();
        CHECK_FALSE(fs::exists(t.path / "rec"));
    }

    TEST_CASE("autosave: 주기 · 지금 저장 · 기다리는 동안 멈춤 · recovery.json · 깨끗이 끝나면 다음에 제안 없음") {
        TempDir t("cycle");
        const fs::path project = t.path / "Game";
        fs::create_directories(project);
        AutosaveService a(t.path / "rec", 180);
        a.begin(project, 0);
        CHECK_FALSE(a.pending().has_value());
        CHECK_FALSE(a.due(100));
        CHECK(a.due(180));
        a.markRequested(180);
        CHECK_FALSE(a.due(1000)); // 앞 세이브를 기다린다
        fakeWorldSave(a, 42, 0xabcdef);
        a.onWorldSaved(true, 42, 0xabcdef, 181);
        CHECK(a.saves() == 1);
        const std::optional<RecoveryInfo> r = a.read();
        REQUIRE(r.has_value());
        CHECK(r->tick == 42);
        CHECK(r->worldHash == 0xabcdef);
        CHECK_FALSE(r->clean);
        CHECK(r->hasWorld);
        CHECK(r->project == fs::weakly_canonical(project));
        CHECK(r->savedAt.size() == 20); // "YYYY-MM-DDTHH:MM:SSZ"
        CHECK_FALSE(a.due(200));
        CHECK(a.due(361));
        a.requestNow(); // 플레이 시작 전
        CHECK(a.due(200));
        a.markRequested(200);
        a.onWorldSaved(false, 0, 0, 201); // 실패 — 경고, 다음 주기에
        CHECK(a.takeWarnings().size() == 1);
        CHECK(a.read()->tick == 42); // 앞 복구본 그대로

        a.finishClean();
        CHECK(a.read()->clean);
        AutosaveService next(t.path / "rec", 180);
        next.begin(project, 0);
        CHECK_FALSE(next.pending().has_value());  // 깨끗이 끝났다
        CHECK_FALSE(fs::exists(next.worldDir())); // 앞 실행의 복구본은 지웠다
        REQUIRE(next.read().has_value());
        CHECK_FALSE(next.read()->clean); // 이 실행을 열며 먼저 적는다 — 첫 저장 전에 죽어도 안다
        CHECK_FALSE(next.read()->hasWorld);

        AutosaveService manual(t.path / "rec2", 0); // 주기 끔 — requestNow 만
        manual.begin(project, 0);
        CHECK_FALSE(manual.due(1e6));
        manual.requestNow();
        CHECK(manual.due(0));
    }

    TEST_CASE("autosave: 깨끗이 끝나지 않음 → 복구 제안 (그동안 멈춤) · 복구 (recovered-<n>, 덮어쓰지 않음) · 버리기") {
        TempDir t("crash");
        const fs::path project = t.path / "Game";
        fs::create_directories(project / "worlds" / "main");
        {
            AutosaveService a(t.path / "rec", 180);
            a.begin(project, 0);
            a.markRequested(180);
            fakeWorldSave(a, 70, 0x1111, "old");
            a.onWorldSaved(true, 70, 0x1111, 181);
            // 다음 세이브는 끝났지만 (world.json 이 바뀜) 앱이 결과를 받기 전에 죽었다 — tick · 해시는 세이브의 것이
            // 맞다
            fakeWorldSave(a, 77, 0x1234, "edited");
        } // finishClean 없이 — 강제 종료
        AutosaveService b(t.path / "rec", 180);
        b.begin(project, 1000);
        REQUIRE(b.pending().has_value());
        CHECK(b.pending()->tick == 77);
        CHECK(b.pending()->worldHash == 0x1234);
        CHECK(b.pending()->hasWorld);
        CHECK_FALSE(b.due(1e6)); // 고를 때까지 새 자동 저장은 멈춘다 (복구본을 덮지 않는다)
        b.finishClean();         // 고르지 않고 끝내도 제안은 남는다
        CHECK_FALSE(b.read()->clean);

        const Expected<std::string> name = b.restore();
        REQUIRE_MESSAGE(name.has_value(), name.error().describe());
        CHECK(*name == "recovered-1");
        CHECK(io::readFile(project / "worlds" / "recovered-1" / "world.json")->find("edited") != std::string::npos);
        CHECK(fs::exists(project / "worlds" / "main")); // 프로젝트의 것은 그대로
        CHECK(*b.restore() == "recovered-2");           // 있는 이름은 건너뛴다

        b.discard(2000);
        CHECK_FALSE(b.pending().has_value());
        CHECK_FALSE(fs::exists(AutosaveService::folderFor(t.path / "rec", project)));
        CHECK(b.restore().error().code == ErrorCode::NotFound);
        CHECK_FALSE(b.due(2100));
        CHECK(b.due(2180)); // 다시 주기대로
    }

    TEST_CASE("autosave: 저장하지 않은 문서의 글 — 남기기 · 목록 · 상한을 넘는 것은 건너뛰고 경고") {
        TempDir t("docs");
        const fs::path project = t.path / "Game";
        fs::create_directories(project);
        std::ofstream(project / "a.json", std::ios::binary) << R"({"a": 1})";
        std::ofstream(project / "big.txt", std::ios::binary) << "x";
        std::ofstream(project / "same.txt", std::ios::binary) << "same";
        DocumentService docs;
        const DocId a = *docs.open(project / "a.json");
        const DocId big = *docs.open(project / "big.txt");
        (void)*docs.open(project / "same.txt"); // 고치지 않음 — 남기지 않는다
        REQUIRE(docs.setText(a, R"({"a": 2})").has_value());
        REQUIRE(docs.setText(big, std::string(AutosaveService::kMaxDocumentBytes + 1, 'y')).has_value());

        AutosaveService s(t.path / "rec", 180);
        s.begin(project, 0);
        CHECK(s.saveDocuments(docs) == 1);
        const std::vector<std::string> w = s.takeWarnings();
        REQUIRE(w.size() == 1);
        CHECK(w.front().find("big.txt") != std::string::npos);
        const std::optional<RecoveryInfo> r = s.read();
        REQUIRE(r.has_value());
        REQUIRE(r->documents.size() == 1);
        CHECK(r->documents.front().path.filename() == "a.json");
        CHECK(*io::readFile(r->documents.front().file) == R"({"a": 2})");
        CHECK_FALSE(r->hasWorld);
        // 문서만 남긴 채 강제 종료 — 다음에 제안 (월드 없음 — 복구는 NotFound)
        AutosaveService next(t.path / "rec", 180);
        next.begin(project, 0);
        REQUIRE(next.pending().has_value());
        CHECK(next.pending()->documents.size() == 1);
        CHECK(next.restore().error().code == ErrorCode::NotFound);
    }
}
