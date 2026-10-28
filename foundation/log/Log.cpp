#include "foundation/log/Log.hpp"

#include <array>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>

namespace sbx::log {
namespace {

std::atomic<Level> g_level{Level::Info};
std::mutex g_mutex; // owner: 모든 스레드. 싱크 호출과 교체를 직렬화한다.
Sink g_sink;        // 비어 있으면 기본 싱크

// 최근 줄 고리 (g_mutex 아래). g_next = 다음에 쓸 자리, g_count = 찬 칸 수
std::array<std::string, kRecentLines> g_recent;
std::size_t g_next = 0;
std::size_t g_count = 0;

void remember(Level level, std::string_view category, std::string_view message) {
    std::string line = std::format("[{}] [{}] {}", levelName(level), category, message);
    if (line.size() > kRecentLineBytes) {
        line.resize(kRecentLineBytes);
    }
    g_recent[g_next] = std::move(line);
    g_next = (g_next + 1) % kRecentLines;
    g_count = g_count < kRecentLines ? g_count + 1 : kRecentLines;
}

std::vector<std::string> copyRecent() {
    std::vector<std::string> out;
    out.reserve(g_count);
    const std::size_t first = (g_next + kRecentLines - g_count) % kRecentLines;
    for (std::size_t i = 0; i < g_count; ++i) {
        out.push_back(g_recent[(first + i) % kRecentLines]);
    }
    return out;
}

void defaultSink(Level level, std::string_view category, std::string_view message) {
    const std::string line = std::format("[{}] [{}] {}\n", levelName(level), category, message);
    std::fwrite(line.data(), 1, line.size(), stderr);
    // Windows CRT 는 파일·파이프로 넘긴 stderr 를 버퍼링한다 — 강제 종료·크래시 직전 줄을 잃지 않게 (Phase 6, GUI
    // 클라이언트)
    std::fflush(stderr);
}

} // namespace

std::string_view levelName(Level level) noexcept {
    switch (level) {
    case Level::Trace:
        return "trace";
    case Level::Debug:
        return "debug";
    case Level::Info:
        return "info";
    case Level::Warn:
        return "warn";
    case Level::Error:
        return "error";
    case Level::Fatal:
        return "fatal";
    case Level::Off:
        return "off";
    }
    return "?";
}

void setLevel(Level level) noexcept {
    g_level.store(level, std::memory_order_relaxed);
}

Level level() noexcept {
    return g_level.load(std::memory_order_relaxed);
}

void setSink(Sink sink) {
    std::lock_guard lock(g_mutex);
    g_sink = std::move(sink);
}

void write(Level level, std::string_view category, std::string_view message) {
    std::lock_guard lock(g_mutex);
    remember(level, category, message);
    if (g_sink) {
        g_sink(level, category, message);
    } else {
        defaultSink(level, category, message);
    }
}

std::vector<std::string> recentLines() {
    std::lock_guard lock(g_mutex);
    return copyRecent();
}

std::vector<std::string> recentLinesForCrash() {
    std::unique_lock lock(g_mutex, std::try_to_lock);
    return copyRecent(); // 잠금을 못 잡았어도 읽는다 — 충돌 뒤에는 다른 쓰기가 거의 없다
}

} // namespace sbx::log
