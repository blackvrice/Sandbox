// SandboxClient 진입점. Phase 6: 빈 창 + 앱 상태기계 (docs/16-ROADMAP.md 6.5). Phase 10B: --world · --connect 월드.
// Windows 에서는 GUI 서브시스템 실행 파일이다 (콘솔 창이 뜨지 않는다). 로그를 보려면 --console,
// 또는 CLion·리디렉션처럼 출력이 이미 연결된 곳에서 실행한다. WinMain 대신 main (07 6.1).
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "apps/client/Application.hpp"
#include "apps/client/ClientOptions.hpp"
#include "apps/client/DefaultInput.hpp"
#include "apps/client/NetworkSession.hpp"
#include "apps/client/ui/ImGuiLayer.hpp"
#include "foundation/BuildInfo.hpp"
#include "foundation/io/Console.hpp"
#include "foundation/job/JobSystem.hpp"
#include "foundation/log/Log.hpp"
#include "platform/audio/NullAudioBackend.hpp"
#include "platform/common/HeadlessWindow.hpp"
#include "render/asset/MaterialLibrary.hpp"
#include "render/rhi/RenderDevice.hpp"

namespace {

// 종료 코드: 0 정상, 1 실행 오류(창·설정), 2 잘못된 인자
constexpr int kExitOk = 0;
constexpr int kExitError = 1;
constexpr int kExitBadArgs = 2;

void printText(const std::string& s) {
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
}

} // namespace

int main(int argc, char** argv) {
    sbx::console::useUtf8Output();
    // Windows: argv 는 시스템 코드 페이지라 한글 이름 · 경로가 깨진다 — UTF-16 명령줄에서 UTF-8 로 (10B)
    const std::vector<std::string> argStore = sbx::console::utf8Arguments(argc, argv);
    std::vector<std::string_view> args(argStore.begin() + (argStore.empty() ? 0 : 1), argStore.end());
    auto opts = sbx::client::parseClientOptions(args);
    if (!opts) {
        sbx::platform::attachConsole();
        sbx::log::error("client", "{}", opts.error().message);
        printText(sbx::client::clientUsage());
        return kExitBadArgs;
    }
    if (opts->console || opts->showHelp || opts->showVersion) {
        sbx::platform::attachConsole();
        sbx::console::useUtf8Output();
    }
    if (opts->showHelp) {
        printText(sbx::client::clientUsage());
        return kExitOk;
    }
    if (opts->showVersion) {
        const auto rb = sbx::rhi::defaultBackend();
        printText(
            std::format("SandboxClient {} (commit {}, {}, {})\nwindow {}, render {}, audio null\n",
                        sbx::build::kVersionString, sbx::build::kGitCommit, sbx::build::kSystemName,
                        sbx::build::kCompilerId, sbx::platform::windowBackendName(),
                        rb == sbx::rhi::BackendType::Auto ? std::string_view("none") : sbx::rhi::backendName(rb)));
        return kExitOk;
    }
    if (opts->logLevel) {
        sbx::log::setLevel(*opts->logLevel);
    }

    // 키 바인딩: 기본값 위에 사용자 파일
    auto actions = sbx::platform::ActionMap::parse(sbx::client::defaultInputJson(), "<기본 바인딩>");
    if (!actions) {
        sbx::log::error("client", "{}", actions.error().describe());
        return kExitError;
    }
    if (!opts->inputFile.empty()) {
        auto user = sbx::platform::ActionMap::loadFile(opts->inputFile);
        if (!user) {
            sbx::log::error("client", "{}", user.error().describe());
            return kExitError;
        }
        actions->merge(*user);
        sbx::log::info("client", "키 바인딩 {} 적용", opts->inputFile);
    }
    for (const auto& c : actions->conflicts()) {
        sbx::log::warn("client", "키 바인딩 겹침: {} 이(가) {} 와 {} 에 있습니다",
                       sbx::platform::bindingName(c.binding), c.first, c.second);
    }

    sbx::platform::WindowDesc desc;
    desc.width = opts->width;
    desc.height = opts->height;
    desc.title = "Sandbox";
    std::unique_ptr<sbx::platform::IWindow> window;
    if (opts->headless) {
        window = std::make_unique<sbx::platform::HeadlessWindow>(desc);
    } else {
        auto w = sbx::platform::createWindow(desc);
        if (!w) {
            sbx::log::error("client", "{}", w.error().describe());
            return kExitError;
        }
        window = std::move(*w);
    }

#ifdef SBX_HAS_SHADERS
    constexpr const char* kRendererName = "스프라이트 · 지형 · UI (Phase 8C)";
#else
    constexpr const char* kRendererName = "Clear (셰이더 없는 빌드)";
#endif
    const std::filesystem::path assetRoot = opts->assetRoot.empty() ? std::filesystem::path(SBX_DEFAULT_ASSETS_DIR)
                                                                    : std::filesystem::path(opts->assetRoot);
    // 에셋 디코드 Worker (렌더러가 있을 때만 쓴다). 렌더러보다 먼저 만들고 나중에 사라진다
    sbx::JobSystem jobs(opts->headless ? 0u : 2u);
    // 렌더러 (Phase 7A~): 렌더 백엔드가 있는 OS 의 실제 창에서만. 실패하면 그리지 않고 계속한다 (Phase 6 동작).
    std::unique_ptr<sbx::client::IFrameRenderer> renderer;
    if (!opts->headless && !opts->noRender) {
        sbx::client::RendererOptions ro;
        ro.debugLayer = opts->rhiDebug;
        ro.gpuValidation = opts->rhiGbv;
        ro.warp = opts->rhiWarp;
        ro.allowFeatureLevel11 = opts->rhiFl11;
        ro.vsync = opts->vsync;
        ro.framesInFlight = opts->framesInFlight;
        ro.assetRoot = assetRoot;
        ro.jobs = &jobs;
        auto r = sbx::client::createClientRenderer(*window, ro);
        if (r) {
            renderer = std::move(*r);
        } else {
            sbx::log::warn("client", "렌더러 없이 계속합니다: {}", r.error().describe());
        }
    }

    // 월드 (Phase 10B): --world = 같은 프로세스의 서버, --connect = 원격 서버. 머티리얼은 렌더러가 없어도(헤드리스)
    // 색으로 쓴다
    sbx::render::MaterialLibrary materials;
    std::unique_ptr<sbx::client::NetworkSession> world;
    if (opts->world || opts->connect) {
        if (auto n = materials.loadAll(assetRoot, renderer ? renderer->assets() : nullptr); !n) {
            sbx::log::warn("client", "머티리얼을 읽지 못했습니다 (색 사각형으로 그립니다): {}", n.error().describe());
        } else {
            sbx::log::info("client", "머티리얼 {}개 ({} 의 */materials.json {}개)", materials.size(),
                           assetRoot.generic_string(), *n);
        }
        sbx::client::NetworkSessionDesc nd;
        nd.mode = opts->world ? sbx::client::NetworkSessionMode::Local : sbx::client::NetworkSessionMode::Remote;
        nd.world = opts->world.value_or("");
        nd.connect = opts->connect.value_or("");
        nd.displayName = opts->name;
        nd.seed = opts->seed;
        nd.contentRoot = opts->contentRoot.empty() ? std::filesystem::path(SBX_DEFAULT_CONTENT_DIR)
                                                   : std::filesystem::path(opts->contentRoot);
        // 창: 로컬 서버가 자기 스레드에서 (틱이 느려도 화면은 제 속도로 — ADR-0021 · 0026). 헤드리스: 프레임 안에서
        // (틱 수가 고정된다)
        nd.inlineServer = opts->headless;
        // Worker: 메인 · Simulation · Net 스레드 몫을 남기고 1~4 (결과는 Worker 수와 무관 — D5). 헤드리스는 0
        const unsigned hc = std::thread::hardware_concurrency();
        nd.workers = opts->simThreads.value_or(opts->headless ? 0u : std::clamp(hc > 2 ? hc - 2 : 1u, 1u, 4u));
        auto ns = sbx::client::NetworkSession::create(nd, materials);
        if (!ns) {
            sbx::log::error("client", "{}", ns.error().describe());
            return kExitBadArgs;
        }
        world = std::move(*ns);
    }

    // UI (Phase 8C): ImGui 레이어 — 렌더러가 있으면 UIPass 를 붙인다. 헤드리스도 돈다 (텍스처 요청은 레이어가 받는다)
    std::unique_ptr<sbx::client::ImGuiLayer> ui;
    bool uiRendered = false;
    if (!opts->noUi) {
        sbx::client::ImGuiLayerDesc ud;
        ud.font = opts->font;
        ui = std::make_unique<sbx::client::ImGuiLayer>(*window, ud);
        uiRendered = renderer != nullptr && renderer->attachImGui(ui->io(), ui->platformIo());
    }

    sbx::platform::NullAudioBackend audio;
    audio.init({});

    // 페이싱: 렌더러가 있으면 VSync(또는 무제한)가 정한다. 없으면 60 Hz. --fps 가 둘 다 이긴다
    const double fps = opts->fps.value_or(opts->headless || renderer ? 0.0 : 60.0);
    sbx::client::AppConfig cfg;
    cfg.maxFrames = opts->frames.value_or(0);
    cfg.logInput = opts->logInput;
    cfg.fps = fps > 0 ? fps : 60.0; // 오디오·애니메이션의 명목 dt
    cfg.renderer = renderer.get();
    cfg.world = world.get();
    cfg.fixedDt = opts->headless ? 1.0 / 60.0 : 0.0; // 헤드리스는 프레임마다 1/60 초 — 같은 명령이면 같은 틱 수
    cfg.ui = ui.get();
    cfg.uiRendered = uiRendered;
    sbx::client::Application app(*window, std::move(*actions), audio, cfg);

    std::unique_ptr<sbx::platform::FramePacer> pacer;
    if (fps > 0) {
        pacer = std::make_unique<sbx::platform::FramePacer>(fps);
        sbx::log::info("client", "프레임 페이싱 {} Hz ({})", fps,
                       pacer->highResolution() ? "고해상도 타이머" : "sleep_until");
    }
    sbx::log::info("client", "창 {}, 렌더러 {}, 오디오 null",
                   opts->headless ? std::string_view("headless") : sbx::platform::windowBackendName(),
                   renderer ? kRendererName : "없음");

    const int code = app.run(pacer.get());
    if (world) {
        const sbx::net::ClientWorld* cw = world->clientWorld();
        printText(std::format("world {} tick {} 개체 {} 스냅숏 {} (다시 맞춤 {}) · 받기 · 적용 {:.2f} ms\n",
                              world->name(), world->serverTick(), cw != nullptr ? cw->entityCount() : 0,
                              cw != nullptr ? cw->stats().snapshotsApplied : 0, cw != nullptr ? cw->stats().resets : 0,
                              world->applyMs()));
        const auto t = app.totalTimings();
        if (t.frames > 0) {
            printText(
                std::format("프레임 평균 ({} 프레임, {:.1f} fps): 월드 {:.2f} ms · 추출 {:.2f} ms · 렌더 {:.2f} ms\n",
                            t.frames, 1000.0 / t.frameMs, t.worldMs, t.extractMs, t.renderMs));
        }
        if (sbx::net::LocalServerHost* local = world->localServer()) {
            const auto ss = local->stats();
            std::optional<sbx::net::ServerStats> st;
            if (world->session() != nullptr) {
                st = world->session()->lastStats();
            }
            printText(std::format("로컬 서버: 틱 {} 번 · 최근 틱 {:.2f} ms · 스냅숏 만들기 {:.2f} ms · 밀림 {}\n",
                                  ss.ticksRun, st ? static_cast<double>(st->tickMsAvg) : 0.0, ss.replicationMs,
                                  ss.overruns));
        }
    }
    world.reset();
    if (renderer && ui) {
        renderer->detachImGui(ui->platformIo()); // ImGui 컨텍스트보다 먼저 UI 텍스처를 놓는다
    }
    renderer.reset(); // 창보다 먼저 (스왑체인이 HWND 를 쓴다)
    ui.reset();
    audio.shutdown();
    printText(std::format("SandboxClient 끝: frames {} state {}\n", app.frameCount(),
                          sbx::client::appStateName(app.state())));
    return code;
}
