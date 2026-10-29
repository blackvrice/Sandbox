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
#include "apps/client/ReleaseFeed.hpp"
#include "apps/client/UserSettings.hpp"
#include "apps/client/ui/ImGuiLayer.hpp"
#include "foundation/BuildInfo.hpp"
#include "foundation/diag/Report.hpp"
#include "foundation/io/AppPaths.hpp"
#include "foundation/io/Console.hpp"
#include "foundation/job/JobSystem.hpp"
#include "foundation/log/Log.hpp"
#include "platform/audio/NullAudioBackend.hpp"
#include "platform/common/HeadlessWindow.hpp"
#include "platform/crash/CrashHandler.hpp"
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
        // render: 기본 백엔드 먼저, 이 빌드에 든 다른 백엔드는 괄호 (--rhi 로 고른다)
        const auto rb = sbx::rhi::defaultBackend();
        std::string render(rb == sbx::rhi::BackendType::Auto ? std::string_view("none") : sbx::rhi::backendName(rb));
        for (const auto b :
             {sbx::rhi::BackendType::D3D12, sbx::rhi::BackendType::Vulkan, sbx::rhi::BackendType::Metal}) {
            if (b != rb && sbx::rhi::backendCompiled(b)) {
                render += std::format(" (+{})", sbx::rhi::backendName(b));
            }
        }
        printText(std::format("SandboxClient {} (commit {}, {}, {})\nwindow {}, render {}, audio null\n",
                              sbx::build::kVersionString, sbx::build::kGitCommit, sbx::build::kSystemName,
                              sbx::build::kCompilerId, sbx::platform::windowBackendName(), render));
        return kExitOk;
    }
    if (opts->logLevel) {
        sbx::log::setLevel(*opts->logLevel);
    }
    // 충돌 보고 (v0.5 M1-T1 — docs/20-CREATOR-ALPHA.md 7.1): 덤프 · 마지막 로그 · 빌드 정보, 소유자 열쇠 · 사용자
    // 폴더는 거른다
    sbx::crash::install({sbx::diag::defaultReportsRoot(),
                         "SandboxClient",
                         argStore,
                         opts->ownerKey.empty() ? std::vector<std::string>{} : std::vector<std::string>{opts->ownerKey},
                         {{opts->connect ? "connect" : "world", opts->connect.value_or(opts->world.value_or(""))}}});
    if (opts->crashTest) {
        sbx::crash::crashForTest();
    }

    // 사용자 설정 · 안전 모드 (v0.5 M1-T3 — docs/20-CREATOR-ALPHA.md 7.5). 깨진 설정은 옆으로 옮기고 기본값으로
    // 시작한다
    std::vector<std::string> notices;
    sbx::client::UserSettings user;
    const std::filesystem::path configRoot = sbx::client::defaultConfigRoot();
    std::filesystem::path packageCache; // 비면 기본 (임시 폴더의 sbx-packages)
    if (opts->safeMode) {
        std::error_code ec;
        packageCache = std::filesystem::temp_directory_path(ec) / "sbx-packages-safe";
        std::filesystem::remove_all(packageCache, ec); // 안전 모드 전용 폴더 — 매번 빈 캐시로
        notices.push_back(
            std::format("안전 모드 — 사용자 설정 ({}) · 키 설정 ({}) · UI 배율 · 패키지 캐시를 쓰지 않는다 "
                        "(설정 폴더 {})",
                        sbx::client::kClientSettingsFile, sbx::client::kUserInputFile, configRoot.generic_string()));
    } else {
        user = sbx::client::loadUserSettings(configRoot);
        notices = user.notices;
    }
    // 새 버전 알림 (7.6) — --update-feed 가 이긴다, 설정의 피드는 updateCheck 가 켜져 있을 때만
    const std::string feed =
        !opts->updateFeed.empty() ? opts->updateFeed : (user.settings.updateCheck ? user.settings.updateFeed : "");
    if (!feed.empty()) {
        const sbx::SemVer current{
            sbx::build::kVersionMajor, sbx::build::kVersionMinor, sbx::build::kVersionPatch, {}, {}};
        if (auto n = sbx::client::checkReleaseFeed(feed, current)) {
            notices.push_back(std::move(*n));
        }
    }
    for (const std::string& n : notices) {
        sbx::log::warn("client", "알림: {}", n);
    }

    // 키 바인딩: 기본값 위에 설정 폴더의 input.json, 그 위에 --input
    auto actions = sbx::platform::ActionMap::parse(sbx::client::defaultInputJson(), "<기본 바인딩>");
    if (!actions) {
        sbx::log::error("client", "{}", actions.error().describe());
        return kExitError;
    }
    if (user.input) {
        actions->merge(*user.input);
        sbx::log::info("client", "키 바인딩 {} 적용",
                       (configRoot / std::string(sbx::client::kUserInputFile)).generic_string());
    }
    if (!opts->inputFile.empty()) {
        auto fromFile = sbx::platform::ActionMap::loadFile(opts->inputFile);
        if (!fromFile) {
            sbx::log::error("client", "{}", fromFile.error().describe());
            return kExitError;
        }
        actions->merge(*fromFile);
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
    desc.system = opts->platform;
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
    // 배포 묶음이면 실행 파일 옆 assets/ · content/ (v0.5 M1-T4), 개발 빌드면 저장소의 것
    const std::filesystem::path assetRoot = opts->assetRoot.empty() ? sbx::io::dataDir("assets", SBX_DEFAULT_ASSETS_DIR)
                                                                    : std::filesystem::path(opts->assetRoot);
    // 에셋 디코드 Worker (렌더러가 있을 때만 쓴다). 렌더러보다 먼저 만들고 나중에 사라진다
    sbx::JobSystem jobs(opts->headless ? 0u : 2u);
    // 렌더러 (Phase 7A~): 렌더 백엔드가 있는 OS 의 실제 창에서만. 실패하면 그리지 않고 계속한다 (Phase 6 동작).
    std::unique_ptr<sbx::client::IFrameRenderer> renderer;
    if (!opts->headless && !opts->noRender) {
        sbx::client::RendererOptions ro;
        ro.backend = opts->rhi;
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
    std::optional<sbx::scenario::PlayScript> script;
    const sbx::client::WorldSaveDefaults saveDefaults = sbx::client::worldSaveDefaults(*opts);
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
        nd.ownerKey = opts->ownerKey;
        // 싱글플레이 세이브 · 리플레이 (12D, 게임 프로젝트면 <프로젝트>/worlds — M1-T5)
        nd.savesRoot = saveDefaults.savesRoot;
        if (opts->script) { // 자동 플레이 (v0.2 M6-T1) — 로컬 서버가 헤드리스 서버와 같은 단계에 같은 명령
            auto loaded = sbx::scenario::loadPlayScript(*opts->script);
            if (!loaded) {
                sbx::log::error("client", "{}", loaded.error().describe());
                return kExitBadArgs;
            }
            script = *loaded;
            nd.script = std::move(*loaded);
        }
        nd.seed = opts->seed;
        nd.packageCache = packageCache;
        nd.contentRoot = opts->contentRoot.empty() ? sbx::io::dataDir("content", SBX_DEFAULT_CONTENT_DIR)
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
        ud.uiScale = user.settings.uiScale; // 안전 모드면 기본값 1
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
    cfg.notices = std::move(notices);
    if (opts->world) {
        cfg.saveName = saveDefaults.saveName; // 게임 프로젝트면 시작 월드 — Ctrl+S 가 편집한 맵을 게임에 (M1-T5)
    }
    if (script && opts->headless) {
        // 자동 플레이: 로컬 서버가 멈추면 (게임 끝 · maxTicks) 끝낸다 — 창이 있으면 끝난 판을 보고 사람이 닫는다
        cfg.quitWhen = [&world] {
            sbx::net::LocalServerHost* local = world ? world->localServer() : nullptr;
            return local != nullptr && local->stats().reachedStopTick;
        };
    }
    sbx::client::Application app(*window, std::move(*actions), audio, cfg);

    std::unique_ptr<sbx::platform::FramePacer> pacer;
    if (fps > 0) {
        pacer = std::make_unique<sbx::platform::FramePacer>(fps);
        sbx::log::info("client", "프레임 페이싱 {} Hz ({})", fps,
                       pacer->highResolution() ? "고해상도 타이머" : "sleep_until");
    }
    sbx::log::info("client", "창 {}, 렌더러 {}, 오디오 null", window->backendName(), renderer ? kRendererName : "없음");

    const int code = app.run(pacer.get());
    bool scriptFailed = false;
    if (world) {
        const sbx::net::ClientWorld* cw = world->clientWorld();
        printText(std::format("world {} tick {} 개체 {} 스냅숏 {} (다시 맞춤 {}) · 받기 · 적용 {:.2f} ms\n",
                              world->name(), world->serverTick(), cw != nullptr ? cw->entityCount() : 0,
                              cw != nullptr ? cw->stats().snapshotsApplied : 0, cw != nullptr ? cw->stats().resets : 0,
                              world->applyMs()));
        if (const std::string game = world->info().game; !game.empty()) {
            printText(std::format("game {}\n", game)); // 게임 정체성 (Welcome.game — v0.2 M1-T3)
        }
        const auto t = app.totalTimings();
        if (t.frames > 0) {
            printText(
                std::format("프레임 평균 ({} 프레임, {:.1f} fps): 월드 {:.2f} ms · 추출 {:.2f} ms · 렌더 {:.2f} ms\n",
                            t.frames, 1000.0 / t.frameMs, t.worldMs, t.extractMs, t.renderMs));
        }
        if (const sbx::script::ClientScriptSystems* cs = world->clientScripts()) {
            // client 시스템 (v0.3 M5-T3) — 시스템마다 누적 통계, 마지막 프레임의 표시
            for (const std::string& line : sbx::script::describeClientScriptSystems(*cs)) {
                printText(line + "\n");
            }
            printText(std::format("클라이언트 스크립트 마지막 프레임 {}: 글자 {} · 줄 {}\n", cs->frames(),
                                  cs->host().labels().size(), cs->host().lines().size()));
            for (const sbx::script::ClientScriptLine& l : cs->host().lines()) {
                printText(std::format("  [{}] {}\n", l.system, l.text));
            }
        }
        if (sbx::net::LocalServerHost* local = world->localServer(); local != nullptr && script) {
            // 서버 월드 (권위) 의 결과 — 헤드리스 서버의 "게임 결과:" 줄과 같은 형식 (같아야 한다)
            printText(std::format("게임 결과: {}\n", sbx::scenario::describePlayResult(local->serverWorld())));
            for (const auto& d : sbx::scenario::checkPlayExpectation(local->serverWorld(), script->expect)) {
                sbx::log::error("client", "자동 플레이 기대와 다름 — {}", d);
                scriptFailed = true;
            }
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
    return scriptFailed && code == 0 ? 1 : code;
}
