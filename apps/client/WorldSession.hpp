#pragma once
// Application ↔ "보고 있는 월드" 경계. Application 은 월드가 어디서 오는지 모른다.
//   Phase 8A: DirectSim (클라이언트가 Core 를 직접 돌리는 임시 경로, --direct-sim — Phase 10 에서 삭제, 16-ROADMAP)
//   Phase 10: ClientSession (서버 스냅샷 → ClientWorld) 이 같은 자리에
// 단위 테스트는 가짜 세션을 끼운다.

#include <string>

#include "render/renderer/RenderWorld.hpp"

namespace sbx::client {

class IWorldSession {
public:
    virtual ~IWorldSession() = default;
    // 실제 경과 시간만큼 월드를 진행한다 (고정 틱 + 따라잡기 상한은 구현이)
    virtual void update(f64 dtSeconds) = 0;
    // out.sprites 에 그릴 것을 더한다 (카메라는 Application 이 채운다)
    virtual void extract(render::RenderWorld& out) = 0;
    // 처음 카메라를 맞출 월드 사각형
    [[nodiscard]] virtual render::WorldRect bounds() const = 0;

    virtual void togglePause() = 0;
    virtual void stepOnce() = 0;                          // 일시정지 중 한 틱
    virtual void changeSpeed(int dir) = 0;                // +1 빠르게 · -1 느리게
    [[nodiscard]] virtual std::string status() const = 0; // 제목 줄 한 토막
};

} // namespace sbx::client
