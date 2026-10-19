// FarmGame.cpp
// 实现游戏主循环：窗口/画布/输入、相机坐标换算、无窗口截图。
// 玩法状态全在 FarmWorld，像素绘制全在 FarmRender，这一层只做胶水。
#include "game/FarmGame.hpp"

#include "core/Canvas.hpp"
#include "core/FrameBuffer.hpp"
#include "core/ImageIO.hpp"
#include "game/FarmRender.hpp"
#include "game/FarmWorld.hpp"
#include "ui/Window.hpp"

#include <GLFW/glfw3.h>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <vector>

namespace cg::game {
namespace {

FPoint screenToWorld(double mx, double my, const FarmWorld& world) {
    return {static_cast<float>(mx) + world.camera().x,
            static_cast<float>(my) + world.camera().y};
}

} // namespace

int runFarmGame() {
    FarmWorld world;
    world.setViewport(800, 600);

    std::cout << "[game] 正在烘焙地图（2x 超采样）..." << std::endl;
    FarmRender render;
    render.build(world);
    std::cout << "[game] 地图就绪，左键点地面移动，右键取消，1/2/3 选种子，R 重开" << std::endl;

    Window window(800, 600, "胖龙农场 - 左键移动", false);
    if (!window.valid()) {
        return 1;
    }
    window.setMinSize(800, 600);

    FrameBuffer fb(window.fbWidth(), window.fbHeight());
    Canvas canvas(fb.width(), fb.height(), window.loader());
    if (!canvas.valid() || fb.width() <= 0 || fb.height() <= 0) {
        return 1;
    }
    world.setViewport(fb.width(), fb.height());

    window.setOnResize([&](int w, int h) {
        if (w <= 0 || h <= 0) {
            return;
        }
        fb.resize(w, h);
        canvas.resize(w, h);
        world.setViewport(w, h);
    });

    int mouseX = 0;
    int mouseY = 0;
    bool mouseSeen = false;

    window.setOnMouseMove([&](double x, double y, int) {
        mouseX = static_cast<int>(x);
        mouseY = static_cast<int>(y);
        mouseSeen = true;
    });

    window.setOnMouseButton([&](int button, int action, int) {
        if (action != GLFW_PRESS) {
            return;
        }
        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            world.cancelMove();
            return;
        }
        if (button != GLFW_MOUSE_BUTTON_LEFT) {
            return;
        }

        const IPoint m{mouseX, mouseY};
        if (inHud(m, fb.width(), fb.height())) {
            for (int i = 0; i < 3; ++i) {
                if (hotbarSlotRect(i, fb.width(), fb.height()).inflated(4).contains(m)) {
                    world.selectSeed(i);
                    return;
                }
            }
            return;
        }
        world.onClick(screenToWorld(mouseX, mouseY, world));
    });

    window.setOnKey([&](int key, int action, int) {
        if (action != GLFW_PRESS) {
            return;
        }
        if (key == GLFW_KEY_1) {
            world.selectSeed(0);
        } else if (key == GLFW_KEY_2) {
            world.selectSeed(1);
        } else if (key == GLFW_KEY_3) {
            world.selectSeed(2);
        } else if (key == GLFW_KEY_R) {
            world.reset();
            world.setViewport(fb.width(), fb.height());
        }
    });

    auto last = std::chrono::steady_clock::now();
    while (!window.shouldClose()) {
        const auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (dt > 0.05f) {
            dt = 0.05f;
        }

        world.update(dt);

        if (mouseSeen && !inHud({mouseX, mouseY}, fb.width(), fb.height())) {
            world.onHover(screenToWorld(mouseX, mouseY, world));
        } else {
            world.clearHover();
        }

        if (fb.width() > 0 && fb.height() > 0) {
            render.frame(fb, world, true);
            canvas.present(fb);
            window.swapBuffers();
        }
        window.pollEvents();
    }
    return 0;
}

bool writeFarmCapture() {
    FarmWorld world;
    world.setViewport(800, 600);

    // 汇报/报告用的纯背景：空田、无角色、无 HUD。
    {
        FarmWorld bgWorld;
        bgWorld.setViewport(800, 600);
        FarmRender bgRender;
        bgRender.build(bgWorld);
        if (!cg::writePPM("game-background.ppm", bgRender.captureBackground())) {
            return false;
        }

        const char* layerNames[] = {"game-layer-1-ground.ppm",
                                    "game-layer-2-path.ppm",
                                    "game-layer-3-farm.ppm",
                                    "game-layer-4-pond.ppm",
                                    "game-layer-5-meadow.ppm",
                                    "game-layer-6-trees.ppm"};
        const std::vector<FrameBuffer> layers = bgRender.captureLayers(bgWorld);
        for (std::size_t i = 0; i < layers.size(); ++i) {
            if (!cg::writePPM(layerNames[i], layers[i])) {
                return false;
            }
        }
    }

    // 美术展示用的一组作物：三种作物、三个生长阶段、干土/湿土都有。
    world.debugPlant(0, Seed::Cabbage, CropStage::Mature, false);
    world.debugPlant(1, Seed::Cabbage, CropStage::Sprout, true);
    world.debugPlant(2, Seed::Cabbage, CropStage::Seed, false);
    world.debugPlant(3, Seed::Carrot, CropStage::Mature, false);
    world.debugPlant(4, Seed::Tomato, CropStage::Mature, true);
    world.debugPlant(6, Seed::Cabbage, CropStage::Mature, true);
    world.debugPlant(7, Seed::Tomato, CropStage::Sprout, false);
    world.debugPlant(8, Seed::Carrot, CropStage::Sprout, true);
    world.debugPlant(9, Seed::Tomato, CropStage::Mature, true);
    world.debugPlant(11, Seed::Carrot, CropStage::Mature, false);

    world.debugSetPlayerCell(kDockTipCol - 1, kDockRow);

    std::cout << "[game] 正在渲染美术预览..." << std::endl;
    FarmRender render;
    render.build(world);

    const FrameBuffer map = render.captureWorld(world);
    if (!cg::writePPM("game-world.ppm", map)) {
        return false;
    }

    FrameBuffer view(800, 600);
    render.frame(view, world, true);
    if (!cg::writePPM("game-pond.ppm", view)) {
        return false;
    }

    world.debugSetPlayerCell(16, 15);
    render.frame(view, world, false);
    if (!cg::writePPM("game-farm.ppm", view)) {
        return false;
    }

    std::cout << "[game] 已写出 game-background.ppm / game-layer-1..6.ppm / "
                 "game-world.ppm / game-pond.ppm / game-farm.ppm" << std::endl;
    return true;
}

} // namespace cg::game
