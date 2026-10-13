# SandboxRender — RHI · 백엔드 · (Phase 8) Renderer · Asset. 의존: Platform (NativeWindowHandle). Core 를 링크하지 않는다 (06 R1).
# 그래픽 API 헤더는 render/<backend>/ 안에만 (tools/check_includes.py).
add_library(SandboxRender STATIC
    rhi/RhiTypes.hpp
    rhi/RhiTypes.cpp
    rhi/RenderDevice.hpp
    rhi/Backends.hpp
    rhi/RenderDeviceFactory.cpp
    rhi/HandlePool.hpp
    rhi/DeferredDestruction.hpp
    rhi/UploadRing.hpp
    rhi/UploadRing.cpp
    rhi/ShaderTypes.hpp
    rhi/RangeAllocator.hpp
    rhi/PipelineValidation.hpp
    rhi/PipelineValidation.cpp
    asset/Image.hpp
    asset/Image.cpp
    asset/AssetId.hpp
    asset/ShelfPacker.hpp
    asset/AssetManager.hpp
    asset/AssetManager.cpp
    asset/MaterialLibrary.hpp
    asset/MaterialLibrary.cpp
    renderer/Camera2D.hpp
    renderer/Camera2D.cpp
    renderer/RenderWorld.hpp
    renderer/DebugDraw.hpp
    renderer/DebugDraw.cpp
    renderer/TerrainPass.hpp
    renderer/TerrainPass.cpp
    renderer/OverlayPasses.hpp
    renderer/OverlayPasses.cpp
    imgui/ImGuiRenderer.hpp
    imgui/ImGuiRenderer.cpp
    renderer/SpriteBatcher.hpp
    renderer/SpriteBatcher.cpp
    renderer/Renderer.hpp
    renderer/Renderer.cpp
)
add_library(Sandbox::Render ALIAS SandboxRender)

# stb 구현 TU 는 우리 경고 정책 밖 (서드파티 코드)
add_library(sbx_stb_impl OBJECT asset/StbImpl.cpp)
target_link_libraries(sbx_stb_impl PRIVATE sbx_stb)
target_sources(SandboxRender PRIVATE $<TARGET_OBJECTS:sbx_stb_impl>)

if(WIN32)
    target_sources(SandboxRender PRIVATE
        dx12/Dx12Common.hpp
        dx12/Dx12Common.cpp
        dx12/Dx12Device.hpp
        dx12/Dx12Device.cpp
        dx12/Dx12CommandList.cpp
        dx12/Dx12Pipeline.cpp
        dx12/Dx12SwapChain.cpp)
    target_compile_definitions(SandboxRender PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
                                                     _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_link_libraries(SandboxRender PRIVATE d3d12 dxgi dxguid)
endif()
# Vulkan (Linux 기본, Windows 는 SBX_ENABLE_VULKAN 으로 D3D12 와 함께 — ADR-0032). 로더는 volk 가 실행 중에 연다.
if(SBX_ENABLE_VULKAN)
    target_sources(SandboxRender PRIVATE
        vulkan/VulkanCommon.hpp
        vulkan/VulkanCommon.cpp
        vulkan/VulkanDevice.hpp
        vulkan/VulkanDevice.cpp
        vulkan/VulkanPipeline.cpp
        vulkan/VulkanCommandList.cpp
        vulkan/VulkanSwapChain.cpp)
    target_link_libraries(SandboxRender PRIVATE sbx_volk)
    # Vk*Info x{VK_STRUCTURE_TYPE_…} 는 Vulkan 의 관용 초기화 — 나머지 필드는 0 이 맞다
    if(NOT MSVC)
        set_source_files_properties(vulkan/VulkanCommon.cpp vulkan/VulkanDevice.cpp vulkan/VulkanPipeline.cpp
                                    vulkan/VulkanCommandList.cpp vulkan/VulkanSwapChain.cpp
                                    PROPERTIES COMPILE_OPTIONS -Wno-missing-field-initializers)
    endif()
endif()
# 이 빌드에 들어간 백엔드 (render/rhi/Backends.hpp · RenderDeviceFactory, 시험의 기대값 — PUBLIC)
if(WIN32)
    target_compile_definitions(SandboxRender PUBLIC SBX_HAS_D3D12=1)
endif()
if(SBX_ENABLE_VULKAN)
    target_compile_definitions(SandboxRender PUBLIC SBX_HAS_VULKAN=1)
endif()

target_link_libraries(SandboxRender
    PUBLIC SandboxPlatform
    PRIVATE sbx_stb sbx_nlohmann_json sbx_imgui sbx_warnings)

# 내장 셰이더 (06 6장, ADR-0019) — SBX_BUILD_SHADERS=ON 일 때만. 생성 헤더: render/generated/<Pascal>Shader.hpp
sbx_add_shader(TARGET SandboxRender NAME basic_color SOURCE shaders/basic_color.hlsl STAGES vs:VSMain ps:PSMain)
sbx_add_shader(TARGET SandboxRender NAME basic_texture SOURCE shaders/basic_texture.hlsl STAGES vs:VSMain ps:PSMain)
sbx_add_shader(TARGET SandboxRender NAME sprite SOURCE shaders/sprite.hlsl STAGES vs:VSMain ps:PSMain)
sbx_add_shader(TARGET SandboxRender NAME terrain SOURCE shaders/terrain.hlsl STAGES vs:VSMain ps:PSMain)
sbx_add_shader(TARGET SandboxRender NAME grid SOURCE shaders/grid.hlsl STAGES vs:VSMain ps:PSMain)
sbx_add_shader(TARGET SandboxRender NAME lines SOURCE shaders/lines.hlsl STAGES vs:VSMain ps:PSMain)
sbx_add_shader(TARGET SandboxRender NAME imgui SOURCE shaders/imgui.hlsl STAGES vs:VSMain ps:PSMain)
if(SBX_BUILD_SHADERS)
    target_compile_definitions(SandboxRender PUBLIC SBX_HAS_SHADERS=1)
endif()
