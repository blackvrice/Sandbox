#include "core/scenarios/SaveMigration.hpp"

#include <algorithm>
#include <format>

#include "foundation/io/FileIo.hpp"

namespace sbx::scenario {

Expected<MigrationPlan> planSaveMigration(std::string_view saved, const SemVer& current,
                                          std::span<const project::MigrationStep> steps) {
    auto from = parseSemVer(saved);
    if (!from) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("세이브의 게임 버전 '{}' 이 SemVer 가 아니다 — {}", saved, from.error().message));
    }
    MigrationPlan plan;
    const auto cmp = compareSemVer(*from, current);
    if (cmp == 0) {
        return plan;
    }
    if (cmp > 0) {
        return makeError(
            ErrorCode::VersionMismatch,
            std::format("더 새 게임 버전 {} 의 세이브 — 이 게임 ({}) 으로는 열지 않는다 (옛 판으로 되돌린 게임)",
                        from->toString(), current.toString()));
    }
    if (steps.empty()) {
        plan.undeclared = true;
        return plan;
    }
    SemVer at = *from;
    // 단계마다 to > from 이라 체인은 늘 앞으로 간다 — 단계 수만큼이면 끝난다
    for (usize n = 0; n <= steps.size(); ++n) {
        const auto it =
            std::find_if(steps.begin(), steps.end(), [&](const project::MigrationStep& s) { return s.from == at; });
        if (it == steps.end()) {
            return makeError(
                ErrorCode::NotFound,
                std::format("게임 버전 {} 에서 시작하는 마이그레이션 단계가 없다 ({} 까지 — sandbox.project.json "
                            "migrations)",
                            at.toString(), current.toString()));
        }
        plan.steps.push_back(*it);
        at = it->to;
        const auto left = compareSemVer(at, current);
        if (left == 0) {
            return plan;
        }
        if (left > 0) {
            return makeError(ErrorCode::NotFound,
                             std::format("단계 {} → {} 이 게임 버전 {} 을 넘어간다", it->from.toString(),
                                         it->to.toString(), current.toString()));
        }
    }
    return makeError(ErrorCode::NotFound, "마이그레이션 체인이 끝나지 않는다");
}

Expected<std::filesystem::path> backupSave(const std::filesystem::path& dir, std::string_view saved) {
    namespace fs = std::filesystem;
    const fs::path target = dir.parent_path() / std::format("{}.bak-{}", dir.filename().string(), saved);
    std::error_code ec;
    if (fs::exists(target, ec)) {
        return target; // 처음 백업이 가장 옛것 — 덮지 않는다
    }
    fs::copy(dir, target, fs::copy_options::recursive, ec);
    if (ec) {
        const std::string why = ec.message();
        fs::remove_all(target, ec); // 반쯤 만든 백업은 남기지 않는다
        return makeError(ErrorCode::IoError, std::format("세이브 백업 실패 — {}", why), io::displayPath(target));
    }
    return target;
}

Expected<void> applyMigration(sim::SimulationWorld& world, std::span<const project::MigrationStep> steps) {
    for (const project::MigrationStep& s : steps) {
        if (auto r = world.runScriptMigration(s.system); !r) {
            return makeError(r.error().code, std::format("마이그레이션 {} → {} ({}) 실패 — {}", s.from.toString(),
                                                         s.to.toString(), s.system, r.error().message));
        }
    }
    return {};
}

} // namespace sbx::scenario
