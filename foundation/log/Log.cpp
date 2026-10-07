#include "foundation/log/Log.hpp"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>

namespace sbx::log {
namespace {

std::atomic<Level> g_level{Level::Info};
std::mutex g_mutex; // owner: 모든 스레드. 싱크 호출과 교체를 직렬화한다.
Sink g_sink;        // 비어 있으면 기본 싱크

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
    if (g_sink) {
        g_sink(level, category, message);
    } else {
        defaultSink(level, category, message);
    }
}

} // namespace sbx::log
