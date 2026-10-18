// FarmChecks.hpp
// 农场玩法无窗口自测入口（--game-smoke）：只依赖 FarmWorld，不需要 GLFW/OpenGL。
#pragma once

namespace cg::tests {

// 返回失败项数（0 = 全部通过）。
int runFarmChecks();

} // namespace cg::tests
