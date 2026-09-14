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

# glad（OpenGL 函数加载器）源码已生成并提交在 third_party/glad，构建零额外依赖。
# 如需重新生成（如换 GL 版本）：克隆 https://github.com/Dav1dde/glad ，
# 在其目录执行 pip install jinja2 && python -m glad --api "gl:core=3.3" --out-path <仓库>/third_party/glad c

# ---------------- 引擎本体 ----------------
add_executable(cg-engine
    src/main.cpp
    src/core/FrameBuffer.cpp
    src/core/Canvas.cpp
    src/raster/Rasterizer.cpp
    src/ui/Window.cpp
    src/ui/PaintController.cpp
    third_party/glad/src/gl.c
)

target_include_directories(cg-engine PRIVATE src third_party/glad/include)
target_link_libraries(cg-engine PRIVATE glfw)

if(MSVC)
    target_compile_options(cg-engine PRIVATE /W4)
else()
    target_compile_options(cg-engine PRIVATE -Wall -Wextra -Wpedantic)
endif()
