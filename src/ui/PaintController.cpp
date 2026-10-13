// PaintController.cpp
// 实现交互控制器：把鼠标键盘事件翻译成绘图/填充/裁剪操作，两层渲染（提交层缓存 + 每帧预览），最终像素交给 FrameBuffer。
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
constexpr const char* kModeNames[7] = {"直线", "圆", "圆弧", "多边形", "种子填充", "裁剪", "变换"};
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

// ---- 变换辅助（实验三） ----
constexpr float kPiF = 3.14159265358979f;
constexpr float kHitTol = 5.0f;   // 选中命中容差（像素）
constexpr float kSnapTol = 12.0f; // 对接点吸附容差（像素）

IPoint roundPt(FPoint p) {
    return {static_cast<int>(std::lround(p.x)), static_cast<int>(std::lround(p.y))};
}

FPoint toF(IPoint p) { return {static_cast<float>(p.x), static_cast<float>(p.y)}; }

float normDeg(float deg) { // 归一化到 [0,360)
    deg = std::fmod(deg, 360.0f);
    return deg < 0.0f ? deg + 360.0f : deg;
}

float degOf(FPoint dir) { // 向量 → 角度（屏幕系顺时针为正，与 drawArc 一致）
    const float a = std::atan2(dir.y, dir.x) * (180.0f / kPiF);
    return a < 0.0f ? a + 360.0f : a;
}

FPoint unitVec(FPoint v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y);
    if (len < 1e-6f) {
        return {1.0f, 0.0f};
    }
    return {v.x / len, v.y / len};
}

// 角度是否落在弧 [start→end]（顺时针段）内；span==0 视为整圆
bool degInRange(float deg, float start, float end) {
    const float span = normDeg(end - start);
    return span <= 0.0f || normDeg(deg - start) <= span;
}

// 点到线段的最短距离
float distToSegment(IPoint p, IPoint a, IPoint b) {
    const float abx = static_cast<float>(b.x - a.x);
    const float aby = static_cast<float>(b.y - a.y);
    const float apx = static_cast<float>(p.x - a.x);
    const float apy = static_cast<float>(p.y - a.y);
    const float ab2 = abx * abx + aby * aby;
    float t = ab2 > 0.0f ? (apx * abx + apy * aby) / ab2 : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float cx = abx * t - apx;
    const float cy = aby * t - apy;
    return std::sqrt(cx * cx + cy * cy);
}

// 偶奇规则点在多边形内（选中判定用）
bool pointInPolygon(IPoint p, const std::vector<IPoint>& poly) {
    bool inside = false;
    const int n = static_cast<int>(poly.size());
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const IPoint& pi = poly[i];
        const IPoint& pj = poly[j];
        if ((pi.y > p.y) != (pj.y > p.y) &&
            p.x < (pj.x - pi.x) * (p.y - pi.y) / static_cast<float>(pj.y - pi.y) + pi.x) {
            inside = !inside;
        }
    }
    return inside;
}

// 矩阵作用到一个点并取整（图形参数是整数像素坐标，变换结果四舍五入回存）
IPoint xformPt(const geom::Mat3& m, IPoint p) { return roundPt(geom::apply(m, toF(p))); }

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
    case Mode::Transform:
        break; // 变换模式没有"图形"本体（选中/参考点标记画在预览层）
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
        case Mode::Transform:
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

    // 变换模式标记：品红=各选中图形重心，青=当前参考点，黄=等待指定的参考点，橙=A 的对接点
    if (mode_ == Mode::Transform) {
        for (int idx : selected_) {
            if (idx < 0 || idx >= static_cast<int>(shapes_.size())) {
                continue;
            }
            drawCross(fb, roundPt(shapeCentroid(shapes_[idx])), Color::Magenta(), 4);
        }
        if (!selected_.empty()) {
            drawCross(fb, roundPt(activeRefPoint()), Color::Cyan(), 6);
        }
        if (refPick_) {
            drawCross(fb, curr_, Color::Yellow(), 6);
        }
        if (dockStage_ >= 1 && dockAShapeIdx_ >= 0) {
            drawCross(fb, roundPt(dockA_), Color::Orange(), 5);
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
    case GLFW_KEY_7:
        mode_ = Mode::Transform;
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
        if (mode_ == Mode::Polygon && !pendingPolygon_.empty()) {
            // 正在连点：切换本次闭合用的样式
            polygonFilled_ = !polygonFilled_;
        } else {
            // 不在输入中：切换历史里最后一个多边形；一个都没有则改默认值
            bool toggled = false;
            for (auto it = shapes_.rbegin(); it != shapes_.rend(); ++it) {
                if (it->mode == Mode::Polygon) {
                    it->filled = !it->filled;
                    toggled = true;
                    markDirty();
                    break;
                }
            }
            if (!toggled) {
                polygonFilled_ = !polygonFilled_;
            }
        }
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
    case GLFW_KEY_LEFT: // 方向键微移 1px（变换模式，支持长按连发）
    case GLFW_KEY_RIGHT:
    case GLFW_KEY_UP:
    case GLFW_KEY_DOWN:
        if (mode_ == Mode::Transform && !selected_.empty()) {
            const float dx = static_cast<float>(key == GLFW_KEY_LEFT  ? -1
                                                : key == GLFW_KEY_RIGHT ? 1 : 0);
            const float dy = static_cast<float>(key == GLFW_KEY_UP ? -1
                                                : key == GLFW_KEY_DOWN  ? 1 : 0);
            transformSelected(geom::translate(dx, dy), 0.0f, 1.0f);
        }
        break;
    case GLFW_KEY_EQUAL: // +：等比放大 1.1
    case GLFW_KEY_MINUS: // -：等比缩小 1/1.1
        if (mode_ == Mode::Transform && !selected_.empty()) {
            const FPoint ref = activeRefPoint();
            const float f = (key == GLFW_KEY_EQUAL) ? 1.1f : 1.0f / 1.1f;
            transformSelected(geom::scaleAbout(ref.x, ref.y, f, f), 0.0f, f);
        }
        break;
    case GLFW_KEY_T: // 顺时针 5°（屏幕系正角）
    case GLFW_KEY_R: // 逆时针 5°
        if (mode_ == Mode::Transform && !selected_.empty()) {
            const FPoint ref = activeRefPoint();
            const float deg = (key == GLFW_KEY_T) ? 5.0f : -5.0f;
            transformSelected(geom::rotateAbout(ref.x, ref.y, deg * kPiF / 180.0f), deg, 1.0f);
        }
        break;
    case GLFW_KEY_G: // 参考点：重心（默认）↔ 指定点（随后左键点击画布）
        if (mode_ == Mode::Transform) {
            refManual_ = !refManual_;
            refPick_ = refManual_;
        }
        break;
    case GLFW_KEY_J: // 挑战题：两图形对接拼接
        if (mode_ == Mode::Transform) {
            dockStage_ = 1;
            dockAShapeIdx_ = -1;
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

// ================= 变换（实验三） =================

// 种子填充是像素操作的记录、裁剪线段是演示辅助，都不是参数图形，不参与选中/变换
bool PaintController::shapeSelectable(const Shape& s) const {
    return s.mode == Mode::Line || s.mode == Mode::Circle || s.mode == Mode::Arc ||
           s.mode == Mode::Polygon;
}

FPoint PaintController::shapeCentroid(const Shape& s) const {
    switch (s.mode) {
    case Mode::Line:
        return {(s.a.x + s.b.x) * 0.5f, (s.a.y + s.b.y) * 0.5f};
    case Mode::Polygon: {
        if (s.polygon.empty()) {
            return toF(s.a);
        }
        FPoint sum{0.0f, 0.0f};
        for (const auto& p : s.polygon) {
            sum.x += static_cast<float>(p.x);
            sum.y += static_cast<float>(p.y);
        }
        const float n = static_cast<float>(s.polygon.size());
        return {sum.x / n, sum.y / n}; // 课程口径：顶点平均（非面积重心）
    }
    case Mode::Circle:
    case Mode::Arc:
    default:
        return toF(s.a); // 圆/弧的重心取圆心
    }
}

FPoint PaintController::combinedCentroid() const {
    FPoint sum{0.0f, 0.0f};
    int n = 0;
    for (int idx : selected_) {
        if (idx < 0 || idx >= static_cast<int>(shapes_.size())) {
            continue;
        }
        const FPoint c = shapeCentroid(shapes_[idx]);
        sum.x += c.x;
        sum.y += c.y;
        ++n;
    }
    if (n == 0) {
        return {0.0f, 0.0f};
    }
    return {sum.x / n, sum.y / n};
}

FPoint PaintController::activeRefPoint() const {
    return refManual_ ? toF(refPoint_) : combinedCentroid();
}

void PaintController::transformSelected(const geom::Mat3& m, float rotDeg, float scaleF) {
    for (int idx : selected_) {
        if (idx < 0 || idx >= static_cast<int>(shapes_.size())) {
            continue;
        }
        applyToShape(shapes_[idx], m, rotDeg, scaleF);
    }
    markDirty();
}

// 矩阵作用到图形参数；rotDeg/scaleF 是矩阵对应的旋转角/等比因子，
// 圆/弧的参数化字段（半径、角度）按它们修正（设计决策见 issue #35）
void PaintController::applyToShape(Shape& s, const geom::Mat3& m, float rotDeg, float scaleF) {
    switch (s.mode) {
    case Mode::Line:
        s.a = xformPt(m, s.a);
        s.b = xformPt(m, s.b);
        break;
    case Mode::Circle:
        s.a = xformPt(m, s.a);
        s.radius = std::max(1, static_cast<int>(std::lround(s.radius * scaleF)));
        break;
    case Mode::Arc:
        s.a = xformPt(m, s.a);
        s.radius = std::max(1, static_cast<int>(std::lround(s.radius * scaleF)));
        s.startDeg = normDeg(s.startDeg + rotDeg);
        s.endDeg = normDeg(s.endDeg + rotDeg);
        break;
    case Mode::Polygon:
        for (auto& p : s.polygon) {
            p = xformPt(m, p);
        }
        break;
    default:
        break;
    }
}

int PaintController::hitShape(IPoint p) const {
    for (int i = static_cast<int>(shapes_.size()) - 1; i >= 0; --i) {
        const Shape& s = shapes_[i];
        if (!shapeSelectable(s)) {
            continue;
        }
        switch (s.mode) {
        case Mode::Line:
            if (distToSegment(p, s.a, s.b) <= kHitTol) {
                return i;
            }
            break;
        case Mode::Circle:
            if (std::fabs(dist(p, s.a) - s.radius) <= kHitTol) {
                return i;
            }
            break;
        case Mode::Arc:
            if (std::fabs(dist(p, s.a) - s.radius) <= kHitTol &&
                degInRange(angleOf(p, s.a), s.startDeg, s.endDeg)) {
                return i;
            }
            break;
        case Mode::Polygon:
            if (pointInPolygon(p, s.polygon)) {
                return i;
            }
            break;
        default:
            break;
        }
    }
    return -1;
}

// 点击点吸附到某图形轮廓的最近点，同时给出该点的单位切向（对接拼接用）
int PaintController::pickOutlineShape(IPoint p, FPoint& outPt, FPoint& outTan, int skipIdx) const {
    for (int i = static_cast<int>(shapes_.size()) - 1; i >= 0; --i) {
        const Shape& s = shapes_[i];
        if (!shapeSelectable(s) || i == skipIdx) {
            continue;
        }
        switch (s.mode) {
        case Mode::Line: {
            if (distToSegment(p, s.a, s.b) > kSnapTol) {
                break;
            }
            const float abx = static_cast<float>(s.b.x - s.a.x);
            const float aby = static_cast<float>(s.b.y - s.a.y);
            const float apx = static_cast<float>(p.x - s.a.x);
            const float apy = static_cast<float>(p.y - s.a.y);
            const float ab2 = abx * abx + aby * aby;
            float t = ab2 > 0.0f ? (apx * abx + apy * aby) / ab2 : 0.0f;
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            outPt = {s.a.x + abx * t, s.a.y + aby * t};
            outTan = unitVec({abx, aby});
            return i;
        }
        case Mode::Circle: {
            if (std::fabs(dist(p, s.a) - s.radius) > kSnapTol) {
                break;
            }
            const FPoint u = unitVec({static_cast<float>(p.x - s.a.x),
                                      static_cast<float>(p.y - s.a.y)});
            outPt = {s.a.x + s.radius * u.x, s.a.y + s.radius * u.y};
            outTan = {-u.y, u.x}; // 切向 = 半径方向转 90°（屏幕系）
            return i;
        }
        case Mode::Arc: {
            if (std::fabs(dist(p, s.a) - s.radius) > kSnapTol) {
                break;
            }
            const float ang = angleOf(p, s.a);
            const float span = normDeg(s.endDeg - s.startDeg);
            float a = ang;
            if (span > 0.0f && normDeg(ang - s.startDeg) > span) {
                // 点击方向在弧外：钳位到较近的端点
                const float d = normDeg(ang - s.startDeg);
                a = (d - span <= 360.0f - d) ? normDeg(s.startDeg + span) : s.startDeg;
            }
            const float rad = a * kPiF / 180.0f;
            const FPoint u{std::cos(rad), std::sin(rad)};
            outPt = {s.a.x + s.radius * u.x, s.a.y + s.radius * u.y};
            outTan = {-u.y, u.x};
            return i;
        }
        case Mode::Polygon: {
            float best = 1e30f;
            FPoint bestPt{}, bestTan{};
            const int n = static_cast<int>(s.polygon.size());
            for (int k = 0; k < n; ++k) {
                const IPoint& p1 = s.polygon[k];
                const IPoint& p2 = s.polygon[(k + 1) % n];
                const float dd = distToSegment(p, p1, p2);
                if (dd < best) {
                    best = dd;
                    const float abx = static_cast<float>(p2.x - p1.x);
                    const float aby = static_cast<float>(p2.y - p1.y);
                    const float apx = static_cast<float>(p.x - p1.x);
                    const float apy = static_cast<float>(p.y - p1.y);
                    const float ab2 = abx * abx + aby * aby;
                    float t = ab2 > 0.0f ? (apx * abx + apy * aby) / ab2 : 0.0f;
                    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                    bestPt = {p1.x + abx * t, p1.y + aby * t};
                    bestTan = unitVec({abx, aby});
                }
            }
            if (best <= kSnapTol) {
                outPt = bestPt;
                outTan = bestTan;
                return i;
            }
            break;
        }
        default:
            break;
        }
    }
    return -1;
}

void PaintController::cancelInProgress() {
    dragging_ = false;
    arcAdjust_ = false;
    clipRectEdit_ = false;
    clipPolyEdit_ = false;
    xformDrag_ = false;
    refPick_ = false;
    dockStage_ = 0;
    dockAShapeIdx_ = -1;
    selected_.clear();
    pendingPolygon_.clear();
    pendingClipWin_.clear();
}

void PaintController::onMouseButton(int button, int action, int mods) {
    const bool press = (action == GLFW_PRESS);
    shiftHeld_ = (mods & GLFW_MOD_SHIFT) != 0;

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

    case Mode::Transform:
        if (press) {
            // 对接流程进行中：本次点击是"选对接点"，不改选中
            if (dockStage_ > 0) {
                FPoint pt{}, tan{};
                const int idx = pickOutlineShape(curr_, pt, tan, dockAShapeIdx_);
                if (idx >= 0 && dockStage_ == 1) {
                    dockA_ = pt;
                    dockATangent_ = tan;
                    dockAShapeIdx_ = idx;
                    dockStage_ = 2;
                } else if (idx >= 0 && dockStage_ == 2) {
                    // 拼接 = 先绕 B 的对接点旋转 Δθ（两切向角差，±90° 内取最短），
                    // 再把 B 的对接点平移到 A 的对接点上：M = T(A-B) · R(B, Δθ)
                    float diff = degOf(dockATangent_) - degOf(tan);
                    while (diff > 90.0f) {
                        diff -= 180.0f;
                    }
                    while (diff < -90.0f) {
                        diff += 180.0f;
                    }
                    const geom::Mat3 m = geom::mul(
                        geom::translate(dockA_.x - pt.x, dockA_.y - pt.y),
                        geom::rotateAbout(pt.x, pt.y, diff * kPiF / 180.0f));
                    applyToShape(shapes_[idx], m, diff, 1.0f);
                    dockStage_ = 0;
                    dockAShapeIdx_ = -1;
                    markDirty();
                }
                break;
            }
            // 指定参考点：本次点击只设定参考点，不动选中
            if (refPick_) {
                refPoint_ = curr_;
                refPick_ = false;
                markDirty();
                break;
            }
            // 选中 / 多选 / 拖拽平移
            const int hit = hitShape(curr_);
            auto inSel = [&](int idx) {
                return std::find(selected_.begin(), selected_.end(), idx) != selected_.end();
            };
            if (hit >= 0) {
                bool sel = inSel(hit);
                if (shiftHeld_) { // Shift：加选/移除
                    if (sel) {
                        selected_.erase(std::find(selected_.begin(), selected_.end(), hit));
                    } else {
                        selected_.push_back(hit);
                    }
                    sel = !sel;
                    markDirty();
                } else if (!sel) { // 普通点击：只选它一个
                    selected_.clear();
                    selected_.push_back(hit);
                    sel = true;
                    markDirty();
                }
                if (sel) { // 在选中集合里才可拖拽平移
                    xformDrag_ = true;
                    lastDrag_ = curr_;
                }
            } else { // 点空白：清除选中
                if (!selected_.empty()) {
                    selected_.clear();
                    markDirty();
                }
            }
        } else {
            xformDrag_ = false;
        }
        break;
    }
}

void PaintController::onMouseMove(IPoint pos) {
    // 拖拽平移：以像素为步长的增量平移（整数增量，无舍入累积）
    if (xformDrag_ && mode_ == Mode::Transform && !selected_.empty()) {
        const float dx = static_cast<float>(pos.x - curr_.x);
        const float dy = static_cast<float>(pos.y - curr_.y);
        if (dx != 0.0f || dy != 0.0f) {
            transformSelected(geom::translate(dx, dy), 0.0f, 1.0f);
        }
    }
    curr_ = pos;
}

std::string PaintController::title() const {
    std::string t = "绘图引擎（实验一/二/三） | 1直线 2圆 3圆弧 4多边形 5种子填充 6裁剪 7变换 | "
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
    case Mode::Transform:
        t += " 选中:";
        t += std::to_string(selected_.size());
        t += " 参考点:";
        t += refManual_ ? "指定" : "重心";
        t += " | 左键选(Shift加选) 拖拽/方向键平移 +−缩放 R/T旋转 G参考点 J对接";
        if (refPick_) {
            t += " | 点击画布指定参考点";
        }
        if (dockStage_ == 1) {
            t += " | 对接:点击图形A轮廓选对接点";
        }
        if (dockStage_ == 2) {
            t += " | 对接:点击图形B轮廓完成拼接（右键取消）";
        }
        break;
    }
    return t;
}

} // namespace cg
