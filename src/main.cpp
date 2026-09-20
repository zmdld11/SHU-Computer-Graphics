// 程序入口：组装窗口、画板、上屏与交互控制器。
// 具体算法在 src/raster、src/geom、src/curves；自测与演示图在 src/tests。
#include "core/Canvas.hpp"
#include "core/FrameBuffer.hpp"
#include "tests/SmokeChecks.hpp"
#include "ui/PaintController.hpp"
#include "ui/Window.hpp"

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
    window.setOnMouseButton([&ctrl](int button, int action) { ctrl.onMouseButton(button, action); });
    window.setOnMouseMove([&ctrl](double x, double y) { ctrl.onMouseMove({int(x), int(y)}); });

    while (!window.shouldClose()) {
        ctrl.render(live);
        window.setTitle(ctrl.title());
        liveCanvas.present(live);
        window.swapBuffers();
        window.pollEvents();
    }
    return 0;
}
