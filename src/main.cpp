// main.cpp
// 实现程序入口：解析 --smoke / --demo 启动参数，组装窗口、画板、上屏与交互控制器并运行主循环。
// 具体算法在 src/raster、src/geom、src/curves；自测与演示图在 src/tests。
#include "core/Canvas.hpp"
#include "core/FrameBuffer.hpp"
#include "tests/SmokeChecks.hpp"
#include "ui/PaintController.hpp"
#include "ui/Window.hpp"

#include <GLFW/glfw3.h>
#include <iostream>
#include <memory>
#include <string>

namespace {
constexpr int kWidth = 800;
constexpr int kHeight = 600;
} // namespace

int main(int argc, char** argv) {
    const std::string arg = (argc > 1) ? argv[1] : "";

    // --demo：不开窗口，直接把演示场景渲染成图片（供自查/报告插图）
    if (arg == "--demo") {
        if (!cg::tests::writeDemoImage("demo.ppm")) {
            return 1;
        }
        std::cout << "demo image written: demo.ppm" << std::endl;
        return 0;
    }

    // --demo-ui：不开窗口，模拟一段操作把"任务栏 + 图形 + 常驻裁剪 + 选中"渲染成图片
    //（UI 自查/报告截图；任务栏文字用系统字体，画在画布里所以截得下来）
    if (arg == "--demo-ui") {
        cg::FrameBuffer fb(kWidth, kHeight);
        cg::PaintController ctrl;
        ctrl.onKey(GLFW_KEY_1, GLFW_PRESS); // 直线（穿过窗口左缘）
        ctrl.onMouseMove({60, 120});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({280, 220});
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onKey(GLFW_KEY_2, GLFW_PRESS); // 圆（右上，部分出窗）
        ctrl.onMouseMove({560, 180});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({680, 180});
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onKey(GLFW_KEY_4, GLFW_PRESS); // 填充多边形（下缘出窗）
        const cg::IPoint tri[3] = {{120, 400}, {320, 380}, {200, 540}};
        for (const auto& pt : tri) {
            ctrl.onMouseMove(pt);
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseButton(0, GLFW_RELEASE);
        }
        ctrl.onMouseButton(1, GLFW_PRESS); // 右键闭合
        ctrl.onKey(GLFW_KEY_8, GLFW_PRESS); // 曲线：4 控制点连点 + 右键提交（下方会被窗口裁剪）
        const cg::IPoint cpts[4] = {{380, 520}, {470, 420}, {580, 540}, {690, 440}};
        for (const auto& pt : cpts) {
            ctrl.onMouseMove(pt);
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseButton(0, GLFW_RELEASE);
        }
        ctrl.onMouseButton(1, GLFW_PRESS); // 右键提交曲线
        ctrl.onKey(GLFW_KEY_6, GLFW_PRESS); // 裁剪模式：K 拖拽定义窗口 → 常驻裁剪开启
        ctrl.onKey(GLFW_KEY_K, GLFW_PRESS);
        ctrl.onMouseMove({150, 150});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({650, 500});
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onKey(GLFW_KEY_7, GLFW_PRESS); // 变换模式：点圆环最右点选中圆
        ctrl.onMouseMove({680, 180});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onMouseMove({400, 300});
        ctrl.render(fb);
        if (!cg::tests::writePPM("demo-ui.ppm", fb)) {
            return 1;
        }
        std::cout << "demo ui image written: demo-ui.ppm" << std::endl;
        return 0;
    }

    const bool smoke = (arg == "--smoke"); // --smoke：隐藏窗口跑算法自测即可退出

    cg::Window window(kWidth, kHeight, "SHU-Computer-Graphics 绘图引擎", smoke);
    if (!window.valid()) {
        return 1;
    }

    cg::FrameBuffer fb(kWidth, kHeight);
    cg::Canvas canvas(kWidth, kHeight, window.loader());
    if (!canvas.valid()) {
        return 1;
    }

    if (smoke) {
        const int fails = cg::tests::runAlgorithmSelfChecks();
        canvas.present(fb); // 渲染一帧验证上屏管线
        window.swapBuffers();
        std::cout << (fails == 0 ? "smoke test passed" : "smoke test FAILED") << std::endl;
        return fails == 0 ? 0 : 1;
    }

    cg::PaintController ctrl;

    // 画板跟随窗口尺寸：用 unique_ptr 以便 resize 时重建
    auto fbLive = std::make_unique<cg::FrameBuffer>(window.fbWidth(), window.fbHeight());
    auto canvasLive =
        std::make_unique<cg::Canvas>(fbLive->width(), fbLive->height(), window.loader());
    if (!canvasLive->valid()) {
        return 1;
    }
    cg::FrameBuffer& live = *fbLive;
    cg::Canvas& liveCanvas = *canvasLive;
    window.setOnResize([&](int w, int h) {
        live.resize(w, h);
        liveCanvas.resize(w, h);
    });

    window.setOnKey([&ctrl](int key, int action) { ctrl.onKey(key, action); });
    window.setOnMouseButton([&ctrl](int button, int action, int mods) {
        ctrl.onMouseButton(button, action, mods);
    });
    window.setOnMouseMove([&ctrl](double x, double y) { ctrl.onMouseMove({int(x), int(y)}); });

    while (!window.shouldClose()) {
        // 1. 画出这一帧所有的像素
        ctrl.render(live);
        // 2. 标题栏显示当前模式提示
        window.setTitle(ctrl.title());
        // 3. 像素阵列 → GPU → 窗口
        liveCanvas.present(live);
        // 4. 显示画面（双缓冲）
        window.swapBuffers();
        // 5. 接收新的鼠标和键盘事件
        window.pollEvents();
    }
    return 0;
}
