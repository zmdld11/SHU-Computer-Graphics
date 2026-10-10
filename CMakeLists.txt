cmake_minimum_required(VERSION 3.20)
project(SHUComputerGraphics LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------- 第三方依赖（FetchContent，首次 configure 需联网） ----------------
include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)

# glad（OpenGL 函数加载器）已替换为自写的 src/core/GLLoader.{hpp,cpp}——
# 仓库不再包含任何第三方源码（历史版本曾用 glad 生成器，见 CHANGELOG lab2 前记录）。

# ---------------- 引擎本体 ----------------
add_executable(cg-engine
    src/main.cpp
    src/core/FrameBuffer.cpp
    src/core/Canvas.cpp
    src/core/GLLoader.cpp
    src/raster/Rasterizer.cpp
    src/raster/Fill.cpp
    src/raster/Clip.cpp
    src/tests/SmokeChecks.cpp
    src/ui/Window.cpp
    src/ui/PaintController.cpp
)

target_include_directories(cg-engine PRIVATE src)
target_link_libraries(cg-engine PRIVATE glfw)

if(MSVC)
    target_compile_options(cg-engine PRIVATE /W4)
else()
    target_compile_options(cg-engine PRIVATE -Wall -Wextra -Wpedantic)
endif()
