#include "core/Canvas.hpp"
#include "core/FrameBuffer.hpp"
#include "raster/Rasterizer.hpp"
#include "ui/PaintController.hpp"
#include "ui/Window.hpp"
#include <iostream>
#include <string>

namespace {

constexpr int kWidth = 800;
constexpr int kHeight = 600;

// 实验一算法自测：不依赖人眼，退出码 0 = 全部通过
int runAlgorithmSelfChecks() {
    using namespace cg;
    using namespace cg::raster;
    const Color W = Color::White();

    int fails = 0;
    auto expect = [&](bool ok, const char* name) {
        if (!ok) {
            std::cerr << "[smoke] FAIL: " << name << std::endl;
            ++fails;
        }
    };

    FrameBuffer fb(kWidth, kHeight);

    // ---- 直线：任意斜率 ----
    fb.clear(Color::Black());
    drawLine(fb, {0, 0}, {kWidth - 1, kHeight - 1}, W, {});
    expect(fb.pixel(0, 0) == W && fb.pixel(kWidth - 1, kHeight - 1) == W, "line: 对角线端点");

    fb.clear(Color::Black());
    drawLine(fb, {100, 0}, {100, 50}, W, {});
    expect(fb.pixel(100, 25) == W, "line: 垂直线");

    fb.clear(Color::Black());
    drawLine(fb, {400, 100}, {300, 400}, W, {});
    expect(fb.pixel(400, 100) == W && fb.pixel(300, 400) == W, "line: 陡负斜率端点");

    // 起终点对称性：A→B 与 B→A 像素集合应一致
    FrameBuffer f1(kWidth, kHeight), f2(kWidth, kHeight);
    drawLine(f1, {10, 20}, {200, 90}, W, {});
    drawLine(f2, {200, 90}, {10, 20}, W, {});
    bool symmetric = true;
    for (int y = 0; y < kHeight && symmetric; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            if (f1.pixel(x, y) != f2.pixel(x, y)) {
                symmetric = false;
                break;
            }
        }
    }
    expect(symmetric, "line: 起终点对称");

    // ---- 圆：四轴点 + 45° 附近 ----
    fb.clear(Color::Black());
    drawCircle(fb, {200, 200}, 50, W, {});
    expect(fb.pixel(250, 200) == W && fb.pixel(150, 200) == W, "circle: 左右轴点");
    expect(fb.pixel(200, 150) == W && fb.pixel(200, 250) == W, "circle: 上下轴点");
    bool found45 = false;
    for (int x = 230; x <= 240 && !found45; ++x) {
        for (int y = 230; y <= 240; ++y) {
            if (fb.pixel(x, y) == W) {
                found45 = true;
                break;
            }
        }
    }
    expect(found45, "circle: 45°附近有像素");

    // ---- 圆弧：0°→90°（顺时针，右→下）----
    fb.clear(Color::Black());
    drawArc(fb, {400, 300}, 80, 0.0f, 90.0f, W, {});
    expect(fb.pixel(480, 300) == W, "arc: 含 0°");
    expect(fb.pixel(400, 380) == W, "arc: 含 90°");
    expect(fb.pixel(320, 300) == Color::Black(), "arc: 不含 180°");
    expect(fb.pixel(400, 220) == Color::Black(), "arc: 不含 270°");

    // ---- 线宽：宽 5 的水平线，中点上下各扩 2 像素 ----
    fb.clear(Color::Black());
    drawLine(fb, {50, 100}, {150, 100}, W, {0xFFFFu, 5});
    expect(fb.pixel(100, 98) == W && fb.pixel(100, 102) == W, "width: 线宽5扩2像素");

    // ---- 线型：点线 0x8888（bit3/7/11/15 落点）----
    fb.clear(Color::Black());
    drawLine(fb, {50, 120}, {150, 120}, W, {0x8888u, 1});
    expect(fb.pixel(50, 120) == Color::Black(), "style: 点线第0步不画");
    expect(fb.pixel(53, 120) == W, "style: 点线第3步画");

    return fails;
}

} // namespace

int main(int argc, char** argv) {
    const bool smoke = argc > 1 && std::string(argv[1]) == "--smoke";

    cg::Window window(kWidth, kHeight, "SHU-Computer-Graphics 实验一", smoke);
    if (!window.valid()) {
        return 1;
    }

    cg::FrameBuffer fb(kWidth, kHeight);
    cg::Canvas canvas(kWidth, kHeight, window.loader());
    if (!canvas.valid()) {
        return 1;
    }

    if (smoke) {
        const int fails = runAlgorithmSelfChecks();
        canvas.present(fb); // 最后一组自测图形上屏一帧
        window.swapBuffers();
        std::cout << (fails == 0 ? "smoke test passed" : "smoke test FAILED") << std::endl;
        return fails == 0 ? 0 : 1;
    }

    cg::PaintController ctrl;
    window.setOnKey([&ctrl](int key, int action) { ctrl.onKey(key, action); });
    window.setOnMouseButton([&ctrl](int button, int action) { ctrl.onMouseButton(button, action); });
    window.setOnMouseMove([&ctrl](double x, double y) { ctrl.onMouseMove({int(x), int(y)}); });

    while (!window.shouldClose()) {
        ctrl.render(fb);
        window.setTitle(ctrl.title());
        canvas.present(fb);
        window.swapBuffers();
        window.pollEvents();
    }
    return 0;
}
