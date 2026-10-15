// PaintController.hpp
// 实现交互控制器：把鼠标键盘事件翻译成绘图/填充/裁剪/变换操作，两层渲染（提交层缓存 + 每帧预览），最终像素交给 FrameBuffer。
#pragma once
#include "core/FrameBuffer.hpp"
#include "core/Point.hpp"
#include "geom/Transform.hpp"
#include "raster/Clip.hpp"
#include "raster/Rasterizer.hpp"
#include <memory>
#include <string>
#include <vector>

namespace cg {

// 交互控制器：把鼠标键盘事件翻译成"图形/填充/裁剪/变换"操作，并按两层渲染：
//   · 提交层（committed_）：图形列表的完整重绘结果，只在内容变化时重算一次
//     （种子填充这类重算法不必每帧重放；窗口缩放、重绘都不丢内容）
//   · 预览层：每帧在提交层之上叠加当前操作的实时预览与光标十字
// 图形列表按顺序重放 —— 变换直接改列表里的参数（端点/圆心/半径/顶点表），像素随之重算。
class PaintController {
public:
    // key 用 GLFW 键码；button: 0=左键 1=右键；action: 0=释放 1=按下；mods: GLFW 修饰键位集
    void onKey(int key, int action);
    void onMouseButton(int button, int action, int mods = 0);
    void onMouseMove(IPoint pos);

    // 每帧调用：必要时重绘提交层，然后拷贝 + 叠加预览
    void render(FrameBuffer& fb);

    // 窗口标题（当前模式/属性/快捷键提示）
    std::string title() const;

private:
    enum class Mode { Line = 0, Circle = 1, Arc = 2, Polygon = 3, SeedFill = 4, Clip = 5,
                      Transform = 6 };

    // 一个已完成、可重放的图形（参数化存储：变换改这里，像素是重算结果）
    struct Shape {
        Mode mode = Mode::Line;
        IPoint a{};                  // Line: 起点；Circle/Arc: 圆心；SeedFill: 种子点
        IPoint b{};                  // Line: 终点；Clip: 原线段终点
        int radius = 0;              // Circle / Arc
        float startDeg = 0.0f;       // Arc
        float endDeg = 360.0f;       // Arc / Circle（整圆）
        std::vector<IPoint> polygon; // Polygon: 顶点表
        bool filled = true;          // Polygon: 填充 or 仅轮廓
        raster::LineStyle style;
        Color color = Color::White();
    };

    // 裁剪窗口：矩形或任意凸多边形（挑战题）
    struct ClipWindow {
        enum class Kind { Rect, ConvexPoly };
        Kind kind = Kind::Rect;
        raster::Rect rect;
        std::vector<FPoint> poly;
        bool valid = false;
    };

    // ---- 自绘任务栏（无界面框架，全部经 putPixel 绘制；键盘与按钮共用 handleAction） ----
    enum class Act {
        None = 0,
        ModeLine, ModeCircle, ModeArc, ModePolygon, ModeSeed, ModeClip, ModeXform,
        Style, Width, Color, Clear,
        PolyFill, PolyClose,
        ClipRect, ClipPoly, ClipAlgo, ClipToggle,
        ScaleUp, ScaleDown, RotCW, RotCCW, RefPoint, Dock,
        NudgeL, NudgeR, NudgeU, NudgeD,
    };
    struct ToolButton {
        Act act = Act::None;
        std::string label;
        int x = 0, y = 0, w = 0, h = 0;
        bool active = false; // 当前模式/状态高亮
    };

    // ---- 绘制 ----
    raster::LineStyle currentStyle() const;
    Color currentFillColor() const;
    void drawShape(FrameBuffer& fb, const Shape& s) const;
    void drawClipWindow(FrameBuffer& fb, const Color& c, bool dashed) const;
    void drawPreview(FrameBuffer& fb) const;
    void commitRender(); // 图形列表 → committed_
    void copyCommittedClipped(FrameBuffer& fb) const; // 常驻裁剪：窗口内照抄，窗口外保持背景
    bool clipLine(IPoint& a, IPoint& b) const;
    const char* clipAlgoName() const;

    // ---- 变换（实验三） ----
    bool shapeSelectable(const Shape& s) const;
    FPoint shapeCentroid(const Shape& s) const;    // 线=中点 圆/弧=圆心 多边形=顶点平均
    FPoint combinedCentroid() const;               // 选中集合的整体参考点
    FPoint activeRefPoint() const;                 // 手动参考点（未指定时回退重心）
    void transformSelected(const geom::Mat3& m, float rotDeg, float scaleF);
    static void applyToShape(Shape& s, const geom::Mat3& m, float rotDeg, float scaleF);
    int hitShape(IPoint p) const;                  // 返回命中的图形下标，无命中 -1
    bool shapeIntersectsRect(const Shape& s, const raster::Rect& r) const; // 框选：图形与矩形相交
    // 点击点吸附到某图形轮廓的最近点（对接点），返回该图形下标；skipIdx 用于选 B 时跳过 A
    int pickOutlineShape(IPoint p, FPoint& outPt, FPoint& outTan, int skipIdx = -1) const;

    // ---- 状态变更 ----
    void markDirty() { dirty_ = true; }
    void closePolygon();
    void cancelInProgress();
    void setRectClipWindow(IPoint p1, IPoint p2);

    // ---- 任务栏 ----
    void handleAction(Act a);                        // 键盘与按钮的统一操作入口
    void buildToolbar(std::vector<ToolButton>& btns) const;
    void drawToolbar(FrameBuffer& fb) const;
    bool toolbarPress(IPoint p);                     // 命中按钮则执行并返回 true
    static constexpr int kToolbarH = 46;             // 顶部任务栏总高（像素）

    Mode mode_ = Mode::Line;
    int styleIdx_ = 0;     // 0/1/2 → 实线/虚线/点线
    int widthIdx_ = 0;     // 0/1/2 → 1/3/5
    int fillColorIdx_ = 0; // 填充色调色板下标
    std::vector<Shape> shapes_;

    // 拖拽 / 圆弧 / 多边形输入状态
    bool dragging_ = false;
    bool arcAdjust_ = false;
    IPoint press_{};
    IPoint curr_{};
    IPoint arcCenter_{};
    int arcRadius_ = 0;
    float arcStart_ = 0.0f;
    std::vector<IPoint> pendingPolygon_;
    bool polygonFilled_ = true;

    // 裁剪状态
    ClipWindow clipWin_;
    bool clipOn_ = false;       // 常驻裁剪开关：开=所有模式只显示窗口内内容（V/任务栏切换）
    int clipAlgoIdx_ = 0;       // 0=C-S，1=中点分割（矩形窗口时有效）
    bool clipRectEdit_ = false; // K：拖拽定义矩形窗口
    bool clipPolyEdit_ = false; // P：连点定义凸多边形窗口
    std::vector<IPoint> pendingClipWin_;

    // 变换状态（实验三）
    std::vector<int> selected_;  // 选中图形的下标（shapes_ 内）
    bool shiftHeld_ = false;     // 最近一次鼠标事件是否带 Shift（多选）
    bool xformDrag_ = false;     // 拖拽平移进行中
    IPoint lastDrag_{};          // 上一次增量平移的参考位置
    bool bandSelect_ = false;    // 框选拖拽进行中（空白处按下）
    IPoint bandStart_{};         // 框选起点
    bool refManual_ = false;     // 参考点：false=重心（默认） true=手动指定
    bool refPick_ = false;       // 等待左键点击指定参考点
    IPoint refPoint_{};          // 手动参考点
    int dockStage_ = 0;          // 对接流程：0=未开始 1=选 A 的对接点 2=选 B 的对接点
    int dockAShapeIdx_ = -1;     // 图形 A 的下标
    FPoint dockA_{};             // A 上的对接点
    FPoint dockATangent_{};      // A 对接点的单位切向

    // 提交层缓存
    std::unique_ptr<FrameBuffer> committed_;
    bool dirty_ = true;
};

} // namespace cg
