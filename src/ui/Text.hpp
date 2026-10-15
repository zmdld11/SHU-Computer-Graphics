// Text.hpp
// 实现界面文字统一入口：Windows 下用 GDI 把系统字体（宋体 16px）栅格化成点阵，逐像素写入
// FrameBuffer；其他平台回退自写 5x7 点阵（Font5x7.hpp）。不走任何界面框架，绘制管线不变。
#pragma once
#include "core/Color.hpp"
#include "core/FrameBuffer.hpp"

namespace cg::text {

// 字体行高（像素）。任务栏布局按它动态计算，不要写死。
int height();

// UTF-8 字符串的显示宽度（像素），按缓存字形的步进宽累加
int textWidth(const char* s);

// 在 (x, y) = 首行行框顶部的位置绘制 UTF-8 文本（基线 = y + height() 的上开区间内）
void drawText(FrameBuffer& fb, int x, int y, const char* s, const Color& c);

} // namespace cg::text
