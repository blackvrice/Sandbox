#pragma once
// sbx — 게임 프로젝트 도구 명령줄 (v0.2 M1-T4, ADR-0043). docs/18-PLATFORM.md 5장, docs/15-BUILD.md 7장.
//
//   sbx new <폴더> --id <gameId> [--name <이름>] [--from <시나리오>] [--content-root <d>] [--seed <n>]
//   sbx check <폴더>
//   sbx templates
//
// v0.4 패키지 관리자가 같은 도구에 init · add · remove · update · build · test 를 더한다 [계획].

#include <span>
#include <string>
#include <string_view>

#include "foundation/types/Error.hpp"

namespace sbx::tool {

inline constexpr int kExitOk = 0;
inline constexpr int kExitCheckFailed = 1; // check: 검사 오류
inline constexpr int kExitError = 2;       // 잘못된 인자 · 만들기 실패

enum class SbxCommand : u8 { Help, Version, New, Check, Templates };

struct SbxOptions {
    SbxCommand command = SbxCommand::Help;
    std::string dir;         // new · check 의 프로젝트 폴더
    std::string gameId;      // new --id (필수)
    std::string name;        // new --name (비면 gameId 의 마지막 마디)
    std::string from;        // new --from <시나리오> (비면 빈 월드)
    std::string contentRoot; // new --content-root (비면 빌드 때 정한 저장소의 content/)
    u64 seed = 1;            // new --seed
};

[[nodiscard]] Expected<SbxOptions> parseSbxOptions(std::span<const std::string_view> args);
[[nodiscard]] std::string sbxUsage();

} // namespace sbx::tool
