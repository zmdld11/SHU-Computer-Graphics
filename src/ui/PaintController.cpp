#include "ui/PaintController.hpp"
#include <GLFW/glfw3.h>
#include <cmath>

namespace cg {
namespace {

constexpr unsigned kMasks[3] = {0xFFFFu, 0xF0F0u, 0x8888u}; // 实线/虚线/点线
constexpr int kWidths[3] = {1, 3, 5};
constexpr const char* kStyleNames[3] = {"实线", "虚线", "点线"};
constexpr const char* kModeNames[3] = {"直线", "圆", "圆弧"};

float dist(IPoint a, IPoint b) {
    const int dx = a.x - b.x;
    const int dy = a.y - b.y;
    return std::sqrt(static_cast<float>(dx * dx + dy * dy));
}

// 屏幕坐标角度（度，[0,360)，顺时针为正）——与 raster::drawArc 的约定一致
float angleOf(IPoint p, IPoint c) {
    constexpr float kPi = 3.14159265358979f;
    float a = std::atan2(static_cast<float>(p.y - c.y), static_cast<float>(p.x - c.x)) *
              (180.0f / kPi);
    return a < 0.0f ? a + 360.0f : a;
}

void drawCross(FrameBuffer& fb, IPoint p, const Color& c, int half) {
    for (int i = -half; i <= half; ++i) {
        fb.putPixel(p.x + i, p.y, c);
        fb.putPixel(p.x, p.y + i, c);
    }
}

} // namespace

raster::LineStyle PaintController::currentStyle() const {
    return {kMasks[styleIdx_], kWidths[widthIdx_]};
}

void PaintController::drawShape(FrameBuffer& fb, const Shape& s, const Color& color) const {
    switch (s.mode) {
    case Mode::Line:
        raster::drawLine(fb, s.a, s.b, color, s.style);
        break;
    case Mode::Circle:
        raster::drawCircle(fb, s.a, s.radius, color, s.style);
        break;
    case Mode::Arc:
        raster::drawArc(fb, s.a, s.radius, s.startDeg, s.endDeg, color, s.style);
        break;
    }
}

void PaintController::onKey(int key, int action) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) {
        return;
    }
    switch (key) {
    case GLFW_KEY_1:
    case GLFW_KEY_L:
        mode_ = Mode::Line;
        arcAdjust_ = false;
        break;
    case GLFW_KEY_2:
    case GLFW_KEY_C:
        mode_ = Mode::Circle;
        arcAdjust_ = false;
        break;
    case GLFW_KEY_3:
    case GLFW_KEY_A:
        mode_ = Mode::Arc;
        break;
    case GLFW_KEY_S:
        styleIdx_ = (styleIdx_ + 1) % 3;
        break;
    case GLFW_KEY_W:
        widthIdx_ = (widthIdx_ + 1) % 3;
        break;
    case GLFW_KEY_E: // 清屏
        shapes_.clear();
        dragging_ = false;
        arcAdjust_ = false;
        break;
    default:
        break;
    }
}

void PaintController::onMouseButton(int button, int action) {
    const bool press = (action == GLFW_PRESS);

    if (button == 1) { // 右键：取消当前操作
        if (press) {
            dragging_ = false;
            arcAdjust_ = false;
        }
        return;
    }
    if (button != 0) {
        return;
    }

    switch (mode_) {
    case Mode::Line:
    case Mode::Circle:
        if (press) {
            dragging_ = true;
            press_ = curr_;
        } else if (dragging_) {
            dragging_ = false;
            if (mode_ == Mode::Line) {
                shapes_.push_back({Mode::Line, press_, curr_, 0, 0, 360, currentStyle()});
            } else if (dist(press_, curr_) >= 1.0f) {
                Shape s{};
                s.mode = Mode::Circle;
                s.a = press_;
                s.radius = static_cast<int>(dist(press_, curr_));
                s.endDeg = 360;
                s.style = currentStyle();
                shapes_.push_back(s);
            }
        }
        break;

    case Mode::Arc:
        if (!arcAdjust_) {
            // 第一阶段：拖拽定圆心、半径、起始角
            if (press) {
                dragging_ = true;
                press_ = curr_;
            } else if (dragging_) {
                dragging_ = false;
                if (dist(press_, curr_) >= 1.0f) {
                    arcAdjust_ = true;
                    arcCenter_ = press_;
                    arcRadius_ = static_cast<int>(dist(press_, curr_));
                    arcStart_ = angleOf(curr_, press_);
                }
            }
        } else if (press) {
            // 第二阶段：左键确认终止角并提交
            Shape s{};
            s.mode = Mode::Arc;
            s.a = arcCenter_;
            s.radius = arcRadius_;
            s.startDeg = arcStart_;
            s.endDeg = angleOf(curr_, arcCenter_);
            s.style = currentStyle();
            shapes_.push_back(s);
            arcAdjust_ = false;
        }
        break;
    }
}

void PaintController::onMouseMove(IPoint pos) {
    curr_ = pos;
}

void PaintController::render(FrameBuffer& fb) const {
    fb.clear(Color::Black());

    for (const auto& s : shapes_) {
        drawShape(fb, s, Color::White());
    }

    // 预览：黄色为当前图元，灰色为参考
    if (dragging_) {
        const int r = static_cast<int>(dist(press_, curr_));
        if (mode_ == Mode::Line) {
            raster::drawLine(fb, press_, curr_, Color::Yellow(), currentStyle());
        } else if (mode_ == Mode::Circle) {
            if (r >= 1) {
                raster::drawCircle(fb, press_, r, Color::Yellow(), currentStyle());
            }
        } else { // Arc 第一阶段：参考圆 + 起点角标记
            if (r >= 1) {
                raster::drawCircle(fb, press_, r, Color::Gray(), {});
                drawCross(fb, press_, Color::Gray(), 3);
            }
        }
    }
    if (arcAdjust_) {
        raster::drawCircle(fb, arcCenter_, arcRadius_, Color::Gray(), {});
        drawCross(fb, arcCenter_, Color::Gray(), 3);
        raster::drawArc(fb, arcCenter_, arcRadius_, arcStart_, angleOf(curr_, arcCenter_),
                        Color::Yellow(), currentStyle());
    }

    drawCross(fb, curr_, Color::White(), 4); // 光标十字
}

std::string PaintController::title() const {
    std::string t = "实验一 | 1直线 2圆 3圆弧 | S线型 W线宽 E清屏 右键取消 | ";
    t += kModeNames[static_cast<int>(mode_)];
    t += " ";
    t += kStyleNames[styleIdx_];
    t += " 宽";
    t += std::to_string(kWidths[widthIdx_]);
    if (arcAdjust_) {
        t += " | 圆弧：移动鼠标调终止角，左键确认";
    }
    return t;
}

} // namespace cg
