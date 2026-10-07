#pragma once
// 벤치 공통: 측정, 중앙값, JSON 출력. 벽시계는 벤치·도구에서만 쓴다 (시뮬레이션 금지).

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace sbx::bench {

struct Context {
    bool quick = false;
    int repeats = 5;      // 같은 측정을 반복해 중앙값을 쓴다 (14-PERFORMANCE 3장)
    unsigned threads = 0; // 경로 Job Worker 수 (sim.ecosystem)
    std::string only;     // 비어 있지 않으면 이름이 이것으로 시작하는 벤치만 ("sim.ecosystem")
    std::vector<nlohmann::json> results;
};

// fn 을 repeats 번 실행해 각 실행의 소요 시간(ns)을 돌려준다
inline std::vector<double> measure(int repeats, const std::function<void()>& fn) {
    std::vector<double> ns;
    ns.reserve(static_cast<std::size_t>(repeats));
    for (int i = 0; i < repeats; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        fn();
        const auto t1 = std::chrono::steady_clock::now();
        ns.push_back(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
    }
    return ns;
}

inline double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n == 0 ? 0.0 : (n % 2 == 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0);
}

// 결과가 최적화로 사라지지 않게 한다
template <class T>
inline void doNotOptimize(const T& value) {
#if defined(_MSC_VER) && !defined(__clang__)
    static volatile const void* sink;
    sink = &value;
#else
    asm volatile("" : : "g"(&value) : "memory");
#endif
}

// --only 필터: name 이 only 로 시작하거나 only 가 name 으로 시작하면 (묶음 이름 "sim." 으로도 고를 수 있게)
inline bool wants(const Context& ctx, std::string_view name) {
    return ctx.only.empty() || name.starts_with(ctx.only) || std::string_view(ctx.only).starts_with(name);
}

void runEcsBenchmarks(Context& ctx);
void runSimBenchmarks(Context& ctx);
void runRenderBenchmarks(Context& ctx);

} // namespace sbx::bench
