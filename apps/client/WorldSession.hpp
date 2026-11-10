#pragma once
// Application ↔ "보고 있는 월드" 경계. Application 은 월드가 어디서 오는지 모른다.
//   Phase 10B: NetworkSession — 서버(같은 프로세스의 LocalServerHost 또는 원격 SandboxServer)의 스냅숏 → ClientWorld.
//   (Phase 8A 의 --direct-sim 은 지웠다 — ADR-0026)
// 단위 테스트는 가짜 세션을 끼운다. 8B: 선택 · 디버그 오버레이 (ADR-0022). 12A: 에디터 호스트 (ADR-0028).
// 12C: 접속자 · 역할 ("접속자" 패널 — ADR-0030). 12D: 세이브 · 로드 · 리플레이 ("월드" 패널 — ADR-0031).
// v0.2 M2-T3: 게임 진행 ("게임" 패널 — ADR-0045). v0.2 M3-T1: 플레이어 행동 ("행동" 패널 — ADR-0047).
// v0.3 M5-T3: 클라이언트 스크립트의 표시 (개체 위 글자 · "스크립트" 칸 — ADR-0067). v0.3 M5-T4: 스크립트 프로파일
// (ADR-0068).

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "render/renderer/RenderWorld.hpp"
#include "ui/binding/UiBinding.hpp"

namespace sbx::script {
struct ClientScriptInput;
}

namespace sbx::editor {
class IEditorHost;
}

namespace sbx::client {

// 패널(네트워크)이 보는 연결 상태 (10B)
struct NetInfo {
    bool local = true;
    std::string server; // "로컬" 또는 "host:port"
    std::string role;
    f64 rttMs = 0;
    u64 snapshots = 0;
    u64 resyncs = 0; // 새 epoch 로 다시 맞춤
    u32 epoch = 0;
    f64 receivedKBps = 0; // 최근 1 초
    f64 applyMs = 0;      // 받기 · 복제 월드 적용 (프레임마다, 지수 평균)
    f64 delayMs = 0;      // 보간 지연
    f64 behindTicks = 0;  // 마지막 서버 틱 − 그린 틱
    // 관심 청크 사각형 ("월드 전체" 또는 "청크 x0..x1 × y0..y1") · 다시 접속 중 · 다시 접속한 횟수 (Phase 11)
    std::string interest;
    bool reconnecting = false;
    u32 reconnects = 0;
    u64 commandsRejected = 0;
    std::string lastRejection; // 가장 최근 거절된 명령 (명령 · 사유)
};

// 접속자 한 사람 (12C). role = 역할 번호 (0 observer · 1 player · 2 editor · 3 admin · 4 owner — net::Role 과 같은 값)
struct PlayerRow {
    u16 clientId = 0;
    u8 role = 0;
    std::string name;
    bool self = false;
};
struct PlayersInfo {
    std::vector<PlayerRow> players; // clientId 오름차순
    u8 myRole = 0;
    std::string message; // 가장 최근 역할 바꾸기 결과 (거절 사유 등)
};

// 세이브 · 로드 · 리플레이 요청 (12D). 값 = net::WorldOpKind (Restart = 게임 재시작 — v0.2 M2-T3,
// TestPlay · TestStop = 테스트 플레이 시작 · 멈추고 되돌리기 — v0.2 M5-T1)
// Seek = 리플레이 탐색 (v0.8 M5-T2 — 프로토콜 17)
enum class WorldOp : u8 { List = 1, Save, Load, RecordStart, RecordStop, Play, Restart, TestPlay, TestStop, Seek };
struct SavedWorldRow {
    std::string name;
    bool replay = false; // 리플레이 (재생) — 아니면 세이브 (불러오기)
    u64 tick = 0;        // 세이브의 틱 (리플레이는 시작 틱)
    u64 entities = 0;
};
// 서버가 돌려준 WorldOp 결과 하나 (v0.8 M5-T1 — 리플레이 뷰어가 재생 시작 · 끝을 안다. Play 는 시작에 한 번 (수락 ·
// 거절), 끝에 한 번 (수락 = 기록과 같다 · 거절 = 다르다 · 오류))
struct WorldOpOutcome {
    WorldOp op = WorldOp::List;
    std::string name;
    bool accepted = false;
    std::string detail;
};
// "월드" 패널이 보는 것 (12D)
struct WorldOpsInfo {
    bool canChange = false;           // 저장 · 불러오기 · 기록 · 재생 (admin 부터 — 판단은 서버)
    bool listed = false;              // 목록을 받았다
    std::vector<SavedWorldRow> saved; // 이름 순
    u8 mode = 0;                      // 0 보통 · 1 기록 중 · 2 재생 중 · 3 테스트 플레이 (서버 통계 — 1 초마다)
    std::string modeName;             // 기록 · 재생 중인 이름
    u64 replayCall = 0;               // 기록: 기록한 tick() 수 · 재생: 재생한 수
    u64 replayCalls = 0;              // 재생: 전체
    u32 hashesChecked = 0;
    bool mismatch = false; // 재생: 기록과 다른 해시
    bool replayPaused = false;
    f32 replaySpeed = 1;
    u32 generation = 0;  // 서버 월드가 바뀐 횟수
    std::string message; // 가장 최근 결과 (거절 사유 · 요약)
};

// 점수 한 줄 (v0.2 M3-T2): 팀이 있는 모드면 팀마다 (+ 공용 칸이 0 이 아니면 "공용"), 없으면 "점수" 하나
struct ScoreRow {
    std::string name;
    i32 points = 0;
};

// "게임" 패널이 보는 것 (v0.2 M2-T3). state · result = comp::GameState · GameOutcome 값 (0 Lobby · 1 Starting ·
// 2 Running · 3 Finished / 0 없음 · 1 win · 2 lose · 3 draw). 복제된 game.session · game.score + 콘텐츠의 GameMode
struct GameInfo {
    std::string modeId;
    std::string modeName; // GameMode.name (콘텐츠에 없으면 id)
    u8 state = 0;
    u8 result = 0;
    u8 rule = 0;       // 끝낸 end 규칙 (1 부터, 0 = 없음 — 중단)
    std::string team;  // result 의 팀 이름 (없으면 빈 문자열)
    u64 tick = 0;      // 서버 틱 (복제 월드)
    u64 stateTick = 0; // 지금 상태에 들어간 틱
    u32 countdownTicks = 0;
    u32 minPlayers = 1;
    u32 maxPlayers = 16;
    bool autoRestart = false;
    u32 restartDelayTicks = 0;
    bool canControl = false;      // 시작 · 중단 · 재시작 (admin 부터 — 판단은 서버)
    std::string message;          // 가장 최근 거절 · 결과
    std::vector<ScoreRow> scores; // game.score 가 없으면 비어 있다
};
enum class GameCommand : u8 { Start = 1, Abort = 2, Restart = 3 };

// "행동" 패널이 보는 것 (v0.2 M3-T1): 콘텐츠의 PlayerAction (actions.json) 과 쓸 수 있는 역할인가
struct ActionRow {
    std::string id;
    std::string name;
    f32 range = 1;
    u32 maxTargets = 1;
};
// 팩의 presentation/input.json 원문 (v0.2 M3-T3 — 게임 키 기본값, PlayKeys.hpp)
struct PackInput {
    std::string pack; // 팩 폴더 (메시지용)
    std::string text;
};
struct ActionsInfo {
    std::vector<ActionRow> actions;   // id 순
    bool canAct = false;              // 역할 player 부터 (판단은 서버)
    std::string message;              // 가장 최근 결과 · 거절 사유
    std::vector<PackInput> packInput; // 로드 순서, input.json 이 있는 팩만
};

// 카메라 처음 자리 (v0.2 M3-T3): 복제 월드의 game.camera_start. generation 이 바뀌면 (불러오기 · 재시작) 다시 맞춘다
struct CameraStart {
    Vec2 center{};
    f32 height = 24.f; // 화면 세로에 보이는 월드 단위
    u32 generation = 0;
};

// 클라이언트 스크립트 (v0.3 M5-T3, ADR-0067) 가 이번 프레임에 표시한 것
struct ScriptLabelView {
    Vec2 world; // 개체의 그린 자리 (보간한 위치)
    std::string text;
};
struct ScriptOverlayInfo {
    std::vector<ScriptLabelView> labels; // 부른 차례 (그린 자리가 없는 개체는 빠진다)
    std::vector<std::string> lines;      // ctx.print — "스크립트" 칸
};

// 스크립트 시스템 하나의 프로파일 (v0.3 M5-T4, ADR-0068). 창 = 서버는 ServerStats 의 창 (약 1 초), 클라이언트는 최근 1
// 초. 시간은 진단 시계 (서버 · 이 클라이언트의 것) — 시뮬레이션 결과와 무관
struct ScriptProfileRow {
    std::string id;
    f64 callsPerSecond = 0; // 창
    f64 fuelPerCall = 0;    // 창 평균 (부르지 않았으면 0)
    f64 msPerSecond = 0;    // 창 — 1 초에 쓴 시간
    f64 msPerCall = 0;      // 창 평균
    f64 maxMs = 0;          // 누적 — 호출 하나의 최대
    u64 maxFuel = 0;        // 누적
    u64 traps = 0;          // 누적
    u64 budget = 0;         // 누적 (예산으로 멈춤)
    u64 cut = 0;            // 누적 (건너뜀)
    bool disabled = false;
};
struct ScriptProfileInfo {
    std::vector<ScriptProfileRow> server; // 실행 순서 (서버가 보낸 표 — 프로토콜 14)
    u32 serverOmitted = 0;                // 표 상한으로 빠진 서버 시스템
    std::vector<ScriptProfileRow> client; // 이 클라이언트의 client 시스템
};

// 게임 UI 원문 (v0.7 M3-T3 — 팩의 presentation/ui, ADR-0079). 클라이언트의 GameUiLayer 가 읽는다 (검사는 sbx check)
struct GameUiSource {
    std::string pack;
    std::string stem; // 파일 이름 (.json 앞)
    std::string text;
};
struct GameUiInfo {
    std::vector<GameUiSource> documents; // 팩 로드 차례 · 이름 순
    std::vector<GameUiSource> styles;    // 팩마다 styles.json (stem = "styles")
    std::vector<GameUiSource> strings;   // 팩마다 presentation/strings/<로케일>.json (stem = 로케일 — v0.7 M4-T1)
};

// 패널(시뮬레이션)이 보는 진행 상태 (8C)
struct WorldInfo {
    std::string name;
    std::string game; // 게임 프로젝트면 "<이름> — <gameId> <버전>" (Welcome.game, v0.2 M1-T3), 아니면 빈 문자열
    u64 tick = 0;
    u32 entities = 0;
    f32 speed = 1;
    bool paused = false;
    f64 ticksPerSecond = 0;       // 실제 (최근 1 초)
    f64 targetTicksPerSecond = 0; // 30 × 속도
    f64 tickMs = 0;               // 최근 틱 시간
    usize selected = 0;
    bool canControl = true;     // 일시정지 · 한 틱 · 속도를 쓸 수 있는 역할 (12C — 단추를 끈다. 판단은 서버)
    std::optional<NetInfo> net; // 10B
};

class IWorldSession {
public:
    virtual ~IWorldSession() = default;
    // 실제 경과 시간만큼 진행한다 (접속 · 받은 스냅숏 적용 · 로컬 서버면 그 진행). Connecting 에서도 불린다
    virtual void update(f64 dtSeconds) = 0;
    // 그릴 수 있다 (접속하고 첫 스냅숏을 받았다). false 면 Application 은 Connecting 에 머문다 (10B)
    [[nodiscard]] virtual bool ready() const { return true; }
    // 접속 실패 · 끊김 · 거절 — 있으면 Application 이 끝낸다 (메뉴 UI 가 생기면 MainMenu 로 [계획])
    [[nodiscard]] virtual std::optional<std::string> failure() const { return std::nullopt; }
    // out.sprites 에 그릴 것을 더한다 (카메라는 Application 이 채운다)
    virtual void extract(render::RenderWorld& out) = 0;
    // 처음 카메라를 맞출 월드 사각형
    [[nodiscard]] virtual render::WorldRect bounds() const = 0;
    // 화면에 보이는 월드 사각형 (프레임마다, InWorld) — 서버에 관심 영역으로 알린다 (Phase 11)
    virtual void setView(render::WorldRect /*visible*/) {}

    virtual void togglePause() = 0;
    virtual void stepOnce() = 0;                          // 일시정지 중 한 틱
    virtual void changeSpeed(int dir) = 0;                // +1 빠르게 · -1 느리게
    [[nodiscard]] virtual std::string status() const = 0; // 제목 줄 한 토막

    // ---- 8B: 선택 (기본 = 지원하지 않음). extract 가 선택 외곽선 · 디버그 선을 RenderWorld 에 더한다 ----
    // 점 아래 맨 위 개체 하나. additive 면 지금 선택에 더하고(이미 있으면 뺀다), 아니면 바꾼다. 빈 곳이면 비운다
    virtual void selectAt(Vec2 /*world*/, bool /*additive*/) {}
    // 가운데가 사각형 안에 든 개체 전부
    virtual void selectBox(render::WorldRect /*area*/, bool /*additive*/) {}
    virtual void clearSelection() {}
    // 선택한 개체의 감지 반경 · 경로 · 대상 · 속도 선
    virtual void setDetailOverlay(bool /*on*/) {}
    [[nodiscard]] virtual std::string selectionStatus() const { return {}; } // 비면 선택 없음
    [[nodiscard]] virtual WorldInfo info() const { return {}; }
    // ---- 12A: 에디터 (없으면 null — Application 은 8B 의 선택만 한다) ----
    [[nodiscard]] virtual editor::IEditorHost* editorHost() { return nullptr; }
    // ---- 12C: 접속자 · 역할 (없으면 패널을 그리지 않는다) ----
    [[nodiscard]] virtual std::optional<PlayersInfo> players() const { return std::nullopt; }
    // 역할 바꾸기 요청 (서버가 판단 — 결과는 players().message)
    virtual void requestRole(u16 /*clientId*/, u8 /*role*/) {}
    // ---- 12D: 세이브 · 로드 · 리플레이 (없으면 "월드" 패널을 그리지 않는다) ----
    [[nodiscard]] virtual std::optional<WorldOpsInfo> worldOps() const { return std::nullopt; }
    // 받은 WorldOp 결과를 꺼낸다 (v0.8 M5-T1 — 꺼내면 비운다)
    virtual std::vector<WorldOpOutcome> takeWorldOpOutcomes() { return {}; }
    // 서버에 요청 (결과는 worldOps().message — 목록은 saved). target = Seek 의 call (v0.8 M5-T2 — 다른 op 는 0)
    virtual void worldOp(WorldOp /*op*/, std::string /*name*/, u64 /*target*/ = 0) {}
    // Studio 자동 저장 (v0.8 M6-T7 — ADR-0113): 같은 프로세스의 로컬 서버면 편집 월드를 dir 에 세이브 (틱 사이).
    // 원격이면 false
    struct RecoverySave {
        std::filesystem::path dir;
        bool ok = false;
        u64 tick = 0;
        u64 worldHash = 0;
        std::string error;
    };
    virtual bool requestRecoverySave(const std::filesystem::path& /*dir*/) { return false; }
    [[nodiscard]] virtual std::vector<RecoverySave> takeRecoverySaves() { return {}; }
    // ---- v0.2 M2-T3: 게임 진행 (세션이 없으면 "게임" 패널을 그리지 않는다) ----
    [[nodiscard]] virtual std::optional<GameInfo> game() const { return std::nullopt; }
    // 시작 · 중단 = 명령 GameControl, 재시작 = WorldOp Restart (결과는 game().message)
    virtual void gameCommand(GameCommand /*c*/) {}
    // ---- v0.2 M3-T1: 플레이어 행동 (콘텐츠에 PlayerAction 이 없으면 "행동" 패널을 그리지 않는다) ----
    [[nodiscard]] virtual std::optional<ActionsInfo> playerActions() const { return std::nullopt; }
    // 명령 PlayerAction 을 보낸다 (결과는 playerActions().message)
    virtual void playerAction(std::string /*id*/, Vec2 /*world*/) {}
    // 게임 UI (v0.7 M3-T3) — 콘텐츠의 presentation/ui 원문, 버튼의 신호 (PlayerSignal — 서버가 판정)
    [[nodiscard]] virtual std::optional<GameUiInfo> gameUi() const { return std::nullopt; }
    virtual void playerSignal(std::string /*id*/, i32 /*value*/) {}
    // 게임 UI 바인딩의 데이터 (v0.7 M3-T4 — var · session · component@owned|selected). 없으면 바인딩은 모두 "-"
    [[nodiscard]] virtual const ui::IUiDataSource* uiData() const { return nullptr; }
    // 게임 UI 버튼의 행동 자리 (v0.7 M3-T5): 첫째 선택 개체 → 내 개체 (game.owner) 의 자리. 없으면 nullopt (앱은 화면
    // 가운데)
    [[nodiscard]] virtual std::optional<Vec2> uiActionPoint() const { return std::nullopt; }
    // client 스크립트의 문서 보이기 (client.showUi — v0.7 M3-T4): (문서 id, 보이기) 부른 차례. 꺼내면 비운다
    [[nodiscard]] virtual std::vector<std::pair<std::string, bool>> takeUiShows() { return {}; }
    // 문자열 표 (v0.7 M4-T1 — 앱의 게임 UI 층이 가진다, 앱보다 짧게 산다). client.uiText · uiFormat 이 본다
    virtual void setUiStrings(const ui::StringTable* /*strings*/) {}
    // var 바인딩 값 쓰기 (v0.7 NG-06 (1) — 게임 UI 입력 컨트롤이 쓴다, client 스크립트 setUi 와 같은 표)
    virtual void setUiVar(std::string /*key*/, ui::UiScalar /*value*/) {}
    // ---- v0.2 M3-T3: 카메라 처음 자리 (월드에 game.camera_start 가 없으면 nullopt — 월드 전체에 맞춘다) ----
    [[nodiscard]] virtual std::optional<CameraStart> cameraStart() const { return std::nullopt; }
    // ---- v0.3 M5-T3: 클라이언트 스크립트 (client 시스템이 없으면 nullopt — 아무것도 그리지 않는다) ----
    [[nodiscard]] virtual std::optional<ScriptOverlayInfo> scriptOverlay() const { return std::nullopt; }
    // ---- v0.3 M5-T4: 스크립트 프로파일 (서버 · 클라이언트 둘 다 시스템이 없으면 nullopt — "스크립트 프로파일" 패널)
    // ----
    [[nodiscard]] virtual std::optional<ScriptProfileInfo> scriptProfile() const { return std::nullopt; }
    // client 스크립트의 입력 (v0.5 M2-T6) — update 앞에 그 프레임의 게임 키 · 커서. 소리 이벤트 (id, 부른 차례) 는
    // update 뒤에 가져간다
    virtual void setScriptInput(const script::ClientScriptInput& /*input*/) {}
    [[nodiscard]] virtual std::vector<std::string> takeScriptSounds() { return {}; }
};

} // namespace sbx::client
