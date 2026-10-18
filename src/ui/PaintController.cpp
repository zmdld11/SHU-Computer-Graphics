// PaintController.cpp
// 实现交互控制器：把鼠标键盘事件翻译成绘图/填充/裁剪操作，两层渲染（提交层缓存 + 每帧预览），最终像素交给 FrameBuffer。
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

// ---- 变换辅助（实验三） ----
constexpr float kPiF = 3.14159265358979f;
constexpr float kHitTol = 5.0f;   // 选中命中容差（像素）
constexpr float kSnapTol = 12.0f; // 对接点吸附容差（像素）

// ---- 修饰键约束（Shift 45° 吸附 / Ctrl 定步长） ----
constexpr float kStepPx = 20.0f; // Ctrl 步进长度（像素，README 已注明可调）

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

// 直线拖拽终点：Shift 按住时方向吸附 45° 整数倍（长度取投影分量）
IPoint PaintController::lineDragEnd() const {
    if (!shiftHeld_) {
        return curr_;
    }
    const FPoint d = snapDir45({static_cast<float>(curr_.x - press_.x),
                                static_cast<float>(curr_.y - press_.y)});
    return {press_.x + static_cast<int>(std::lround(d.x)),
            press_.y + static_cast<int>(std::lround(d.y))};
}

// 圆/弧拖拽半径：Ctrl 按住时按 kStepPx（默认 20px）步进取整
int PaintController::dragRadius() const {
    const float r = dist(press_, curr_);
    return ctrlHeld_ ? quantStepLen(r) : static_cast<int>(std::lround(r));
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
    case Mode::Curve: {
        // 曲线 = 采样折线逐段落像素，沿用线型/线宽属性
        const std::vector<FPoint> samples = curveSamples(s);
        for (std::size_t i = 0; i + 1 < samples.size(); ++i) {
            raster::drawLine(fb, roundPt(samples[i]), roundPt(samples[i + 1]), s.color, s.style);
        }
        break;
    }
    case Mode::Transform:
        break; // 变换模式没有"图形"本体（选中/参考点标记画在预览层）
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
    // 裁剪窗口边框不画进提交层：它在预览层绘制（常驻裁剪时所有模式可见，且不被掩码覆盖）
}

// 常驻裁剪的显示层缓存：窗口内照抄提交层，窗口外保持背景色（黑）。
// 只在提交层重绘/裁剪窗变化/画布尺寸变化时重建，之后每帧一条 memcpy 上屏。
// 不改提交层——关掉开关，原图立即完整恢复。
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
    // 进行中的拖拽预览（黄色/绿色为待定内容，不进入提交层）
    if (dragging_) {
        const int r = dragRadius(); // 圆/弧半径（Ctrl 步进）
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
            break; // 曲线不用拖拽预览（输入预览/控制点手柄在本函数后面单独画）
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

    // 曲线模式：已提交曲线的控制多边形与手柄（控制点可抓取）
    if (mode_ == Mode::Curve) {
        drawCurveHandles(fb);
    }

    // 曲线输入：控制多边形橡皮筋 + 以光标作临时末控制点的曲线实时预览
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
        const std::vector<FPoint> samples =
            curveBSpline_ ? curves::sampleBSpline(pts, 3, 64) : curves::sampleBezier(pts, 64);
        for (std::size_t i = 0; i + 1 < samples.size(); ++i) {
            raster::drawLine(fb, roundPt(samples[i]), roundPt(samples[i + 1]), Color::Yellow(),
                             currentStyle());
        }
    }

    // 裁剪窗口边框（预览层）：常驻裁剪开启时所有模式可见（青色虚线）；
    // 未开启时仅在裁剪模式显示（白色实线，作为算法演示的背景窗口）
    if (clipWin_.valid) {
        if (clipOn_) {
            drawClipWindow(fb, Color::Cyan(), true);
        } else if (mode_ == Mode::Clip) {
            drawClipWindow(fb, Color::White(), false);
        }
    }

    // 变换模式标记：选中=橙色高亮+虚线外接矩形+控制柄；青=当前参考点，黄=等待指定的参考点，橙=A 的对接点
    if (mode_ == Mode::Transform) {
        if (bandSelect_) { // 框选橡皮筋（品红虚线）
            const raster::Rect band = rectFrom(bandStart_, curr_);
            raster::drawLine(fb, {band.xmin, band.ymin}, {band.xmax, band.ymin}, Color::Magenta(),
                             {0xF0F0u, 1});
            raster::drawLine(fb, {band.xmax, band.ymin}, {band.xmax, band.ymax}, Color::Magenta(),
                             {0xF0F0u, 1});
            raster::drawLine(fb, {band.xmax, band.ymax}, {band.xmin, band.ymax}, Color::Magenta(),
                             {0xF0F0u, 1});
            raster::drawLine(fb, {band.xmin, band.ymax}, {band.xmin, band.ymin}, Color::Magenta(),
                             {0xF0F0u, 1});
        }
        drawSelection(fb); // 画板式选择：高亮重描 + 外接矩形 + 控制柄（替代旧的重心十字）
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
        clipCacheDirty_ = true; // 提交层变了，裁剪掩码缓存失效
    }
    if (clipOn_ && clipWin_.valid) {
        copyCommittedClipped(fb); // 常驻裁剪：缓存命中时只做一次整块拷贝
    } else {
        fb.copyFrom(*committed_); // 提交层 → 显示层
    }
    drawPreview(fb);
    drawToolbar(fb);          // 任务栏画在最上层（UI 元素，不属于画布内容）
}

void PaintController::onKey(int key, int action, int mods) {
    shiftHeld_ = (mods & GLFW_MOD_SHIFT) != 0; // 任意事件都刷新修饰键状态（拖拽中按下也生效）
    ctrlHeld_ = (mods & GLFW_MOD_CONTROL) != 0;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) {
        return;
    }
    // 键盘只是"快捷键"：与任务栏按钮共用同一操作入口
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

// 键盘与任务栏按钮的统一操作入口：所有交互动作的真正实现
void PaintController::handleAction(Act a) {
    switch (a) {
    case Act::ModeLine:
        mode_ = Mode::Line;
        cancelInProgress();
        markDirty();
        break;
    case Act::ModeCircle:
        mode_ = Mode::Circle;
        cancelInProgress();
        markDirty();
        break;
    case Act::ModeArc:
        mode_ = Mode::Arc;
        cancelInProgress();
        markDirty();
        break;
    case Act::ModePolygon:
        mode_ = Mode::Polygon;
        cancelInProgress();
        markDirty();
        break;
    case Act::ModeSeed:
        mode_ = Mode::SeedFill;
        cancelInProgress();
        markDirty();
        break;
    case Act::ModeClip:
        mode_ = Mode::Clip;
        cancelInProgress();
        markDirty();
        break;
    case Act::ModeXform:
        mode_ = Mode::Transform;
        cancelInProgress();
        markDirty();
        break;
    case Act::ModeCurve:
        mode_ = Mode::Curve;
        cancelInProgress();
        markDirty();
        break;
    case Act::CurveType:
        if (mode_ == Mode::Curve) {
            curveBSpline_ = !curveBSpline_; // 切换"新曲线"的类型，已提交曲线不受影响
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
    case Act::Clear:
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
            clipOn_ = !clipOn_; // 常驻裁剪开关：窗口外内容隐藏/恢复（不动提交层）
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
            // 删除父图形时，绑定在它上面的种子填充一起删掉，避免留下孤立色块
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

// 任务栏总高随字体度量变化：上下 2px 边距 + 两行按钮（行高 = 字高 + 8）+ 行间 3px
int PaintController::toolbarHeight() const {
    const int rowH = text::height() + 8;
    return 2 + rowH + 3 + rowH + 2;
}

void PaintController::buildToolbar(std::vector<ToolButton>& btns) const {
    btns.clear();
    const int rowH = text::height() + 8;
    const int y1 = 2;
    const int y2 = 2 + rowH + 3;
    auto add = [&](Act act, const char* label, int x, int y, bool active) {
        ToolButton b;
        b.act = act;
        b.label = label;
        b.w = text::textWidth(label) + 12;
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

    // 第二行：属性 + 随模式变化的上下文按钮
    x = add(Act::Style, "S 线型", 2, y2, false);
    x = add(Act::Width, "W 线宽", x, y2, false);
    x = add(Act::Color, "X 颜色", x, y2, false);
    if (mode_ == Mode::Polygon) {
        x = add(Act::PolyFill, polygonFilled_ ? "F 填充:开" : "F 填充:关", x + 8, y2, true);
        x = add(Act::PolyClose, "右键闭合", x, y2, false);
    } else if (mode_ == Mode::Clip) {
        x = add(Act::ClipRect, "K 矩形窗", x + 8, y2, false);
        x = add(Act::ClipPoly, "P 多边形窗", x, y2, false);
        x = add(Act::ClipAlgo, "M 算法", x, y2, false);
    } else if (mode_ == Mode::Curve) {
        x = add(Act::CurveType, curveBSpline_ ? "B 样条:开" : "B 样条:关", x + 8, y2,
                curveBSpline_);
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
    // 常驻裁剪开关：定义过裁剪窗口后所有模式都显示（V 键同效）
    if (clipWin_.valid) {
        add(Act::ClipToggle, clipOn_ ? "V 裁剪:开" : "V 裁剪:关", x + 8, y2, clipOn_);
    }
}

void PaintController::drawToolbar(FrameBuffer& fb) const {
    const Color kBg(34, 34, 40);
    const Color kFace(58, 58, 70);
    const Color kActive(70, 110, 180);
    const Color kBorder(120, 120, 130);
    const int tbH = toolbarHeight();
    fb.fillRect(0, 0, fb.width() - 1, tbH - 1, kBg);
    std::vector<ToolButton> btns;
    buildToolbar(btns);
    for (const auto& b : btns) {
        fb.fillRect(b.x, b.y, b.x + b.w - 1, b.y + b.h - 1, b.active ? kActive : kFace);
        for (int x = b.x; x < b.x + b.w; ++x) { // 1px 边框
            fb.putPixel(x, b.y, kBorder);
            fb.putPixel(x, b.y + b.h - 1, kBorder);
        }
        for (int y = b.y; y < b.y + b.h; ++y) {
            fb.putPixel(b.x, y, kBorder);
            fb.putPixel(b.x + b.w - 1, y, kBorder);
        }
        const int tx = b.x + (b.w - text::textWidth(b.label.c_str())) / 2 + 1;
        const int ty = b.y + (b.h - text::height()) / 2;
        text::drawText(fb, tx, ty, b.label.c_str(), Color(230, 230, 235));
    }
}

bool PaintController::toolbarPress(IPoint p) {
    const int tbH = toolbarHeight();
    // 负坐标是拖拽到窗口外时的越界鼠标事件，不属于任务栏：放行给画布逻辑，
    // 否则按住图形拖出窗口上缘再回来会"松不开"（按下/释放被吞）
    if (p.y < 0 || p.y >= tbH || p.x < 0) {
        return false;
    }
    std::vector<ToolButton> btns;
    buildToolbar(btns);
    for (const auto& b : btns) {
        if (p.x >= b.x && p.x < b.x + b.w && p.y >= b.y && p.y < b.y + b.h) {
            handleAction(b.act);
            markDirty(); // 高亮/标签可能变化（如当前模式按钮）
            return true;
        }
    }
    return true; // 点在栏上但没点中按钮：吞掉事件，不落到画布
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
        return; // 太小的窗口忽略（避免误拖成一个点）
    }
    clipWin_.kind = ClipWindow::Kind::Rect;
    clipWin_.rect = r;
    clipWin_.valid = true;
    clipOn_ = true; // 显式定义窗口 → 常驻裁剪自动开启
    markDirty();
}

// ================= 曲线（实验四） =================

void PaintController::commitCurve() {
    if (pendingCurve_.size() >= 2) {
        Shape s;
        s.mode = Mode::Curve;
        s.curve.reserve(pendingCurve_.size());
        for (const auto& p : pendingCurve_) {
            s.curve.push_back(toF(p));
        }
        s.bspline = curveBSpline_;
        s.curveDeg = 3;
        s.style = currentStyle();
        addShape(std::move(s));
        markDirty();
    }
    pendingCurve_.clear();
}

std::vector<FPoint> PaintController::curveSamples(const Shape& s) const {
    return s.bspline ? curves::sampleBSpline(s.curve, s.curveDeg, kCurveSegments)
                     : curves::sampleBezier(s.curve, kCurveSegments);
}

// 曲线模式下可抓取的控制点：从上层图形往下找，命中容差与选中一致
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

// 曲线模式的辅助显示（预览层）：已提交曲线的控制多边形（暗灰虚线）+ 控制点手柄，
// 正被拖拽的控制点用橙色高亮——控制点即"把手"，点住拖拽实时改形
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

// ================= 变换（实验三） =================

// 种子填充是像素操作的记录、裁剪线段是演示辅助，都不是参数图形，不参与选中/变换
bool PaintController::shapeSelectable(const Shape& s) const {
    return s.mode == Mode::Line || s.mode == Mode::Circle || s.mode == Mode::Arc ||
           s.mode == Mode::Polygon || s.mode == Mode::Curve;
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
    case Mode::Curve: {
        // 与多边形同口径：控制点平均
        if (s.curve.empty()) {
            return toF(s.a);
        }
        FPoint sum{0.0f, 0.0f};
        for (const auto& p : s.curve) {
            sum.x += p.x;
            sum.y += p.y;
        }
        const float n = static_cast<float>(s.curve.size());
        return {sum.x / n, sum.y / n};
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
    transformAttachedSeedsForSelection(m, false); // 键盘变换：种子跟随所属图形
    markDirty();
}

// 开始整体拖拽：记录按下点（绝对式偏移的锚）并快照当前选中集合。
// 控制柄拖拽和外接矩形拖拽都从这里进入，保证 selSnapshot_ 与 selected_ 同序。
void PaintController::beginXformDrag(IPoint anchor) {
    xformDrag_ = true;
    lastDrag_ = anchor;
    selSnapshot_.clear();
    for (int idx : selected_) {
        if (idx >= 0 && idx < static_cast<int>(shapes_.size())) {
            selSnapshot_.push_back(shapes_[idx]);
        }
    }
    snapshotAttachedSeeds();
}

// 图形入列表并分配稳定 id：种子填充用 parentId 指向所属圆/多边形，
// 下标会随删除/重排变化，id 不会。
void PaintController::addShape(Shape s) {
    s.id = nextShapeId_++;
    shapes_.push_back(std::move(s));
}

// 找包含点 p 的最上层闭合图形（圆内部 / 多边形内部）；没有则返回 0（自由种子）
std::uint64_t PaintController::findSeedParent(IPoint p) const {
    for (int i = static_cast<int>(shapes_.size()) - 1; i >= 0; --i) {
        const Shape& s = shapes_[i];
        if (s.mode == Mode::Circle && dist(p, s.a) <= static_cast<float>(s.radius)) {
            return s.id;
        }
        if (s.mode == Mode::Polygon && pointInPolygon(p, s.polygon)) {
            return s.id;
        }
    }
    return 0;
}

// 拖拽开始时记录选中图形所绑定的种子点；拖拽期间每帧从这份快照重算，避免整数漂移
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

// 变换某个图形绑定的所有种子。useSnapshot=true 时从拖拽起点的种子快照用同一矩阵重算；
// 键盘/对接等一次性变换直接作用于当前种子。
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

// 变换/取整后把种子钳回父图形内部。圆：向圆心投影到 radius-1；多边形：
// 先看原位，落在外面就在 1~8px 邻域找最近内点，最后退回顶点平均（若它在内部）。
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
    case Mode::Curve:
        // 控制点是浮点参数：直接保留浮点精度，不经过取整（Bezier 变换后仍是 Bezier）
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
        case Mode::Circle:
            // 圆环 5px 容差，或圆内任意位置：填充圆不必非得抓那一圈细线
            if (std::fabs(dist(p, s.a) - s.radius) <= kHitTol ||
                dist(p, s.a) <= static_cast<float>(s.radius)) {
                return i;
            }
            break;
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
            // 轮廓 5px 容差：未填充多边形屏幕上只有边，点在边附近也应该能抓住
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

// 框选：图形是否与矩形相交。线段/多边形边按 ~4px 步进采样，圆用解析法（圆心到矩形
// 最近点的距离 ≤ 半径，含"圆整个包住矩形"），弧在角度范围内采样。
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
                return true; // 矩形整个落在多边形内部
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
        case Mode::Curve: {
            // 轮廓 = 采样折线：取最近段的投影点，切向沿该段方向
            const std::vector<FPoint> samples = curveSamples(s);
            float best = 1e30f;
            FPoint bestPt{}, bestTan{};
            for (std::size_t k = 0; k + 1 < samples.size(); ++k) {
                const FPoint& q1 = samples[k];
                const FPoint& q2 = samples[k + 1];
                const float dd = distToSegment(p, roundPt(q1), roundPt(q2));
                if (dd < best) {
                    best = dd;
                    const float abx = q2.x - q1.x;
                    const float aby = q2.y - q1.y;
                    const float apx = static_cast<float>(p.x) - q1.x;
                    const float apy = static_cast<float>(p.y) - q1.y;
                    const float ab2 = abx * abx + aby * aby;
                    float t = ab2 > 0.0f ? (apx * abx + apy * aby) / ab2 : 0.0f;
                    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                    bestPt = {q1.x + abx * t, q1.y + aby * t};
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

// ================= 画板式选择编辑（实验三增强：外接矩形 + 控制柄 + 高亮） =================

// 单图形最小外接矩形（矩形字段为像素闭区间）
raster::Rect PaintController::shapeBounds(const Shape& s) const {
    switch (s.mode) {
    case Mode::Line:
        return rectFrom(s.a, s.b);
    case Mode::Circle:
        return {s.a.x - s.radius, s.a.y - s.radius, s.a.x + s.radius, s.a.y + s.radius};
    case Mode::Arc: {
        // 沿弧采样取范围（含两端点；步长 5° 足以覆盖轴向极值）
        const float span = normDeg(s.endDeg - s.startDeg);
        if (span <= 0.0f) { // 整圆
            return {s.a.x - s.radius, s.a.y - s.radius, s.a.x + s.radius, s.a.y + s.radius};
        }
        raster::Rect r{s.a.x, s.a.y, s.a.x, s.a.y};
        const int steps = std::max(2, static_cast<int>(span / 5.0f) + 1);
        for (int k = 0; k <= steps; ++k) {
            const float rad = normDeg(s.startDeg + span * k / steps) * kPiF / 180.0f;
            const int x = s.a.x + static_cast<int>(std::lround(s.radius * std::cos(rad)));
            const int y = s.a.y + static_cast<int>(std::lround(s.radius * std::sin(rad)));
            r.xmin = std::min(r.xmin, x);
            r.ymin = std::min(r.ymin, y);
            r.xmax = std::max(r.xmax, x);
            r.ymax = std::max(r.ymax, y);
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
        // 外接矩形取采样折线范围（比控制点范围更贴合曲线本体）
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

// 圆/弧没有椭圆参数化：非等比缩放表达不了 → 选中集合含圆/弧时只允许等比（4 角柄）
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

// 恰好单选一条线段：进入端点编辑模式（画两个端点柄，不画外接矩形）
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

// 实心方柄（7×7 橙色，拖拽中的柄高亮为白色；黑色描边保证在亮色图形上可见）
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
    // 1) 高亮重描：只重画橙色加粗轮廓，不重填填充区域——
    //    选中仍然一眼可辨，同时保留原填充色，也避免预览层每帧再做一次整块扫描填充
    const Color hi(255, 150, 40);
    const raster::LineStyle hiStyle{0xFFFFu, 3}; // 实线加粗，虚线图形也能看清选中态
    for (int idx : selected_) {
        if (idx < 0 || idx >= static_cast<int>(shapes_.size())) {
            continue;
        }
        const Shape& s = shapes_[idx];
        switch (s.mode) {
        case Mode::Line:
            raster::drawLine(fb, s.a, s.b, hi, hiStyle);
            break;
        case Mode::Circle:
            raster::drawCircle(fb, s.a, s.radius, hi, hiStyle);
            break;
        case Mode::Arc:
            raster::drawArc(fb, s.a, s.radius, s.startDeg, s.endDeg, hi, hiStyle);
            break;
        case Mode::Polygon: {
            const std::size_t n = s.polygon.size();
            for (std::size_t k = 0; n >= 2 && k < n; ++k) {
                raster::drawLine(fb, s.polygon[k], s.polygon[(k + 1) % n], hi, hiStyle);
            }
            break;
        }
        case Mode::Curve: {
            const std::vector<FPoint> samples = curveSamples(s);
            for (std::size_t k = 0; k + 1 < samples.size(); ++k) {
                raster::drawLine(fb, roundPt(samples[k]), roundPt(samples[k + 1]), hi, hiStyle);
            }
            break;
        }
        default:
            break;
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
    drawRectOutline(fb, b, Color(255, 150, 40), {0xF0F0u, 1}); // 橙色虚线外接矩形
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

// 柄拖拽中每帧调用：端点柄直接改线段参数；bbox 柄以对角（对边）为锚做缩放。
// 关键：从拖拽开始时的快照重算（而非在当前参数上叠加）——参数是整数，逐帧叠加会累积舍入漂移
void PaintController::applyHandleDrag(IPoint pos) {
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
    // 锚点 = 柄的对角（角柄）/对边（边柄）；中列/中行柄另一轴不缩放
    const float anchorX =
        east ? static_cast<float>(b0.xmin)
             : (west ? static_cast<float>(b0.xmax) : (b0.xmin + b0.xmax) * 0.5f);
    const float anchorY =
        south ? static_cast<float>(b0.ymin)
              : (north ? static_cast<float>(b0.ymax) : (b0.ymin + b0.ymax) * 0.5f);
    auto clampAxis = [](float s) { return s < 0.02f ? 0.02f : s; }; // 防翻转/防退化
    float sx = 1.0f;
    float sy = 1.0f;
    if (east) {
        sx = clampAxis((static_cast<float>(pos.x) - anchorX) / w0);
    } else if (west) {
        sx = clampAxis((anchorX - static_cast<float>(pos.x)) / w0);
    }
    if (south) {
        sy = clampAxis((static_cast<float>(pos.y) - anchorY) / h0);
    } else if (north) {
        sy = clampAxis((anchorY - static_cast<float>(pos.y)) / h0);
    }
    float scaleF = 1.0f; // applyToShape 里圆/弧的半径等比因子（仅等比模式用到）
    if (selectionUniformOnly()) {
        const float s = std::max(sx, sy); // 跟随拖拽幅度较大的轴，保持长宽比
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
    transformAttachedSeedsForSelection(m, true); // 柄缩放：种子从拖拽起点快照同步
    markDirty();
}

void PaintController::cancelInProgress() {
    dragging_ = false;
    arcAdjust_ = false;
    clipRectEdit_ = false;
    clipPolyEdit_ = false;
    xformDrag_ = false;
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
    const bool press = (action == GLFW_PRESS);
    shiftHeld_ = (mods & GLFW_MOD_SHIFT) != 0; // 鼠标事件刷新修饰键状态
    ctrlHeld_ = (mods & GLFW_MOD_CONTROL) != 0;

    // 任务栏优先：点中按钮（或点在栏上）不落到画布
    if (button == 0 && press && toolbarPress(curr_)) {
        return;
    }

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
                clipOn_ = true; // 显式定义窗口 → 常驻裁剪自动开启
                markDirty();
            }
            pendingClipWin_.clear();
            clipPolyEdit_ = false;
        }
        return;
    }

    if (button == 1) { // 右键：闭合多边形/曲线输入 / 取消当前操作
        if (press) {
            if (mode_ == Mode::Polygon && !pendingPolygon_.empty()) {
                closePolygon();
            } else if (mode_ == Mode::Curve && !pendingCurve_.empty()) {
                commitCurve();
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
                s.b = lineDragEnd(); // Shift：45° 方向吸附
                s.style = currentStyle();
                addShape(std::move(s));
                markDirty();
            } else if (dist(press_, curr_) >= 1.0f) {
                Shape s;
                s.mode = Mode::Circle;
                s.a = press_;
                s.radius = dragRadius(); // Ctrl：步进半径
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
                    arcRadius_ = dragRadius(); // Ctrl：步进半径
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
            pendingPolygon_.push_back(curr_); // 左键连点加顶点，右键闭合
        }
        break;

    case Mode::SeedFill:
        if (press) {
            Shape s;
            s.mode = Mode::SeedFill;
            s.a = curr_; // 种子点
            // 点在闭合图形内 → 绑定到它，图形变换时种子跟随；否则是自由种子填充
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
                    transformSeedsOfParent(shapes_[idx].id, m, false); // 对接时种子同步
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
            // 画板式编辑：控制柄优先于图形命中（同曲线模式控制点先例）
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
                    handleDrag_ = true;
                    activeHandle_ = h;
                    handleShapeIdx_ =
                        (h == SelHandle::EndA || h == SelHandle::EndB) && selEndpointsMode()
                            ? selected_[0]
                            : -1;
                    if (handleShapeIdx_ < 0) { // bbox 柄：记录初始外接矩形 + 选中图形快照
                        selBounds0_ = selectionBounds();
                        selSnapshot_.clear();
                        for (int idx : selected_) {
                            selSnapshot_.push_back(shapes_[idx]);
                        }
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
                    markDirty();
                } else if (!sel) { // 普通点击：只选它一个
                    selected_.clear();
                    selected_.push_back(hit);
                    sel = true;
                    markDirty();
                }
                if (sel) { // 在选中集合里才可拖拽平移
                    beginXformDrag(curr_);
                }
            } else if (!selected_.empty()) {
                // 多选组合体：外接矩形内部空白处按下也进入整体拖拽（PPT 组合体行为），
                // 不再把这次按下当成"重新框选"而清掉选择
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
            handleDrag_ = false; // 柄拖拽结束（快照使命完成）
            activeHandle_ = SelHandle::None;
            handleShapeIdx_ = -1;
            selSnapshot_.clear();
            attachedSeedSnapshot_.clear();
            xformDrag_ = false;
            if (bandSelect_) {
                bandSelect_ = false;
                const raster::Rect band = rectFrom(bandStart_, curr_);
                if (band.xmax - band.xmin >= 4 || band.ymax - band.ymin >= 4) {
                    // 框选：与矩形相交的可选图形全部选中；Shift=叠加，否则替换
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
                    markDirty();
                } else if (!selected_.empty()) { // 原地微点：清除选中
                    selected_.clear();
                    markDirty();
                }
            }
        }
        break;

    case Mode::Curve:
        if (press) {
            // 输入空闲时优先抓取已提交曲线的控制点（按住拖拽 = 实时调整形状）
            if (pendingCurve_.empty()) {
                const auto grab = pickCurveCtrl(curr_);
                if (grab.first >= 0) {
                    curveDrag_ = true;
                    dragShapeIdx_ = grab.first;
                    dragCtrlIdx_ = grab.second;
                    break;
                }
            }
            if (!curveDrag_) {
                pendingCurve_.push_back(curr_); // 左键连点加控制点，右键提交
                markDirty();
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
    shiftHeld_ = (mods & GLFW_MOD_SHIFT) != 0; // 移动事件实时反映修饰键（GLFW 光标回调无 mods，
    ctrlHeld_ = (mods & GLFW_MOD_CONTROL) != 0; // 由 Window 层查询键盘状态补齐）
    // 控制柄拖拽：端点编辑 / bbox 缩放（每帧从快照重算，与拖拽平移互斥）
    if (handleDrag_ && mode_ == Mode::Transform) {
        applyHandleDrag(pos);
    }
    // 拖拽平移：绝对式——从拖拽开始的快照整体平移到当前偏移（整数结果零累积）；
    // Shift 按住时偏移方向吸附 45° 整数倍（水平/垂直/对角线）
    if (xformDrag_ && mode_ == Mode::Transform && !selected_.empty()) {
        FPoint off{static_cast<float>(pos.x - lastDrag_.x),
                   static_cast<float>(pos.y - lastDrag_.y)};
        if (shiftHeld_) {
            off = snapDir45(off);
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
        transformAttachedSeedsForSelection(m, true); // 平移拖拽：种子从快照同步
        markDirty();
    }
    // 曲线控制点拖拽：改参数（控制点），像素由提交层重算——实时跟随
    if (curveDrag_ && mode_ == Mode::Curve && dragShapeIdx_ >= 0 &&
        dragShapeIdx_ < static_cast<int>(shapes_.size())) {
        auto& curve = shapes_[static_cast<std::size_t>(dragShapeIdx_)].curve;
        if (dragCtrlIdx_ >= 0 && dragCtrlIdx_ < static_cast<int>(curve.size())) {
            curve[static_cast<std::size_t>(dragCtrlIdx_)] = toF(pos);
            markDirty();
        }
    }
    curr_ = pos;
}

std::string PaintController::title() const {
    std::string t =
        "绘图引擎（实验一/二/三/四） | 1直线 2圆 3圆弧 4多边形 5种子填充 6裁剪 7变换 8曲线 | "
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
        t += " | 左键选(Shift加选) 空白拖拽框选 拖拽/方向键平移(Shift=45°) 柄缩放(线段拖端点 Ctrl=步进) Del删除 +−缩放 R/T旋转 G参考点 J对接";
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
        t += curveBSpline_ ? " B样条" : " Bezier";
        t += "（B 切换）| 左键连点控制点，右键结束；点住已提交曲线的控制点拖拽调整";
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
    return t;
}

} // namespace cg
