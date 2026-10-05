# check_includes.py 자체 시험용 픽스처

**컴파일되지 않습니다.** 일부러 경계를 어긴 파일들이며, `arch_include_lint_selftest`가 정확히 **8건**의 위반을 기대합니다.
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
| **합계** | **8** |
