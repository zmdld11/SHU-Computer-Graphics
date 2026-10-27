// SmokeChecks.cpp — 算法自测与演示图导出；按实验分节，每节先 clear() 再断言。
#include "tests/SmokeChecks.hpp"

#include "core/Color.hpp"
#include "core/FrameBuffer.hpp"
#include "core/ImageIO.hpp"
#include "curves/Bezier.hpp"
#include "curves/BSpline.hpp"
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
    int checks = 0;
    auto expect = [&](bool ok, const char* name) {
        ++checks;
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

    // ================= F 键切换已闭合多边形的填充/轮廓 =================
    {
        PaintController ctrl;
        FrameBuffer canvas(kWidth, kHeight);

        // 模式 4 连点三角形顶点（默认填充、默认填充色黄），右键闭合
        ctrl.onKey(GLFW_KEY_4, GLFW_PRESS);
        const IPoint tri[3] = {{100, 100}, {300, 100}, {200, 250}};
        for (const auto& pt : tri) {
            ctrl.onMouseMove(pt);
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseButton(0, GLFW_RELEASE);
        }
        ctrl.onMouseMove({500, 500});
        ctrl.onMouseButton(1, GLFW_PRESS); // 右键闭合
        ctrl.render(canvas);
        expect(canvas.pixel(200, 150) == Color::Yellow(), "ui-f: 闭合后按当前填充色（黄）填充");

        // 闭合后按 F：最后一个多边形切为仅轮廓（描边沿用自身颜色，仍为黄）
        ctrl.onKey(GLFW_KEY_F, GLFW_PRESS);
        ctrl.render(canvas);
        expect(canvas.pixel(200, 150) == Color::Black(), "ui-f: F 切为仅轮廓，内部不填");
        expect(canvas.pixel(200, 100) == Color::Yellow(), "ui-f: 仅轮廓描边存在");

        // 再按 F：填充恢复
        ctrl.onKey(GLFW_KEY_F, GLFW_PRESS);
        ctrl.render(canvas);
        expect(canvas.pixel(200, 150) == Color::Yellow(), "ui-f: 再按 F 填充恢复");
    }

    // ================= 实验三：几何变换数学 =================
    {
        using namespace cg::geom;
        const float pi = 3.14159265f;
        auto nearF = [](FPoint a, float x, float y, float tol) {
            return std::fabs(a.x - x) <= tol && std::fabs(a.y - y) <= tol;
        };

        expect(nearF(apply(translate(3, -2), {5, 5}), 8, 3, 1e-3f), "xform: 平移");
        expect(nearF(apply(scale(2, 3), {4, 5}), 8, 15, 1e-3f), "xform: 放缩");
        expect(nearF(apply(rotate(pi / 2), {10, 0}), 0, 10, 1e-3f),
               "xform: 旋转90°（屏幕系顺时针）");

        // 绕任意点旋转 == T(p)·R·T(-p)
        const FPoint q1 = apply(rotateAbout(100, 50, pi / 3), {120, 80});
        const FPoint q2 =
            apply(mul(translate(100, 50), mul(rotate(pi / 3), translate(-100, -50))), {120, 80});
        expect(nearF(q1, q2.x, q2.y, 1e-3f), "xform: rotateAbout 等价于平移组合");

        // 绕任意点缩放：参考点不动，其余按比例
        expect(nearF(apply(scaleAbout(10, 10, 2, 2), {12, 10}), 14, 10, 1e-3f) &&
                   nearF(apply(scaleAbout(10, 10, 2, 2), {10, 10}), 10, 10, 1e-3f),
               "xform: scaleAbout 参考点不动");

        // 列向量约定：apply(mul(A,B), p) == apply(A, apply(B, p))（先 B 后 A）
        const FPoint m1 = apply(mul(translate(5, 0), scale(2, 2)), {3, 3});
        const FPoint m2 = apply(translate(5, 0), apply(scale(2, 2), {3, 3}));
        expect(nearF(m1, m2.x, m2.y, 1e-3f), "xform: mul 组合顺序（先 B 后 A）");
    }

    // ================= 实验三：选中 + 变换交互（无窗口模拟） =================
    {
        PaintController ctrl;
        FrameBuffer canvas(kWidth, kHeight);

        // 画一条水平线 (100,300)→(200,300)，模式 7 点击选中，方向键右移 5px
        ctrl.onKey(GLFW_KEY_1, GLFW_PRESS);
        ctrl.onMouseMove({100, 300});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({200, 300});
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onKey(GLFW_KEY_7, GLFW_PRESS);
        ctrl.onMouseMove({150, 300});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseButton(0, GLFW_RELEASE);
        for (int i = 0; i < 5; ++i) {
            ctrl.onKey(GLFW_KEY_RIGHT, GLFW_PRESS);
        }
        ctrl.render(canvas);
        // 选中状态下图形被橙色高亮重描（画板式选择），不再露白
        const Color HI(255, 150, 40);
        expect(canvas.pixel(205, 300) == HI, "ui3: 右移 5px 后新位置有像素（橙色高亮）");
        expect(canvas.pixel(100, 300) == Color::Black(), "ui3: 原位置已空");

        // 点空白清除选中 → 方向键不再生效
        ctrl.onMouseMove({700, 500});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onKey(GLFW_KEY_RIGHT, GLFW_PRESS);
        ctrl.render(canvas);
        expect(canvas.pixel(205, 300) == Color::White() && canvas.pixel(206, 300) == Color::Black(),
               "ui3: 清除选中后方向键无效");

        // 圆的等比缩放（+/-，圆心为默认参考点）：r 50→55→50
        ctrl.onKey(GLFW_KEY_2, GLFW_PRESS);
        ctrl.onMouseMove({400, 450});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({450, 450}); // 半径 50
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onKey(GLFW_KEY_7, GLFW_PRESS);
        ctrl.onMouseMove({450, 450}); // 点圆环选中
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onKey(GLFW_KEY_EQUAL, GLFW_PRESS); // ×1.1
        ctrl.render(canvas);
        expect(canvas.pixel(455, 450) != Color::Black() && canvas.pixel(345, 450) != Color::Black(),
               "ui3: 圆绕圆心等比放大 r≈55");
        ctrl.onMouseMove({700, 550}); // 光标挪开再验证缩小，避免十字盖住断言点
        ctrl.onKey(GLFW_KEY_MINUS, GLFW_PRESS); // ÷1.1
        ctrl.render(canvas);
        expect(canvas.pixel(450, 450) != Color::Black() && canvas.pixel(455, 450) == Color::Black(),
               "ui3: 缩小后 r 恢复≈50");

        // 三角形绕重心旋转（T×9 = 45°）：画面变化且重心附近仍有填充
        ctrl.onKey(GLFW_KEY_E, GLFW_PRESS);
        ctrl.onKey(GLFW_KEY_4, GLFW_PRESS);
        const IPoint tri[3] = {{300, 100}, {500, 100}, {400, 260}};
        for (const auto& pt : tri) {
            ctrl.onMouseMove(pt);
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseButton(0, GLFW_RELEASE);
        }
        ctrl.onMouseMove({700, 500});
        ctrl.onMouseButton(1, GLFW_PRESS); // 右键闭合（填充）
        ctrl.onKey(GLFW_KEY_7, GLFW_PRESS);
        ctrl.onMouseMove({400, 150}); // 三角形内部 → 选中
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseButton(0, GLFW_RELEASE);
        FrameBuffer before(kWidth, kHeight);
        ctrl.render(before);
        for (int i = 0; i < 9; ++i) {
            ctrl.onKey(GLFW_KEY_T, GLFW_PRESS);
        }
        FrameBuffer after(kWidth, kHeight);
        ctrl.render(after);
        bool rotated = false;
        for (int y = 60; y < 320 && !rotated; ++y) {
            for (int x = 260; x < 540; ++x) {
                if (before.pixel(x, y) != after.pixel(x, y)) {
                    rotated = true;
                    break;
                }
            }
        }
        expect(rotated, "ui3: 绕重心旋转后画面变化");
        expect(after.pixel(400, 165) != Color::Black() &&
                   after.pixel(400, 165) != Color(255, 150, 40),
               "ui3: 旋转后重心附近保留原填充色");

        // G 指定任意参考点 → 缩放绕它（不再是重心）
        ctrl.onKey(GLFW_KEY_G, GLFW_PRESS);
        ctrl.onMouseMove({200, 500});
        ctrl.onMouseButton(0, GLFW_PRESS); // 参考点 = (200,500)
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onKey(GLFW_KEY_MINUS, GLFW_PRESS); // ÷1.1 绕 (200,500)
        ctrl.render(canvas);
        bool moved = false;
        for (int y = 60; y < 320 && !moved; ++y) {
            for (int x = 260; x < 540; ++x) {
                if (after.pixel(x, y) != canvas.pixel(x, y)) {
                    moved = true;
                    break;
                }
            }
        }
        expect(moved, "ui3: 指定任意参考点后缩放生效");

        // Shift 多选：两条线一起平移
        ctrl.onKey(GLFW_KEY_E, GLFW_PRESS);
        ctrl.onKey(GLFW_KEY_1, GLFW_PRESS);
        ctrl.onMouseMove({100, 100});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({150, 100});
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onMouseMove({300, 200});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({350, 200});
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onKey(GLFW_KEY_7, GLFW_PRESS);
        ctrl.onMouseMove({125, 100});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onMouseMove({325, 200});
        ctrl.onMouseButton(0, GLFW_PRESS, GLFW_MOD_SHIFT); // Shift 加选
        ctrl.onMouseButton(0, GLFW_RELEASE, GLFW_MOD_SHIFT);
        ctrl.onMouseMove({130, 100});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({180, 100}); // 拖拽 +50px
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.render(canvas);
        expect(canvas.pixel(200, 100) == Color(255, 150, 40) &&
                   canvas.pixel(380, 200) == Color(255, 150, 40),
               "ui3: Shift 多选后拖拽平移两条线一起移动（橙色高亮）");
    }

    // ================= 实验三补充：右键拖拽任意角度旋转 =================
    {
        PaintController ctrl;
        FrameBuffer canvas(kWidth, kHeight);

        // 水平线 (300,300)→(400,300)，重心 (350,300) 是默认旋转中心
        ctrl.onKey(GLFW_KEY_1, GLFW_PRESS);
        ctrl.onMouseMove({300, 300});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({400, 300});
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onKey(GLFW_KEY_7, GLFW_PRESS);
        ctrl.onMouseMove({350, 300});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseButton(0, GLFW_RELEASE);

        // 右键在重心正下方（90°）按下，拖到正左方（180°）→ 顺时针 90°，线变竖线
        ctrl.onMouseMove({350, 400});
        ctrl.onMouseButton(1, GLFW_PRESS);
        ctrl.onMouseMove({250, 300});
        ctrl.onMouseButton(1, GLFW_RELEASE);
        ctrl.onMouseMove({700, 550}); // 光标挪开，避免十字盖住断言点
        ctrl.render(canvas);
        const Color HI(255, 150, 40);
        expect(canvas.pixel(350, 250) == HI && canvas.pixel(350, 350) == HI,
               "ui3b: 右键拖拽 90° 后线段竖直（端点绕重心准确旋转）");
        expect(canvas.pixel(400, 300) == Color::Black() &&
                   canvas.pixel(300, 300) == Color::Black(),
               "ui3b: 原水平位置已清空");

        // 一次旋转拖拽只占一步撤销
        ctrl.onKey(GLFW_KEY_Z, GLFW_PRESS, GLFW_MOD_CONTROL);
        ctrl.onMouseMove({700, 560});
        ctrl.render(canvas);
        expect(canvas.pixel(400, 300) == Color::White() && canvas.pixel(350, 250) == Color::Black(),
               "ui3b: 一次旋转拖拽 = 一步撤销，回滚为水平线");

        // Shift 吸附 15°：拖 20° 应落在 15°（端点 400,300 → 398,313）
        ctrl.onMouseMove({350, 300});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onMouseMove({350, 400});
        ctrl.onMouseButton(1, GLFW_PRESS);
        ctrl.onMouseMove({329, 356}, GLFW_MOD_SHIFT); // 极角 110° - 90° = 20°
        ctrl.onMouseButton(1, GLFW_RELEASE, GLFW_MOD_SHIFT);
        ctrl.onMouseMove({700, 550});
        ctrl.render(canvas);
        expect(canvas.pixel(398, 313) == HI, "ui3b: Shift 吸附，20° 拖拽落到 15° 端点");
        expect(canvas.pixel(400, 300) == Color::Black(), "ui3b: 吸附后原端点不再有线");

        // 无选中时右键仍是取消：退出变换模式再确认标题/状态不被旋转流程占用
        ctrl.onMouseMove({700, 560});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseButton(0, GLFW_RELEASE); // 点空白清除选中
        ctrl.onMouseMove({700, 400});
        ctrl.onMouseButton(1, GLFW_PRESS);
        ctrl.onMouseMove({600, 400}); // 无选中：右键拖拽不应转动任何图形
        ctrl.onMouseButton(1, GLFW_RELEASE);
        ctrl.onMouseMove({700, 550});
        ctrl.render(canvas);
        expect(canvas.pixel(398, 313) == Color::White(),
               "ui3b: 无选中时右键拖拽不误旋转（图形保持原位）");
    }

    // ================= 实验三（挑战题）：对接拼接 =================
    {
        PaintController ctrl;
        FrameBuffer canvas(kWidth, kHeight);

        // A：45° 斜线 (100,100)→(200,200)；B：圆 (500,400) r=40
        ctrl.onKey(GLFW_KEY_1, GLFW_PRESS);
        ctrl.onMouseMove({100, 100});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({200, 200});
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onKey(GLFW_KEY_2, GLFW_PRESS);
        ctrl.onMouseMove({500, 400});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({500, 360});
        ctrl.onMouseButton(0, GLFW_RELEASE);

        ctrl.onKey(GLFW_KEY_7, GLFW_PRESS);
        ctrl.onKey(GLFW_KEY_J, GLFW_PRESS);
        ctrl.onMouseMove({150, 150}); // A 的对接点：斜线中点（切向 45°）
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onMouseMove({500, 360}); // B 的对接点：圆顶点（切向 0°）
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onMouseMove({700, 550}); // 光标挪开，避免十字干扰断言
        ctrl.render(canvas);

        // 圆的顶点（原 (500,360)）应旋转 45° 并平移到 A 的中点 (150,150) 上
        bool docked = false;
        for (int dy = -2; dy <= 2 && !docked; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                if (canvas.pixel(150 + dx, 150 + dy) != Color::Black()) {
                    docked = true;
                    break;
                }
            }
        }
        expect(docked, "ui3-j: 拼接后 B 的对接点落在 A 的对接点上（±2px）");
        expect(canvas.pixel(500, 360) == Color::Black(), "ui3-j: 圆已离开原位");
    }

    // ================= 实验三：框选（橡皮筋矩形批量选中） =================
    {
        PaintController ctrl;
        FrameBuffer canvas(kWidth, kHeight);

        // 三条相离直线
        ctrl.onKey(GLFW_KEY_1, GLFW_PRESS);
        const IPoint segs[3][2] = {{{100, 100}, {200, 100}}, {{400, 100}, {500, 100}},
                                   {{650, 400}, {720, 450}}};
        for (const auto& sg : segs) {
            ctrl.onMouseMove(sg[0]);
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseMove(sg[1]);
            ctrl.onMouseButton(0, GLFW_RELEASE);
        }
        ctrl.onKey(GLFW_KEY_7, GLFW_PRESS);

        // 空白处拖拽框选前两条 → 选中:2（品红重心标记出现在两线中点）
        ctrl.onMouseMove({60, 60});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({550, 160});
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onMouseMove({750, 60}); // 光标移开，不遮断言点
        ctrl.render(canvas);
        expect(ctrl.title().find("选中:2") != std::string::npos, "band: 框选两条直线");
        // 画板式选择：两条选中线整体橙色高亮（替代旧的重心十字标记）
        expect(canvas.pixel(150, 100) == Color(255, 150, 40) &&
                   canvas.pixel(450, 100) == Color(255, 150, 40),
               "band: 两条选中线已高亮");

        // Shift+框选第三条 → 叠加为 选中:3
        ctrl.onMouseMove({600, 360});
        ctrl.onMouseButton(0, GLFW_PRESS, GLFW_MOD_SHIFT);
        ctrl.onMouseMove({750, 480});
        ctrl.onMouseButton(0, GLFW_RELEASE, GLFW_MOD_SHIFT);
        expect(ctrl.title().find("选中:3") != std::string::npos, "band: Shift 框选叠加第三条");

        // 空白处原地微点 → 清除选中
        ctrl.onMouseMove({60, 550});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseButton(0, GLFW_RELEASE);
        expect(ctrl.title().find("选中:0") != std::string::npos, "band: 空白微点清除选中");
    }

    // ================= 画板式选择编辑（高亮 / 外接矩形 / 控制柄 / Del） =================
    {
        PaintController ctrl;
        FrameBuffer canvas(kWidth, kHeight);
        const Color HI(255, 150, 40); // 选中高亮橙（与实现常量一致）
        auto click = [&](int x, int y) {
            ctrl.onMouseMove({x, y});
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseButton(0, GLFW_RELEASE);
        };
        auto drag = [&](int x0, int y0, int x1, int y1) {
            ctrl.onMouseMove({x0, y0});
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseMove({x1, y1});
            ctrl.onMouseButton(0, GLFW_RELEASE);
        };

        // 单选线段 → 端点编辑模式：高亮 + 两个端点柄，不画外接矩形
        ctrl.onKey(GLFW_KEY_1, GLFW_PRESS);
        drag(100, 100, 300, 180); // 线段 (100,100)-(300,180)
        ctrl.onKey(GLFW_KEY_7, GLFW_PRESS);
        click(200, 140); // 点线中段选中
        ctrl.onMouseMove({50, 550});
        ctrl.render(canvas);
        // 探针避开线段中点（那里画参考点青十字）
        expect(canvas.pixel(150, 120) == HI, "sel: 选中线段高亮重描为橙色");
        expect(canvas.pixel(100, 100) == HI && canvas.pixel(300, 180) == HI,
               "sel: 线段两端点柄已绘制");
        expect(canvas.pixel(103, 100) == Color::Black(), "sel: 柄带黑色描边（亮底可见）");
        expect(canvas.pixel(250, 150) == Color::Black(), "sel: 线段模式不画外接矩形填充");

        // 拖端点柄 b：(300,180) → (340,120)，线段改形
        drag(300, 180, 340, 120);
        ctrl.onMouseMove({50, 550});
        ctrl.render(canvas);
        // 新线段 (100,100)-(340,120) 上取整数点 (172,106)（中点 (220,110) 有参考点十字）
        expect(canvas.pixel(172, 106) == HI, "sel: 拖端点后线段改形（新线段高亮）");
        expect(canvas.pixel(300, 180) == Color::Black(), "sel: 旧端点位置已空");

        // 多选两条水平线 → bbox 8 柄；E 边柄单轴缩放（sx=4，y 不变）
        ctrl.onKey(GLFW_KEY_E, GLFW_PRESS);
        ctrl.onKey(GLFW_KEY_1, GLFW_PRESS);
        drag(100, 400, 150, 400);
        drag(100, 450, 150, 450);
        ctrl.onKey(GLFW_KEY_7, GLFW_PRESS);
        drag(80, 380, 400, 480); // 框选两条
        ctrl.onMouseMove({50, 550});
        ctrl.render(canvas);
        expect(ctrl.title().find("选中:2") != std::string::npos, "sel: 框选两条线");
        expect(canvas.pixel(150, 425) == HI, "sel: E 边中点柄已绘制");
        drag(150, 425, 300, 425); // 拖 E 柄 ×4（水平）
        ctrl.onMouseMove({50, 550});
        ctrl.render(canvas);
        expect(canvas.pixel(250, 400) == HI && canvas.pixel(250, 450) == HI,
               "sel: E 边柄水平缩放两条线");
        expect(canvas.pixel(110, 430) == Color::Black(), "sel: 单轴缩放纵向不变");

        // 圆：等比模式只给 4 角柄；拖 SE 角柄等比放大 1.5 倍
        ctrl.onKey(GLFW_KEY_E, GLFW_PRESS);
        ctrl.onKey(GLFW_KEY_2, GLFW_PRESS);
        drag(500, 300, 560, 300); // 圆心(500,300) r=60
        ctrl.onKey(GLFW_KEY_7, GLFW_PRESS);
        click(560, 300); // 点圆环右轴选中
        ctrl.onMouseMove({50, 550});
        ctrl.render(canvas);
        expect(canvas.pixel(440, 240) == HI && canvas.pixel(560, 360) == HI,
               "sel: 圆的角柄已绘制");
        expect(canvas.pixel(556, 300) == Color::Black(), "sel: 等比模式无边中点柄");
        drag(560, 360, 620, 420); // SE 角柄 → 等比 1.5：圆心(530,330) r=90
        ctrl.onMouseMove({50, 550});
        ctrl.render(canvas);
        expect(canvas.pixel(620, 330) == HI, "sel: 角柄等比缩放后新右轴点");
        expect(canvas.pixel(560, 300) == Color::Black(), "sel: 旧右轴点已空");

        // Del 删除选中图形
        ctrl.onKey(GLFW_KEY_DELETE, GLFW_PRESS);
        ctrl.onMouseMove({50, 550});
        ctrl.render(canvas);
        expect(canvas.pixel(620, 330) == Color::Black(), "sel: Del 删除选中图形");
        expect(ctrl.title().find("选中:0") != std::string::npos, "sel: 删除后清空选中");
    }

    // ================= 变换拖拽覆盖：多边形边缘 / 圆内部 / 多选组合体 =================
    {
        PaintController ctrl;
        FrameBuffer canvas(kWidth, kHeight);
        const Color HI(255, 150, 40);
        auto key = [&](int k) { ctrl.onKey(k, GLFW_PRESS); };
        auto click = [&](int x, int y) {
            ctrl.onMouseMove({x, y});
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseButton(0, GLFW_RELEASE);
        };
        auto drag = [&](int x0, int y0, int x1, int y1) {
            ctrl.onMouseMove({x0, y0});
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseMove({x1, y1});
            ctrl.onMouseButton(0, GLFW_RELEASE);
        };

        // 多边形：点上边（轮廓，不是内部）也能选中并拖动
        key(GLFW_KEY_4);
        click(100, 100);
        click(300, 100);
        click(300, 250);
        click(100, 250);
        ctrl.onMouseButton(1, GLFW_PRESS); // 右键闭合
        key(GLFW_KEY_7);
        click(200, 100);
        expect(ctrl.title().find("选中:1") != std::string::npos, "drag: 多边形边缘命中选中");
        drag(200, 100, 250, 150); // 整体 +50,+50
        ctrl.onMouseMove({50, 550});
        ctrl.render(canvas);
        expect(canvas.pixel(250, 190) == Color::Yellow(), "drag: 多边形边缘拖拽整体移动");
        expect(canvas.pixel(200, 150) == HI, "drag: 选中多边形为橙色加粗轮廓");
        expect(canvas.pixel(200, 100) == Color::Black(), "drag: 多边形旧边位置已空");

        // 圆：点圆心（不是圆环）也能选中并拖动
        key(GLFW_KEY_2);
        drag(400, 300, 450, 300); // 圆心 (400,300) r=50
        key(GLFW_KEY_7);
        click(400, 300);
        expect(ctrl.title().find("选中:1") != std::string::npos, "drag: 圆内部命中选中");
        drag(400, 300, 500, 300);
        ctrl.onMouseMove({50, 550});
        ctrl.render(canvas);
        expect(canvas.pixel(550, 300) == HI, "drag: 圆内部拖拽后新右轴点");
        expect(canvas.pixel(400, 300) == Color::Black(), "drag: 圆旧圆心位置已空");
    }
    {
        // 多选组合体：外接矩形内空白处按下 = 整体拖拽；点矩形外才重新框选
        PaintController ctrl;
        FrameBuffer canvas(kWidth, kHeight);
        const Color HI(255, 150, 40);
        auto key = [&](int k) { ctrl.onKey(k, GLFW_PRESS); };
        auto click = [&](int x, int y) {
            ctrl.onMouseMove({x, y});
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseButton(0, GLFW_RELEASE);
        };
        auto drag = [&](int x0, int y0, int x1, int y1) {
            ctrl.onMouseMove({x0, y0});
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseMove({x1, y1});
            ctrl.onMouseButton(0, GLFW_RELEASE);
        };

        key(GLFW_KEY_1);
        drag(100, 100, 200, 100);
        key(GLFW_KEY_1);
        drag(100, 300, 200, 300);
        key(GLFW_KEY_7);
        drag(50, 70, 260, 360); // 框选两条线，组合体 bbox=(100,100)-(200,300)
        expect(ctrl.title().find("选中:2") != std::string::npos, "drag: 框选形成组合体");
        drag(150, 200, 200, 250); // 从 bbox 内部空白处整体拖 +50,+50
        ctrl.onMouseMove({600, 550});
        ctrl.render(canvas);
        expect(canvas.pixel(180, 150) == HI && canvas.pixel(180, 350) == HI,
               "drag: 组合体空白处整体拖拽两条线");
        expect(canvas.pixel(180, 100) == Color::Black() &&
                   canvas.pixel(180, 300) == Color::Black(),
               "drag: 组合体旧位置已空");
        click(600, 500); // bbox 外微点 → 清除选中
        expect(ctrl.title().find("选中:0") != std::string::npos, "drag: 点组合体外清除选中");
    }

    // ================= 修饰键约束（Shift 45° 吸附 / Ctrl 定步长） =================
    {
        PaintController ctrl;
        FrameBuffer canvas(kWidth, kHeight);
        const Color HI(255, 150, 40);

        // Shift 画线：方向吸附到 0°（水平）——终点 (140,113) 的斜向被吸附为 (140,100)
        ctrl.onKey(GLFW_KEY_1, GLFW_PRESS);
        ctrl.onMouseMove({100, 100});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({140, 113}, GLFW_MOD_SHIFT);
        ctrl.onMouseButton(0, GLFW_RELEASE, GLFW_MOD_SHIFT);
        ctrl.onMouseMove({50, 550});
        ctrl.render(canvas);
        expect(canvas.pixel(120, 100) == Color::White() && canvas.pixel(120, 101) == Color::Black(),
               "mod: Shift 画线吸附为水平线");
        expect(canvas.pixel(140, 100) == Color::White() && canvas.pixel(140, 113) == Color::Black(),
               "mod: Shift 吸附终点在投影长度处");

        // Shift 画线：吸附到 45°（(233,244)≈52.8° → 对角线，投影长 54.45 → (238,238)）
        ctrl.onMouseMove({200, 200}, GLFW_MOD_SHIFT);
        ctrl.onMouseButton(0, GLFW_PRESS, GLFW_MOD_SHIFT);
        ctrl.onMouseMove({233, 244}, GLFW_MOD_SHIFT);
        ctrl.onMouseButton(0, GLFW_RELEASE, GLFW_MOD_SHIFT);
        ctrl.onMouseMove({50, 550});
        ctrl.render(canvas);
        expect(canvas.pixel(220, 220) == Color::White(), "mod: Shift 画线吸附 45° 对角线");

        // Ctrl 画圆：半径 37 步进为 40
        ctrl.onKey(GLFW_KEY_E, GLFW_PRESS);
        ctrl.onKey(GLFW_KEY_2, GLFW_PRESS);
        ctrl.onMouseMove({400, 300});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({437, 300}, GLFW_MOD_CONTROL);
        ctrl.onMouseButton(0, GLFW_RELEASE, GLFW_MOD_CONTROL);
        ctrl.onMouseMove({50, 550});
        ctrl.render(canvas);
        expect(canvas.pixel(440, 300) == Color::White(), "mod: Ctrl 半径步进到 40");
        expect(canvas.pixel(437, 300) == Color::Black(), "mod: 步进外的 37 处无像素");

        // Ctrl 拖线段端点：长度 153 步进为 160
        ctrl.onKey(GLFW_KEY_E, GLFW_PRESS);
        ctrl.onKey(GLFW_KEY_1, GLFW_PRESS);
        ctrl.onMouseMove({100, 100});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({200, 100});
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onKey(GLFW_KEY_7, GLFW_PRESS);
        ctrl.onMouseMove({150, 100}); // 点线中段选中 → 端点编辑模式
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onMouseMove({200, 100}); // 抓住端点 b
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({253, 100}, GLFW_MOD_CONTROL);
        ctrl.onMouseButton(0, GLFW_RELEASE, GLFW_MOD_CONTROL);
        ctrl.onMouseMove({50, 550});
        ctrl.render(canvas);
        expect(canvas.pixel(250, 100) == HI, "mod: Ctrl 端点步进延伸线段");
        expect(canvas.pixel(270, 100) == Color::Black(), "mod: 端点柄落在 260（160px 步进）");

        // Shift 拖动平移：偏移 (25,40)≈58° 吸附为 45° 对角 → 线从 y=300 沿对角移到 y≈333
        ctrl.onKey(GLFW_KEY_E, GLFW_PRESS);
        ctrl.onKey(GLFW_KEY_1, GLFW_PRESS);
        ctrl.onMouseMove({100, 300});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({150, 300});
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onKey(GLFW_KEY_7, GLFW_PRESS);
        ctrl.onMouseMove({125, 300});
        ctrl.onMouseButton(0, GLFW_PRESS); // 选中并开始拖拽（无 Shift）
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onMouseMove({125, 300});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({150, 340}, GLFW_MOD_SHIFT); // 拖动中按 Shift → 45°
        ctrl.onMouseButton(0, GLFW_RELEASE, GLFW_MOD_SHIFT);
        ctrl.onMouseMove({50, 550});
        ctrl.render(canvas);
        // 探针避开线中点的参考点十字（横臂 ±6）与端点柄（±3）：取 (145,333)
        expect(canvas.pixel(145, 333) == HI, "mod: Shift 拖动沿 45° 对角移动");
        expect(canvas.pixel(145, 300) == Color::Black(), "mod: 原水平位置已空");
    }

    // ================= 常驻裁剪（窗口独立于模式，持续生效可开关） =================
    {
        PaintController ctrl;
        FrameBuffer canvas(kWidth, kHeight);

        // 裁剪模式里 K+拖拽显式定义矩形窗口 → 常驻裁剪自动开启
        ctrl.onKey(GLFW_KEY_6, GLFW_PRESS);
        ctrl.onKey(GLFW_KEY_K, GLFW_PRESS);
        ctrl.onMouseMove({200, 150});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({640, 480});
        ctrl.onMouseButton(0, GLFW_RELEASE);
        // 切回直线/圆模式画跨界图形：线 (100,300)→(700,300)，圆心 (200,300) r=100
        ctrl.onKey(GLFW_KEY_1, GLFW_PRESS);
        ctrl.onMouseMove({100, 300});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({700, 300});
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onKey(GLFW_KEY_2, GLFW_PRESS);
        ctrl.onMouseMove({200, 300});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({300, 300});
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onMouseMove({750, 80}); // 光标移开
        ctrl.render(canvas);

        auto rowHasWhite = [&](int y, int x0, int x1) {
            for (int x = x0; x <= x1; ++x) {
                if (canvas.pixel(x, y) == Color::White()) {
                    return true;
                }
            }
            return false;
        };
        expect(rowHasWhite(300, 300, 500), "clip2: 窗口内线段正常显示");
        expect(!rowHasWhite(300, 100, 199), "clip2: 窗口外线段/圆弧被隐藏（左）");
        expect(!rowHasWhite(300, 641, 700), "clip2: 窗口外线段被隐藏（右）");

        // V 关闭 → 窗口外内容立即恢复；再开 → 再次隐藏
        ctrl.onKey(GLFW_KEY_V, GLFW_PRESS);
        ctrl.render(canvas);
        expect(rowHasWhite(300, 100, 199), "clip2: 关闭常驻裁剪后窗口外恢复显示");
        ctrl.onKey(GLFW_KEY_V, GLFW_PRESS);
        ctrl.render(canvas);
        expect(!rowHasWhite(300, 100, 199), "clip2: 重新开启后窗口外再次隐藏");

        // 凸多边形窗口（挑战题）同样常驻：P+三点+右键闭合
        ctrl.onKey(GLFW_KEY_6, GLFW_PRESS);
        ctrl.onKey(GLFW_KEY_P, GLFW_PRESS);
        const IPoint tri[3] = {{250, 200}, {600, 200}, {420, 500}};
        for (const auto& pt : tri) {
            ctrl.onMouseMove(pt);
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseButton(0, GLFW_RELEASE);
        }
        ctrl.onMouseButton(1, GLFW_PRESS); // 右键闭合
        ctrl.onKey(GLFW_KEY_1, GLFW_PRESS);
        ctrl.onMouseMove({100, 350});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({750, 350});
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onMouseMove({750, 80});
        ctrl.render(canvas);
        expect(rowHasWhite(350, 400, 450), "clip2: 凸多边形窗口内线段显示");
        expect(!rowHasWhite(350, 100, 240), "clip2: 凸多边形窗口外线段隐藏");
        expect(ctrl.title().find("裁剪:开") != std::string::npos, "clip2: 标题栏显示常驻裁剪状态");
    }

    // ================= 实验四：Bezier 曲线数学（de Casteljau） =================
    {
        using namespace cg::curves;
        auto nearF = [](FPoint a, float x, float y, float tol) {
            return std::fabs(a.x - x) <= tol && std::fabs(a.y - y) <= tol;
        };

        const std::vector<FPoint> cubic = {{0.0f, 0.0f}, {100.0f, 0.0f}, {100.0f, 100.0f},
                                           {0.0f, 100.0f}};
        const std::vector<FPoint> samples = sampleBezier(cubic, 64);
        expect(nearF(samples.front(), 0.0f, 0.0f, 1e-3f) &&
                   nearF(samples.back(), 0.0f, 100.0f, 1e-3f),
               "bezier: 端点插值（过首末控制点）");
        expect(samples.size() == 65, "bezier: 采样数 = segments+1");

        const std::vector<FPoint> lin = sampleBezier({{0.0f, 0.0f}, {10.0f, 20.0f}}, 2);
        expect(nearF(lin[1], 5.0f, 10.0f, 1e-3f), "bezier: 两点退化为直线（中点）");

        const std::vector<FPoint> quad = sampleBezier({{0.0f, 0.0f}, {40.0f, 40.0f}, {80.0f, 0.0f}}, 2);
        expect(nearF(quad[1], 40.0f, 20.0f, 1e-3f), "bezier: 二次中点 = (P0+2P1+P2)/4");

        const FPoint cubicMid = sampleBezier(cubic, 2)[1];
        expect(nearF(cubicMid, 75.0f, 50.0f, 1e-3f), "bezier: 三次中点 = (P0+3P1+3P2+P3)/8");

        // 对称性：控制点倒序 → 采样序列倒序
        const std::vector<FPoint> rev =
            sampleBezier({cubic.rbegin(), cubic.rend()}, 64);
        bool mirrored = true;
        for (std::size_t k = 0; k < samples.size() && mirrored; ++k) {
            const FPoint& f = samples[k];
            const FPoint& r = rev[rev.size() - 1 - k];
            if (!nearF(f, r.x, r.y, 1e-3f)) {
                mirrored = false;
            }
        }
        expect(mirrored, "bezier: 控制点倒序则曲线倒序（对称性）");

        bool inHull = true;
        for (const auto& pt : samples) {
            if (pt.x < -1e-3f || pt.x > 100.0f + 1e-3f || pt.y < -1e-3f ||
                pt.y > 100.0f + 1e-3f) {
                inHull = false;
            }
        }
        expect(inHull, "bezier: 曲线在控制点凸包（包围盒）内");
    }

    // ================= 实验四（挑战题）：B 样条数学（Cox-de Boor） =================
    {
        using namespace cg::curves;
        auto nearF = [](FPoint a, float x, float y, float tol) {
            return std::fabs(a.x - x) <= tol && std::fabs(a.y - y) <= tol;
        };

        const std::vector<FPoint> cps = {{0.0f, 0.0f}, {100.0f, 0.0f}, {100.0f, 100.0f},
                                         {0.0f, 100.0f}};
        const std::vector<FPoint> bs = sampleBSpline(cps, 3, 64);
        expect(nearF(bs.front(), 0.0f, 0.0f, 1e-3f) && nearF(bs.back(), 0.0f, 100.0f, 1e-3f),
               "bspline: clamped 端点过首末控制点");

        // 4 点三次 clamped B 样条 == 三次 Bezier（clamped 基退化为 Bernstein 基）
        const std::vector<FPoint> bz = sampleBezier(cps, 64);
        bool same = true;
        for (std::size_t k = 0; k < bs.size() && same; ++k) {
            if (!nearF(bs[k], bz[k].x, bz[k].y, 1e-3f)) {
                same = false;
            }
        }
        expect(same, "bspline: 4点三次 clamped 逐点退化为 Bezier");

        const std::vector<FPoint> line2 = sampleBSpline({{0.0f, 0.0f}, {10.0f, 20.0f}}, 3, 2);
        expect(nearF(line2[1], 5.0f, 10.0f, 1e-3f), "bspline: 控制点不足降阶，2点=直线");

        const std::vector<FPoint> bs5 = sampleBSpline(
            {{0.0f, 0.0f}, {200.0f, 0.0f}, {200.0f, 200.0f}, {0.0f, 200.0f}, {100.0f, 100.0f}},
            3, 64);
        bool hull5 = true;
        for (const auto& pt : bs5) {
            if (pt.x < -1e-3f || pt.x > 200.0f + 1e-3f || pt.y < -1e-3f ||
                pt.y > 200.0f + 1e-3f) {
                hull5 = false;
            }
        }
        expect(hull5, "bspline: 曲线在控制点凸包（包围盒）内");

        // 均匀节点（非固支）：4 点三次 → 起点 (P0+4P1+P2)/6、终点 (P1+4P2+P3)/6
        const std::vector<FPoint> uni = sampleBSpline(cps, 3, 64, false);
        expect(nearF(uni.front(), 500.0f / 6.0f, 100.0f / 6.0f, 1e-3f),
               "bspline: 均匀节点起点 = (P0+4P1+P2)/6");
        expect(nearF(uni.back(), 500.0f / 6.0f, 500.0f / 6.0f, 1e-3f),
               "bspline: 均匀节点终点 = (P1+4P2+P3)/6");
        const float dStart = std::fabs(uni.front().x - cps.front().x) +
                             std::fabs(uni.front().y - cps.front().y);
        expect(dStart > 1.0f, "bspline: 均匀节点不过首控制点");
        // 均匀节点下 4 点与 Bezier 必须不同（这是该选项存在的意义）
        bool differs = false;
        for (std::size_t k = 0; k < uni.size() && !differs; ++k) {
            if (!nearF(uni[k], bz[k].x, bz[k].y, 1e-2f)) {
                differs = true;
            }
        }
        expect(differs, "bspline: 均匀节点与 Bezier 明显不同");
    }

    // ================= 实验四：曲线交互（连点输入 + 控制点拖拽调整） =================
    {
        PaintController ctrl;
        FrameBuffer canvas(kWidth, kHeight);

        // 模式 8：连点 4 个控制点 + 右键提交 → 三次 Bezier
        ctrl.onKey(GLFW_KEY_8, GLFW_PRESS);
        const IPoint cps[4] = {{100, 150}, {300, 100}, {500, 200}, {700, 150}};
        for (const auto& pt : cps) {
            ctrl.onMouseMove(pt);
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseButton(0, GLFW_RELEASE);
        }
        ctrl.onMouseMove({750, 500}); // 光标挪开再右键，不遮断言点
        ctrl.onMouseButton(1, GLFW_PRESS);
        ctrl.render(canvas);
        // 断言点取 B(0.25)：权重 (27,27,9,1)/64 → (250, 135.94)≈(250,136)，
        // 刻意避开 B(0.5)——曲线模式下控制多边形虚线恰过 (400,150)，会盖掉曲线像素
        expect(canvas.pixel(250, 136) == Color::White(), "ui4: 连点+右键提交后曲线经过 B(0.25)");

        // 拖拽控制点：抓 P1=(300,100) 拖到 (300,300) → 曲线实时跟随
        ctrl.onMouseMove({300, 100});
        ctrl.onMouseButton(0, GLFW_PRESS); // 抓住 P1
        ctrl.onMouseMove({300, 300});
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.render(canvas);
        // 新 B(0.25) = (250, 220.31)≈(250,220)：x 不变、y 变 —— 曲线确实跟随控制点改形
        expect(canvas.pixel(250, 220) == Color::White(), "ui4: 拖拽控制点后曲线跟随新形状");
        expect(canvas.pixel(250, 136) == Color::Black(), "ui4: 原位置已不在曲线上");

        // B 键切样条：新曲线按 clamped 三次 B 样条生成（5 控制点，C(0.5)=(400,400)）
        ctrl.onKey(GLFW_KEY_B, GLFW_PRESS);
        const IPoint bps[5] = {{100, 400}, {250, 350}, {400, 450}, {550, 350}, {700, 400}};
        for (const auto& pt : bps) {
            ctrl.onMouseMove(pt);
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseButton(0, GLFW_RELEASE);
        }
        ctrl.onMouseMove({750, 550});
        ctrl.onMouseButton(1, GLFW_PRESS); // 右键提交
        ctrl.render(canvas);
        expect(canvas.pixel(400, 400) == Color::White(), "ui4: B样条曲线按切换后类型生成");
        expect(ctrl.title().find("B样条") != std::string::npos, "ui4: 标题栏显示 B样条 类型");

        // 变换模式也能选中曲线并平移（曲线是参数图形，接入既有选中/变换体系）
        ctrl.onKey(GLFW_KEY_7, GLFW_PRESS);
        ctrl.onMouseMove({400, 400}); // 点在 B样条曲线中点上 → 选中
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseButton(0, GLFW_RELEASE);
        for (int i = 0; i < 10; ++i) {
            ctrl.onKey(GLFW_KEY_RIGHT, GLFW_PRESS); // 右移 10px
        }
        ctrl.render(canvas);
        expect(canvas.pixel(410, 400) == Color(255, 150, 40),
               "ui4: 曲线可被选中并随方向键平移（橙色高亮）");

        // 框选曲线：空白拖拽矩形罩住样条 → 选中:1
        ctrl.onMouseMove({60, 60});
        ctrl.onMouseButton(0, GLFW_PRESS); // 原地微点清除选中
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onMouseMove({330, 320});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({780, 580});
        ctrl.onMouseButton(0, GLFW_RELEASE);
        expect(ctrl.title().find("选中:1") != std::string::npos, "ui4: 框选可选中曲线");
    }

    // ================= 实验四补充：均匀节点（非固支）B 样条 =================
    {
        PaintController ctrl;
        FrameBuffer canvas(kWidth, kHeight);
        ctrl.onKey(GLFW_KEY_8, GLFW_PRESS);
        ctrl.onKey(GLFW_KEY_B, GLFW_PRESS); // Bezier → 固支
        ctrl.onKey(GLFW_KEY_B, GLFW_PRESS); // 固支 → 均匀
        expect(ctrl.title().find("均匀") != std::string::npos,
               "ui4b: B 键三态循环到均匀节点");
        bool labelOk = false;
        for (const auto& b : ctrl.toolbarButtons()) {
            if (b.label == "B 类型:均匀") {
                labelOk = true;
            }
        }
        expect(labelOk, "ui4b: 任务栏标签显示均匀");

        // 4 点：起点 (P0+4P1+P2)/6 = (400,150)，终点 (P1+4P2+P3)/6 = (400,350)
        const IPoint ups[4] = {{150, 100}, {450, 100}, {450, 400}, {150, 400}};
        for (const auto& pt : ups) {
            ctrl.onMouseMove(pt);
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseButton(0, GLFW_RELEASE);
        }
        ctrl.onMouseMove({760, 560});
        ctrl.onMouseButton(1, GLFW_PRESS); // 右键提交
        ctrl.onKey(GLFW_KEY_1, GLFW_PRESS); // 离开曲线模式：控制点手柄会盖住断言点
        ctrl.render(canvas);
        expect(canvas.pixel(400, 150) == Color::White(),
               "ui4b: 均匀节点起点 = (P0+4P1+P2)/6");
        expect(canvas.pixel(400, 350) == Color::White(),
               "ui4b: 均匀节点终点 = (P1+4P2+P3)/6");
        expect(canvas.pixel(150, 100) == Color::Black(), "ui4b: 均匀节点不过首控制点");

        ctrl.onKey(GLFW_KEY_8, GLFW_PRESS); // 回到曲线模式
        ctrl.onKey(GLFW_KEY_B, GLFW_PRESS); // uniform → Bezier
        expect(ctrl.title().find("Bezier") != std::string::npos,
               "ui4b: 第三次 B 回到 Bezier");
    }

    // ================= 自绘任务栏（按钮点击 = 原按键操作） =================
    {
        PaintController ctrl;
        FrameBuffer canvas(kWidth, kHeight);

        ctrl.render(canvas);
        // 按标签定位按钮中心并点击——布局随字体度量变化，断言不写死坐标
        auto clickBtn = [&](const char* label) {
            for (const auto& b : ctrl.toolbarButtons()) {
                if (b.label == label) {
                    ctrl.onMouseMove({b.x + b.w / 2, b.y + b.h / 2});
                    ctrl.onMouseButton(0, GLFW_PRESS);
                    ctrl.onMouseButton(0, GLFW_RELEASE);
                    return true;
                }
            }
            return false;
        };

        const int tbH = ctrl.toolbarHeight();
        expect(canvas.pixel(799, tbH - 3) == Color(34, 34, 40), "tb: 顶部任务栏背景已绘制");

        // "7 变换" 按钮内应存在浅色文字像素（系统字体/回退点阵都在按钮框内绘制）
        bool hasText = false;
        for (const auto& btn : ctrl.toolbarButtons()) {
            if (btn.label != "7 变换") {
                continue;
            }
            for (int y = btn.y; y < btn.y + btn.h && !hasText; ++y) {
                for (int x = btn.x; x < btn.x + btn.w; ++x) {
                    if (canvas.pixel(x, y) == Color(230, 230, 235)) {
                        hasText = true;
                        break;
                    }
                }
            }
        }
        expect(hasText, "tb: 按钮文字已渲染");

        expect(clickBtn("7 变换"), "tb: 找到模式按钮");
        expect(ctrl.title().find("变换") != std::string::npos, "tb: 点击按钮切换到变换模式");
        expect(clickBtn("G 参考点"), "tb: 找到参考点按钮");
        expect(ctrl.title().find("指定") != std::string::npos, "tb: 参考点按钮切为指定");

        // E 清屏：先画圆，再点 "E 清屏"
        ctrl.onKey(GLFW_KEY_2, GLFW_PRESS);
        ctrl.onMouseMove({400, 450});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({450, 450});
        ctrl.onMouseButton(0, GLFW_RELEASE);
        expect(clickBtn("E 清屏"), "tb: 找到清屏按钮");
        ctrl.onMouseMove({700, 550});
        ctrl.render(canvas);
        expect(canvas.pixel(450, 450) == Color::Black(), "tb: 清屏按钮清屏");
    }

    // ================= 撤销（Ctrl+Z）与属性按钮反馈 =================
    {
        PaintController ctrl;
        FrameBuffer canvas(kWidth, kHeight);
        auto clickBtn = [&](const std::string& label) {
            for (const auto& b : ctrl.toolbarButtons()) {
                if (b.label == label) {
                    ctrl.onMouseMove({b.x + b.w / 2, b.y + b.h / 2});
                    ctrl.onMouseButton(0, GLFW_PRESS);
                    ctrl.onMouseButton(0, GLFW_RELEASE);
                    return true;
                }
            }
            return false;
        };

        // 画一条水平线 → 撤销 → 像素恢复背景
        ctrl.onKey(GLFW_KEY_1, GLFW_PRESS);
        ctrl.onMouseMove({150, 520});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({350, 520});
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.render(canvas);
        expect(canvas.pixel(250, 520) == Color::White(), "undo: 画线已提交");
        ctrl.onKey(GLFW_KEY_Z, GLFW_PRESS, GLFW_MOD_CONTROL);
        ctrl.render(canvas);
        expect(canvas.pixel(250, 520) == Color::Black(), "undo: Ctrl+Z 撤销新增图形");
        ctrl.onKey(GLFW_KEY_Z, GLFW_PRESS, GLFW_MOD_CONTROL);
        ctrl.render(canvas);
        expect(canvas.pixel(250, 520) == Color::Black(), "undo: 空栈再撤销不崩溃");

        // 选中 → 删除 → 撤销恢复
        ctrl.onMouseMove({150, 520});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({350, 520});
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onKey(GLFW_KEY_7, GLFW_PRESS);
        ctrl.onMouseMove({250, 520});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseButton(0, GLFW_RELEASE);
        ctrl.onKey(GLFW_KEY_DELETE, GLFW_PRESS);
        ctrl.onMouseMove({700, 560}); // 光标移开：预览层十字会盖住探测像素
        ctrl.render(canvas);
        expect(canvas.pixel(250, 520) == Color::Black(), "undo: 删除选中图形");
        ctrl.onKey(GLFW_KEY_Z, GLFW_PRESS, GLFW_MOD_CONTROL);
        ctrl.render(canvas);
        expect(canvas.pixel(250, 520) == Color::White(), "undo: 撤销删除");

        // 属性按钮：标签显示当前值，点击后立即变化
        expect(clickBtn("S 线型:实线"), "tb: 线型按钮初始标签");
        expect(ctrl.toolbarButtons()[0].label.size() > 0, "tb: 按钮布局可读");
        bool styleChanged = false;
        for (const auto& b : ctrl.toolbarButtons()) {
            if (b.label == "S 线型:虚线") {
                styleChanged = true;
            }
        }
        expect(styleChanged, "tb: 线型按钮点击后标签变为虚线");

        // 曲线：5 个控制点 + B 键 → 最近曲线切换为 B 样条
        ctrl.onKey(GLFW_KEY_8, GLFW_PRESS);
        const IPoint cp[5] = {{300, 460}, {380, 380}, {480, 470}, {560, 380}, {620, 450}};
        for (const auto& pt : cp) {
            ctrl.onMouseMove(pt);
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseButton(0, GLFW_RELEASE);
        }
        ctrl.onMouseButton(1, GLFW_PRESS);
        expect(clickBtn("B 类型:Bezier"), "tb: 找到样条切换按钮");
        expect(ctrl.title().find("B样条") != std::string::npos, "curve: B 键切到 B 样条");
    }

    // ================= 越界绘制防护（画布外坐标不崩溃，落点被安全钳制） =================
    {
        FrameBuffer oob(kWidth, kHeight);

        // 缓冲尺寸不变量：resize/copyFrom 之后尺寸与像素数组必须同步
        oob.resize(300, 200);
        expect(oob.pixel(299, 199) == Color::Black() && oob.pixel(-1, -1) == Color::Black(),
               "oob: resize 后读写仍安全");
        FrameBuffer small(50, 40);
        small.putPixel(10, 10, FC);
        oob.copyFrom(small);
        expect(oob.pixel(10, 10) == FC && oob.pixel(300, 199) == Color::Black(),
               "oob: copyFrom 尺寸同步且越界读安全");
        oob.resize(kWidth, kHeight);

        // 直线两端远在画布外：仍应斜穿画布
        oob.clear(Color::Black());
        drawLine(oob, {-100, -100}, {2000, 1500}, W, {});
        bool crossesMid = false;
        for (int dy = -2; dy <= 2 && !crossesMid; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                if (oob.pixel(400 + dx, 281 + dy) == W) {
                    crossesMid = true;
                    break;
                }
            }
        }
        expect(crossesMid, "oob: 画布外端点直线穿过画布内部");

        // 线宽 5 画到画布右下角：笔刷越界部分被钳制
        oob.clear(Color::Black());
        drawLine(oob, {700, 550}, {799, 599}, W, {0xFFFFu, 5});
        expect(oob.pixel(799, 599) == W, "oob: 宽线画到画布角端点存在");

        // 圆心在画布外：落在画布内的轴点仍正确
        oob.clear(Color::Black());
        drawCircle(oob, {-50, 300}, 200, W, {});
        expect(oob.pixel(150, 300) == W, "oob: 圆心在画布外左，右轴点正确");
        oob.clear(Color::Black());
        drawCircle(oob, {400, -100}, 200, W, {});
        expect(oob.pixel(400, 100) == W, "oob: 圆心在画布外上，下轴点正确");

        // 扫描线填充：顶点远在画布外的三角形
        oob.clear(Color::Black());
        const std::vector<IPoint> bigTri = {{-200, 100}, {500, -150}, {300, 900}};
        scanlineFillPolygon(oob, bigTri, FC);
        expect(oob.pixel(200, 283) == FC, "oob: 出界多边形填充重心正确");

        // 种子填充：画布外种子安全忽略；画布角落种子填背景区但不越白色边界
        oob.clear(Color::Black());
        drawLine(oob, {50, 50}, {150, 50}, W, {});
        drawLine(oob, {150, 50}, {150, 150}, W, {});
        drawLine(oob, {150, 150}, {50, 150}, W, {});
        drawLine(oob, {50, 150}, {50, 50}, W, {});
        const Color FC2(230, 120, 40);
        scanlineSeedFill(oob, {60, 60}, FC2);
        expect(oob.pixel(60, 60) == FC2, "oob: 框内种子正常填充");
        scanlineSeedFill(oob, {-5, -5}, Color::Magenta());
        expect(oob.pixel(60, 60) == FC2, "oob: 画布外种子被忽略");
        const Color FC3(90, 220, 90);
        scanlineSeedFill(oob, {2, 2}, FC3);
        expect(oob.pixel(10, 10) == FC3 && oob.pixel(60, 60) == FC2, "oob: 角落种子填背景不越界");
    }

    // ================= 越界交互（无窗口模拟：出界画图/拖拽/极小窗口） =================
    {
        PaintController ctrl;
        FrameBuffer canvas(kWidth, kHeight);
        auto click = [&](int x, int y) {
            ctrl.onMouseMove({x, y});
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseButton(0, GLFW_RELEASE);
        };
        auto drag = [&](int x0, int y0, int x1, int y1) {
            ctrl.onMouseMove({x0, y0});
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseMove({x1, y1});
            ctrl.onMouseButton(0, GLFW_RELEASE);
        };

        // 曲线：首末控制点在画布外，曲线体应仍横穿画布
        ctrl.onKey(GLFW_KEY_8, GLFW_PRESS);
        const IPoint cpts[4] = {{-100, -50}, {300, 100}, {500, 500}, {900, 700}};
        for (const auto& pt : cpts) {
            click(pt.x, pt.y);
        }
        ctrl.onMouseButton(1, GLFW_PRESS); // 右键提交
        ctrl.render(canvas);
        bool curveMid = false;
        for (int dy = -3; dy <= 3 && !curveMid; ++dy) {
            for (int dx = -3; dx <= 3; ++dx) {
                if (canvas.pixel(400 + dx, 306 + dy) != Color::Black()) {
                    curveMid = true;
                    break;
                }
            }
        }
        expect(curveMid, "ui-oob: 出界控制点曲线仍穿过画布");

        // 变换：圆拖出画布深处再拖回，圆心应精确回到原位
        ctrl.onKey(GLFW_KEY_2, GLFW_PRESS);
        drag(400, 300, 450, 300); // 圆心 (400,300) 半径 50
        ctrl.onKey(GLFW_KEY_7, GLFW_PRESS);
        click(450, 300); // 点右轴点选中
        drag(450, 300, -1750, -1900); // 拖出画布
        ctrl.render(canvas);
        expect(canvas.pixel(450, 300) == Color::Black(), "ui-oob: 圆拖出画布后原位无像素");
        drag(-1750, -1900, 450, 300); // 从画布外按住圆拖回
        ctrl.render(canvas);
        expect(canvas.pixel(450, 300) != Color::Black(), "ui-oob: 圆从画布外拖回原位");

        // 超大矩形框选（含画布外图形）+ 方向键微移
        drag(-2000, -2000, 2000, 2000);
        for (int i = 0; i < 5; ++i) {
            ctrl.onKey(GLFW_KEY_RIGHT, GLFW_PRESS);
        }
        ctrl.render(canvas);
        ctrl.render(canvas);
        expect(canvas.pixel(455, 300) != Color::Black() && canvas.pixel(450, 300) == Color::Black(),
               "ui-oob: 框选后方向键微移生效");

        // 最小合法常驻裁剪窗口（8×8，小于 8px 的会被忽略）：只显示窗口内像素
        ctrl.onKey(GLFW_KEY_6, GLFW_PRESS);
        ctrl.onKey(GLFW_KEY_K, GLFW_PRESS);
        drag(100, 100, 108, 108);
        drag(0, 100, 200, 100);
        ctrl.render(canvas);
        expect(canvas.pixel(100, 100) == Color::Green(), "ui-oob: 8×8 窗口内为裁剪结果");
        expect(canvas.pixel(112, 100) != Color::Green() && canvas.pixel(140, 100) != Color::Green(),
               "ui-oob: 8×8 窗口外不显示绿色");
    }

    // ================= 显示层整块操作（#55：FrameBuffer 提速） =================
    {
        FrameBuffer bufA(16, 8);
        bufA.clear(Color::Black());
        bufA.fillRect(2, 3, 5, 6, Color::Red());
        expect(bufA.pixel(2, 3) == Color::Red() && bufA.pixel(5, 6) == Color::Red() &&
                   bufA.pixel(1, 3) == Color::Black() && bufA.pixel(6, 3) == Color::Black(),
               "fb: fillRect 闭区间与边界");
        bufA.fillRect(-5, -5, 100, 100, Color::Blue());
        expect(bufA.pixel(0, 0) == Color::Blue() && bufA.pixel(15, 7) == Color::Blue(),
               "fb: fillRect 越界自动夹取");

        FrameBuffer bufB(16, 8);
        bufB.copySpanFrom(bufA, 4, 3, 6);
        expect(bufB.pixel(3, 4) == Color::Blue() && bufB.pixel(2, 4) == Color::Black() &&
                   bufB.pixel(7, 4) == Color::Black(),
               "fb: copySpanFrom 整段拷贝");

        FrameBuffer bufC(4, 4);
        bufC.copyFrom(bufA);
        expect(bufC.width() == 16 && bufC.height() == 8 && bufC.pixel(15, 7) == Color::Blue(),
               "fb: copyFrom 尺寸不同自动重建");

        FrameBuffer bufD(8, 4);
        bufD.clear(Color::Black());
        bufD.fillSpan(2, -2, 5, Color::Red());
        expect(bufD.pixel(0, 2) == Color::Red() && bufD.pixel(5, 2) == Color::Red() &&
                   bufD.pixel(6, 2) == Color::Black(),
               "fb: fillSpan 行内整段填充与边界");

        FrameBuffer bufE(6, 4);
        bufE.clear(Color::Green());
        bufE.copyRectFrom(bufA, 1, 2, 4, 3, 2, 1);
        expect(bufE.pixel(2, 1) == Color::Blue() && bufE.pixel(5, 3) == Color::Blue() &&
                   bufE.pixel(1, 1) == Color::Green() && bufE.pixel(2, 0) == Color::Green(),
               "fb: copyRectFrom 子矩形与边界");
    }

    // ================= 常驻裁剪显示层缓存 =================
    {
        PaintController ctrl;
        FrameBuffer canvas(kWidth, kHeight);
        auto key = [&](int k) { ctrl.onKey(k, GLFW_PRESS); };
        auto drag = [&](int x0, int y0, int x1, int y1) {
            ctrl.onMouseMove({x0, y0});
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseMove({x1, y1});
            ctrl.onMouseButton(0, GLFW_RELEASE);
        };

        key(GLFW_KEY_1);
        drag(100, 300, 700, 300); // 横穿画布的白色直线
        key(GLFW_KEY_6);
        key(GLFW_KEY_K);
        drag(300, 200, 500, 400); // 矩形裁剪窗，自动开启常驻裁剪
        ctrl.render(canvas);
        expect(canvas.pixel(400, 300) == Color::White(), "clip-cache: 矩形窗内可见");
        expect(canvas.pixel(100, 300) == Color::Black(), "clip-cache: 矩形窗外隐藏");

        key(GLFW_KEY_V); // 关裁剪：缓存不重建，但显示层应整块恢复
        ctrl.render(canvas);
        expect(canvas.pixel(100, 300) == Color::White(), "clip-cache: V 关后完整画面恢复");
        key(GLFW_KEY_V); // 再开：沿用有效缓存
        ctrl.render(canvas);
        expect(canvas.pixel(100, 300) == Color::Black(), "clip-cache: V 再开恢复掩码");

        key(GLFW_KEY_K); // 改窗口：缓存必须失效重建
        drag(600, 100, 700, 500);
        ctrl.render(canvas);
        expect(canvas.pixel(650, 300) == Color::White(), "clip-cache: 改窗后新窗口内可见");
        expect(canvas.pixel(400, 300) == Color::Black(), "clip-cache: 改窗后旧区域隐藏");

        FrameBuffer small(640, 480); // resize：提交层与裁剪缓存都要按新尺寸重建
        ctrl.render(small);
        expect(small.pixel(620, 300) == Color::White(), "clip-cache: resize 后窗口内可见");
        expect(small.pixel(400, 300) == Color::Black(), "clip-cache: resize 后窗口外隐藏");
    }

    // ================= 凸多边形裁剪窗缓存 =================
    {
        PaintController ctrl;
        FrameBuffer canvas(kWidth, kHeight);
        auto key = [&](int k) { ctrl.onKey(k, GLFW_PRESS); };
        auto click = [&](int x, int y) {
            ctrl.onMouseMove({x, y});
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseButton(0, GLFW_RELEASE);
        };
        key(GLFW_KEY_1);
        ctrl.onMouseMove({100, 300});
        ctrl.onMouseButton(0, GLFW_PRESS);
        ctrl.onMouseMove({700, 300});
        ctrl.onMouseButton(0, GLFW_RELEASE);
        key(GLFW_KEY_6);
        key(GLFW_KEY_P);
        click(300, 200);
        click(500, 200);
        click(400, 400);
        ctrl.onMouseButton(1, GLFW_PRESS); // 右键闭合凸多边形窗
        ctrl.render(canvas);
        expect(canvas.pixel(400, 300) == Color::White(), "clip-cache: 凸多边形窗内可见");
        expect(canvas.pixel(200, 300) == Color::Black(), "clip-cache: 凸多边形窗外隐藏");
    }

    // ================= 种子填充绑定：圆/多边形缩放不内外翻转 =================
    {
        PaintController ctrl;
        FrameBuffer canvas(kWidth, kHeight);
        auto key = [&](int k) { ctrl.onKey(k, GLFW_PRESS); };
        auto click = [&](int x, int y) {
            ctrl.onMouseMove({x, y});
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseButton(0, GLFW_RELEASE);
        };
        auto drag = [&](int x0, int y0, int x1, int y1) {
            ctrl.onMouseMove({x0, y0});
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseMove({x1, y1});
            ctrl.onMouseButton(0, GLFW_RELEASE);
        };

        // 画圆 r=100，在圆内 (450,300) 做种子填充（绑定到圆）
        key(GLFW_KEY_2);
        drag(400, 300, 500, 300);
        key(GLFW_KEY_5);
        click(450, 300);
        ctrl.render(canvas);
        expect(canvas.pixel(400, 260) == Color::Yellow(), "seed-bind: 圆内种子填充生效");
        expect(canvas.pixel(100, 100) == Color::Black(), "seed-bind: 填充未溢出圆外");

        // 变换选中圆，拖 SE 角柄缩到 0.5：种子应随圆缩到新圆心附近，不填背景
        key(GLFW_KEY_7);
        click(500, 300); // 圆环右轴点选中
        drag(500, 400, 350, 300); // SE 角柄 → 中心 (350,250)、r=50
        ctrl.onMouseMove({700, 550});
        ctrl.render(canvas);
        expect(canvas.pixel(340, 250) == Color::Yellow(),
               "seed-bind: 缩小后圆内仍保留填充色");
        expect(canvas.pixel(450, 300) == Color::Black(),
               "seed-bind: 缩小后圆外未翻转为填充色");

        // 整体平移圆：种子跟随，不留旧位置
        drag(350, 250, 430, 250);
        ctrl.onMouseMove({700, 550});
        ctrl.render(canvas);
        expect(canvas.pixel(430, 240) == Color::Yellow(), "seed-bind: 平移后填充跟随圆");
        expect(canvas.pixel(350, 250) == Color::Black(), "seed-bind: 平移后旧位置已清");

        // 删除圆：绑定在它上面的种子填充一起删除
        key(GLFW_KEY_DELETE);
        ctrl.render(canvas);
        expect(canvas.pixel(430, 240) == Color::Black(), "seed-bind: 删除圆后填充一起删除");
    }
    {
        // 多边形：非等比缩放时种子也应保持在内部（绑定+取整钳制）
        PaintController ctrl;
        FrameBuffer canvas(kWidth, kHeight);
        auto key = [&](int k) { ctrl.onKey(k, GLFW_PRESS); };
        auto click = [&](int x, int y) {
            ctrl.onMouseMove({x, y});
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseButton(0, GLFW_RELEASE);
        };
        auto drag = [&](int x0, int y0, int x1, int y1) {
            ctrl.onMouseMove({x0, y0});
            ctrl.onMouseButton(0, GLFW_PRESS);
            ctrl.onMouseMove({x1, y1});
            ctrl.onMouseButton(0, GLFW_RELEASE);
        };

        key(GLFW_KEY_4);
        key(GLFW_KEY_F); // 关闭多边形自身填充，只留轮廓
        click(100, 100);
        click(300, 100);
        click(300, 250);
        click(100, 250);
        ctrl.onMouseButton(1, GLFW_PRESS); // 右键闭合
        key(GLFW_KEY_5);
        click(200, 175); // 多边形内种子填充
        ctrl.render(canvas);
        expect(canvas.pixel(200, 150) == Color::Yellow(), "seed-bind: 多边形内种子填充生效");

        key(GLFW_KEY_7);
        click(200, 100); // 上边选中多边形
        drag(300, 175, 200, 175); // E 边柄水平缩到 0.5
        ctrl.onMouseMove({700, 550});
        ctrl.render(canvas);
        expect(canvas.pixel(150, 150) == Color::Yellow(),
               "seed-bind: 多边形非等比缩小后填充仍在内部");
        expect(canvas.pixel(250, 175) == Color::Black(),
               "seed-bind: 多边形缩小后旧区域未翻色");
    }

    std::cout << "smoke: " << checks << " checks, " << fails << " failed" << std::endl;
    return fails;
}

bool writePPM(const char* path, const FrameBuffer& fb) {
    return cg::writePPM(path, fb);
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

    // ⑤ Bezier 曲线（实验四）：de Casteljau 采样折线 + 控制多边形（灰虚线）
    const std::vector<FPoint> bctrl = {{40.0f, 320.0f}, {100.0f, 430.0f}, {170.0f, 430.0f},
                                       {230.0f, 320.0f}};
    for (std::size_t i = 0; i + 1 < bctrl.size(); ++i) {
        drawLine(fb, {static_cast<int>(bctrl[i].x), static_cast<int>(bctrl[i].y)},
                 {static_cast<int>(bctrl[i + 1].x), static_cast<int>(bctrl[i + 1].y)},
                 Color(120, 120, 130), {0xF0F0u, 1});
    }
    {
        const std::vector<FPoint> samples = curves::sampleBezier(bctrl, 96);
        for (std::size_t i = 0; i + 1 < samples.size(); ++i) {
            drawLine(fb, {static_cast<int>(std::lround(samples[i].x)),
                          static_cast<int>(std::lround(samples[i].y))},
                     {static_cast<int>(std::lround(samples[i + 1].x)),
                      static_cast<int>(std::lround(samples[i + 1].y))},
                     Color::Yellow(), {0xFFFFu, 3});
        }
    }

    // ⑥ B 样条（实验四挑战题）：clamped 三次，5 控制点（过首末点）
    const std::vector<FPoint> sctrl = {{40.0f, 480.0f}, {85.0f, 560.0f}, {140.0f, 470.0f},
                                       {195.0f, 560.0f}, {240.0f, 480.0f}};
    for (std::size_t i = 0; i + 1 < sctrl.size(); ++i) {
        drawLine(fb, {static_cast<int>(sctrl[i].x), static_cast<int>(sctrl[i].y)},
                 {static_cast<int>(sctrl[i + 1].x), static_cast<int>(sctrl[i + 1].y)},
                 Color(120, 120, 130), {0xF0F0u, 1});
    }
    {
        const std::vector<FPoint> samples = curves::sampleBSpline(sctrl, 3, 96);
        for (std::size_t i = 0; i + 1 < samples.size(); ++i) {
            drawLine(fb, {static_cast<int>(std::lround(samples[i].x)),
                          static_cast<int>(std::lround(samples[i].y))},
                     {static_cast<int>(std::lround(samples[i + 1].x)),
                      static_cast<int>(std::lround(samples[i + 1].y))},
                     Color::Cyan(), {0xFFFFu, 3});
        }
    }

    return cg::writePPM(path, fb);
}

} // namespace cg::tests
