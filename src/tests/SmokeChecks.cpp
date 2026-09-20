// 算法自测与演示图导出。
// 断言按"实验"分节；每节先 fb.clear()，再对具体像素/返回值断言。
#include "tests/SmokeChecks.hpp"

#include "core/Color.hpp"
#include "core/FrameBuffer.hpp"
#include "raster/Clip.hpp"
#include "raster/Fill.hpp"
#include "raster/Rasterizer.hpp"
#include "ui/PaintController.hpp"

#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

namespace cg::tests {
namespace {

constexpr int kWidth = 800;
constexpr int kHeight = 600;

bool inRect(IPoint p, const raster::Rect& r, int tol) {
    return p.x >= r.xmin - tol && p.x <= r.xmax + tol && p.y >= r.ymin - tol &&
           p.y <= r.ymax + tol;
}

bool nearPoint(IPoint p, IPoint q, int tol) {
    return std::abs(p.x - q.x) <= tol && std::abs(p.y - q.y) <= tol;
}

} // namespace

int runAlgorithmSelfChecks() {
    using namespace cg;
    using namespace cg::raster;

    int fails = 0;
    auto expect = [&](bool ok, const char* name) {
        if (!ok) {
            std::cerr << "[smoke] FAIL: " << name << std::endl;
            ++fails;
        }
    };

    const Color W = Color::White();
    const Color FC(0, 200, 200); // 测试用填充色
    FrameBuffer fb(kWidth, kHeight);

    // ================= 实验一：图元生成 =================

    // ---- 直线：任意斜率 ----
    fb.clear(Color::Black());
    drawLine(fb, {0, 0}, {kWidth - 1, kHeight - 1}, W, {});
    expect(fb.pixel(0, 0) == W && fb.pixel(kWidth - 1, kHeight - 1) == W, "line: 对角线端点");

    fb.clear(Color::Black());
    drawLine(fb, {100, 0}, {100, 50}, W, {});
    expect(fb.pixel(100, 25) == W, "line: 垂直线");

    fb.clear(Color::Black());
    drawLine(fb, {400, 100}, {300, 400}, W, {});
    expect(fb.pixel(400, 100) == W && fb.pixel(300, 400) == W, "line: 陡负斜率端点");

    // 起终点对称性：A→B 与 B→A 像素集合应一致
    FrameBuffer f1(kWidth, kHeight);
    FrameBuffer f2(kWidth, kHeight);
    drawLine(f1, {10, 20}, {200, 90}, W, {});
    drawLine(f2, {200, 90}, {10, 20}, W, {});
    bool symmetric = true;
    for (int y = 0; y < kHeight && symmetric; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            if (f1.pixel(x, y) != f2.pixel(x, y)) {
                symmetric = false;
                break;
            }
        }
    }
    expect(symmetric, "line: 起终点对称");

    // ---- 圆：四轴点 + 45° 附近 ----
    fb.clear(Color::Black());
    drawCircle(fb, {200, 200}, 50, W, {});
    expect(fb.pixel(250, 200) == W && fb.pixel(150, 200) == W, "circle: 左右轴点");
    expect(fb.pixel(200, 150) == W && fb.pixel(200, 250) == W, "circle: 上下轴点");
    bool found45 = false;
    for (int x = 230; x <= 240 && !found45; ++x) {
        for (int y = 230; y <= 240; ++y) {
            if (fb.pixel(x, y) == W) {
                found45 = true;
                break;
            }
        }
    }
    expect(found45, "circle: 45°附近有像素");

    // ---- 圆弧：0°→90°（顺时针，右→下）----
    fb.clear(Color::Black());
    drawArc(fb, {400, 300}, 80, 0.0f, 90.0f, W, {});
    expect(fb.pixel(480, 300) == W, "arc: 含 0°");
    expect(fb.pixel(400, 380) == W, "arc: 含 90°");
    expect(fb.pixel(320, 300) == Color::Black(), "arc: 不含 180°");
    expect(fb.pixel(400, 220) == Color::Black(), "arc: 不含 270°");

    // ---- 线宽：宽 5 的水平线，中点上下各扩 2 像素 ----
    fb.clear(Color::Black());
    drawLine(fb, {50, 100}, {150, 100}, W, {0xFFFFu, 5});
    expect(fb.pixel(100, 98) == W && fb.pixel(100, 102) == W, "width: 线宽5扩2像素");

    // ---- 线型：点线 0x8888（bit3/7/11/15 落点）----
    fb.clear(Color::Black());
    drawLine(fb, {50, 120}, {150, 120}, W, {0x8888u, 1});
    expect(fb.pixel(50, 120) == Color::Black(), "style: 点线第0步不画");
    expect(fb.pixel(53, 120) == W, "style: 点线第3步画");

    // ---- 宽线 × 虚线不粘连：掩码按线宽放大，空档仍在 ----
    fb.clear(Color::Black());
    drawLine(fb, {50, 140}, {149, 140}, W, {0xF0F0u, 5});
    expect(fb.pixel(60, 140) == Color::Black(), "width+dash: 空档仍为空");
    expect(fb.pixel(75, 140) == W, "width+dash: 实段存在");

    // ================= 实验二：填充 =================

    // ---- 扫描线填充：凸多边形（正方形）----
    fb.clear(Color::Black());
    const std::vector<IPoint> square = {{200, 100}, {300, 100}, {300, 200}, {200, 200}};
    scanlineFillPolygon(fb, square, FC);
    expect(fb.pixel(250, 150) == FC, "fill: 正方形内部填充");
    expect(fb.pixel(150, 150) == Color::Black(), "fill: 正方形左侧外部不填");
    expect(fb.pixel(250, 250) == Color::Black(), "fill: 正方形下方外部不填");

    // ---- 扫描线填充：凹多边形（L 形），凹口处不能误填 ----
    fb.clear(Color::Black());
    const std::vector<IPoint> lshape = {{100, 300}, {200, 300}, {200, 350},
                                        {150, 350}, {150, 400}, {100, 400}};
    scanlineFillPolygon(fb, lshape, FC);
    expect(fb.pixel(180, 320) == FC, "fill: 凹多边形上部（宽处）填充");
    expect(fb.pixel(120, 380) == FC, "fill: 凹多边形左列（窄处）填充");
    expect(fb.pixel(180, 380) == Color::Black(), "fill: 凹多边形凹口内不填（非凸正确性）");

    // ---- 扫描线填充：顶点规则（顶点所在扫描线之上不应有填充）----
    fb.clear(Color::Black());
    const std::vector<IPoint> apex = {{400, 100}, {500, 100}, {450, 50}};
    scanlineFillPolygon(fb, apex, FC);
    expect(fb.pixel(450, 80) == FC, "fill: 三角形内部填充");
    bool rowAboveEmpty = true;
    for (int x = 400; x <= 500; ++x) {
        if (fb.pixel(x, 49) != Color::Black()) {
            rowAboveEmpty = false;
            break;
        }
    }
    expect(rowAboveEmpty, "fill: 顶点上方扫描线整行为空（顶点计数规则）");

    // ---- 扫描线种子填充：方框内区域 ----
    fb.clear(Color::Black());
    drawLine(fb, {500, 300}, {700, 300}, W, {});
    drawLine(fb, {700, 300}, {700, 450}, W, {});
    drawLine(fb, {700, 450}, {500, 450}, W, {});
    drawLine(fb, {500, 450}, {500, 300}, W, {});
    scanlineSeedFill(fb, {600, 380}, FC);
    expect(fb.pixel(600, 380) == FC, "seed: 种子处被填充");
    expect(fb.pixel(510, 310) == FC, "seed: 填充覆盖整个矩形区域");
    expect(fb.pixel(690, 440) == FC, "seed: 填充到区域角落");
    expect(fb.pixel(490, 380) == Color::Black(), "seed: 不越出边界（左外）");
    expect(fb.pixel(600, 290) == Color::Black(), "seed: 不越出边界（上外）");
    expect(fb.pixel(500, 380) == W, "seed: 边界像素保持原色");
    // 种子色已是填充色：应安全返回（不重填、不死循环）
    scanlineSeedFill(fb, {600, 380}, FC);
    expect(fb.pixel(490, 380) == Color::Black(), "seed: 重复调用无副作用");

    // ================= 实验二：裁剪 =================

    const Rect win{200, 150, 600, 450};

    // ---- 完全可见：端点不变 ----
    IPoint a{250, 200};
    IPoint b{350, 300};
    expect(cohenSutherland(a, b, win) && a.x == 250 && a.y == 200 && b.x == 350 &&
               b.y == 300,
           "clip: 完全可见，端点不变");

    // ---- 完全不可见 ----
    a = {10, 10};
    b = {100, 100};
    expect(!cohenSutherland(a, b, win), "clip: 完全不可见（拒绝）");

    // ---- 部分可见：两端都在窗外，水平线被夹到窗口左右边界 ----
    a = {100, 300};
    b = {700, 300};
    expect(cohenSutherland(a, b, win) && a.x == 200 && a.y == 300 && b.x == 600 &&
               b.y == 300,
           "clip: 水平线端点夹到窗口边界");

    // ---- 两端在外但斜穿窗口 ----
    a = {100, 100};
    b = {700, 500};
    expect(cohenSutherland(a, b, win) && inRect(a, win, 1) && inRect(b, win, 1),
           "clip: 斜线穿越窗口，结果端点在窗口内");

    // ---- 窗口对角线上（边界情形）----
    a = {200, 150};
    b = {600, 450};
    expect(cohenSutherland(a, b, win) && a.x == 200 && a.y == 150 && b.x == 600 &&
               b.y == 450,
           "clip: 窗口对角线上的线段完全可见");

    // ---- 中点分割与 C-S 结果一致性（差 ≤ 2 像素）----
    const IPoint cases[][2] = {
        {{100, 300}, {700, 300}},
        {{100, 100}, {700, 500}},
        {{250, 100}, {250, 500}},
        {{150, 160}, {650, 440}},
        {{300, 200}, {520, 400}},
    };
    bool consistent = true;
    for (const auto& seg : cases) {
        IPoint c1 = seg[0];
        IPoint c2 = seg[1];
        IPoint m1 = seg[0];
        IPoint m2 = seg[1];
        const bool okC = cohenSutherland(c1, c2, win);
        const bool okM = midpointSplit(m1, m2, win);
        if (okC != okM || !nearPoint(c1, m1, 2) || !nearPoint(c2, m2, 2)) {
            consistent = false;
            break;
        }
    }
    expect(consistent, "clip: 中点分割与 C-S 结果一致（±2 像素）");

    // ---- 中点分割：完全不可见 ----
    a = {10, 10};
    b = {100, 100};
    expect(!midpointSplit(a, b, win), "clip: 中点分割完全不可见");

    // ---- 挑战题：Cyrus-Beck 凸多边形窗口 ----
    const std::vector<FPoint> triWin = {{300, 150}, {500, 150}, {400, 300}};
    FPoint p{250, 225};
    FPoint q{550, 225};
    expect(cyrusBeck(p, q, triWin) && insideConvex(p, triWin) && insideConvex(q, triWin),
           "cyrus-beck: 穿越三角形窗口，结果端点在窗口内");
    p = {100, 500};
    q = {700, 560};
    expect(!cyrusBeck(p, q, triWin), "cyrus-beck: 窗口外线段不可见");
    p = {380, 200};
    q = {420, 220};
    expect(cyrusBeck(p, q, triWin) && nearPoint({(int)p.x, (int)p.y}, {380, 200}, 1) &&
               nearPoint({(int)q.x, (int)q.y}, {420, 220}, 1),
           "cyrus-beck: 完全在窗口内不变");

    // ================= 交互状态机（无窗口模拟：事件 → 像素）=================
    {
        PaintController ctrl;
        FrameBuffer canvas(kWidth, kHeight);

        // 模式 4：连点四个顶点 + 右键闭合 → 多边形填充
        ctrl.onKey(GLFW_KEY_4, GLFW_PRESS);
        const IPoint quad[4] = {{100, 100}, {300, 100}, {300, 250}, {100, 250}};
        for (const auto& pt : quad) {
            ctrl.onMouseMove(pt);
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseButton(0, GLFW_RELEASE);
        }
        ctrl.onMouseMove({500, 500});
        ctrl.onMouseButton(1, GLFW_PRESS); // 右键闭合
        ctrl.render(canvas);
        expect(canvas.pixel(200, 180) != Color::Black(), "ui: 多边形连点+右键闭合后填充");
        expect(canvas.pixel(500, 400) == Color::Black(), "ui: 多边形外仍是背景");

        // 模式 2 画圆 → 模式 5 点圆心做种子填充
        ctrl.onKey(GLFW_KEY_E, GLFW_PRESS); // 清屏
        ctrl.onKey(GLFW_KEY_2, GLFW_PRESS);
        ctrl.onMouseMove({400, 300});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({500, 300}); // 半径 100
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onKey(GLFW_KEY_5, GLFW_PRESS);
        ctrl.onMouseMove({400, 300});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.render(canvas);
        expect(canvas.pixel(400, 260) != Color::Black(), "ui: 圆内种子填充");
        expect(canvas.pixel(150, 300) == Color::Black(), "ui: 圆外未被填充（不泄漏）");

        // 模式 6：拖拽画线 → 默认矩形窗口裁剪（窗口外灰虚线，窗口内绿色）
        ctrl.onKey(GLFW_KEY_E, GLFW_PRESS);
        ctrl.onKey(GLFW_KEY_6, GLFW_PRESS);
        ctrl.onMouseMove({100, 300});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({700, 300});
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.render(canvas);
        expect(canvas.pixel(400, 300) == Color::Green(), "ui: 裁剪结果在窗口内为绿色");
        // 原线段是"灰色虚线"，某一点可能正落在空档上，所以按区间检查存在性
        auto hasGrayInRow = [&](int y, int x0, int x1) {
            for (int x = x0; x <= x1; ++x) {
                if (canvas.pixel(x, y) == Color::Gray()) {
                    return true;
                }
            }
            return false;
        };
        expect(hasGrayInRow(300, 100, 159), "ui: 窗口外左侧原线段为灰色虚线");
        expect(hasGrayInRow(300, 641, 700), "ui: 窗口外右侧原线段为灰色虚线");

        // 线型/线宽/填充色切换不改变已提交内容（只影响后续图形）
        ctrl.onKey(GLFW_KEY_S, GLFW_PRESS);
        ctrl.onKey(GLFW_KEY_W, GLFW_PRESS);
        ctrl.onKey(GLFW_KEY_X, GLFW_PRESS);
        ctrl.render(canvas);
        expect(canvas.pixel(400, 300) == Color::Green(), "ui: 属性切换后已提交内容不变");
    }

    return fails;
}

bool writeDemoImage(const char* path) {
    using namespace cg;
    using namespace cg::raster;

    FrameBuffer fb(kWidth, kHeight);
    fb.clear(Color(18, 18, 24)); // 深色背景，便于查看

    // ① 扫描线填充：凹多边形（L 形）+ 轮廓
    const std::vector<IPoint> lshape = {{40, 40},  {200, 40},  {200, 140},
                                        {120, 140}, {120, 260}, {40, 260}};
    scanlineFillPolygon(fb, lshape, Color(0, 160, 220));
    for (std::size_t i = 0; i < lshape.size(); ++i) {
        drawLine(fb, lshape[i], lshape[(i + 1) % lshape.size()], Color(170, 230, 255));
    }

    // ② 种子填充：圆形轮廓内以圆心为种子做扫描线种子填充
    drawCircle(fb, {330, 150}, 95, Color::White(), {});
    scanlineSeedFill(fb, {330, 150}, Color::Magenta());

    // ③ 扫描线填充：三角形
    const std::vector<IPoint> tri = {{470, 50}, {720, 50}, {595, 215}};
    scanlineFillPolygon(fb, tri, Color(90, 200, 120));
    for (std::size_t i = 0; i < tri.size(); ++i) {
        drawLine(fb, tri[i], tri[(i + 1) % tri.size()], Color(170, 240, 190));
    }

    // ④ 线段裁剪：矩形窗口 + 三种情形（灰虚线=原线段，绿实线=裁剪结果）
    const Rect win{260, 320, 740, 560};
    drawLine(fb, {win.xmin, win.ymin}, {win.xmax, win.ymin}, Color::White(), {});
    drawLine(fb, {win.xmax, win.ymin}, {win.xmax, win.ymax}, Color::White(), {});
    drawLine(fb, {win.xmax, win.ymax}, {win.xmin, win.ymax}, Color::White(), {});
    drawLine(fb, {win.xmin, win.ymax}, {win.xmin, win.ymin}, Color::White(), {});

    const IPoint segs[3][2] = {
        {{60, 340}, {780, 520}},  // 两端都在窗外，穿越窗口
        {{300, 380}, {700, 500}}, // 完全可见
        {{60, 590}, {240, 590}},  // 完全不可见
    };
    for (const auto& seg : segs) {
        drawLine(fb, seg[0], seg[1], Color::Gray(), {0xF0F0u, 1}); // 原线段
        IPoint a = seg[0];
        IPoint b = seg[1];
        if (cohenSutherland(a, b, win)) {
            drawLine(fb, a, b, Color::Green(), {0xFFFFu, 3}); // 裁剪结果
        }
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "[demo] 无法写入 " << path << std::endl;
        return false;
    }
    out << "P6\n" << fb.width() << " " << fb.height() << "\n255\n";
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            const Color c = fb.pixel(x, y);
            out.put(static_cast<char>(c.r));
            out.put(static_cast<char>(c.g));
            out.put(static_cast<char>(c.b));
        }
    }
    return out.good();
}

} // namespace cg::tests
