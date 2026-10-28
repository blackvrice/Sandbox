#pragma once
// 충돌 처리기 (v0.5 M1-T1 — docs/20-CREATOR-ALPHA.md 7.1). 타깃 SandboxCrash — 의존 Foundation 만 (서버도 링크한다 —
// SandboxPlatform 과 따로). OS 헤더는 .cpp 에만.
//
//   Windows  SetUnhandledExceptionFilter → 미니덤프 crash.dmp (dbghelp — MiniDumpNormal) + report.json · log.txt,
//            std::set_terminate (처리되지 않은 C++ 예외) · SIGABRT 도 보고 (덤프 없이)
//   POSIX    SIGSEGV · SIGBUS · SIGFPE · SIGILL · SIGABRT (대체 스택) → report.json · log.txt,
//            그 뒤 기본 동작으로 다시 올린다
//   보고     foundation/diag (createReportDir · writeReport — 민감정보 제거). 한 번만 (두 번째 충돌은 건너뛴다).
//            stderr 에 "충돌 보고: <폴더>" 한 줄
//
// 처리기 안에서 할당 · 파일 쓰기를 한다 — async-signal-safe 가 아니다 (최선). 스택 넘침은 덤프 없이 끝날 수 있다
// [계획].

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace sbx::crash {

struct CrashConfig {
    std::filesystem::path reportsRoot; // 보통 diag::defaultReportsRoot()
    std::string app;
    std::vector<std::string> args;                            // 명령줄 (거른다)
    std::vector<std::string> secrets;                         // 소유자 열쇠 등 — "<SECRET>" 으로
    std::vector<std::pair<std::string, std::string>> context; // "world" · "package" …
};

// 프로세스에 한 번. 다시 부르면 설정만 바꾼다
void install(CrashConfig config);

// 문맥 한 쌍을 더하거나 바꾼다 (같은 키면 값만) — 월드를 연 뒤 게임 · 패키지 digest 를 묶음에 남긴다 (v0.5 M1-T2).
// 아무 스레드에서나 부른다
void setContext(std::string key, std::string value);

// 시험용 — 일부러 잘못된 메모리에 쓴다 (--crash-test). 돌아오지 않는다
[[noreturn]] void crashForTest();

} // namespace sbx::crash
