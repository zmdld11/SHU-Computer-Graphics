// PaintController.cpp — 交互控制器：事件 → 绘图/填充/裁剪/变换。
#include "ui/PaintController.hpp"
#include "curves/Bezier.hpp"
#include "curves/BSpline.hpp"
#include "raster/Fill.hpp"
#include "ui/Text.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace cg {
namespace {

constexpr unsigned kMasks[3] = {0xFFFFu, 0xF0F0u, 0x8888u}; // 实线/虚线/点线的像素掩码
constexpr int kWidths[3] = {1, 3, 5};
constexpr const char* kStyleNames[3] = {"实线", "虚线", "点线"};
constexpr const char* kModeNames[8] = {"直线", "圆", "圆弧", "多边形", "种子填充", "裁剪", "变换",
                                       "曲线"};
constexpr int kCurveSegments = 128; // 曲线采样段数：折线足够光滑且确定性可测
constexpr const char* kClipAlgoNames[2] = {"Cohen-Sutherland", "中点分割"};
constexpr const char* kPaletteNames[6] = {"黄", "青", "品红", "绿", "橙", "红"};
const Color kPalette[6] = {Color::Yellow(), Color::Cyan(),  Color::Magenta(),
                           Color::Green(),  Color::Orange(), Color::Red()};

float distSq(IPoint a, IPoint b) {
    const float dx = static_cast<float>(a.x) - static_cast<float>(b.x);
    const float dy = static_cast<float>(a.y) - static_cast<float>(b.y);
    return dx * dx + dy * dy;
}

float dist(IPoint a, IPoint b) { return std::sqrt(distSq(a, b)); }

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

void drawRectOutline(FrameBuffer& fb, const raster::Rect& r, const Color& c,
                     const raster::LineStyle& ls = {}) {
    raster::drawLine(fb, {r.xmin, r.ymin}, {r.xmax, r.ymin}, c, ls);
    raster::drawLine(fb, {r.xmax, r.ymin}, {r.xmax, r.ymax}, c, ls);
    raster::drawLine(fb, {r.xmax, r.ymax}, {r.xmin, r.ymax}, c, ls);
    raster::drawLine(fb, {r.xmin, r.ymax}, {r.xmin, r.ymin}, c, ls);
}

raster::Rect rectFrom(IPoint p1, IPoint p2) {
    return {std::min(p1.x, p2.x), std::min(p1.y, p2.y), std::max(p1.x, p2.x),
            std::max(p1.y, p2.y)};
}

// ---- 变换辅助 ----
constexpr float kPiF = 3.14159265358979f;
constexpr float kHitTol = 5.0f;   // 选中命中容差（像素）
constexpr float kSnapTol = 12.0f; // 对接点吸附容差（像素）

// ---- 修饰键约束：Shift 45° 吸附 / Ctrl 定步长 ----
constexpr float kStepPx = 20.0f;

// Shift 约束：把向量方向吸附到 45° 整数倍，保留沿该方向的投影长度（可为负 = 反向）
FPoint snapDir45(FPoint v) {
    if (std::fabs(v.x) < 1e-6f && std::fabs(v.y) < 1e-6f) {
        return v;
    }
    const float t = std::round(std::atan2(v.y, v.x) / (kPiF / 4.0f)) * (kPiF / 4.0f);
    const float len = v.x * std::cos(t) + v.y * std::sin(t);
    return {std::cos(t) * len, std::sin(t) * len};
}

// Ctrl 约束：长度按 kStepPx 步进取整（最小 1，避免零长度图形）
int quantStepLen(float len) {
    return std::max(1, static_cast<int>(std::lround(std::fabs(len) / kStepPx) * kStepPx));
}

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

// 点到线段的最近点（距离 + 投影点）。
struct SegmentProjection {
    float dist = 0.0f;
    FPoint point{};
};

SegmentProjection projectOnSegment(IPoint p, FPoint a, FPoint b) {
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float apx = static_cast<float>(p.x) - a.x;
    const float apy = static_cast<float>(p.y) - a.y;
    const float ab2 = abx * abx + aby * aby;
    float t = ab2 > 0.0f ? (apx * abx + apy * aby) / ab2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const float cx = abx * t - apx;
    const float cy = aby * t - apy;
    return {std::sqrt(cx * cx + cy * cy), {a.x + abx * t, a.y + aby * t}};
}

float distToSegment(IPoint p, IPoint a, IPoint b) {
    return projectOnSegment(p, toF(a), toF(b)).dist;
}

// 顶点/控制点平均（课程口径的重心）。
template <typename Pt>
FPoint averageOf(const std::vector<Pt>& pts) {
    FPoint sum{0.0f, 0.0f};
    for (const auto& p : pts) {
        sum.x += static_cast<float>(p.x);
        sum.y += static_cast<float>(p.y);
    }
    const float n = static_cast<float>(pts.size());
    return {sum.x / n, sum.y / n};
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

// 矩阵作用到点并取整（图形参数为整数像素）。
IPoint xformPt(const geom::Mat3& m, IPoint p) { return roundPt(geom::apply(m, toF(p))); }

} // namespace

raster::LineStyle PaintController::currentStyle() const {
    return {kMasks[styleIdx_], kWidths[widthIdx_]};
}

Color PaintController::currentFillColor() const {
    return kPalette[fillColorIdx_];
}

// 直线拖拽终点（Shift 吸附 45°）。
IPoint PaintController::lineDragEnd() const {
    if (!shiftHeld_) {
        return curr_;
    }
    const FPoint d = snapDir45({static_cast<float>(curr_.x - press_.x),
                                static_cast<float>(curr_.y - press_.y)});
    return {press_.x + static_cast<int>(std::lround(d.x)),
            press_.y + static_cast<int>(std::lround(d.y))};
}

// 圆/弧拖拽半径（Ctrl 按 kStepPx 步进）。
int PaintController::dragRadius() const {
    const float r = dist(press_, curr_);
    return ctrlHeld_ ? quantStepLen(r) : static_cast<int>(std::lround(r));
}

const char* PaintController::clipAlgoName() const {
    if (clipWin_.valid && clipWin_.kind == ClipWindow::Kind::ConvexPoly) {
        return "Cyrus-Beck";
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
    FPoint fa = toF(a);
    FPoint fb = toF(b);
    if (!raster::cyrusBeck(fa, fb, clipWin_.poly)) {
        return false;
    }
    a = roundPt(fa);
    b = roundPt(fb);
    return true;
}

// 图形轮廓重描：线/圆/弧/多边形边/曲线折线共用。
void PaintController::drawOutline(FrameBuffer& fb, const Shape& s, const Color& c,
                                  const raster::LineStyle& ls) const {
    switch (s.mode) {
    case Mode::Line:
        raster::drawLine(fb, s.a, s.b, c, ls);
        break;
    case Mode::Circle:
        raster::drawCircle(fb, s.a, s.radius, c, ls);
        break;
    case Mode::Arc:
        raster::drawArc(fb, s.a, s.radius, s.startDeg, s.endDeg, c, ls);
        break;
    case Mode::Polygon: {
        const std::size_t n = s.polygon.size();
        for (std::size_t i = 0; n >= 2 && i < n; ++i) {
            raster::drawLine(fb, s.polygon[i], s.polygon[(i + 1) % n], c, ls);
        }
        break;
    }
    case Mode::Curve: {
        const std::vector<FPoint> samples = curveSamples(s);
        for (std::size_t i = 0; i + 1 < samples.size(); ++i) {
            raster::drawLine(fb, roundPt(samples[i]), roundPt(samples[i + 1]), c, ls);
        }
        break;
    }
    default:
        break;
    }
}

void PaintController::drawShape(FrameBuffer& fb, const Shape& s) const {
    switch (s.mode) {
    case Mode::Line:
    case Mode::Circle:
    case Mode::Arc:
        drawOutline(fb, s, s.color, s.style);
        break;
    case Mode::Polygon:
        if (s.filled) {
            raster::scanlineFillPolygon(fb, s.polygon, s.color);
        }
        // 填充多边形用实线描边；仅轮廓多边形沿用当前线型（虚线可看顶点接缝）
        drawOutline(fb, s, s.color, s.filled ? raster::LineStyle{} : s.style);
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
    case Mode::Curve:
        drawOutline(fb, s, s.color, s.style);
        break;
    case Mode::Transform:
        break; // 变换模式无图形本体
    }
}

void PaintController::drawClipWindow(FrameBuffer& fb, const Color& c, bool dashed) const {
    if (!clipWin_.valid) {
        return;
    }
    const raster::LineStyle ls = dashed ? raster::LineStyle{0xF0F0u, 1} : raster::LineStyle{};
    if (clipWin_.kind == ClipWindow::Kind::Rect) {
        drawRectOutline(fb, clipWin_.rect, c, ls);
        return;
    }
    const auto& poly = clipWin_.poly;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const FPoint& p1 = poly[i];
        const FPoint& p2 = poly[(i + 1) % poly.size()];
        raster::drawLine(fb, {static_cast<int>(std::lround(p1.x)), static_cast<int>(std::lround(p1.y))},
                         {static_cast<int>(std::lround(p2.x)), static_cast<int>(std::lround(p2.y))},
                         c, ls);
    }
}

void PaintController::commitRender() {
    committed_->clear(Color::Black());

    if (mode_ == Mode::Clip && !clipWin_.valid) {
        // 首次进入裁剪模式：给一个居中的默认矩形窗口。
        clipWin_.kind = ClipWindow::Kind::Rect;
        clipWin_.rect = {committed_->width() / 5, committed_->height() / 5,
                         committed_->width() * 4 / 5, committed_->height() * 4 / 5};
        clipWin_.valid = true;
    }

    for (const auto& s : shapes_) {
        drawShape(*committed_, s);
    }
    // 裁剪窗口边框画在预览层，不进提交层。
}

// 常驻裁剪显示层缓存：窗口内照抄提交层，窗口外保持黑底。
void PaintController::rebuildClipCache() {
    const int w = committed_->width();
    const int h = committed_->height();
    if (!clipCache_ || clipCache_->width() != w || clipCache_->height() != h) {
        clipCache_ = std::make_unique<FrameBuffer>(w, h);
    }
    clipCache_->clear(Color::Black());

    if (clipWin_.kind == ClipWindow::Kind::Rect) {
        const int x0 = std::max(0, clipWin_.rect.xmin);
        const int x1 = std::min(w - 1, clipWin_.rect.xmax);
        const int y0 = std::max(0, clipWin_.rect.ymin);
        const int y1 = std::min(h - 1, clipWin_.rect.ymax);
        for (int y = y0; y <= y1; ++y) {
            clipCache_->copySpanFrom(*committed_, y, x0, x1);
        }
    } else {
        // 凸多边形窗口：每条扫描线与凸多边形相交为单段 [lo, hi]，逐行整段拷贝
        const auto& poly = clipWin_.poly;
        if (poly.empty()) {
            clipCacheDirty_ = false;
            return;
        }
        float minY = poly[0].y;
        float maxY = minY;
        for (const auto& p : poly) {
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
        const int yLo = std::max(0, static_cast<int>(std::floor(minY)));
        const int yHi = std::min(h - 1, static_cast<int>(std::ceil(maxY)));
        for (int y = yLo; y <= yHi; ++y) {
            int lo = std::numeric_limits<int>::max();
            int hi = std::numeric_limits<int>::min();
            for (std::size_t i = 0; i < poly.size(); ++i) {
                const FPoint& p1 = poly[i];
                const FPoint& p2 = poly[(i + 1) % poly.size()];
                if ((p1.y <= y) == (p2.y <= y)) {
                    continue; // 该边不跨此扫描线
                }
                const float t = (static_cast<float>(y) - p1.y) / (p2.y - p1.y);
                const int x = static_cast<int>(std::lround(p1.x + (p2.x - p1.x) * t));
                lo = std::min(lo, x);
                hi = std::max(hi, x);
            }
            if (lo <= hi) {
                clipCache_->copySpanFrom(*committed_, y, lo, hi);
            }
        }
    }
    clipCacheDirty_ = false;
}

void PaintController::copyCommittedClipped(FrameBuffer& fb) {
    if (clipCacheDirty_ || !clipCache_ || clipCache_->width() != fb.width() ||
        clipCache_->height() != fb.height()) {
        rebuildClipCache();
    }
    fb.copyFrom(*clipCache_);
}

void PaintController::drawPreview(FrameBuffer& fb) const {
    // 进行中的拖拽预览（不进入提交层）。
    if (dragging_) {
        const int r = dragRadius();
        switch (mode_) {
        case Mode::Line:
            raster::drawLine(fb, press_, lineDragEnd(), Color::Yellow(), currentStyle());
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
        case Mode::Curve:
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

    // 曲线模式：控制多边形与手柄。
    if (mode_ == Mode::Curve) {
        drawCurveHandles(fb);
    }

    // 曲线输入：控制多边形橡皮筋 + 实时预览。
    if (mode_ == Mode::Curve && !pendingCurve_.empty()) {
        for (std::size_t i = 0; i + 1 < pendingCurve_.size(); ++i) {
            raster::drawLine(fb, pendingCurve_[i], pendingCurve_[i + 1], Color::White(), {});
        }
        raster::drawLine(fb, pendingCurve_.back(), curr_, Color::White(), {});
        for (const auto& p : pendingCurve_) {
            drawCross(fb, p, Color::White(), 3);
        }
        std::vector<FPoint> pts;
        pts.reserve(pendingCurve_.size() + 1);
        for (const auto& p : pendingCurve_) {
            pts.push_back(toF(p));
        }
        pts.push_back(toF(curr_));
        const std::vector<FPoint> samples = sampleCurveKind(curveKind_, pts, 3, 64);
        for (std::size_t i = 0; i + 1 < samples.size(); ++i) {
            raster::drawLine(fb, roundPt(samples[i]), roundPt(samples[i + 1]), Color::Yellow(),
                             currentStyle());
        }
    }

    // 裁剪窗口边框：常驻裁剪时所有模式可见（青色虚线），
    // 否则仅裁剪模式显示（白色实线）。
    if (clipWin_.valid) {
        if (clipOn_) {
            drawClipWindow(fb, Color::Cyan(), true);
        } else if (mode_ == Mode::Clip) {
            drawClipWindow(fb, Color::White(), false);
        }
    }

    // 变换模式标记：选中橙色、参考点青色、对接点橙色。
    if (mode_ == Mode::Transform) {
        if (bandSelect_) { // 框选橡皮筋（品红虚线）
            drawRectOutline(fb, rectFrom(bandStart_, curr_), Color::Magenta(), {0xF0F0u, 1});
        }
        if (rotDrag_) { // 旋转引导：参考点→光标虚线 + 角度读数
            const IPoint pivot = roundPt(rotPivot_);
            raster::drawLine(fb, pivot, curr_, Color::Cyan(), {0xF0F0u, 1});
            raster::drawCircle(fb, pivot, 6, Color::Cyan(), {});
            const std::string deg =
                std::to_string(static_cast<int>(std::lround(rotDeg_))) + "°";
            text::drawText(fb, curr_.x + 12, curr_.y - text::height() - 6, deg.c_str(),
                           Color::Cyan());
        }
        drawSelection(fb);
        if (!selected_.empty()) {
            drawCross(fb, roundPt(rotDrag_ ? rotPivot_ : activeRefPoint()), Color::Cyan(), 6);
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
        clipCacheDirty_ = true;
    }
    if (clipOn_ && clipWin_.valid) {
        copyCommittedClipped(fb);
    } else {
        fb.copyFrom(*committed_); // 提交层 → 显示层
    }
    drawPreview(fb);
    drawToolbar(fb);
}

void PaintController::onKey(int key, int action, int mods) {
    titleDirty_ = true;
    shiftHeld_ = (mods & GLFW_MOD_SHIFT) != 0;
    ctrlHeld_ = (mods & GLFW_MOD_CONTROL) != 0;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) {
        return;
    }
    if (key == GLFW_KEY_Z && (mods & GLFW_MOD_CONTROL) != 0) {
        handleAction(Act::Undo);
        return;
    }
    // 键盘与任务栏按钮共用 handleAction。
    switch (key) {
    case GLFW_KEY_1:
    case GLFW_KEY_L: handleAction(Act::ModeLine); break;
    case GLFW_KEY_2:
    case GLFW_KEY_C: handleAction(Act::ModeCircle); break;
    case GLFW_KEY_3:
    case GLFW_KEY_A: handleAction(Act::ModeArc); break;
    case GLFW_KEY_4: handleAction(Act::ModePolygon); break;
    case GLFW_KEY_5: handleAction(Act::ModeSeed); break;
    case GLFW_KEY_6: handleAction(Act::ModeClip); break;
    case GLFW_KEY_7: handleAction(Act::ModeXform); break;
    case GLFW_KEY_8: handleAction(Act::ModeCurve); break;
    case GLFW_KEY_B: handleAction(Act::CurveType); break;
    case GLFW_KEY_S: handleAction(Act::Style); break;
    case GLFW_KEY_W: handleAction(Act::Width); break;
    case GLFW_KEY_X: handleAction(Act::Color); break;
    case GLFW_KEY_F: handleAction(Act::PolyFill); break;
    case GLFW_KEY_M: handleAction(Act::ClipAlgo); break;
    case GLFW_KEY_V: handleAction(Act::ClipToggle); break;
    case GLFW_KEY_K: handleAction(Act::ClipRect); break;
    case GLFW_KEY_P: handleAction(Act::ClipPoly); break;
    case GLFW_KEY_LEFT: handleAction(Act::NudgeL); break;
    case GLFW_KEY_RIGHT: handleAction(Act::NudgeR); break;
    case GLFW_KEY_UP: handleAction(Act::NudgeU); break;
    case GLFW_KEY_DOWN: handleAction(Act::NudgeD); break;
    case GLFW_KEY_EQUAL: handleAction(Act::ScaleUp); break;
    case GLFW_KEY_MINUS: handleAction(Act::ScaleDown); break;
    case GLFW_KEY_T: handleAction(Act::RotCW); break;
    case GLFW_KEY_R: handleAction(Act::RotCCW); break;
    case GLFW_KEY_G: handleAction(Act::RefPoint); break;
    case GLFW_KEY_J: handleAction(Act::Dock); break;
    case GLFW_KEY_DELETE:
    case GLFW_KEY_BACKSPACE: handleAction(Act::DeleteSel); break;
    case GLFW_KEY_E: handleAction(Act::Clear); break;
    default:
        break;
    }
}

void PaintController::switchMode(Mode m) {
    mode_ = m;
    cancelInProgress();
    titleDirty_ = true;
    markDirty();
}

// 键盘与任务栏按钮的统一操作入口。
void PaintController::handleAction(Act a) {
    switch (a) {
    case Act::ModeLine: switchMode(Mode::Line); break;
    case Act::ModeCircle: switchMode(Mode::Circle); break;
    case Act::ModeArc: switchMode(Mode::Arc); break;
    case Act::ModePolygon: switchMode(Mode::Polygon); break;
    case Act::ModeSeed: switchMode(Mode::SeedFill); break;
    case Act::ModeClip: switchMode(Mode::Clip); break;
    case Act::ModeXform: switchMode(Mode::Transform); break;
    case Act::ModeCurve: switchMode(Mode::Curve); break;
    case Act::CurveType:
        if (mode_ == Mode::Curve) {
            // 三态循环：Bezier → 固支 B 样条 → 均匀 B 样条 → Bezier。
            curveKind_ = static_cast<CurveKind>((static_cast<int>(curveKind_) + 1) % 3);
            // 同时切换最近一条已提交曲线的类型：按一下就能看到两种样条的差别。
            for (auto it = shapes_.rbegin(); it != shapes_.rend(); ++it) {
                if (it->mode == Mode::Curve) {
                    pushUndo();
                    it->curveKind = curveKind_;
                    markDirty();
                    break;
                }
            }
        }
        break;
    case Act::CurveClose:
        if (mode_ == Mode::Curve) {
            commitCurve();
        }
        break;
    case Act::Style:
        styleIdx_ = (styleIdx_ + 1) % 3;
        break;
    case Act::Width:
        widthIdx_ = (widthIdx_ + 1) % 3;
        break;
    case Act::Color:
        fillColorIdx_ = (fillColorIdx_ + 1) % 6;
        break;
    case Act::Undo:
        undo();
        break;
    case Act::Clear:
        if (!shapes_.empty()) {
            pushUndo();
        }
        shapes_.clear();
        cancelInProgress();
        markDirty();
        break;
    case Act::PolyFill:
        if (mode_ == Mode::Polygon && !pendingPolygon_.empty()) {
            // 正在连点：切换本次闭合用的样式
            polygonFilled_ = !polygonFilled_;
        } else {
            // 不在输入中：切换历史里最后一个多边形；一个都没有则改默认值
            bool toggled = false;
            for (auto it = shapes_.rbegin(); it != shapes_.rend(); ++it) {
                if (it->mode == Mode::Polygon) {
                    pushUndo();
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
    case Act::PolyClose:
        if (mode_ == Mode::Polygon && !pendingPolygon_.empty()) {
            closePolygon();
        }
        break;
    case Act::ClipRect:
        if (mode_ == Mode::Clip) {
            clipRectEdit_ = true;
            clipPolyEdit_ = false;
            pendingClipWin_.clear();
        }
        break;
    case Act::ClipPoly:
        if (mode_ == Mode::Clip) {
            clipPolyEdit_ = true;
            clipRectEdit_ = false;
            pendingClipWin_.clear();
        }
        break;
    case Act::ClipAlgo:
        if (mode_ == Mode::Clip) {
            clipAlgoIdx_ = 1 - clipAlgoIdx_;
            markDirty();
        }
        break;
    case Act::ClipToggle:
        if (clipWin_.valid) {
            clipOn_ = !clipOn_;
        }
        break;
    case Act::ScaleUp:
    case Act::ScaleDown:
        if (mode_ == Mode::Transform && !selected_.empty()) {
            const FPoint ref = activeRefPoint();
            const float f = (a == Act::ScaleUp) ? 1.1f : 1.0f / 1.1f;
            transformSelected(geom::scaleAbout(ref.x, ref.y, f, f), 0.0f, f);
        }
        break;
    case Act::RotCW:
    case Act::RotCCW:
        if (mode_ == Mode::Transform && !selected_.empty()) {
            const FPoint ref = activeRefPoint();
            const float deg = (a == Act::RotCW) ? 5.0f : -5.0f;
            transformSelected(geom::rotateAbout(ref.x, ref.y, deg * kPiF / 180.0f), deg, 1.0f);
        }
        break;
    case Act::RefPoint:
        if (mode_ == Mode::Transform) {
            refManual_ = !refManual_;
            refPick_ = refManual_;
        }
        break;
    case Act::Dock:
        if (mode_ == Mode::Transform) {
            dockStage_ = 1;
            dockAShapeIdx_ = -1;
        }
        break;
    case Act::NudgeL:
    case Act::NudgeR:
    case Act::NudgeU:
    case Act::NudgeD:
        if (mode_ == Mode::Transform && !selected_.empty()) {
            const float dx = static_cast<float>((a == Act::NudgeR) - (a == Act::NudgeL));
            const float dy = static_cast<float>((a == Act::NudgeD) - (a == Act::NudgeU));
            transformSelected(geom::translate(dx, dy), 0.0f, 1.0f);
        }
        break;
    case Act::DeleteSel:
        if (mode_ == Mode::Transform && !selected_.empty()) {
            std::vector<std::uint64_t> parentIds;
            std::vector<int> toErase;
            for (int idx : selected_) {
                if (idx >= 0 && idx < static_cast<int>(shapes_.size())) {
                    parentIds.push_back(shapes_[idx].id);
                    toErase.push_back(idx);
                }
            }
            // 删父图形时一并删掉绑定的种子填充。
            for (int i = 0; i < static_cast<int>(shapes_.size()); ++i) {
                const Shape& s = shapes_[i];
                if (s.mode == Mode::SeedFill && s.parentId != 0 &&
                    std::find(parentIds.begin(), parentIds.end(), s.parentId) !=
                        parentIds.end()) {
                    toErase.push_back(i);
                }
            }
            std::sort(toErase.begin(), toErase.end());
            toErase.erase(std::unique(toErase.begin(), toErase.end()), toErase.end());
            if (!toErase.empty()) {
                pushUndo();
            }
            for (auto it = toErase.rbegin(); it != toErase.rend(); ++it) {
                shapes_.erase(shapes_.begin() + *it);
            }
            selected_.clear();
            selSnapshot_.clear();
            attachedSeedSnapshot_.clear();
            handleDrag_ = false;
            activeHandle_ = SelHandle::None;
            markDirty();
        }
        break;
    case Act::None:
    default:
        break;
    }
}

// ================= 自绘任务栏 =================

// 任务栏总高随字体度量变化（两行按钮 + 边距）。
int PaintController::toolbarHeight() const {
    const int rowH = text::height() + 8;
    return 2 + rowH + 3 + rowH + 2;
}

void PaintController::buildToolbar(std::vector<ToolButton>& btns) const {
    btns.clear();
    const int rowH = text::height() + 8;
    const int y1 = 2;
    const int y2 = 2 + rowH + 3;
    auto add = [&](Act act, const std::string& label, int x, int y, bool active) {
        ToolButton b;
        b.act = act;
        b.label = label;
        b.w = text::textWidth(label.c_str()) + 12;
        b.h = rowH;
        b.x = x;
        b.y = y;
        b.active = active;
        btns.push_back(b);
        return x + b.w + 3;
    };

    // 第一行：7 个模式 + 清屏（标签保留快捷键前缀）
    const struct {
        Act act;
        const char* label;
        Mode mode;
    } modes[] = {
        {Act::ModeLine, "1 直线", Mode::Line},     {Act::ModeCircle, "2 圆", Mode::Circle},
        {Act::ModeArc, "3 圆弧", Mode::Arc},       {Act::ModePolygon, "4 多边形", Mode::Polygon},
        {Act::ModeSeed, "5 种子", Mode::SeedFill}, {Act::ModeClip, "6 裁剪", Mode::Clip},
        {Act::ModeXform, "7 变换", Mode::Transform}, {Act::ModeCurve, "8 曲线", Mode::Curve},
    };
    int x = 2;
    for (const auto& m : modes) {
        x = add(m.act, m.label, x, y1, mode_ == m.mode);
    }
    x = add(Act::Clear, "E 清屏", x, y1, false);
    add(Act::Undo, "Ctrl+Z 撤销", x, y1, false);

    // 第二行：属性 + 随模式变化的上下文按钮（标签显示当前值）
    x = add(Act::Style, std::string("S 线型:") + kStyleNames[styleIdx_], 2, y2, false);
    x = add(Act::Width, "W 线宽:" + std::to_string(kWidths[widthIdx_]), x, y2, false);
    x = add(Act::Color, std::string("X 颜色:") + kPaletteNames[fillColorIdx_], x, y2, false);
    if (mode_ == Mode::Polygon) {
        x = add(Act::PolyFill, polygonFilled_ ? "F 填充:开" : "F 填充:关", x + 8, y2, true);
        x = add(Act::PolyClose, "右键闭合", x, y2, false);
    } else if (mode_ == Mode::Clip) {
        x = add(Act::ClipRect, "K 矩形窗", x + 8, y2, false);
        x = add(Act::ClipPoly, "P 多边形窗", x, y2, false);
        x = add(Act::ClipAlgo, "M 算法", x, y2, false);
    } else if (mode_ == Mode::Curve) {
        x = add(Act::CurveType, std::string("B 类型:") + curveKindShort(curveKind_), x + 8, y2,
                curveKind_ != CurveKind::Bezier);
        x = add(Act::CurveClose, "右键结束", x, y2, false);
    } else if (mode_ == Mode::Transform) {
        x = add(Act::ScaleUp, "+", x + 8, y2, false);
        x = add(Act::ScaleDown, "-", x, y2, false);
        x = add(Act::RotCCW, "R 逆旋", x, y2, false);
        x = add(Act::RotCW, "T 顺旋", x, y2, false);
        x = add(Act::RefPoint, "G 参考点", x, y2, refManual_);
        x = add(Act::Dock, "J 对接", x, y2, dockStage_ > 0);
        x = add(Act::DeleteSel, "Del 删除", x, y2, false);
        x = add(Act::NudgeL, "←", x, y2, false);
        x = add(Act::NudgeR, "→", x, y2, false);
        x = add(Act::NudgeU, "↑", x, y2, false);
        x = add(Act::NudgeD, "↓", x, y2, false);
    }
    // 常驻裁剪开关：定义过窗口后所有模式都显示。
    if (clipWin_.valid) {
        add(Act::ClipToggle, clipOn_ ? "V 裁剪:开" : "V 裁剪:关", x + 8, y2, clipOn_);
    }
}

// 状态指纹：不变时复用缓存帧。
std::uint64_t PaintController::toolbarKey(int width) const {
    std::uint64_t k = static_cast<std::uint64_t>(width) << 32;
    k |= static_cast<std::uint64_t>(mode_);                                // 0-3
    k |= static_cast<std::uint64_t>(polygonFilled_) << 4;
    k |= static_cast<std::uint64_t>(clipWin_.valid) << 5;
    k |= static_cast<std::uint64_t>(clipOn_) << 6;
    k |= static_cast<std::uint64_t>(static_cast<int>(curveKind_)) << 7;     // 7-8
    k |= static_cast<std::uint64_t>(refManual_) << 9;
    k |= static_cast<std::uint64_t>(dockStage_ > 0) << 10;
    k |= static_cast<std::uint64_t>(styleIdx_) << 11;                      // 11-12
    k |= static_cast<std::uint64_t>(widthIdx_) << 13;                      // 13-14
    k |= static_cast<std::uint64_t>(fillColorIdx_) << 15;                  // 15-17
    return k;
}

void PaintController::renderToolbar(FrameBuffer& fb,
                                    const std::vector<ToolButton>& btns) const {
    const Color kBg(34, 34, 40);
    const Color kFace(58, 58, 70);
    const Color kActive(70, 110, 180);
    const Color kBorder(120, 120, 130);
    fb.fillRect(0, 0, fb.width() - 1, fb.height() - 1, kBg);
    for (const auto& b : btns) {
        fb.fillRect(b.x, b.y, b.x + b.w - 1, b.y + b.h - 1, b.active ? kActive : kFace);
        fb.fillRect(b.x, b.y, b.x + b.w - 1, b.y, kBorder);
        fb.fillRect(b.x, b.y + b.h - 1, b.x + b.w - 1, b.y + b.h - 1, kBorder);
        fb.fillRect(b.x, b.y, b.x, b.y + b.h - 1, kBorder);
        fb.fillRect(b.x + b.w - 1, b.y, b.x + b.w - 1, b.y + b.h - 1, kBorder);
        const int tx = b.x + (b.w - text::textWidth(b.label.c_str())) / 2 + 1;
        const int ty = b.y + (b.h - text::height()) / 2;
        text::drawText(fb, tx, ty, b.label.c_str(), Color(230, 230, 235));
    }
}

void PaintController::drawToolbar(FrameBuffer& fb) const {
    const int tbH = toolbarHeight();
    const std::uint64_t key = toolbarKey(fb.width());
    if (!toolbarCache_ || toolbarCache_->width() != fb.width() ||
        toolbarCache_->height() != tbH || toolbarCacheKey_ != key) {
        toolbarCache_ = std::make_unique<FrameBuffer>(fb.width(), tbH);
        buildToolbar(toolbarLayout_);
        renderToolbar(*toolbarCache_, toolbarLayout_);
        toolbarCacheKey_ = key;
    }
    for (int y = 0; y < tbH; ++y) {
        fb.copySpanFrom(*toolbarCache_, y, 0, fb.width() - 1);
    }
}

bool PaintController::toolbarPress(IPoint p) {
    const int tbH = toolbarHeight();
    // 负坐标是越界鼠标事件，放行给画布逻辑。
    if (p.y < 0 || p.y >= tbH || p.x < 0) {
        return false;
    }
    std::vector<ToolButton> btns;
    buildToolbar(btns);
    for (const auto& b : btns) {
        if (p.x >= b.x && p.x < b.x + b.w && p.y >= b.y && p.y < b.y + b.h) {
            handleAction(b.act);
            return true;
        }
    }
    return true; // 未点中按钮：吞掉
}

void PaintController::closePolygon() {
    if (pendingPolygon_.size() >= 3) {
        Shape s;
        s.mode = Mode::Polygon;
        s.polygon = pendingPolygon_;
        s.filled = polygonFilled_;
        s.style = currentStyle();
        s.color = polygonFilled_ ? currentFillColor() : Color::White();
        addShape(std::move(s));
        markDirty();
    }
    pendingPolygon_.clear();
}

void PaintController::setRectClipWindow(IPoint p1, IPoint p2) {
    const raster::Rect r = rectFrom(p1, p2);
    if (r.xmax - r.xmin < 8 || r.ymax - r.ymin < 8) {
        return; // 太小的窗口忽略
    }
    pushUndo();
    clipWin_.kind = ClipWindow::Kind::Rect;
    clipWin_.rect = r;
    clipWin_.valid = true;
    clipOn_ = true;
    markDirty();
}

// ================= 曲线 =================

void PaintController::commitCurve() {
    if (pendingCurve_.size() >= 2) {
        Shape s;
        s.mode = Mode::Curve;
        s.curve.reserve(pendingCurve_.size());
        for (const auto& p : pendingCurve_) {
            s.curve.push_back(toF(p));
        }
        s.curveKind = curveKind_;
        s.curveDeg = 3;
        s.style = currentStyle();
        addShape(std::move(s));
        markDirty();
    }
    pendingCurve_.clear();
}

const char* PaintController::curveKindName(CurveKind k) {
    switch (k) {
    case CurveKind::BSplineClamped: return "B样条·固支";
    case CurveKind::BSplineUniform: return "B样条·均匀";
    default: return "Bezier";
    }
}

const char* PaintController::curveKindShort(CurveKind k) {
    switch (k) {
    case CurveKind::BSplineClamped: return "固支";
    case CurveKind::BSplineUniform: return "均匀";
    default: return "Bezier";
    }
}

std::vector<FPoint> PaintController::sampleCurveKind(CurveKind kind,
                                                     const std::vector<FPoint>& pts,
                                                     int degree, int segments) {
    if (kind == CurveKind::Bezier) {
        return curves::sampleBezier(pts, segments);
    }
    return curves::sampleBSpline(pts, degree, segments, kind == CurveKind::BSplineClamped);
}

std::vector<FPoint> PaintController::curveSamples(const Shape& s) const {
    return sampleCurveKind(s.curveKind, s.curve, s.curveDeg, kCurveSegments);
}

// 曲线模式下可抓取的控制点（从上层往下找）。
std::pair<int, int> PaintController::pickCurveCtrl(IPoint p) const {
    for (int i = static_cast<int>(shapes_.size()) - 1; i >= 0; --i) {
        const Shape& s = shapes_[static_cast<std::size_t>(i)];
        if (s.mode != Mode::Curve) {
            continue;
        }
        for (int j = 0; j < static_cast<int>(s.curve.size()); ++j) {
            if (dist(p, roundPt(s.curve[static_cast<std::size_t>(j)])) <= kHitTol) {
                return {i, j};
            }
        }
    }
    return {-1, -1};
}

// 曲线辅助显示：控制多边形（暗灰虚线）+ 控制点手柄，拖拽中的点橙色高亮。
void PaintController::drawCurveHandles(FrameBuffer& fb) const {
    for (int i = 0; i < static_cast<int>(shapes_.size()); ++i) {
        const Shape& s = shapes_[static_cast<std::size_t>(i)];
        if (s.mode != Mode::Curve) {
            continue;
        }
        for (std::size_t j = 0; j + 1 < s.curve.size(); ++j) {
            raster::drawLine(fb, roundPt(s.curve[j]), roundPt(s.curve[j + 1]),
                             Color(90, 90, 100), {0xF0F0u, 1});
        }
        for (std::size_t j = 0; j < s.curve.size(); ++j) {
            const bool hot = curveDrag_ && dragShapeIdx_ == i &&
                             dragCtrlIdx_ == static_cast<int>(j);
            drawCross(fb, roundPt(s.curve[j]), hot ? Color::Orange() : Color::Cyan(), 3);
        }
    }
}

// ================= 变换 =================

// 种子填充/裁剪线段不是参数图形，不参与选中与变换。
bool PaintController::shapeSelectable(const Shape& s) const {
    return s.mode == Mode::Line || s.mode == Mode::Circle || s.mode == Mode::Arc ||
           s.mode == Mode::Polygon || s.mode == Mode::Curve;
}

FPoint PaintController::shapeCentroid(const Shape& s) const {
    switch (s.mode) {
    case Mode::Line:
        return {(s.a.x + s.b.x) * 0.5f, (s.a.y + s.b.y) * 0.5f};
    case Mode::Polygon: // 课程口径：顶点平均（非面积重心）
        return s.polygon.empty() ? toF(s.a) : averageOf(s.polygon);
    case Mode::Curve: // 与多边形同口径：控制点平均
        return s.curve.empty() ? toF(s.a) : averageOf(s.curve);
    case Mode::Circle:
    case Mode::Arc:
    default:
        return toF(s.a);
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
    if (!selected_.empty()) {
        pushUndo();
    }
    for (int idx : selected_) {
        if (idx < 0 || idx >= static_cast<int>(shapes_.size())) {
            continue;
        }
        applyToShape(shapes_[idx], m, rotDeg, scaleF);
    }
    transformAttachedSeedsForSelection(m, false);
    markDirty();
}

// 选中参数快照：拖拽期间从快照重算，避免整数舍入累积。
void PaintController::snapshotSelection() {
    selSnapshot_.clear();
    selSnapshot_.reserve(selected_.size());
    for (int idx : selected_) {
        if (idx >= 0 && idx < static_cast<int>(shapes_.size())) {
            selSnapshot_.push_back(shapes_[idx]);
        }
    }
}

// 开始整体拖拽：记录锚点并快照选中集合。
void PaintController::beginXformDrag(IPoint anchor) {
    beginGesture();
    xformDrag_ = true;
    lastDrag_ = anchor;
    snapshotSelection();
    snapshotAttachedSeeds();
}

// 右键拖拽旋转：参考点在按下时锁定（默认重心，G 指定后为用户点），起始角同刻锁定。
void PaintController::beginRotateDrag() {
    beginGesture();
    rotDrag_ = true;
    rotArmed_ = false;
    rotDeg_ = 0.0f;
    rotPivot_ = activeRefPoint();
    const float dx = static_cast<float>(curr_.x) - rotPivot_.x;
    const float dy = static_cast<float>(curr_.y) - rotPivot_.y;
    if (dx * dx + dy * dy >= 36.0f) { // 按下点离参考点太近时不锁角，等光标移开后补锁
        rotStartAngle_ = std::atan2(dy, dx);
        rotArmed_ = true;
    }
    snapshotSelection();
    snapshotAttachedSeeds();
}

// 旋转角 = 当前极角 - 按下极角；每帧从快照重算，避免整数端点累积漂移。
void PaintController::applyRotateDrag(IPoint pos) {
    const float dx = static_cast<float>(pos.x) - rotPivot_.x;
    const float dy = static_cast<float>(pos.y) - rotPivot_.y;
    if (!rotArmed_) {
        if (dx * dx + dy * dy < 36.0f) { // 6px 内极角不稳定，先不锁起始角
            return;
        }
        rotStartAngle_ = std::atan2(dy, dx);
        rotArmed_ = true;
    }
    ensureGestureUndo();
    float deg = (std::atan2(dy, dx) - rotStartAngle_) * (180.0f / kPiF);
    if (shiftHeld_) {
        deg = std::round(deg / 15.0f) * 15.0f;
    }
    rotDeg_ = deg;
    const geom::Mat3 m = geom::rotateAbout(rotPivot_.x, rotPivot_.y, deg * kPiF / 180.0f);
    for (std::size_t k = 0; k < selected_.size() && k < selSnapshot_.size(); ++k) {
        const int idx = selected_[k];
        if (idx < 0 || idx >= static_cast<int>(shapes_.size())) {
            continue;
        }
        shapes_[idx] = selSnapshot_[k];
        applyToShape(shapes_[idx], m, deg, 1.0f);
    }
    transformAttachedSeedsForSelection(m, true);
    markDirty();
}

// 入列表并分配稳定 id（种子填充用 parentId 指向所属图形）。
void PaintController::pushUndo() {
    if (undoStack_.size() >= 64) {
        undoStack_.erase(undoStack_.begin());
    }
    undoStack_.push_back(DocState{shapes_, nextShapeId_, clipWin_, clipOn_});
}

void PaintController::undo() {
    if (undoStack_.empty()) {
        return;
    }
    DocState st = std::move(undoStack_.back());
    undoStack_.pop_back();
    shapes_ = std::move(st.shapes);
    nextShapeId_ = st.nextShapeId;
    clipWin_ = st.clipWin;
    clipOn_ = st.clipOn;
    selected_.clear();
    handleDrag_ = false;
    xformDrag_ = false;
    rotDrag_ = false;
    rotArmed_ = false;
    rotDeg_ = 0.0f;
    curveDrag_ = false;
    dockStage_ = 0;
    pendingPolygon_.clear();
    pendingCurve_.clear();
    pendingClipWin_.clear();
    dragging_ = false;
    arcAdjust_ = false;
    clipRectEdit_ = false;
    clipPolyEdit_ = false;
    refPick_ = false;
    bandSelect_ = false;
    markDirty();
    titleDirty_ = true;
}

void PaintController::beginGesture() { gestureUndoPushed_ = false; }

void PaintController::ensureGestureUndo() {
    if (!gestureUndoPushed_) {
        pushUndo();
        gestureUndoPushed_ = true;
    }
}

void PaintController::addShape(Shape s) {
    pushUndo();
    s.id = nextShapeId_++;
    shapes_.push_back(std::move(s));
}

// 包含 p 的最上层闭合图形 id；无则 0（自由种子）。
std::uint64_t PaintController::findSeedParent(IPoint p) const {
    for (int i = static_cast<int>(shapes_.size()) - 1; i >= 0; --i) {
        const Shape& s = shapes_[i];
        if (s.mode == Mode::Circle &&
            distSq(p, s.a) <= static_cast<float>(s.radius) * static_cast<float>(s.radius)) {
            return s.id;
        }
        if (s.mode == Mode::Polygon && pointInPolygon(p, s.polygon)) {
            return s.id;
        }
    }
    return 0;
}

// 拖拽开始：快照选中图形的附加种子。
void PaintController::snapshotAttachedSeeds() {
    attachedSeedSnapshot_.clear();
    auto selectedId = [&](std::uint64_t id) {
        for (int idx : selected_) {
            if (idx >= 0 && idx < static_cast<int>(shapes_.size()) && shapes_[idx].id == id) {
                return true;
            }
        }
        return false;
    };
    for (int i = 0; i < static_cast<int>(shapes_.size()); ++i) {
        const Shape& s = shapes_[i];
        if (s.mode == Mode::SeedFill && s.parentId != 0 && selectedId(s.parentId)) {
            attachedSeedSnapshot_.push_back({i, s.a});
        }
    }
}

// 变换某图形的附加种子；useSnapshot=true 时从拖拽起点快照重算。
void PaintController::transformSeedsOfParent(std::uint64_t parentId, const geom::Mat3& m,
                                             bool useSnapshot) {
    const Shape* parent = nullptr;
    for (const auto& s : shapes_) {
        if (s.id == parentId) {
            parent = &s;
            break;
        }
    }
    if (parent == nullptr) {
        return;
    }
    for (int i = 0; i < static_cast<int>(shapes_.size()); ++i) {
        Shape& seed = shapes_[i];
        if (seed.mode != Mode::SeedFill || seed.parentId != parentId) {
            continue;
        }
        IPoint base = seed.a;
        if (useSnapshot) {
            for (const auto& snap : attachedSeedSnapshot_) {
                if (snap.first == i) {
                    base = snap.second;
                    break;
                }
            }
        }
        seed.a = clampSeedToParent(roundPt(geom::apply(m, toF(base))), *parent);
    }
}

void PaintController::transformAttachedSeedsForSelection(const geom::Mat3& m, bool useSnapshot) {
    for (int idx : selected_) {
        if (idx < 0 || idx >= static_cast<int>(shapes_.size())) {
            continue;
        }
        transformSeedsOfParent(shapes_[idx].id, m, useSnapshot);
    }
}

// 取整后把种子钳回父图形内（圆投影到半径内，多边形找最近内点）。
IPoint PaintController::clampSeedToParent(IPoint seed, const Shape& parent) const {
    if (parent.mode == Mode::Circle) {
        if (parent.radius <= 1) {
            return parent.a;
        }
        const float dx = static_cast<float>(seed.x - parent.a.x);
        const float dy = static_cast<float>(seed.y - parent.a.y);
        const float d = std::sqrt(dx * dx + dy * dy);
        const float maxD = static_cast<float>(parent.radius - 1);
        if (d > maxD && d > 1e-6f) {
            return {parent.a.x + static_cast<int>(std::lround(dx / d * maxD)),
                    parent.a.y + static_cast<int>(std::lround(dy / d * maxD))};
        }
        return seed;
    }
    if (parent.mode == Mode::Polygon) {
        if (parent.polygon.size() < 3 || pointInPolygon(seed, parent.polygon)) {
            return seed;
        }
        for (int r = 1; r <= 8; ++r) {
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (std::abs(dx) != r && std::abs(dy) != r) {
                        continue;
                    }
                    const IPoint q{seed.x + dx, seed.y + dy};
                    if (pointInPolygon(q, parent.polygon)) {
                        return q;
                    }
                }
            }
        }
        const IPoint c = roundPt(shapeCentroid(parent));
        if (pointInPolygon(c, parent.polygon)) {
            return c;
        }
    }
    return seed;
}

// 矩阵作用到图形参数；圆/弧的半径与角度按 rotDeg/scaleF 修正。
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
    case Mode::Curve:
        // 控制点是浮点参数，不取整。
        for (auto& p : s.curve) {
            p = geom::apply(m, p);
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
        case Mode::Circle: {
            // 圆内或圆环外 5px：到圆心 <= r + 5。
            const float reach = static_cast<float>(s.radius) + kHitTol;
            if (distSq(p, s.a) <= reach * reach) {
                return i;
            }
            break;
        }
        case Mode::Arc:
            if (std::fabs(dist(p, s.a) - s.radius) <= kHitTol &&
                degInRange(angleOf(p, s.a), s.startDeg, s.endDeg)) {
                return i;
            }
            break;
        case Mode::Polygon: {
            if (pointInPolygon(p, s.polygon)) {
                return i;
            }
            // 未填充多边形按轮廓 5px 容差命中。
            const int n = static_cast<int>(s.polygon.size());
            for (int k = 0; k < n; ++k) {
                if (distToSegment(p, s.polygon[k], s.polygon[(k + 1) % n]) <= kHitTol) {
                    return i;
                }
            }
            break;
        }
        case Mode::Curve: {
            // 命中 = 点击点到采样折线的最短距离在容差内
            const std::vector<FPoint> samples = curveSamples(s);
            for (std::size_t k = 0; k + 1 < samples.size(); ++k) {
                if (distToSegment(p, roundPt(samples[k]), roundPt(samples[k + 1])) <= kHitTol) {
                    return i;
                }
            }
            break;
        }
        default:
            break;
        }
    }
    return -1;
}

// 框选相交判定：线段/多边形边采样，圆用解析法，弧按角度范围。
bool PaintController::shapeIntersectsRect(const Shape& s, const raster::Rect& r) const {
    auto inRect = [&](IPoint p) {
        return p.x >= r.xmin && p.x <= r.xmax && p.y >= r.ymin && p.y <= r.ymax;
    };
    auto segHits = [&](IPoint a, IPoint b) {
        if (inRect(a) || inRect(b)) {
            return true;
        }
        const int steps = std::max(1, (std::abs(b.x - a.x) + std::abs(b.y - a.y)) / 4);
        for (int k = 1; k < steps; ++k) {
            const IPoint p{a.x + (b.x - a.x) * k / steps, a.y + (b.y - a.y) * k / steps};
            if (inRect(p)) {
                return true;
            }
        }
        return false;
    };
    switch (s.mode) {
    case Mode::Line:
        return segHits(s.a, s.b);
    case Mode::Circle: {
        const int cx = std::max(r.xmin, std::min(s.a.x, r.xmax));
        const int cy = std::max(r.ymin, std::min(s.a.y, r.ymax));
        const int dx = s.a.x - cx;
        const int dy = s.a.y - cy;
        return dx * dx + dy * dy <= s.radius * s.radius;
    }
    case Mode::Arc: {
        const float span = normDeg(s.endDeg - s.startDeg);
        const int n = span <= 0.0f ? 64 : std::max(1, static_cast<int>(span / 5.0f));
        for (int k = 0; k <= n; ++k) {
            const float deg = s.startDeg + span * static_cast<float>(k) / static_cast<float>(n);
            const float rad = deg * kPiF / 180.0f;
            const IPoint p{s.a.x + static_cast<int>(std::lround(std::cos(rad) * s.radius)),
                           s.a.y + static_cast<int>(std::lround(std::sin(rad) * s.radius))};
            if (inRect(p)) {
                return true;
            }
        }
        return false;
    }
    case Mode::Polygon: {
        for (const auto& p : s.polygon) {
            if (inRect(p)) {
                return true;
            }
        }
        const IPoint corners[4] = {{r.xmin, r.ymin}, {r.xmax, r.ymin}, {r.xmax, r.ymax},
                                   {r.xmin, r.ymax}};
        for (const auto& c : corners) {
            if (pointInPolygon(c, s.polygon)) {
                return true;
            }
        }
        const int n = static_cast<int>(s.polygon.size());
        for (int k = 0; k < n; ++k) {
            if (segHits(s.polygon[k], s.polygon[(k + 1) % n])) {
                return true;
            }
        }
        return false;
    }
    case Mode::Curve: {
        const std::vector<FPoint> samples = curveSamples(s);
        for (std::size_t k = 0; k + 1 < samples.size(); ++k) {
            if (segHits(roundPt(samples[k]), roundPt(samples[k + 1]))) {
                return true;
            }
        }
        return false;
    }
    default:
        return false;
    }
}

// 吸附到轮廓最近点，并给出单位切向（对接用）。
int PaintController::pickOutlineShape(IPoint p, FPoint& outPt, FPoint& outTan, int skipIdx) const {
    for (int i = static_cast<int>(shapes_.size()) - 1; i >= 0; --i) {
        const Shape& s = shapes_[i];
        if (!shapeSelectable(s) || i == skipIdx) {
            continue;
        }
        switch (s.mode) {
        case Mode::Line: {
            const SegmentProjection pr = projectOnSegment(p, toF(s.a), toF(s.b));
            if (pr.dist > kSnapTol) {
                break;
            }
            outPt = pr.point;
            outTan = unitVec({static_cast<float>(s.b.x) - static_cast<float>(s.a.x),
                              static_cast<float>(s.b.y) - static_cast<float>(s.a.y)});
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
                const SegmentProjection pr = projectOnSegment(p, toF(p1), toF(p2));
                if (pr.dist < best) {
                    best = pr.dist;
                    bestPt = pr.point;
                    bestTan = unitVec({static_cast<float>(p2.x) - static_cast<float>(p1.x),
                                       static_cast<float>(p2.y) - static_cast<float>(p1.y)});
                }
            }
            if (best <= kSnapTol) {
                outPt = bestPt;
                outTan = bestTan;
                return i;
            }
            break;
        }
        case Mode::Curve: {
            // 采样折线：取最近段投影点，切向沿该段。
            const std::vector<FPoint> samples = curveSamples(s);
            float best = 1e30f;
            FPoint bestPt{}, bestTan{};
            for (std::size_t k = 0; k + 1 < samples.size(); ++k) {
                const FPoint& q1 = samples[k];
                const FPoint& q2 = samples[k + 1];
                const SegmentProjection pr = projectOnSegment(p, q1, q2);
                if (pr.dist < best) {
                    best = pr.dist;
                    bestPt = pr.point;
                    bestTan = unitVec({q2.x - q1.x, q2.y - q1.y});
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

// ================= 画板式选择编辑：外接矩形 + 控制柄 =================

// 单图形最小外接矩形（矩形字段为像素闭区间）
raster::Rect PaintController::shapeBounds(const Shape& s) const {
    switch (s.mode) {
    case Mode::Line:
        return rectFrom(s.a, s.b);
    case Mode::Circle:
        return {s.a.x - s.radius, s.a.y - s.radius, s.a.x + s.radius, s.a.y + s.radius};
    case Mode::Arc: {
        const float span = normDeg(s.endDeg - s.startDeg);
        if (span <= 0.0f) { // 整圆
            return {s.a.x - s.radius, s.a.y - s.radius, s.a.x + s.radius, s.a.y + s.radius};
        }
        const auto pointAt = [&](float deg) {
            const float rad = deg * kPiF / 180.0f;
            return IPoint{s.a.x + static_cast<int>(std::lround(s.radius * std::cos(rad))),
                          s.a.y + static_cast<int>(std::lround(s.radius * std::sin(rad)))};
        };
        const IPoint first = pointAt(s.startDeg);
        raster::Rect r{first.x, first.y, first.x, first.y};
        const auto include = [&](IPoint p) {
            r.xmin = std::min(r.xmin, p.x);
            r.ymin = std::min(r.ymin, p.y);
            r.xmax = std::max(r.xmax, p.x);
            r.ymax = std::max(r.ymax, p.y);
        };
        include(pointAt(s.endDeg));
        // 圆的轴向极值在 0/90/180/270，落在弧段内的才纳入。
        for (const float deg : {0.0f, 90.0f, 180.0f, 270.0f}) {
            if (degInRange(deg, s.startDeg, s.endDeg)) {
                include(pointAt(deg));
            }
        }
        return r;
    }
    case Mode::Polygon: {
        if (s.polygon.empty()) {
            return {s.a.x, s.a.y, s.a.x, s.a.y};
        }
        raster::Rect r{s.polygon[0].x, s.polygon[0].y, s.polygon[0].x, s.polygon[0].y};
        for (const auto& p : s.polygon) {
            r.xmin = std::min(r.xmin, p.x);
            r.ymin = std::min(r.ymin, p.y);
            r.xmax = std::max(r.xmax, p.x);
            r.ymax = std::max(r.ymax, p.y);
        }
        return r;
    }
    case Mode::Curve: {
        // 取采样折线范围（比控制点范围更贴合曲线）。
        const std::vector<FPoint> samples = curveSamples(s);
        if (samples.empty()) {
            return {s.a.x, s.a.y, s.a.x, s.a.y};
        }
        raster::Rect r{static_cast<int>(samples[0].x), static_cast<int>(samples[0].y),
                       static_cast<int>(samples[0].x), static_cast<int>(samples[0].y)};
        for (const auto& q : samples) {
            const int x = static_cast<int>(std::lround(q.x));
            const int y = static_cast<int>(std::lround(q.y));
            r.xmin = std::min(r.xmin, x);
            r.ymin = std::min(r.ymin, y);
            r.xmax = std::max(r.xmax, x);
            r.ymax = std::max(r.ymax, y);
        }
        return r;
    }
    default:
        return {s.a.x, s.a.y, s.a.x, s.a.y};
    }
}

raster::Rect PaintController::selectionBounds() const {
    raster::Rect r{0, 0, 0, 0};
    bool first = true;
    for (int idx : selected_) {
        if (idx < 0 || idx >= static_cast<int>(shapes_.size())) {
            continue;
        }
        const raster::Rect b = shapeBounds(shapes_[idx]);
        if (first) {
            r = b;
            first = false;
        } else {
            r.xmin = std::min(r.xmin, b.xmin);
            r.ymin = std::min(r.ymin, b.ymin);
            r.xmax = std::max(r.xmax, b.xmax);
            r.ymax = std::max(r.ymax, b.ymax);
        }
    }
    return r;
}

// 圆/弧无法表达椭圆：含圆/弧时只允许等比缩放（4 角柄）。
bool PaintController::selectionUniformOnly() const {
    for (int idx : selected_) {
        if (idx < 0 || idx >= static_cast<int>(shapes_.size())) {
            continue;
        }
        if (shapes_[idx].mode == Mode::Circle || shapes_[idx].mode == Mode::Arc) {
            return true;
        }
    }
    return false;
}

// 单选一条线段：端点编辑模式。
bool PaintController::selEndpointsMode() const {
    return selected_.size() == 1 && selected_[0] >= 0 &&
           selected_[0] < static_cast<int>(shapes_.size()) &&
           shapes_[selected_[0]].mode == Mode::Line;
}

PaintController::SelHandle PaintController::pickHandle(IPoint p) const {
    if (selected_.empty()) {
        return SelHandle::None;
    }
    constexpr float kHandleTol = 6.0f;
    auto near = [&](IPoint c) { return dist(p, c) <= kHandleTol; };
    if (selEndpointsMode()) {
        const Shape& s = shapes_[selected_[0]];
        if (near(s.a)) {
            return SelHandle::EndA;
        }
        if (near(s.b)) {
            return SelHandle::EndB;
        }
        return SelHandle::None;
    }
    const raster::Rect b = selectionBounds();
    if (b.xmax < b.xmin || b.ymax < b.ymin) {
        return SelHandle::None;
    }
    if (near({b.xmin, b.ymin})) {
        return SelHandle::NW;
    }
    if (near({b.xmax, b.ymin})) {
        return SelHandle::NE;
    }
    if (near({b.xmax, b.ymax})) {
        return SelHandle::SE;
    }
    if (near({b.xmin, b.ymax})) {
        return SelHandle::SW;
    }
    if (!selectionUniformOnly()) { // 等比模式只给角柄
        const int xm = (b.xmin + b.xmax) / 2;
        const int ym = (b.ymin + b.ymax) / 2;
        if (near({xm, b.ymin})) {
            return SelHandle::N;
        }
        if (near({b.xmax, ym})) {
            return SelHandle::E;
        }
        if (near({xm, b.ymax})) {
            return SelHandle::S;
        }
        if (near({b.xmin, ym})) {
            return SelHandle::W;
        }
    }
    return SelHandle::None;
}

// 实心方柄（拖拽中高亮为白色）。
void PaintController::drawHandleBox(FrameBuffer& fb, IPoint c, bool hot) const {
    const Color fill = hot ? Color::White() : Color(255, 150, 40);
    for (int y = -3; y <= 3; ++y) {
        for (int x = -3; x <= 3; ++x) {
            const bool border = x == -3 || x == 3 || y == -3 || y == 3;
            fb.putPixel(c.x + x, c.y + y, border ? Color::Black() : fill);
        }
    }
}

void PaintController::drawSelection(FrameBuffer& fb) const {
    // 1) 高亮重描：只画橙色加粗轮廓，不重填。
    const Color hi(255, 150, 40);
    const raster::LineStyle hiStyle{0xFFFFu, 3};
    for (int idx : selected_) {
        if (idx >= 0 && idx < static_cast<int>(shapes_.size())) {
            drawOutline(fb, shapes_[idx], hi, hiStyle);
        }
    }
    // 2) 控制柄与外接矩形
    if (selEndpointsMode()) {
        const Shape& s = shapes_[selected_[0]];
        drawHandleBox(fb, s.a, handleDrag_ && activeHandle_ == SelHandle::EndA);
        drawHandleBox(fb, s.b, handleDrag_ && activeHandle_ == SelHandle::EndB);
        return;
    }
    const raster::Rect b = selectionBounds();
    if (b.xmax <= b.xmin || b.ymax <= b.ymin) {
        return;
    }
    drawRectOutline(fb, b, Color(255, 150, 40), {0xF0F0u, 1});
    auto hot = [&](SelHandle h) { return handleDrag_ && activeHandle_ == h; };
    drawHandleBox(fb, {b.xmin, b.ymin}, hot(SelHandle::NW));
    drawHandleBox(fb, {b.xmax, b.ymin}, hot(SelHandle::NE));
    drawHandleBox(fb, {b.xmax, b.ymax}, hot(SelHandle::SE));
    drawHandleBox(fb, {b.xmin, b.ymax}, hot(SelHandle::SW));
    if (!selectionUniformOnly()) { // 等比模式只给角柄
        const int xm = (b.xmin + b.xmax) / 2;
        const int ym = (b.ymin + b.ymax) / 2;
        drawHandleBox(fb, {xm, b.ymin}, hot(SelHandle::N));
        drawHandleBox(fb, {b.xmax, ym}, hot(SelHandle::E));
        drawHandleBox(fb, {xm, b.ymax}, hot(SelHandle::S));
        drawHandleBox(fb, {b.xmin, ym}, hot(SelHandle::W));
    }
}

// 柄拖拽：端点柄改线段参数，bbox 柄以对角为锚缩放；从拖拽起点快照重算。
void PaintController::applyHandleDrag(IPoint pos) {
    ensureGestureUndo();
    if (activeHandle_ == SelHandle::EndA || activeHandle_ == SelHandle::EndB) {
        if (handleShapeIdx_ >= 0 && handleShapeIdx_ < static_cast<int>(shapes_.size())) {
            Shape& s = shapes_[handleShapeIdx_];
            const bool endA = activeHandle_ == SelHandle::EndA;
            const IPoint other = endA ? s.b : s.a;
            IPoint pt = pos;
            if (ctrlHeld_) { // Ctrl：线段长度按 kStepPx 步进（方向不变）
                const FPoint u = unitVec({static_cast<float>(pt.x - other.x),
                                          static_cast<float>(pt.y - other.y)});
                const int q = quantStepLen(dist(pt, other));
                pt = {other.x + static_cast<int>(std::lround(u.x * q)),
                      other.y + static_cast<int>(std::lround(u.y * q))};
            }
            if (shiftHeld_) { // Shift：端点方向吸附 45°（长度不变）
                const FPoint u = unitVec({static_cast<float>(pt.x - other.x),
                                          static_cast<float>(pt.y - other.y)});
                const FPoint sn = snapDir45(u);
                const float len = dist(pt, other);
                pt = {other.x + static_cast<int>(std::lround(sn.x * len)),
                      other.y + static_cast<int>(std::lround(sn.y * len))};
            }
            if (endA) {
                s.a = pt;
            } else {
                s.b = pt;
            }
            markDirty();
        }
        return;
    }
    const raster::Rect& b0 = selBounds0_;
    const int w0 = std::max(1, b0.xmax - b0.xmin);
    const int h0 = std::max(1, b0.ymax - b0.ymin);
    const bool east = activeHandle_ == SelHandle::E || activeHandle_ == SelHandle::NE ||
                      activeHandle_ == SelHandle::SE;
    const bool west = activeHandle_ == SelHandle::W || activeHandle_ == SelHandle::NW ||
                      activeHandle_ == SelHandle::SW;
    const bool north = activeHandle_ == SelHandle::N || activeHandle_ == SelHandle::NW ||
                       activeHandle_ == SelHandle::NE;
    const bool south = activeHandle_ == SelHandle::S || activeHandle_ == SelHandle::SW ||
                       activeHandle_ == SelHandle::SE;
    // 锚点 = 对角（角柄）/对边（边柄）。
    const bool centerAnchor = ctrlHeld_;
    const float anchorX =
        centerAnchor ? (b0.xmin + b0.xmax) * 0.5f
                     : (east ? static_cast<float>(b0.xmin)
                             : (west ? static_cast<float>(b0.xmax)
                                     : (b0.xmin + b0.xmax) * 0.5f));
    const float anchorY =
        centerAnchor ? (b0.ymin + b0.ymax) * 0.5f
                     : (south ? static_cast<float>(b0.ymin)
                              : (north ? static_cast<float>(b0.ymax)
                                       : (b0.ymin + b0.ymax) * 0.5f));
    auto clampAxis = [](float s) { return s < 0.02f ? 0.02f : s; }; // 防翻转/防退化
    // 中心锚点时参考长度是半宽/半高，缩放比才与鼠标位移一致。
    const float wRef = centerAnchor ? w0 * 0.5f : w0;
    const float hRef = centerAnchor ? h0 * 0.5f : h0;
    float sx = 1.0f;
    float sy = 1.0f;
    if (east) {
        sx = clampAxis((static_cast<float>(pos.x) - anchorX) / wRef);
    } else if (west) {
        sx = clampAxis((anchorX - static_cast<float>(pos.x)) / wRef);
    }
    if (south) {
        sy = clampAxis((static_cast<float>(pos.y) - anchorY) / hRef);
    } else if (north) {
        sy = clampAxis((anchorY - static_cast<float>(pos.y)) / hRef);
    }
    float scaleF = 1.0f; // applyToShape 里圆/弧的半径等比因子（仅等比模式用到）
    if (selectionUniformOnly() || shiftHeld_) { // Shift：等比缩放
        const float s = std::max(sx, sy);
        sx = s;
        sy = s;
        scaleF = s;
    }
    const geom::Mat3 m = geom::scaleAbout(anchorX, anchorY, sx, sy);
    for (std::size_t k = 0; k < selected_.size() && k < selSnapshot_.size(); ++k) {
        const int idx = selected_[k];
        if (idx < 0 || idx >= static_cast<int>(shapes_.size())) {
            continue;
        }
        shapes_[idx] = selSnapshot_[k];
        applyToShape(shapes_[idx], m, 0.0f, scaleF);
    }
    transformAttachedSeedsForSelection(m, true);
    markDirty();
}

void PaintController::cancelInProgress() {
    dragging_ = false;
    arcAdjust_ = false;
    clipRectEdit_ = false;
    clipPolyEdit_ = false;
    xformDrag_ = false;
    rotDrag_ = false;
    rotArmed_ = false;
    rotDeg_ = 0.0f;
    bandSelect_ = false;
    refPick_ = false;
    dockStage_ = 0;
    dockAShapeIdx_ = -1;
    curveDrag_ = false;
    dragShapeIdx_ = -1;
    dragCtrlIdx_ = -1;
    handleDrag_ = false;
    activeHandle_ = SelHandle::None;
    handleShapeIdx_ = -1;
    selSnapshot_.clear();
    attachedSeedSnapshot_.clear();
    selected_.clear();
    pendingPolygon_.clear();
    pendingClipWin_.clear();
    pendingCurve_.clear();
}

void PaintController::onMouseButton(int button, int action, int mods) {
    titleDirty_ = true;
    const bool press = (action == GLFW_PRESS);
    shiftHeld_ = (mods & GLFW_MOD_SHIFT) != 0;
    ctrlHeld_ = (mods & GLFW_MOD_CONTROL) != 0;

    // 任务栏优先：点在栏上不落到画布。
    if (button == 0 && press && toolbarPress(curr_)) {
        return;
    }

    // 裁剪模式：正在连点定义凸多边形窗口
    if (mode_ == Mode::Clip && clipPolyEdit_) {
        if (button == 0 && press) {
            pendingClipWin_.push_back(curr_);
        } else if (button == 1 && press) { // 右键闭合
            if (pendingClipWin_.size() >= 3) {
                pushUndo();
                clipWin_.kind = ClipWindow::Kind::ConvexPoly;
                clipWin_.poly.clear();
                for (const auto& p : pendingClipWin_) {
                    clipWin_.poly.push_back({static_cast<float>(p.x), static_cast<float>(p.y)});
                }
                clipWin_.valid = true;
                clipOn_ = true;
                markDirty();
            }
            pendingClipWin_.clear();
            clipPolyEdit_ = false;
        }
        return;
    }

    if (button == 1) { // 右键：闭合输入 / 拖拽旋转 / 取消当前操作
        if (rotDrag_) {
            if (!press) { // 释放：结束旋转，图形保留当前姿态
                rotDrag_ = false;
                rotArmed_ = false;
                rotDeg_ = 0.0f;
                markDirty();
            }
            return;
        }
        if (press) {
            if (mode_ == Mode::Polygon && !pendingPolygon_.empty()) {
                closePolygon();
            } else if (mode_ == Mode::Curve && !pendingCurve_.empty()) {
                commitCurve();
            } else if (mode_ == Mode::Transform && !selected_.empty() && !refPick_ &&
                       dockStage_ == 0 && curr_.y >= toolbarHeight()) {
                // 有选中且不在对接/拾取参考点流程中：右键按下进入旋转，拖动即转动。
                beginRotateDrag();
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
                s.b = lineDragEnd();
                s.style = currentStyle();
                addShape(std::move(s));
                markDirty();
            } else if (dist(press_, curr_) >= 1.0f) {
                Shape s;
                s.mode = Mode::Circle;
                s.a = press_;
                s.radius = dragRadius();
                s.style = currentStyle();
                addShape(std::move(s));
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
                    arcRadius_ = dragRadius();
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
            addShape(std::move(s));
            markDirty();
            arcAdjust_ = false;
        }
        break;

    case Mode::Polygon:
        if (press) {
            pendingPolygon_.push_back(curr_);
        }
        break;

    case Mode::SeedFill:
        if (press) {
            Shape s;
            s.mode = Mode::SeedFill;
            s.a = curr_;
            // 点在闭合图形内则绑定，变换时跟随；否则自由种子。
            s.parentId = findSeedParent(curr_);
            s.color = currentFillColor();
            addShape(std::move(s));
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
            addShape(std::move(s));
            markDirty();
        }
        break;

    case Mode::Transform:
        if (press) {
            // 对接中：本次点击选对接点，不改选中。
            if (dockStage_ > 0) {
                FPoint pt{}, tan{};
                const int idx = pickOutlineShape(curr_, pt, tan, dockAShapeIdx_);
                if (idx >= 0 && dockStage_ == 1) {
                    dockA_ = pt;
                    dockATangent_ = tan;
                    dockAShapeIdx_ = idx;
                    dockStage_ = 2;
                } else if (idx >= 0 && dockStage_ == 2) {
                    // 拼接：绕 B 的对触点转 Δθ（±90° 内取最短），再平移到 A 的对触点。
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
                    pushUndo();
                    applyToShape(shapes_[idx], m, diff, 1.0f);
                    transformSeedsOfParent(shapes_[idx].id, m, false);
                    dockStage_ = 0;
                    dockAShapeIdx_ = -1;
                    markDirty();
                }
                break;
            }
            // 指定参考点：只设参考点，不动选中。
            if (refPick_) {
                refPoint_ = curr_;
                refPick_ = false;
                break;
            }
            // 控制柄优先于图形命中。
            if (!selected_.empty()) {
                // 先剔除失效下标，保证 selSnapshot_ 与 selected_ 顺序对齐
                selected_.erase(
                    std::remove_if(selected_.begin(), selected_.end(),
                                   [this](int idx) {
                                       return idx < 0 || idx >= static_cast<int>(shapes_.size());
                                   }),
                    selected_.end());
                const SelHandle h = pickHandle(curr_);
                if (h != SelHandle::None) {
                    beginGesture();
                    handleDrag_ = true;
                    activeHandle_ = h;
                    handleShapeIdx_ =
                        (h == SelHandle::EndA || h == SelHandle::EndB) && selEndpointsMode()
                            ? selected_[0]
                            : -1;
                    if (handleShapeIdx_ < 0) { // bbox 柄：记录初始外接矩形 + 选中图形快照
                        selBounds0_ = selectionBounds();
                        snapshotSelection();
                        snapshotAttachedSeeds();
                    }
                    break;
                }
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
                } else if (!sel) { // 普通点击：只选它一个
                    selected_.clear();
                    selected_.push_back(hit);
                    sel = true;
                }
                if (sel) { // 在选中集合里才可拖拽平移
                    beginXformDrag(curr_);
                }
            } else if (!selected_.empty()) {
                // 组合体：外接矩形内部空白按下也进入整体拖拽。
                const raster::Rect b = selectionBounds();
                const bool inBounds = curr_.x >= b.xmin && curr_.x <= b.xmax &&
                                      curr_.y >= b.ymin && curr_.y <= b.ymax;
                if (inBounds) {
                    beginXformDrag(curr_);
                } else { // 点包围盒外：进入框选拖拽，松开时结算（原地微点=清除选中）
                    bandSelect_ = true;
                    bandStart_ = curr_;
                }
            } else { // 点空白：进入框选拖拽，松开时结算（原地微点=清除选中）
                bandSelect_ = true;
                bandStart_ = curr_;
            }
        } else {
            handleDrag_ = false;
            activeHandle_ = SelHandle::None;
            handleShapeIdx_ = -1;
            selSnapshot_.clear();
            attachedSeedSnapshot_.clear();
            xformDrag_ = false;
            if (bandSelect_) {
                bandSelect_ = false;
                const raster::Rect band = rectFrom(bandStart_, curr_);
                if (band.xmax - band.xmin >= 4 || band.ymax - band.ymin >= 4) {
                    // 框选：相交即可选图形；Shift 叠加，否则替换。
                    if (!shiftHeld_) {
                        selected_.clear();
                    }
                    for (int i = 0; i < static_cast<int>(shapes_.size()); ++i) {
                        if (!shapeSelectable(shapes_[i]) ||
                            !shapeIntersectsRect(shapes_[i], band)) {
                            continue;
                        }
                        if (std::find(selected_.begin(), selected_.end(), i) == selected_.end()) {
                            selected_.push_back(i);
                        }
                    }
                } else if (!selected_.empty()) { // 原地微点：清除选中
                    selected_.clear();
                }
            }
        }
        break;

    case Mode::Curve:
        if (press) {
            // 空闲时优先抓取曲线控制点。
            if (pendingCurve_.empty()) {
                const auto grab = pickCurveCtrl(curr_);
                if (grab.first >= 0) {
                    beginGesture();
                    curveDrag_ = true;
                    dragShapeIdx_ = grab.first;
                    dragCtrlIdx_ = grab.second;
                    break;
                }
            }
            if (!curveDrag_) {
                pendingCurve_.push_back(curr_);
            }
        } else if (curveDrag_) {
            curveDrag_ = false;
            dragShapeIdx_ = -1;
            dragCtrlIdx_ = -1;
        }
        break;
    }
}

void PaintController::onMouseMove(IPoint pos, int mods) {
    shiftHeld_ = (mods & GLFW_MOD_SHIFT) != 0;
    ctrlHeld_ = (mods & GLFW_MOD_CONTROL) != 0;
    // 控制柄拖拽：端点编辑 / bbox 缩放。
    if (handleDrag_ && mode_ == Mode::Transform) {
        applyHandleDrag(pos);
    }
    // 右键拖拽旋转：绕参考点任意角度。
    if (rotDrag_ && mode_ == Mode::Transform) {
        applyRotateDrag(pos);
    }
    // 拖拽平移：从快照整体平移（零累积），Shift 吸附 45°。
    if (xformDrag_ && mode_ == Mode::Transform && !selected_.empty()) {
        ensureGestureUndo();
        FPoint off{static_cast<float>(pos.x - lastDrag_.x),
                   static_cast<float>(pos.y - lastDrag_.y)};
        if (shiftHeld_) {
            off = snapDir45(off);
        } else if (ctrlHeld_) {
            off.x = std::round(off.x / 10.0f) * 10.0f;
            off.y = std::round(off.y / 10.0f) * 10.0f;
        }
        const geom::Mat3 m = geom::translate(off.x, off.y);
        for (std::size_t k = 0; k < selected_.size() && k < selSnapshot_.size(); ++k) {
            const int idx = selected_[k];
            if (idx < 0 || idx >= static_cast<int>(shapes_.size())) {
                continue;
            }
            shapes_[idx] = selSnapshot_[k];
            applyToShape(shapes_[idx], m, 0.0f, 1.0f);
        }
        transformAttachedSeedsForSelection(m, true);
        markDirty();
    }
    // 曲线控制点拖拽：改参数，提交层重算。
    if (curveDrag_ && mode_ == Mode::Curve && dragShapeIdx_ >= 0 &&
        dragShapeIdx_ < static_cast<int>(shapes_.size())) {
        ensureGestureUndo();
        auto& curve = shapes_[static_cast<std::size_t>(dragShapeIdx_)].curve;
        if (dragCtrlIdx_ >= 0 && dragCtrlIdx_ < static_cast<int>(curve.size())) {
            curve[static_cast<std::size_t>(dragCtrlIdx_)] = toF(pos);
            markDirty();
        }
    }
    curr_ = pos;
}

const std::string& PaintController::title() const {
    if (!titleDirty_) {
        return titleCache_;
    }
    titleCache_.clear();
    std::string& t = titleCache_;
    t +=
        "绘图引擎（实验一/二/三/四） | 1直线 2圆 3圆弧 4多边形 5种子填充 6裁剪 7变换 8曲线 | "
        "S线型 W线宽 X填充色 E清屏 Ctrl+Z撤销 右键取消 | ";
    t += kModeNames[static_cast<int>(mode_)];
    switch (mode_) {
    case Mode::Line:
    case Mode::Circle:
    case Mode::Arc:
        t += " ";
        t += kStyleNames[styleIdx_];
        t += " 宽";
        t += std::to_string(kWidths[widthIdx_]);
        if (mode_ == Mode::Line) {
            t += " | Shift=45°吸附";
        } else {
            t += " | Ctrl=半径步进20px";
        }
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
        t += " | 左键选(Shift加选) 空白拖拽框选 平移(Shift=45° Ctrl=10px) 右键拖拽=任意角度旋转(Shift=15°) 柄缩放(Shift=等比 Ctrl=中心) 线段端点 Ctrl=步进 方向键微移 Del删除 +−缩放 R/T旋转 G参考点 J对接";
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
    case Mode::Curve:
        t += " ";
        t += curveKindName(curveKind_);
        t += "（B 切换）| 左键连点控制点，右键结束；点住已提交曲线的控制点拖拽调整";
        if (curveKind_ == CurveKind::BSplineClamped) {
            // 4 点 + 3 次 clamped B 样条与三次 Bezier 基函数等价，提示用户加第 5 点。
            int ctrlCount = static_cast<int>(pendingCurve_.size());
            if (ctrlCount == 0) {
                for (auto it = shapes_.rbegin(); it != shapes_.rend(); ++it) {
                    if (it->mode == Mode::Curve) {
                        ctrlCount = static_cast<int>(it->curve.size());
                        break;
                    }
                }
            }
            if (ctrlCount == 4) {
                t += " | 4 点 B 样条与三次 Bezier 等价，加第 5 点才不同";
            }
        } else if (curveKind_ == CurveKind::BSplineUniform) {
            t += " | 均匀节点：曲线不过首末控制点";
        }
        if (!pendingCurve_.empty()) {
            t += " | 当前 ";
            t += std::to_string(pendingCurve_.size());
            t += " 个控制点";
        }
        break;
    }
    if (clipOn_ && clipWin_.valid) {
        t += " | 裁剪:开（V 关，窗口外内容隐藏）";
    }
    titleDirty_ = false;
    return titleCache_;
}

} // namespace cg
