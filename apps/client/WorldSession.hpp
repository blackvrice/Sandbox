#pragma once
// Application ↔ "보고 있는 월드" 경계. Application 은 월드가 어디서 오는지 모른다.
//   Phase 10B: NetworkSession — 서버(같은 프로세스의 LocalServerHost 또는 원격 SandboxServer)의 스냅숏 → ClientWorld.
//   (Phase 8A 의 --direct-sim 은 지웠다 — ADR-0026)
// 단위 테스트는 가짜 세션을 끼운다. 8B: 선택 · 디버그 오버레이 (ADR-0022). 12A: 에디터 호스트 (ADR-0028).
// 12C: 접속자 · 역할 ("접속자" 패널 — ADR-0030). 12D: 세이브 · 로드 · 리플레이 ("월드" 패널 — ADR-0031).
// v0.2 M2-T3: 게임 진행 ("게임" 패널 — ADR-0045). v0.2 M3-T1: 플레이어 행동 ("행동" 패널 — ADR-0047).
// v0.3 M5-T3: 클라이언트 스크립트의 표시 (개체 위 글자 · "스크립트" 칸 — ADR-0067).

#include <optional>
#include <string>
#include <vector>

#include "render/renderer/RenderWorld.hpp"

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
enum class WorldOp : u8 { List = 1, Save, Load, RecordStart, RecordStop, Play, Restart, TestPlay, TestStop };
struct SavedWorldRow {
    std::string name;
    bool replay = false; // 리플레이 (재생) — 아니면 세이브 (불러오기)
    u64 tick = 0;        // 세이브의 틱 (리플레이는 시작 틱)
    u64 entities = 0;
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
    std::vector<std::string> systems;    // 시스템마다 누적 통계 한 줄
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
    // 서버에 요청 (결과는 worldOps().message — 목록은 saved)
    virtual void worldOp(WorldOp /*op*/, std::string /*name*/) {}
    // ---- v0.2 M2-T3: 게임 진행 (세션이 없으면 "게임" 패널을 그리지 않는다) ----
    [[nodiscard]] virtual std::optional<GameInfo> game() const { return std::nullopt; }
    // 시작 · 중단 = 명령 GameControl, 재시작 = WorldOp Restart (결과는 game().message)
    virtual void gameCommand(GameCommand /*c*/) {}
    // ---- v0.2 M3-T1: 플레이어 행동 (콘텐츠에 PlayerAction 이 없으면 "행동" 패널을 그리지 않는다) ----
    [[nodiscard]] virtual std::optional<ActionsInfo> playerActions() const { return std::nullopt; }
    // 명령 PlayerAction 을 보낸다 (결과는 playerActions().message)
    virtual void playerAction(std::string /*id*/, Vec2 /*world*/) {}
    // ---- v0.2 M3-T3: 카메라 처음 자리 (월드에 game.camera_start 가 없으면 nullopt — 월드 전체에 맞춘다) ----
    [[nodiscard]] virtual std::optional<CameraStart> cameraStart() const { return std::nullopt; }
    // ---- v0.3 M5-T3: 클라이언트 스크립트 (client 시스템이 없으면 nullopt — 아무것도 그리지 않는다) ----
    [[nodiscard]] virtual std::optional<ScriptOverlayInfo> scriptOverlay() const { return std::nullopt; }
};

} // namespace sbx::client
