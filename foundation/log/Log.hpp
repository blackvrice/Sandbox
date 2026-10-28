#pragma once
// 로그. 카테고리는 모듈 이름("core", "net", "render" …)을 쓴다.
//
//   sbx::log::info("net", "client {} connected", clientId);
//
// 스레드 안전. 시뮬레이션 핫 경로에서는 레벨 검사가 먼저 일어나므로 꺼진 레벨의 포맷 비용이 없다.

#include <cstddef>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sbx::log {

enum class Level : unsigned char { Trace = 0, Debug, Info, Warn, Error, Fatal, Off };

[[nodiscard]] std::string_view levelName(Level level) noexcept;

// 이 레벨 미만은 버린다. 기본: Info.
void setLevel(Level level) noexcept;
[[nodiscard]] Level level() noexcept;
[[nodiscard]] inline bool enabled(Level l) noexcept {
    return l >= level() && l != Level::Off;
}

// 출력 대상. 기본은 stderr 한 줄 출력. 테스트·파일 로그·에디터 콘솔이 교체한다.
// nullptr 을 넘기면 기본 싱크로 돌아간다. 싱크는 내부 락 안에서 호출된다 (재진입 금지).
using Sink = std::function<void(Level, std::string_view category, std::string_view message)>;
void setSink(Sink sink);

void write(Level level, std::string_view category, std::string_view message);

// 최근 줄 (v0.5 M1-T1 — 충돌 보고 묶음, docs/20-CREATOR-ALPHA.md 7.1). 싱크와 무관하게 마지막 kRecentLines 줄을
// "[수준] [범주] 내용" 으로 남긴다 (줄마다 kRecentLineBytes 까지 — 넘으면 자른다). 오래된 것부터
inline constexpr std::size_t kRecentLines = 256;
inline constexpr std::size_t kRecentLineBytes = 1024;
[[nodiscard]] std::vector<std::string> recentLines();
// 충돌 처리기용 — 잠금을 잡지 못하면 (그 스레드가 쥔 채 멈췄다) 잠금 없이 읽는다 (최선, 깨진 줄이 섞일 수 있다)
[[nodiscard]] std::vector<std::string> recentLinesForCrash();

template <class... Args>
void print(Level l, std::string_view category, std::format_string<Args...> fmt, Args&&... args) {
    if (!enabled(l)) {
        return;
    }
    write(l, category, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void trace(std::string_view c, std::format_string<Args...> f, Args&&... a) {
    print(Level::Trace, c, f, std::forward<Args>(a)...);
}
template <class... Args>
void debug(std::string_view c, std::format_string<Args...> f, Args&&... a) {
    print(Level::Debug, c, f, std::forward<Args>(a)...);
}
template <class... Args>
void info(std::string_view c, std::format_string<Args...> f, Args&&... a) {
    print(Level::Info, c, f, std::forward<Args>(a)...);
}
template <class... Args>
void warn(std::string_view c, std::format_string<Args...> f, Args&&... a) {
    print(Level::Warn, c, f, std::forward<Args>(a)...);
}
template <class... Args>
void error(std::string_view c, std::format_string<Args...> f, Args&&... a) {
    print(Level::Error, c, f, std::forward<Args>(a)...);
}

} // namespace sbx::log
