cmake_minimum_required(VERSION 3.15)
project(DirectX12Cube LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(DirectX12Cube WIN32 src/main.cpp)

target_compile_definitions(DirectX12Cube PRIVATE WIN32_LEAN_AND_MEAN UNICODE)
target_include_directories(DirectX12Cube PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(DirectX12Cube PRIVATE d3d12 dxgi d3dcompiler)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders.hlsl DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
