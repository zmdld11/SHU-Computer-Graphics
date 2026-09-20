#pragma once

namespace cg::tests {

// 算法自测：逐项断言各算法行为，返回失败项数（0 = 全部通过）。
// main.cpp 的 --smoke 模式调用它；不需要窗口/OpenGL。
int runAlgorithmSelfChecks();

// 把"实验一 + 实验二"的演示场景渲染成一张图，导出为 PPM（P6）文件。
// 用途：无需窗口即可产出自查图/报告插图；无需任何第三方库。
bool writeDemoImage(const char* path);

} // namespace cg::tests
