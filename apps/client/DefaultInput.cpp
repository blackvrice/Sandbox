#include "apps/client/DefaultInput.hpp"

namespace sbx::client {

std::string_view defaultInputJson() noexcept {
    // app.* · debug.* 는 Phase 6 의 빈 창이 쓴다. camera.* · editor.* · sim.* 는 Phase 8·12 가 쓴다 (이름을 먼저 고정).
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
    "editor.context": ["MouseRight"],
    "editor.delete": ["Delete"],
    "editor.undo": ["Ctrl+Z"],
    "editor.redo": ["Ctrl+Y", "Ctrl+Shift+Z"],
    "editor.save": ["Ctrl+S"],
    "sim.toggle_pause": ["Space"],
    "sim.step": ["Period"],
    "sim.speed_up": ["Equal", "KpAdd"],
    "sim.speed_down": ["Minus", "KpSubtract"]
  }
}
)json";
}

} // namespace sbx::client
