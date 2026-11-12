// FarmGame.hpp
// 游戏入口：--game 窗口模式、--game-capture 无窗口出图（美术评审/报告插图）。
#pragma once
#include <string>

namespace cg::game {

// 分辨率配置：来自 resolution.txt（第一行宽、第二行高），缺失/非法时回落默认。
struct FarmResolution {
    int width = 800;
    int height = 600;
};

// 从文本按顺序取前两个正整数当宽/高；不足两个或有非正数判失败。
bool parseResolutionText(const std::string& text, int& width, int& height);

// 把配置值夹到合法窗口范围 [800×600, maxW×maxH]：低于下限 HUD 放不下。
FarmResolution clampResolution(int width, int height, int maxW, int maxH);

// 读工作目录的 resolution.txt，上限取主显示器分辨率；仅窗口模式用。
FarmResolution loadResolutionFile();

// 启动农场窗口，返回进程退出码。
int runFarmGame();

// 写 game-world.ppm / game-pond.ppm / game-farm.ppm，返回是否全部成功。
bool writeFarmCapture();

// 写 dragon-sheet.ppm：6 个角色造型 × 站立/走路，供选型。
bool writeDragonSheet();

} // namespace cg::game
