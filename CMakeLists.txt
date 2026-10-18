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
# 引擎实现抽成静态库：编辑器入口 main.cpp 和游戏入口 game_main.cpp 各链接一次，
# 源码只编译一遍。CLion 里直接选 farm-game 目标运行，不需要配 --game 参数。
add_library(cg-engine-lib STATIC
    src/core/FrameBuffer.cpp
    src/core/Canvas.cpp
    src/core/GLLoader.cpp
    src/core/ImageIO.cpp
    src/raster/Rasterizer.cpp
    src/raster/Fill.cpp
    src/raster/Clip.cpp
    src/curves/Bezier.cpp
    src/curves/BSpline.cpp
    src/geom/Transform.cpp
    src/game/FarmWorld.cpp
    src/game/FarmRender.cpp
    src/game/FarmGame.cpp
    src/tests/SmokeChecks.cpp
    src/tests/FarmChecks.cpp
    src/ui/Window.cpp
    src/ui/PaintController.cpp
    src/ui/Text.cpp
)

target_include_directories(cg-engine-lib PUBLIC src)
target_link_libraries(cg-engine-lib PUBLIC glfw)

# ui/Text 用 GDI 把系统字体栅格化成点阵（GetGlyphOutlineW 在 gdi32）
if(WIN32)
    target_link_libraries(cg-engine-lib PUBLIC gdi32)
endif()

# 编辑器 / 命令行多模式入口：--smoke、--demo、--game、--game-capture、--game-smoke
add_executable(cg-engine src/main.cpp)
target_link_libraries(cg-engine PRIVATE cg-engine-lib)

# 游戏专用入口：CLion 选这个目标直接运行，不用填 Program arguments
add_executable(farm-game src/game_main.cpp)
target_link_libraries(farm-game PRIVATE cg-engine-lib)

# 统一告警/编码/Debug 断言选项；静态库和两个入口都套用。
function(cg_set_build_flags target)
    if(MSVC)
        # /utf-8：源码是 UTF-8（中文注释/字符串），否则按代码页 936 解析会报 C2001/C4819
        # /EHsc：std::vector 等标准库需要异常展开语义（否则 C4530）
        target_compile_options(${target} PRIVATE /W4 /utf-8 /EHsc)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    # Debug 构建显式开启 libstdc++ 容器越界断言：任何越界下标立即 abort，
    # 不依赖工具链默认行为（CLion 捆绑 MinGW 默认开，其他 MinGW 环境未必）。
    # 同时用 -Og：保留断言与调试信息，但逐像素路径比 -O0 快一个量级，交互不至于卡顿。
    target_compile_options(${target} PRIVATE
        $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU,Clang>>:-D_GLIBCXX_ASSERTIONS>
        $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU,Clang>>:-Og>)
endfunction()

cg_set_build_flags(cg-engine-lib)
cg_set_build_flags(cg-engine)
cg_set_build_flags(farm-game)
