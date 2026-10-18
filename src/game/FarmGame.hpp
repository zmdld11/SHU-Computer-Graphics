// FarmGame.hpp
// 游戏入口：--game 窗口模式、--game-capture 无窗口出图（美术评审/报告插图）。
#pragma once

namespace cg::game {

// 启动农场窗口，返回进程退出码。
int runFarmGame();

// 写 game-world.ppm / game-pond.ppm / game-farm.ppm，返回是否全部成功。
bool writeFarmCapture();

} // namespace cg::game
