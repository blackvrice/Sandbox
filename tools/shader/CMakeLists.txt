# sbx_msl — SPIR-V → MSL 호스트 도구 (SBX_SHADER_MSL, ADR-0035). sbx_shader_gen.py 가 셰이더 빌드 중에 부른다.
add_executable(sbx_msl sbx_msl.cpp)
target_link_libraries(sbx_msl PRIVATE sbx_spirv_cross sbx_warnings)
