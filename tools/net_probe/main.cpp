// sbx_net_probe — SandboxServer 에 접속해 핸드셰이크 · 명령 · 통계를 확인한다 (Phase 9). docs/08-NETWORK.md 4장.
//
// 콘텐츠 해시: 처음에는 내장 콘텐츠의 해시로 접속한다. 서버가 Reject{ContentMismatch, packs, contentHash} 로 답하면
// 그 팩을 --content-root 에서 읽어 해시를 맞춘 뒤 한 번 더 접속한다 (08 4장 — 클라이언트가 무엇을 읽어야 하는지 안다).
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "foundation/io/Console.hpp"
#include "foundation/log/Log.hpp"
#include "network/client/ClientSession.hpp"
#include "network/transport/EnetTransport.hpp"
#include "tools/net_probe/NetProbeOptions.hpp"

namespace {

using sbx::f64;
using sbx::usize;

constexpr int kExitOk = 0;
constexpr int kExitFailed = 1;
constexpr int kExitBadArgs = 2;
constexpr int kExitCommandRejected = 3;

using Clock = std::chrono::steady_clock;

f64 since(Clock::time_point t0) {
    return std::chrono::duration<f64>(Clock::now() - t0).count();
}

void printStats(const sbx::net::ServerStats& s) {
    std::printf("서버 tick %llu · 개체 %u · 틱 %.2f ms (최대 %.2f) · %.1f TPS · 속도 x%.2g%s · 접속 %u\n",
                static_cast<unsigned long long>(s.serverTick), s.entities, static_cast<double>(s.tickMsAvg),
                static_cast<double>(s.tickMsMax), static_cast<double>(s.ticksPerSecond), static_cast<double>(s.speed),
                s.paused ? " · 일시정지" : "", static_cast<unsigned>(s.clients));
    std::fflush(stdout);
}

enum class Attempt { Connected, ContentMismatch, Failed };

struct Probe {
    const sbx::probe::NetProbeOptions& opts;
    sbx::net::Endpoint target;
    std::unique_ptr<sbx::net::EnetTransport> transport;
    std::unique_ptr<sbx::net::ClientSession> session;
    const sbx::ecs::ComponentCatalog* catalog = nullptr;
};

// 접속을 한 번 시도한다. Connected 면 probe.session 이 살아 있다
Attempt connectOnce(Probe& p, const sbx::content::ContentDatabase& content,
                    std::optional<sbx::net::Reject>& rejectOut) {
    auto t = sbx::net::EnetTransport::create();
    if (!t) {
        std::fprintf(stderr, "sbx_net_probe: %s\n", t.error().describe().c_str());
        return Attempt::Failed;
    }
    p.session.reset();
    p.transport = std::move(*t);
    sbx::net::ClientSessionDesc d;
    d.displayName = p.opts.name;
    d.contentHash = content.contentHash();
    d.catalog = p.catalog; // 복제 월드를 만든다 (받은 개체 수를 보여 준다)
    d.content = &content;
    d.buildId = sbx::net::localBuildId();
    d.handshakeTimeoutSeconds = p.opts.connectTimeoutSeconds;
    p.session = std::make_unique<sbx::net::ClientSession>(*p.transport, d);
    const auto t0 = Clock::now();
    if (auto r = p.session->connect(p.target, 0); !r) {
        std::fprintf(stderr, "sbx_net_probe: %s\n", r.error().describe().c_str());
        return Attempt::Failed;
    }
    while (true) {
        p.transport->wait(10);
        p.session->update(since(t0));
        switch (p.session->state()) {
        case sbx::net::ClientState::Connected:
            return Attempt::Connected;
        case sbx::net::ClientState::Rejected: {
            const auto& r = *p.session->reject();
            rejectOut = r;
            if (r.reason == sbx::net::RejectReason::ContentMismatch) {
                return Attempt::ContentMismatch;
            }
            std::printf("거절: %.*s — %s\n", static_cast<int>(sbx::net::rejectReasonName(r.reason).size()),
                        sbx::net::rejectReasonName(r.reason).data(), r.detail.c_str());
            return Attempt::Failed;
        }
        case sbx::net::ClientState::Disconnected: {
            const auto reason = sbx::net::disconnectReasonName(p.session->disconnectReason());
            std::printf("접속 실패: %s (%.*s)\n", p.target.describe().c_str(), static_cast<int>(reason.size()),
                        reason.data());
            return Attempt::Failed;
        }
        default:
            break;
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    sbx::console::useUtf8Output();
    std::vector<std::string_view> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    const auto opts = sbx::probe::parseNetProbeOptions(args);
    if (!opts) {
        std::fprintf(stderr, "sbx_net_probe: %s\n\n%s", opts.error().describe().c_str(),
                     sbx::probe::netProbeUsage().c_str());
        return kExitBadArgs;
    }
    if (opts->showHelp) {
        std::fputs(sbx::probe::netProbeUsage().c_str(), stdout);
        return kExitOk;
    }
    if (opts->logLevel) {
        sbx::log::setLevel(*opts->logLevel);
    }
    auto target = sbx::net::parseEndpoint(opts->connect, 7777);
    if (!target) {
        std::fprintf(stderr, "sbx_net_probe: %s\n", target.error().describe().c_str());
        return kExitBadArgs;
    }
    sbx::ecs::ComponentCatalog catalog;
    if (auto r = sbx::comp::registerCoreComponents(catalog); !r) {
        std::fprintf(stderr, "sbx_net_probe: %s\n", r.error().describe().c_str());
        return kExitFailed;
    }

    Probe probe{*opts, *target, nullptr, nullptr, &catalog};
    std::optional<sbx::net::Reject> reject;
    auto content = std::make_unique<sbx::content::ContentDatabase>(sbx::content::ContentDatabase::builtin());
    Attempt a = connectOnce(probe, *content, reject);
    if (a == Attempt::ContentMismatch) {
        // 서버가 알려 준 팩을 읽어 해시를 맞춘다
        const std::filesystem::path root = opts->contentRoot.empty() ? std::filesystem::path(SBX_DEFAULT_CONTENT_DIR)
                                                                     : std::filesystem::path(opts->contentRoot);
        if (!reject->packs.empty()) {
            auto db = sbx::content::loadContent(root, reject->packs, catalog);
            if (!db) {
                std::printf("서버의 콘텐츠 팩을 읽지 못했습니다: %s\n", db.error().describe().c_str());
                return kExitFailed;
            }
            content = std::make_unique<sbx::content::ContentDatabase>(std::move(*db));
        }
        const sbx::u64 hash = content->contentHash();
        if (hash != reject->contentHash) {
            std::printf("콘텐츠가 다릅니다: 서버 %016llx, 이 컴퓨터 %016llx (팩 %zu 개)\n",
                        static_cast<unsigned long long>(reject->contentHash), static_cast<unsigned long long>(hash),
                        reject->packs.size());
            return kExitFailed;
        }
        std::string packs;
        for (const auto& id : reject->packs) {
            packs += (packs.empty() ? "" : ", ") + id;
        }
        std::printf("서버 콘텐츠(%s)를 읽어 다시 접속합니다\n", packs.empty() ? "내장" : packs.c_str());
        reject.reset();
        a = connectOnce(probe, *content, reject);
    }
    if (a != Attempt::Connected) {
        return kExitFailed;
    }

    auto& session = *probe.session;
    const auto& w = *session.welcome();
    std::printf("접속 client #%u role %.*s world %s tick %llu%s · RTT %.0f ms\n", static_cast<unsigned>(w.clientId),
                static_cast<int>(sbx::net::roleName(w.role).size()), sbx::net::roleName(w.role).data(),
                w.world.name.c_str(), static_cast<unsigned long long>(w.serverTick),
                w.world.paused ? " (일시정지)" : "", static_cast<double>(session.transportStats().rttMs));
    std::fflush(stdout);

    for (const auto& c : opts->commands) {
        session.sendCommand(c);
    }
    std::vector<sbx::cmd::CommandPayload> sent = opts->commands;
    usize accepted = 0;
    usize rejected = 0;
    usize answered = 0;
    const auto t0 = Clock::now();
    auto lastResult = Clock::now();
    bool printedStats = false;
    sbx::u64 firstStatsTick = 0;
    while (session.state() == sbx::net::ClientState::Connected) {
        probe.transport->wait(10);
        session.update(since(t0));
        for (const auto& r : session.takeResults()) {
            ++answered;
            lastResult = Clock::now();
            const auto name = r.sequence >= 1 && r.sequence <= sent.size() ? sbx::cmd::commandName(sent[r.sequence - 1])
                                                                           : std::string_view("?");
            if (r.accepted) {
                ++accepted;
                std::printf("결과 #%u %.*s 수락 tick %llu", r.sequence, static_cast<int>(name.size()), name.data(),
                            static_cast<unsigned long long>(r.appliedTick));
                for (const auto id : r.created) {
                    std::printf(" netId %u", id);
                }
                std::printf("\n");
            } else {
                ++rejected;
                const auto code = sbx::errorCodeName(r.reason);
                std::printf("결과 #%u %.*s 거절 %.*s — %s\n", r.sequence, static_cast<int>(name.size()), name.data(),
                            static_cast<int>(code.size()), code.data(), r.detail.c_str());
            }
            std::fflush(stdout);
        }
        if (session.lastStats() && !printedStats) {
            printStats(*session.lastStats());
            printedStats = true;
            firstStatsTick = session.lastStats()->serverTick;
        }
        const bool allAnswered = answered >= sent.size();
        if (allAnswered && since(lastResult) >= opts->seconds && (printedStats || since(t0) > opts->seconds + 2)) {
            break;
        }
        if (!allAnswered && since(t0) > 10) {
            std::printf("결과를 다 받지 못했습니다 (%zu / %zu)\n", answered, sent.size());
            break;
        }
    }
    if (session.state() != sbx::net::ClientState::Connected) {
        const auto reason = sbx::net::disconnectReasonName(session.disconnectReason());
        std::printf("서버가 끊었습니다: %.*s\n", static_cast<int>(reason.size()), reason.data());
    }
    if (session.lastStats() && session.lastStats()->serverTick != firstStatsTick) {
        printStats(*session.lastStats());
    }
    if (const sbx::net::ClientWorld* rw = session.world()) {
        const auto& st = rw->stats();
        std::printf("복제 개체 %zu · 스냅숏 %llu (다시 맞춤 %llu) · 지형 청크 %llu · 받은 %.1f KB\n", rw->entityCount(),
                    static_cast<unsigned long long>(st.snapshotsApplied), static_cast<unsigned long long>(st.resets),
                    static_cast<unsigned long long>(st.terrainChunks),
                    static_cast<double>(session.snapshotBytes()) / 1024.0);
    }
    session.disconnect();
    probe.transport->drain(200);
    std::printf("probe 끝: 명령 %zu 수락 %zu 거절 %zu\n", sent.size(), accepted, rejected);
    if (answered < sent.size()) {
        return kExitFailed;
    }
    return rejected > 0 ? kExitCommandRejected : kExitOk;
}
