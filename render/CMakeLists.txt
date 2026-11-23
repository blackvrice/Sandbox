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

# Graphics SDK 단일 백엔드 (Phase 10: R0.35 — 자체 dx12/vulkan/metal 백엔드 제거, ADR-0168). 설치본 또는 고정 커밋 (ADR-0169)
include(SbxGraphicsSdk)
# 내장 셰이더 원본 (shaders/*.hlsl — include 를 푼 글 · SHA-256): D3D12 런타임 컴파일 (FXC SM 5.1) 과 Vulkan 생성 SPIR-V 의
# 출처 검사 (ADR-0171). 원본이 바뀌면 다시 구성된다
include(SbxEmbedHlsl)
sbx_embed_hlsl(OUTPUT "${SBX_GENERATED_DIR}/render/generated/GraphicsSdkHlslSources.hpp"
               SOURCE_DIR "${PROJECT_SOURCE_DIR}/shaders"
               NAMES basic_color basic_texture sprite terrain grid lines imgui)
target_include_directories(SandboxRender PRIVATE "${SBX_GENERATED_DIR}")
target_sources(SandboxRender PRIVATE
    graphics_sdk/GraphicsSdkTypes.hpp
    graphics_sdk/GraphicsSdkTypes.cpp
    graphics_sdk/GraphicsSdkResource.hpp
    graphics_sdk/GraphicsSdkPipeline.hpp
    graphics_sdk/GraphicsSdkPipeline.cpp
    graphics_sdk/GraphicsSdkSpirvData.hpp
    graphics_sdk/GraphicsSdkShaders.hpp
    graphics_sdk/GraphicsSdkShaders.cpp
    graphics_sdk/GraphicsSdkDevice.hpp
    graphics_sdk/GraphicsSdkDevice.cpp
    graphics_sdk/GraphicsSdkCommandQueue.hpp
    graphics_sdk/GraphicsSdkCommandQueue.cpp
    graphics_sdk/GraphicsSdkCommandList.hpp
    graphics_sdk/GraphicsSdkCommandList.cpp
    graphics_sdk/GraphicsSdkSwapChain.hpp
    graphics_sdk/GraphicsSdkSwapChain.cpp
    graphics_sdk/GraphicsSdkUiBridge.hpp
    graphics_sdk/GraphicsSdkUiBridge.cpp
    graphics_sdk/GraphicsSdkFactory.cpp)
target_compile_definitions(SandboxRender PUBLIC SBX_HAS_GRAPHICS_SDK=1)
target_link_libraries(SandboxRender PRIVATE GraphicsSDK::Core)

if(SBX_ENABLE_METAL AND SBX_BUILD_SHADERS AND NOT SBX_SHADER_MSL)
    message(WARNING "SBX_SHADER_MSL=OFF: 내장 셰이더에 MSL 이 없어 Metal 이 파이프라인을 만들지 못한다 (createShader 실패)")
endif()

# 이 빌드에 들어간 백엔드 (render/rhi/Backends.hpp · RenderDeviceFactory, 시험의 기대값 — PUBLIC)
if(WIN32)
    target_compile_definitions(SandboxRender PUBLIC SBX_HAS_D3D12=1)
endif()
if(SBX_ENABLE_VULKAN)
    target_compile_definitions(SandboxRender PUBLIC SBX_HAS_VULKAN=1)
endif()
if(SBX_ENABLE_METAL)
    target_compile_definitions(SandboxRender PUBLIC SBX_HAS_METAL=1)
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
    if(SBX_SHADER_MSL)
        target_compile_definitions(SandboxRender PUBLIC SBX_HAS_MSL=1) # ShaderBytecode::msl 이 찬다 (ADR-0035)
    endif()
endif()
