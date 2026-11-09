# check_includes.py 자체 시험용 픽스처

**컴파일되지 않습니다.** 일부러 경계를 어긴 파일들이며, `arch_include_lint_selftest`가 정확히 **15건**의 위반을 기대합니다.
파일을 추가·수정하면 `tests/CMakeLists.txt`의 `--expect` 값을 함께 고칩니다.

| 파일 | 기대 위반 |
|---|---|
| `core/BadGraphics.hpp` | banned-header(d3d12) + module-direction(render) = 2 |
| `foundation/BadDependency.hpp` | module-direction(core) = 1 |
| `render/rhi/BadApi.hpp` | banned-header(vulkan) = 1 |
| `network/transport/EnetOk.cpp` | 0 — transport 는 enet 허용 |
| `network/session/BadEnet.cpp` | banned-header(enet) = 1 |
| `apps/client/BadSfml.cpp` | banned-global(SFML) = 1 |
| `platform/common/BadImport.hpp` | objc-import(.mm 아님) + banned-header(Cocoa Foundation) = 2 |
| `platform/windows/Win32Ok.cpp` | 0 — platform/windows 는 windows.h 허용 |
| `script/BadCore.hpp` | module-direction(core) + banned-header(windows.h) = 2 — SandboxScript 는 ECS · OS 를 모른다 (v0.3 M1-T1) |
| `ui/BadCore.hpp` | module-direction(core) + banned-header(imgui) = 2 — SandboxGameUI 는 core · ImGui 를 모른다 (v0.7 M3-T1) |
| `editor/services/BadService.hpp` | banned-header(imgui) + subpath-direction(editor/Editor.hpp · render) = 3 — Studio 서비스는 UI · render 를 모른다 (v0.8 ST-06-1) |
| `editor/services/ServiceOk.hpp` | 0 — foundation · script · core · editor/services |
| `editor/ToolOk.cpp` | 0 — editor/ (툴 · 패널) 는 ImGui · render · services 를 쓴다 |
| **합계** | **15** |
