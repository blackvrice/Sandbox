#pragma once
// 진단 (v0.5 M1-T2 — docs/20-CREATOR-ALPHA.md 7.2 · 7.3). sbx doctor · sbx report 가 쓰고, 클라이언트 오류 화면이 같은
// 문장을 보일 자리 [계획].
//
//   diagnose(<프로젝트 | 패키지.sbpkg>)  항목마다 한 줄 문장 + 고치는 법. 영역 순서:
//     엔진        이 엔진의 버전 · 커밋 · 툴체인 (정보)
//     환경        지원 매트릭스 판정 (Windows = 지원, Linux · macOS = 검증 안 됨 — 7.8)
//     패키지      .sbpkg 일 때: 머리 · 목록 (못 읽으면 "잘렸거나 .sbpkg 가 아니다"),
//                 파일마다 SHA-256 (다르면 "깨졌다"), 형식 (1 이면 라이선스 요약 없음 — 경고), 팩마다 라이선스
//     매니페스트  P1 · P2 · P3 (engine 밖)
//     엔진 범위   P3 /engine — "이 게임은 엔진 R 이 필요한데 이 엔진은 X" + 새 엔진 · 옛 엔진 어느 쪽인지
//     의존 · 설치 P7 · P8 (잠금 · .sandbox/packages — sbx install)
//     콘텐츠      V* · S* · P4 · P5 · IO · JSON (처음 8 개 + 나머지 수 — 전부는 sbx check)
//     시작 월드   열었나 (tick · 엔티티)
//     도구        P6 (SDK · tsconfig)
//   검사 자체는 validateProject · extractPackage 그대로 — 진단은 그 결과를 영역으로 묶고 문장을 붙인다.
//
//   readReportBundle(<묶음 폴더>)  foundation/diag 가 쓴 report.json · log.txt 를 읽는다 (형식 1). 덤프 (crash.dmp) 는
//   읽지 않고 크기만 — 보낼 때 메모리가 든다는 것을 formatReportBundle 이 알린다.

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "foundation/types/Error.hpp"
#include "foundation/version/SemVer.hpp"

namespace sbx::ecs {
class ComponentCatalog;
}

namespace sbx::project {

enum class DiagLevel : u8 { Info, Ok, Warning, Error };

struct DiagItem {
    std::string area; // "엔진" · "환경" · "패키지" · "매니페스트" · "엔진 범위" · "의존 · 설치" · "콘텐츠" · …
    DiagLevel level = DiagLevel::Info;
    std::string text; // 한 줄 문장
    std::string hint; // 고치는 법 (비면 없음)
};

struct DiagnosticReport {
    std::string target; // 보인 이름 (경로)
    std::vector<DiagItem> items;

    [[nodiscard]] usize errorCount() const noexcept;
    [[nodiscard]] usize warningCount() const noexcept;
    [[nodiscard]] bool ok() const noexcept; // 오류가 없다
};

struct DiagnoseOptions {
    SemVer engine;                      // 이 엔진의 버전 (BuildInfo)
    std::string engineBuild;            // "commit 2d84a7f · windows-msvc" (정보 줄 — 비면 버전만)
    std::string system;                 // CMAKE_SYSTEM_NAME ("Windows" · "Linux" · "Darwin")
    std::filesystem::path packageCache; // .sbpkg 를 풀 곳 (package::defaultPackageCache)
};

[[nodiscard]] DiagnosticReport diagnose(const std::filesystem::path& target, const DiagnoseOptions& options,
                                        const ecs::ComponentCatalog& catalog);

// "  [영역] 수준  문장\n        → 고치는 법\n" … + 끝 줄 "진단: 오류 n · 경고 n"
[[nodiscard]] std::string formatDiagnosticReport(const DiagnosticReport& report);

// 보고 묶음 (foundation/diag — 형식 1)
struct ReportBundle {
    std::filesystem::path dir;
    u32 format = 0;
    std::string app;
    std::string reason;
    std::string time;
    std::string version;
    std::string commit;
    std::string toolchain;
    std::string system;
    std::vector<std::string> args;
    std::vector<std::pair<std::string, std::string>> context;
    std::vector<std::string> stack;
    std::vector<std::pair<std::string, u64>> files; // report.json 의 files 와 크기 (없는 파일은 크기 0 · missing)
    std::vector<std::string> missingFiles;          // files 에 적혔는데 폴더에 없다
    std::vector<std::string> logLines;              // log.txt
};

[[nodiscard]] Expected<ReportBundle> readReportBundle(const std::filesystem::path& dir);

// root 아래의 묶음 폴더 (report.json 이 있는 것) — 새것부터 (이름 = UTC 시각이라 이름 역순)
[[nodiscard]] std::vector<std::filesystem::path> listReportBundles(const std::filesystem::path& root);

// 요약 · 콜스택 · 마지막 logLines 줄 · 파일 (덤프면 메모리가 든다는 알림 · 여는 법). engineVersion 과 묶음의 version 이
// 다르면 알린다 (기호 · 줄 번호는 같은 커밋의 빌드로 본다)
[[nodiscard]] std::string formatReportBundle(const ReportBundle& bundle, usize logLines,
                                             std::string_view engineVersion);

} // namespace sbx::project
