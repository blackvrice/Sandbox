#pragma once
// sbx — 게임 프로젝트 도구 명령줄 (v0.2 M1-T4, ADR-0043). docs/18-PLATFORM.md 5장, docs/15-BUILD.md 7장.
//
//   sbx new <폴더> --id <gameId> [--name <이름>] [--from <시나리오>] [--mode <GameMode id>] [--content-root <d>]
//           [--seed <n>]
//   sbx check <폴더 | 패키지.sbpkg>
//   sbx build <폴더> [--out <파일.sbpkg>]          (v0.2 M5-T2 — 게임 패키지, ADR-0054)
//   sbx templates
//   sbx sdk [<폴더>]                               (v0.3 M2-T2 — SandboxScript SDK sandbox.d.ts 쓰기)
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

enum class SbxCommand : u8 { Help, Version, New, Check, Templates, Build, Sdk, Digest };

struct SbxOptions {
    SbxCommand command = SbxCommand::Help;
    std::string dir;         // new · check 의 프로젝트 폴더 · sdk 의 쓸 폴더 (sdk 는 비면 ".")
    std::string gameId;      // new --id (필수)
    std::string name;        // new --name (비면 gameId 의 마지막 마디)
    std::string from;        // new --from <시나리오> (비면 빈 월드)
    std::string contentRoot; // new --content-root (비면 빌드 때 정한 저장소의 content/)
    u64 seed = 1;            // new --seed
    std::string mode;        // new --mode <GameMode id> (비면 모드 없음 — v0.2 M3-T3)
    std::string out;         // build --out <파일> (비면 <프로젝트>/build/<gameId>-<version>.sbpkg — v0.2 M5-T2)
    bool cache = false;      // digest --cache (사용자 캐시에 넣는다 — v0.4 M2-T1)
    std::string cacheDir;    // digest --cache-dir <폴더> (비면 사용자 캐시)
};

[[nodiscard]] Expected<SbxOptions> parseSbxOptions(std::span<const std::string_view> args);
[[nodiscard]] std::string sbxUsage();

} // namespace sbx::tool
