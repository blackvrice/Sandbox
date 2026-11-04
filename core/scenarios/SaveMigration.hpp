#pragma once
// 세이브 게임 버전 마이그레이션 (v0.6 NG-04 — ADR-0098 2, docs/26-ASSET-PIPELINE.md 8장 · docs/09-SERIALIZATION.md
// 9장).
//
//   세이브의 게임 버전 (world.json "game".version) ↔ 지금 게임 버전 (매니페스트 version):
//     같다                          그대로
//     세이브가 더 새것              거절 — 옛 게임으로 새 세이브를 읽지 않는다 (되돌린 게임)
//     세이브가 옛것 · 단계 없음      그대로 열고 알린다 (지금까지의 동작 — migrations 를 쓰지 않는 게임)
//     세이브가 옛것 · 단계 있음      from = 세이브 버전에서 시작해 to 를 따라 지금 버전까지 — 빠진 단계 · 넘어가는
//     단계면 거절
//   적용 전에 세이브 폴더를 옆에 복사 (<이름>.bak-<세이브 버전> — 이미 있으면 그대로), 단계마다 스크립트 onMigrate 를
//   한 번 (SimulationWorld::runScriptMigration — 결정적). 실패하면 그 오류 — 부른 쪽은 연 월드를 버린다 (세이브 파일은
//   바꾸지 않았다)

#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

#include "core/project/GameProject.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "foundation/types/Error.hpp"
#include "foundation/version/SemVer.hpp"

namespace sbx::scenario {

struct MigrationPlan {
    std::vector<project::MigrationStep> steps; // 적용할 차례
    bool undeclared = false;                   // 옛 세이브인데 게임에 단계가 없다 — 그대로 열고 알린다
};

// saved = 세이브의 게임 버전 문자열. 더 새 세이브 = VersionMismatch, 빠진 · 넘어가는 단계 = NotFound, SemVer 아님 =
// InvalidArgument
[[nodiscard]] Expected<MigrationPlan> planSaveMigration(std::string_view saved, const SemVer& current,
                                                        std::span<const project::MigrationStep> steps);

// dir 옆에 "<이름>.bak-<saved>" 복사 (재귀). 이미 있으면 그대로 둔다 (처음 백업을 덮지 않는다). 백업 폴더 경로
[[nodiscard]] Expected<std::filesystem::path> backupSave(const std::filesystem::path& dir, std::string_view saved);

// 단계마다 world.runScriptMigration(step.system). 실패하면 어느 단계인지 붙인 오류 (그 뒤 단계는 하지 않는다)
[[nodiscard]] Expected<void> applyMigration(sim::SimulationWorld& world, std::span<const project::MigrationStep> steps);

} // namespace sbx::scenario
