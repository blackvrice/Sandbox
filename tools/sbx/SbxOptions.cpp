#include "tools/sbx/SbxOptions.hpp"

#include <charconv>
#include <format>

namespace sbx::tool {

Expected<SbxOptions> parseSbxOptions(std::span<const std::string_view> args) {
    SbxOptions o;
    if (args.empty()) {
        return o; // 도움말
    }
    const std::string_view cmd = args[0];
    if (cmd == "-h" || cmd == "--help" || cmd == "help") {
        return o;
    }
    if (cmd == "--version") {
        o.command = SbxCommand::Version;
        return o;
    }
    if (cmd == "new") {
        o.command = SbxCommand::New;
    } else if (cmd == "check") {
        o.command = SbxCommand::Check;
    } else if (cmd == "templates") {
        o.command = SbxCommand::Templates;
    } else {
        return makeError(ErrorCode::InvalidArgument, std::format("모르는 명령 '{}' (new · check · templates)", cmd));
    }
    bool haveDir = false;
    for (usize i = 1; i < args.size(); ++i) {
        const std::string_view a = args[i];
        const auto value = [&]() -> Expected<std::string_view> {
            if (i + 1 >= args.size() || args[i + 1].empty()) {
                return makeError(ErrorCode::InvalidArgument, std::format("{} 에 값이 필요합니다", a));
            }
            return args[++i];
        };
        const bool isNew = o.command == SbxCommand::New;
        if (isNew && (a == "--id" || a == "--name" || a == "--from" || a == "--mode" || a == "--content-root")) {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            (a == "--id"     ? o.gameId
             : a == "--name" ? o.name
             : a == "--from" ? o.from
             : a == "--mode" ? o.mode
                             : o.contentRoot) = std::string(*v);
        } else if (isNew && a == "--seed") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            const auto [p, ec] = std::from_chars(v->data(), v->data() + v->size(), o.seed);
            if (ec != std::errc{} || p != v->data() + v->size()) {
                return makeError(ErrorCode::InvalidArgument, std::format("--seed 는 0 이상의 정수: '{}'", *v));
            }
        } else if (!a.starts_with("-") && !haveDir && o.command != SbxCommand::Templates) {
            o.dir = std::string(a);
            haveDir = true;
        } else {
            return makeError(ErrorCode::InvalidArgument, std::format("'{}' 를 알 수 없습니다 ({})", a, cmd));
        }
    }
    if ((o.command == SbxCommand::New || o.command == SbxCommand::Check) && !haveDir) {
        return makeError(ErrorCode::InvalidArgument, std::format("{} 에 프로젝트 폴더가 필요합니다", cmd));
    }
    if (o.command == SbxCommand::New && o.gameId.empty()) {
        return makeError(ErrorCode::InvalidArgument, "new 에 --id <gameId> 가 필요합니다 (예: studio.arena)");
    }
    return o;
}

std::string sbxUsage() {
    return "사용법: sbx <명령> [옵션]  — 게임 프로젝트 도구 (docs/18-PLATFORM.md 5장)\n"
           "  new <폴더> --id <gameId>   새 프로젝트 (빈 폴더에만). sandbox.project.json + worlds/main\n"
           "      --name <이름>          표시 이름 (기본: gameId 의 마지막 마디)\n"
           "      --from <시나리오>      템플릿 — 그 시나리오의 팩을 content/ 로 복사하고 첫 틱의 월드로 시작\n"
           "                             (없으면 빈 월드: 내장 콘텐츠 · 16 × 16 청크 · 일시정지)\n"
           "      --mode <GameMode id>   게임 모드 (매니페스트 schemaVersion 2 gameMode — 템플릿 콘텐츠에 있어야 "
           "한다)\n"
           "                             데모: --from ecosystem_small --mode eco.protect (토끼 지키기)\n"
           "      --content-root <d>     --from 의 팩을 찾을 곳 (기본: 저장소의 content/)\n"
           "      --seed <n>             월드 시드 (기본 1)\n"
           "  check <폴더>               프로젝트 검사 (매니페스트 · 엔진 범위 · 팩 · 시작 월드 열기)\n"
           "  templates                  --from 에 쓸 수 있는 시나리오 목록\n"
           "  --version                  버전\n"
           "실행: SandboxServer --world <폴더> · SandboxClient --world <폴더>\n"
           "종료 코드: 0 성공, 1 검사 오류 (check), 2 잘못된 인자 · 실패\n";
}

} // namespace sbx::tool
