#include "ui/PaintController.hpp"
#include "raster/Fill.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

namespace cg {
namespace {

constexpr unsigned kMasks[3] = {0xFFFFu, 0xF0F0u, 0x8888u}; // 实线/虚线/点线的像素掩码
constexpr int kWidths[3] = {1, 3, 5};
constexpr const char* kStyleNames[3] = {"实线", "虚线", "点线"};
constexpr const char* kModeNames[6] = {"直线", "圆", "圆弧", "多边形", "种子填充", "裁剪"};
constexpr const char* kClipAlgoNames[2] = {"Cohen-Sutherland", "中点分割"};
constexpr const char* kPaletteNames[6] = {"黄", "青", "品红", "绿", "橙", "红"};
const Color kPalette[6] = {Color::Yellow(), Color::Cyan(),  Color::Magenta(),
                           Color::Green(),  Color::Orange(), Color::Red()};

float dist(IPoint a, IPoint b) {
    const int dx = a.x - b.x;
    const int dy = a.y - b.y;
    return std::sqrt(static_cast<float>(dx * dx + dy * dy));
}

// 屏幕坐标角度（度，[0,360)，y 向下 → 顺时针为正）——与 raster::drawArc 约定一致
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

void drawRectOutline(FrameBuffer& fb, const raster::Rect& r, const Color& c) {
    raster::drawLine(fb, {r.xmin, r.ymin}, {r.xmax, r.ymin}, c, {});
    raster::drawLine(fb, {r.xmax, r.ymin}, {r.xmax, r.ymax}, c, {});
    raster::drawLine(fb, {r.xmax, r.ymax}, {r.xmin, r.ymax}, c, {});
    raster::drawLine(fb, {r.xmin, r.ymax}, {r.xmin, r.ymin}, c, {});
}

raster::Rect rectFrom(IPoint p1, IPoint p2) {
    return {std::min(p1.x, p2.x), std::min(p1.y, p2.y), std::max(p1.x, p2.x),
            std::max(p1.y, p2.y)};
}

} // namespace

raster::LineStyle PaintController::currentStyle() const {
    return {kMasks[styleIdx_], kWidths[widthIdx_]};
}

Color PaintController::currentFillColor() const {
    return kPalette[fillColorIdx_];
}

const char* PaintController::clipAlgoName() const {
    if (clipWin_.valid && clipWin_.kind == ClipWindow::Kind::ConvexPoly) {
        return "Cyrus-Beck"; // 凸多边形窗口用参数化裁剪（挑战题）
    }
    return kClipAlgoNames[clipAlgoIdx_];
}

bool PaintController::clipLine(IPoint& a, IPoint& b) const {
    if (!clipWin_.valid) {
        return true; // 还没设窗口：不裁剪
    }
    if (clipWin_.kind == ClipWindow::Kind::Rect) {
        return (clipAlgoIdx_ == 1) ? raster::midpointSplit(a, b, clipWin_.rect)
                                   : raster::cohenSutherland(a, b, clipWin_.rect);
    }
    FPoint fa{static_cast<float>(a.x), static_cast<float>(a.y)};
    FPoint fb{static_cast<float>(b.x), static_cast<float>(b.y)};
    if (!raster::cyrusBeck(fa, fb, clipWin_.poly)) {
        return false;
    }
    a = {static_cast<int>(std::lround(fa.x)), static_cast<int>(std::lround(fa.y))};
    b = {static_cast<int>(std::lround(fb.x)), static_cast<int>(std::lround(fb.y))};
    return true;
}

void PaintController::drawShape(FrameBuffer& fb, const Shape& s) const {
    switch (s.mode) {
    case Mode::Line:
        raster::drawLine(fb, s.a, s.b, s.color, s.style);
        break;
    case Mode::Circle:
        raster::drawCircle(fb, s.a, s.radius, s.color, s.style);
        break;
    case Mode::Arc:
        raster::drawArc(fb, s.a, s.radius, s.startDeg, s.endDeg, s.color, s.style);
        break;
    case Mode::Polygon:
        if (s.filled) {
            raster::scanlineFillPolygon(fb, s.polygon, s.color);
        }
        for (std::size_t i = 0; i < s.polygon.size(); ++i) {
            // 填充多边形用实线描边；仅轮廓多边形沿用当前线型（虚线可看顶点接缝）
            const raster::LineStyle ls = s.filled ? raster::LineStyle{} : s.style;
            raster::drawLine(fb, s.polygon[i], s.polygon[(i + 1) % s.polygon.size()], s.color,
                             ls);
        }
        break;
    case Mode::SeedFill:
        raster::scanlineSeedFill(fb, s.a, s.color);
        break;
    case Mode::Clip: {
        // 灰色虚线画原线段，便于对比"被剪掉了哪一段"
        raster::drawLine(fb, s.a, s.b, Color::Gray(), {0xF0F0u, 1});
        IPoint a = s.a;
        IPoint b = s.b;
        if (clipLine(a, b)) {
            raster::drawLine(fb, a, b, s.color, s.style);
        }
        break;
    }
    }
}

void PaintController::drawClipWindow(FrameBuffer& fb) const {
    if (!clipWin_.valid) {
        return;
    }
    if (clipWin_.kind == ClipWindow::Kind::Rect) {
        drawRectOutline(fb, clipWin_.rect, Color::White());
        return;
    }
    const auto& poly = clipWin_.poly;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const FPoint& p1 = poly[i];
        const FPoint& p2 = poly[(i + 1) % poly.size()];
        raster::drawLine(fb, {static_cast<int>(std::lround(p1.x)), static_cast<int>(std::lround(p1.y))},
                         {static_cast<int>(std::lround(p2.x)), static_cast<int>(std::lround(p2.y))},
                         Color::White(), {});
    }
}

void PaintController::commitRender() {
    committed_->clear(Color::Black());

    if (mode_ == Mode::Clip && !clipWin_.valid) {
        // 首次进入裁剪模式：给一个居中的默认矩形窗口，随时可被 K/P 重设。
        // 必须在绘制图形之前建立，否则首帧的裁剪线会因"无窗口"而整条画出
        clipWin_.kind = ClipWindow::Kind::Rect;
        clipWin_.rect = {committed_->width() / 5, committed_->height() / 5,
                         committed_->width() * 4 / 5, committed_->height() * 4 / 5};
        clipWin_.valid = true;
    }

    for (const auto& s : shapes_) {
        drawShape(*committed_, s);
    }
    if (mode_ == Mode::Clip) {
        drawClipWindow(*committed_);
    }
}

void PaintController::drawPreview(FrameBuffer& fb) const {
    // 进行中的拖拽预览（黄色/绿色为待定内容，不进入提交层）
    if (dragging_) {
        const int r = static_cast<int>(dist(press_, curr_));
        switch (mode_) {
        case Mode::Line:
            raster::drawLine(fb, press_, curr_, Color::Yellow(), currentStyle());
            break;
        case Mode::Circle:
            if (r >= 1) {
                raster::drawCircle(fb, press_, r, Color::Yellow(), currentStyle());
            }
            break;
        case Mode::Arc:
            if (r >= 1) {
                raster::drawCircle(fb, press_, r, Color::Gray(), {});
                drawCross(fb, press_, Color::Gray(), 3);
            }
            break;
        case Mode::Clip:
            if (clipRectEdit_) {
                drawRectOutline(fb, rectFrom(press_, curr_), Color::Cyan());
            } else {
                raster::drawLine(fb, press_, curr_, Color::Green(), currentStyle());
            }
            break;
        case Mode::Polygon:
        case Mode::SeedFill:
            break;
        }
    }

    // 圆弧第二阶段：预览终止角
    if (arcAdjust_) {
        raster::drawCircle(fb, arcCenter_, arcRadius_, Color::Gray(), {});
        drawCross(fb, arcCenter_, Color::Gray(), 3);
        raster::drawArc(fb, arcCenter_, arcRadius_, arcStart_, angleOf(curr_, arcCenter_),
                        Color::Yellow(), currentStyle());
    }

    // 多边形顶点输入：已确定的边 + 到光标的橡皮筋 + 闭合虚边
    if (mode_ == Mode::Polygon && !pendingPolygon_.empty()) {
        for (std::size_t i = 0; i + 1 < pendingPolygon_.size(); ++i) {
            raster::drawLine(fb, pendingPolygon_[i], pendingPolygon_[i + 1], Color::White(), {});
        }
        raster::drawLine(fb, pendingPolygon_.back(), curr_, Color::Yellow(), {});
        if (pendingPolygon_.size() >= 2) {
            raster::drawLine(fb, curr_, pendingPolygon_.front(), Color::Gray(), {0xF0F0u, 1});
        }
        for (const auto& p : pendingPolygon_) {
            drawCross(fb, p, Color::White(), 3);
        }
    }

    // 凸多边形裁剪窗口输入
    if (mode_ == Mode::Clip && clipPolyEdit_ && !pendingClipWin_.empty()) {
        for (std::size_t i = 0; i + 1 < pendingClipWin_.size(); ++i) {
            raster::drawLine(fb, pendingClipWin_[i], pendingClipWin_[i + 1], Color::Cyan(), {});
        }
        raster::drawLine(fb, pendingClipWin_.back(), curr_, Color::Cyan(), {});
        if (pendingClipWin_.size() >= 2) {
            raster::drawLine(fb, curr_, pendingClipWin_.front(), Color::Cyan(), {0xF0F0u, 1});
        }
        for (const auto& p : pendingClipWin_) {
            drawCross(fb, p, Color::Cyan(), 3);
        }
    }

    drawCross(fb, curr_, Color::White(), 4); // 光标十字
}

void PaintController::render(FrameBuffer& fb) {
    const int w = fb.width();
    const int h = fb.height();
    if (committed_ == nullptr || committed_->width() != w || committed_->height() != h) {
        committed_ = std::make_unique<FrameBuffer>(w, h);
        dirty_ = true;
    }
    if (dirty_) {
        commitRender();
        dirty_ = false;
    }
    fb.copyFrom(*committed_); // 提交层 → 显示层
    drawPreview(fb);
}

void PaintController::onKey(int key, int action) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) {
        return;
    }
    switch (key) {
    case GLFW_KEY_1:
    case GLFW_KEY_L:
        mode_ = Mode::Line;
        cancelInProgress();
        markDirty();
        break;
    case GLFW_KEY_2:
    case GLFW_KEY_C:
        mode_ = Mode::Circle;
        cancelInProgress();
        markDirty();
        break;
    case GLFW_KEY_3:
    case GLFW_KEY_A:
        mode_ = Mode::Arc;
        cancelInProgress();
        markDirty();
        break;
    case GLFW_KEY_4:
        mode_ = Mode::Polygon;
        cancelInProgress();
        markDirty();
        break;
    case GLFW_KEY_5:
        mode_ = Mode::SeedFill;
        cancelInProgress();
        markDirty();
        break;
    case GLFW_KEY_6:
        mode_ = Mode::Clip;
        cancelInProgress();
        markDirty();
        break;
    case GLFW_KEY_S: // 线型
        styleIdx_ = (styleIdx_ + 1) % 3;
        break;
    case GLFW_KEY_W: // 线宽
        widthIdx_ = (widthIdx_ + 1) % 3;
        break;
    case GLFW_KEY_X: // 填充色
        fillColorIdx_ = (fillColorIdx_ + 1) % 6;
        break;
    case GLFW_KEY_F: // 多边形：填充 / 仅轮廓
        polygonFilled_ = !polygonFilled_;
        break;
    case GLFW_KEY_M: // 裁剪算法切换（矩形窗口时有效）
        if (mode_ == Mode::Clip) {
            clipAlgoIdx_ = 1 - clipAlgoIdx_;
            markDirty();
        }
        break;
    case GLFW_KEY_K: // 定义矩形裁剪窗口（随后拖拽）
        if (mode_ == Mode::Clip) {
            clipRectEdit_ = true;
            clipPolyEdit_ = false;
            pendingClipWin_.clear();
        }
        break;
    case GLFW_KEY_P: // 定义凸多边形裁剪窗口（随后左键连点、右键闭合）
        if (mode_ == Mode::Clip) {
            clipPolyEdit_ = true;
            clipRectEdit_ = false;
            pendingClipWin_.clear();
        }
        break;
    case GLFW_KEY_E: // 清屏
        shapes_.clear();
        cancelInProgress();
        markDirty();
        break;
    default:
        break;
    }
}

void PaintController::closePolygon() {
    if (pendingPolygon_.size() >= 3) {
        Shape s;
        s.mode = Mode::Polygon;
        s.polygon = pendingPolygon_;
        s.filled = polygonFilled_;
        s.style = currentStyle();
        s.color = polygonFilled_ ? currentFillColor() : Color::White();
        shapes_.push_back(s);
        markDirty();
    }
    pendingPolygon_.clear();
}

void PaintController::setRectClipWindow(IPoint p1, IPoint p2) {
    const raster::Rect r = rectFrom(p1, p2);
    if (r.xmax - r.xmin < 8 || r.ymax - r.ymin < 8) {
        return; // 太小的窗口忽略（避免误拖成一个点）
    }
    clipWin_.kind = ClipWindow::Kind::Rect;
    clipWin_.rect = r;
    clipWin_.valid = true;
    markDirty();
}

void PaintController::cancelInProgress() {
    dragging_ = false;
    arcAdjust_ = false;
    clipRectEdit_ = false;
    clipPolyEdit_ = false;
    pendingPolygon_.clear();
    pendingClipWin_.clear();
}

void PaintController::onMouseButton(int button, int action) {
    const bool press = (action == GLFW_PRESS);

    // 裁剪模式：正在连点定义凸多边形窗口
    if (mode_ == Mode::Clip && clipPolyEdit_) {
        if (button == 0 && press) {
            pendingClipWin_.push_back(curr_);
        } else if (button == 1 && press) { // 右键闭合
            if (pendingClipWin_.size() >= 3) {
                clipWin_.kind = ClipWindow::Kind::ConvexPoly;
                clipWin_.poly.clear();
                for (const auto& p : pendingClipWin_) {
                    clipWin_.poly.push_back({static_cast<float>(p.x), static_cast<float>(p.y)});
                }
                clipWin_.valid = true;
                markDirty();
            }
            pendingClipWin_.clear();
            clipPolyEdit_ = false;
        }
        return;
    }

    if (button == 1) { // 右键：闭合多边形输入 / 取消当前操作
        if (press) {
            if (mode_ == Mode::Polygon && !pendingPolygon_.empty()) {
                closePolygon();
            } else {
                cancelInProgress();
            }
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
                Shape s;
                s.mode = Mode::Line;
                s.a = press_;
                s.b = curr_;
                s.style = currentStyle();
                shapes_.push_back(s);
                markDirty();
            } else if (dist(press_, curr_) >= 1.0f) {
                Shape s;
                s.mode = Mode::Circle;
                s.a = press_;
                s.radius = static_cast<int>(dist(press_, curr_));
                s.style = currentStyle();
                shapes_.push_back(s);
                markDirty();
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
            Shape s;
            s.mode = Mode::Arc;
            s.a = arcCenter_;
            s.radius = arcRadius_;
            s.startDeg = arcStart_;
            s.endDeg = angleOf(curr_, arcCenter_);
            s.style = currentStyle();
            shapes_.push_back(s);
            markDirty();
            arcAdjust_ = false;
        }
        break;

    case Mode::Polygon:
        if (press) {
            pendingPolygon_.push_back(curr_); // 左键连点加顶点，右键闭合
        }
        break;

    case Mode::SeedFill:
        if (press) {
            Shape s;
            s.mode = Mode::SeedFill;
            s.a = curr_; // 种子点
            s.color = currentFillColor();
            shapes_.push_back(s);
            markDirty();
        }
        break;

    case Mode::Clip:
        if (clipRectEdit_) { // K 之后的拖拽：定义矩形窗口
            if (press) {
                dragging_ = true;
                press_ = curr_;
            } else if (dragging_) {
                dragging_ = false;
                setRectClipWindow(press_, curr_);
                clipRectEdit_ = false;
            }
        } else if (press) { // 拖拽画线（裁剪结果实时显示）
            dragging_ = true;
            press_ = curr_;
        } else if (dragging_) {
            dragging_ = false;
            Shape s;
            s.mode = Mode::Clip;
            s.a = press_;
            s.b = curr_;
            s.style = currentStyle();
            s.color = Color::Green();
            shapes_.push_back(s);
            markDirty();
        }
        break;
    }
}

void PaintController::onMouseMove(IPoint pos) {
    curr_ = pos;
}

std::string PaintController::title() const {
    std::string t = "绘图引擎（实验一/二） | 1直线 2圆 3圆弧 4多边形 5种子填充 6裁剪 | "
                    "S线型 W线宽 X填充色 E清屏 右键取消 | ";
    t += kModeNames[static_cast<int>(mode_)];
    switch (mode_) {
    case Mode::Line:
    case Mode::Circle:
    case Mode::Arc:
        t += " ";
        t += kStyleNames[styleIdx_];
        t += " 宽";
        t += std::to_string(kWidths[widthIdx_]);
        if (arcAdjust_) {
            t += " | 圆弧：移动鼠标调终止角，左键确认";
        }
        break;
    case Mode::Polygon:
        t += polygonFilled_ ? " 填充" : " 仅轮廓";
        t += "（F 切换）| 左键连点顶点，右键闭合";
        break;
    case Mode::SeedFill:
        t += " 颜色:";
        t += kPaletteNames[fillColorIdx_];
        t += "（X 换色）| 左键点击封闭区域内一点";
        break;
    case Mode::Clip:
        t += " 窗口:";
        t += (clipWin_.valid && clipWin_.kind == ClipWindow::Kind::ConvexPoly) ? "凸多边形"
                                                                              : "矩形";
        t += " 算法:";
        t += clipAlgoName();
        t += " | 左键拖画线；K+拖拽设矩形窗口；P+连点右键闭合设凸多边形窗口；M 切算法";
        break;
    }
    return t;
}

} // namespace cg
