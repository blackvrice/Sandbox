#include "apps/client/DefaultInput.hpp"

namespace sbx::client {

std::string_view defaultInputJson() noexcept {
    // app.* · debug.* 는 Phase 6 의 빈 창이 쓴다. camera.* · editor.* · sim.* 는 Phase 8·12 가 쓴다 (이름을 먼저 고정).
    // view.* 는 8B 오버레이 (격자 · 선택한 개체의 감지 반경 · 경로). editor.tool.* 는 12A 툴 — 숫자 1 ~ 5 (WASD 가
    // 카메라라서 10-EDITOR 9장 원안의 Q W E B X 대신, ADR-0028).
    // 키 이름은 물리 위치다 (AZERTY 에서도 W 자리).
    return R"json({
  "format": "sandbox.input",
  "version": 1,
  "actions": {
    "app.quit": ["Ctrl+Q"],
    "debug.text_input": ["F2"],
    "debug.capture_mouse": ["F3"],
    "debug.cycle_cursor": ["F4"],
    "debug.copy_text": ["Ctrl+C"],
    "debug.paste_text": ["Ctrl+V"],
    "debug.escape": ["Escape"],
    "debug.erase_char": ["Backspace"],
    "camera.pan.up": ["W", "Up"],
    "camera.pan.down": ["S", "Down"],
    "camera.pan.left": ["A", "Left"],
    "camera.pan.right": ["D", "Right"],
    "camera.drag": ["MouseMiddle"],
    "camera.reset": ["Home"],
    "editor.select": ["MouseLeft"],
    "editor.tool.select": ["Digit1"],
    "editor.tool.move": ["Digit2"],
    "editor.tool.place": ["Digit3"],
    "editor.tool.terrain": ["Digit4"],
    "editor.tool.erase": ["Digit5"],
    "editor.select_add": ["Shift+MouseLeft"],
    "editor.context": ["MouseRight"],
    "editor.delete": ["Delete"],
    "editor.undo": ["Ctrl+Z"],
    "editor.redo": ["Ctrl+Y", "Ctrl+Shift+Z"],
    "editor.save": ["Ctrl+S"],
    "sim.toggle_pause": ["Space"],
    "sim.step": ["Period"],
    "sim.speed_up": ["Equal", "KpAdd"],
    "sim.speed_down": ["Minus", "KpSubtract"],
    "view.grid": ["G"],
    "view.details": ["V"],
    "view.panels": ["F1"]
  }
}
)json";
}

} // namespace sbx::client
