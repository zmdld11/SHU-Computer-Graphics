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
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace cg::game {

// 从文本按顺序取前两个正整数当宽/高：容忍空行/CRLF/多余文字，凑够两个数就成功。
bool parseResolutionText(const std::string& text, int& width, int& height) {
    int values[2] = {0, 0};
    int count = 0;
    bool inNumber = false;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            if (!inNumber) {
                if (count >= 2) {
                    break; // 前两个数已凑齐，后面的数字不再关心
                }
                inNumber = true;
                ++count;
            }
            values[count - 1] = values[count - 1] * 10 + (c - '0');
            if (values[count - 1] > 999999) { // 防御超长数字溢出
                return false;
            }
        } else {
            inNumber = false;
        }
    }
    if (count < 2 || values[0] <= 0 || values[1] <= 0) {
        return false;
    }
    width = values[0];
    height = values[1];
    return true;
}

// 夹到 [800×600, maxW×maxH]：低于下限 HUD 放不下，高于上限会超出屏幕。
FarmResolution clampResolution(int width, int height, int maxW, int maxH) {
    FarmResolution r;
    r.width = std::clamp(width, 800, std::max(800, maxW));
    r.height = std::clamp(height, 600, std::max(600, maxH));
    return r;
}

FarmResolution loadResolutionFile() {
    FarmResolution r;
    std::ifstream in("resolution.txt");
    if (!in) {
        std::cout << "[game] 未找到 resolution.txt，使用默认 800x600" << std::endl;
        return r;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    int w = 0;
    int h = 0;
    if (!parseResolutionText(buffer.str(), w, h)) {
        std::cout << "[game] resolution.txt 内容无效，使用默认 800x600" << std::endl;
        return r;
    }
    // 上限夹到主显示器当前分辨率，避免窗口开得比屏幕还大
    int maxW = 3840;
    int maxH = 2160;
    if (glfwInit() == GLFW_TRUE) {
        if (const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor())) {
            maxW = mode->width;
            maxH = mode->height;
        }
    }
    r = clampResolution(w, h, maxW, maxH);
    std::cout << "[game] resolution.txt: " << r.width << "x" << r.height << std::endl;
    return r;
}

int runFarmGame() {
    const FarmResolution res = loadResolutionFile();

    FarmWorld world;
    const ViewportFit fit0 = fitViewport(res.width, res.height);
    world.setViewport(fit0.viewW, fit0.viewH);

    std::cout << "[game] 正在烘焙地图（2x 超采样）..." << std::endl;
    FarmRender render;
    render.build(world);
    std::cout << "[game] 地图就绪，左键点地面移动，右键取消，1/2/3 选种子，R 重开" << std::endl;

    Window window(res.width, res.height, "胖龙农场 - 左键移动", false);
    if (!window.valid()) {
        return 1;
    }
    window.setMinSize(800, 600);

    FrameBuffer fb(window.fbWidth(), window.fbHeight());
    Canvas canvas(fb.width(), fb.height(), window.loader());
    if (!canvas.valid() || fb.width() <= 0 || fb.height() <= 0) {
        return 1;
    }

    // 窗口 > 地图时先把相机窗渲到 camFb，再整幅放大铺满窗口；scale==1 保持直渲零开销
    ViewportFit fit = fitViewport(fb.width(), fb.height());
    FrameBuffer camFb(fit.viewW, fit.viewH);
    world.setViewport(fit.viewW, fit.viewH);

    auto applyViewport = [&](int w, int h) {
        fit = fitViewport(w, h);
        camFb.resize(fit.viewW, fit.viewH);
        world.setViewport(fit.viewW, fit.viewH);
    };

    window.setOnResize([&](int w, int h) {
        if (w <= 0 || h <= 0) {
            return;
        }
        fb.resize(w, h);
        canvas.resize(w, h);
        applyViewport(w, h);
    });

    // 窗口像素 → 相机像素：放大时除以倍数，HUD 命中与点击换算共用
    auto toCam = [&](int mx, int my) {
        return IPoint{static_cast<int>(mx / fit.scale), static_cast<int>(my / fit.scale)};
    };
    auto toWorld = [&](int mx, int my) {
        const IPoint c = toCam(mx, my);
        return FPoint{static_cast<float>(c.x) + world.camera().x,
                      static_cast<float>(c.y) + world.camera().y};
    };

    int mouseX = 0;
    int mouseY = 0;
    bool mouseSeen = false;
    bool timeScrubbing = false;

    window.setOnMouseMove([&](double x, double y, int) {
        mouseX = static_cast<int>(x);
        mouseY = static_cast<int>(y);
        mouseSeen = true;
        if (timeScrubbing) {
            // 拖出窗口外就结束拖拽，避免松开时收不到事件、时间一直冻住
            if (mouseX < 0 || mouseY < 0 || mouseX >= fb.width() || mouseY >= fb.height()) {
                timeScrubbing = false;
                world.endTimeScrub();
                return;
            }
            const IPoint m = toCam(mouseX, mouseY);
            world.scrubTimeTo(timeFromTrackX(static_cast<float>(m.x), fit.viewW));
        }
    });

    window.setOnMouseButton([&](int button, int action, int) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE && timeScrubbing) {
            timeScrubbing = false;
            world.endTimeScrub();
            return;
        }
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

        const IPoint m = toCam(mouseX, mouseY);
        // 右下角常驻重开按钮：任何时候都能开新局（订单完成后游戏不结束）。
        if (restartButtonRect(fit.viewW, fit.viewH).inflated(2).contains(m)) {
            world.reset();
            applyViewport(fb.width(), fb.height());
            return;
        }
        // 时间轴：在轨道上下 8px 内按下即进入拖拽，横向位置直接映射到 0-24 点。
        if (timeTrackRect(fit.viewW).inflated(8).contains(m)) {
            timeScrubbing = true;
            world.beginTimeScrub();
            world.scrubTimeTo(timeFromTrackX(static_cast<float>(m.x), fit.viewW));
            return;
        }
        if (inHud(m, fit.viewW, fit.viewH)) {
            for (int i = 0; i < 3; ++i) {
                if (hotbarSlotRect(i, fit.viewW, fit.viewH).inflated(4).contains(m)) {
                    world.selectSeed(i);
                    return;
                }
            }
            return;
        }
        world.onClick(toWorld(mouseX, mouseY));
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
            applyViewport(fb.width(), fb.height());
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

        if (mouseSeen && !inHud(toCam(mouseX, mouseY), fit.viewW, fit.viewH)) {
            world.onHover(toWorld(mouseX, mouseY));
        } else {
            world.clearHover();
        }

        if (fb.width() > 0 && fb.height() > 0) {
            if (fit.scale > 1.0f) {
                render.frame(camFb, world, true);
                fb.blitScaledFrom(camFb);
            } else {
                render.frame(fb, world, true);
            }
            canvas.present(fb);
            window.swapBuffers();
        }
        window.pollEvents();
    }
    return 0;
}

bool writeDragonSheet() {
    FarmRender render;
    const FrameBuffer sheet = render.captureDragonSheet();
    if (!cg::writePPM("dragon-sheet.ppm", sheet)) {
        return false;
    }
    std::cout << "[game] 已写出 dragon-sheet.ppm（6 造型 × 站立/走路）" << std::endl;
    return true;
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

    // 昼夜系统验证：同一视角在晨/午/暮/夜四个时刻出图，
    // 影子方向与长度、路灯亮灭、环境色都由当帧的光照计算决定。
    // 6.2 与 17.8 落在双光源重叠窗口里（太阳 + 路灯同时投影）。
    const struct {
        float hour;
        const char* name;
    } dayFrames[] = {{6.2f, "game-day-0612.ppm"},
                     {12.5f, "game-day-1230.ppm"},
                     {17.8f, "game-day-1748.ppm"},
                     {23.0f, "game-night-2300.ppm"}};
    for (const auto& f : dayFrames) {
        world.debugSetTime(f.hour);
        world.debugSetPlayerCell(16, 15);
        render.frame(view, world, true);
        if (!cg::writePPM(f.name, view)) {
            return false;
        }
    }

    // 水面碎光：同一镜头在黄昏与深夜各一张，光斑位置随光源方位横移。
    // 站到栈道尽头，镜头才带得到完整的池塘。
    world.debugSetPlayerCell(kDockTipCol - 1, kDockTipRow);
    const struct {
        float hour;
        const char* name;
    } glintFrames[] = {{17.8f, "game-pond-dusk.ppm"},
                       {22.0f, "game-pond-night.ppm"}};
    for (const auto& f : glintFrames) {
        world.debugSetTime(f.hour);
        render.frame(view, world, true);
        if (!cg::writePPM(f.name, view)) {
            return false;
        }
    }

    // 元素动画验证：浇水水花与收获闪光的瞬间帧（白天拍，粒子看得清）。
    world.debugSetTime(10.0f);
    world.debugSetPlayerCell(plotCenterCell(2).x, plotCenterCell(2).y);
    world.debugPlant(2, Seed::Cabbage, CropStage::Sprout, false);
    world.onClick(plotCenter(2)); // 站在田上点田：立即浇水，粒子正好在空中
    render.frame(view, world, true);
    if (!cg::writePPM("game-fx-watering.ppm", view)) {
        return false;
    }
    world.debugPlant(3, Seed::Carrot, CropStage::Mature, false);
    world.debugSetPlayerCell(plotCenterCell(3).x, plotCenterCell(3).y);
    world.onClick(plotCenter(3)); // 收获闪光
    render.frame(view, world, true);
    if (!cg::writePPM("game-fx-harvest.ppm", view)) {
        return false;
    }

    // 抛竿落水：鱼漂飞到水面、水花与涟漪全在开阔水面上，特效一目了然。
    world.debugSetPlayerCell(kDockTipCol, kDockTipRow);
    world.onClick({kPondCx, kPondCy + 80.0f}); // 已在栈道尽头：立即抛竿
    world.update(FarmWorld::kCastTime + 0.12f); // 推过抛竿
    world.update(0.05f);                        // 落水水花正炸开、鱼漂就位
    render.frame(view, world, true);
    if (!cg::writePPM("game-fx-cast.ppm", view)) {
        return false;
    }

    // 场景切换验证：小屋室内白天/深夜各一张（窗户天色与夜色随时刻变）。
    world.debugEnterCabin();
    world.debugSetTime(12.5f);
    render.frame(view, world, true);
    if (!cg::writePPM("game-cabin-day.ppm", view)) {
        return false;
    }
    world.debugSetTime(23.0f);
    render.frame(view, world, true);
    if (!cg::writePPM("game-cabin-night.ppm", view)) {
        return false;
    }

    std::cout << "[game] 已写出 game-background.ppm / game-layer-1..6.ppm / "
                 "game-world.ppm / game-pond.ppm / game-farm.ppm / "
                 "game-day-0612|1230|1748 / game-night-2300.ppm / "
                 "game-fx-watering|harvest|cast.ppm / game-cabin-day|night.ppm"
              << std::endl;
    return true;
}

} // namespace cg::game
