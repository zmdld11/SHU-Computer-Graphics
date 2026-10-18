// game_main.cpp
// 游戏专用入口：CLion 里直接选 farm-game 目标运行，不需要给 cg-engine 配 --game 参数。
// cg-engine 仍保留编辑器与 --smoke/--game-capture/--game-smoke 等命令行模式。
#include "game/FarmGame.hpp"

int main() {
    return cg::game::runFarmGame();
}
