cmake_minimum_required(VERSION 3.15)
project(DirectX12Cube LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_SUPERSAMPLING "Enable 2x supersampling (renders at 1600x900, downsamples to 800x450)" OFF)

add_executable(DirectX12Cube WIN32
    src/main.cpp
    src/camera.cpp
    src/d3d_context.cpp
    src/frame_buffers.cpp
    src/passes/scene_pass.cpp
    src/passes/downsample_pass.cpp
    src/renderer.cpp
)

target_compile_definitions(DirectX12Cube PRIVATE WIN32_LEAN_AND_MEAN UNICODE)
if(ENABLE_SUPERSAMPLING)
    target_compile_definitions(DirectX12Cube PRIVATE ENABLE_SUPERSAMPLING)
endif()

if(MSVC)
    target_compile_options(DirectX12Cube PRIVATE /W4 /WX)
    target_compile_options(DirectX12Cube PRIVATE $<$<CONFIG:Release>:/O2>)
endif()

target_include_directories(DirectX12Cube PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(DirectX12Cube PRIVATE d3d12 dxgi d3dcompiler)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders.hlsl DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
