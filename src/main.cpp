#include "core/Canvas.hpp"
#include "core/FrameBuffer.hpp"
#include "ui/Window.hpp"
#include <iostream>
#include <string>

namespace {

constexpr int kWidth = 800;
constexpr int kHeight = 600;

// 骨架自检图案：绿色十字 + 四角红点，验证 putPixel、行 0 在顶部、上屏方向
void drawTestPattern(cg::FrameBuffer& fb) {
    fb.clear(cg::Color::Black());

    for (int x = 0; x < kWidth; x += 5) {
        fb.putPixel(x, kHeight / 2, cg::Color::Green());
    }
    for (int y = 0; y < kHeight; y += 5) {
        fb.putPixel(kWidth / 2, y, cg::Color::Green());
    }

    fb.putPixel(0, 0, cg::Color::Red());
    fb.putPixel(kWidth - 1, 0, cg::Color::Red());
    fb.putPixel(0, kHeight - 1, cg::Color::Red());
    fb.putPixel(kWidth - 1, kHeight - 1, cg::Color::Red());
}

} // namespace

int main(int argc, char** argv) {
    const bool smoke = argc > 1 && std::string(argv[1]) == "--smoke";

    cg::Window window(kWidth, kHeight, "SHU-Computer-Graphics 绘图引擎", smoke);
    if (!window.valid()) {
        return 1;
    }

    cg::FrameBuffer fb(kWidth, kHeight);
    cg::Canvas canvas(kWidth, kHeight, window.loader());
    if (!canvas.valid()) {
        return 1;
    }

    drawTestPattern(fb);

    if (smoke) {
        // 无窗口自检：渲染一帧即退出，退出码 0 = 渲染管线通
        canvas.present(fb);
        window.swapBuffers();
        std::cout << "smoke test passed" << std::endl;
        return 0;
    }

    while (!window.shouldClose()) {
        canvas.present(fb);
        window.swapBuffers();
        window.pollEvents();
    }
    return 0;
}
