// PaintController.hpp
// 实现交互控制器：把鼠标键盘事件翻译成绘图/填充/裁剪/变换操作，两层渲染（提交层缓存 + 每帧预览），最终像素交给 FrameBuffer。
#pragma once
#include "core/FrameBuffer.hpp"
#include "core/Point.hpp"
#include "geom/Transform.hpp"
#include "raster/Clip.hpp"
#include "raster/Rasterizer.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
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
    // （键盘/鼠标/移动三类事件都带 mods，用于 Shift 吸附、Ctrl 步进等约束类交互）
    void onKey(int key, int action, int mods = 0);
    void onMouseButton(int button, int action, int mods = 0);
    void onMouseMove(IPoint pos, int mods = 0);

    // 每帧调用：必要时重绘提交层，然后拷贝 + 叠加预览
    void render(FrameBuffer& fb);

    // 窗口标题（当前模式/属性/快捷键提示）。返回缓存引用，主循环可每帧比较而不重建字符串。
    const std::string& title() const;

    // ---- 自绘任务栏（无界面框架，全部经 putPixel 绘制；键盘与按钮共用 handleAction） ----
    // 操作与按钮描述放在 public：测试按标签定位按钮中心再模拟点击，布局变化不破坏用例
    enum class Act {
        None = 0,
        ModeLine, ModeCircle, ModeArc, ModePolygon, ModeSeed, ModeClip, ModeXform, ModeCurve,
        Style, Width, Color, Clear,
        PolyFill, PolyClose,
        CurveType, CurveClose,
        ClipRect, ClipPoly, ClipAlgo, ClipToggle,
        ScaleUp, ScaleDown, RotCW, RotCCW, RefPoint, Dock,
        NudgeL, NudgeR, NudgeU, NudgeD,
        DeleteSel,
    };
    struct ToolButton {
        Act act = Act::None;
        std::string label;
        int x = 0, y = 0, w = 0, h = 0;
        bool active = false; // 当前模式/状态高亮
    };
    std::vector<ToolButton> toolbarButtons() const { // 当前按钮布局快照
        std::vector<ToolButton> btns;
        buildToolbar(btns);
        return btns;
    }
    int toolbarHeight() const; // 任务栏总高（随字体度量动态计算）

private:
    enum class Mode { Line = 0, Circle = 1, Arc = 2, Polygon = 3, SeedFill = 4, Clip = 5,
                      Transform = 6, Curve = 7 };

    // 一个已完成、可重放的图形（参数化存储：变换改这里，像素是重算结果）
    struct Shape {
        Mode mode = Mode::Line;
        std::uint64_t id = 0;       // 稳定标识（种子填充绑定所属图形用；0=尚未分配）
        std::uint64_t parentId = 0; // SeedFill: 所属闭合图形 id（0=自由种子）
        IPoint a{};                  // Line: 起点；Circle/Arc: 圆心；SeedFill: 种子点
        IPoint b{};                  // Line: 终点；Clip: 原线段终点
        int radius = 0;              // Circle / Arc
        float startDeg = 0.0f;       // Arc
        float endDeg = 360.0f;       // Arc / Circle（整圆）
        std::vector<IPoint> polygon; // Polygon: 顶点表
        bool filled = true;          // Polygon: 填充 or 仅轮廓
        std::vector<FPoint> curve;   // Curve: 控制点表（浮点，拖拽调整平滑）
        bool bspline = false;        // Curve: false=Bezier true=B 样条（挑战题）
        int curveDeg = 3;            // Curve: B 样条阶数（Bezier 阶数=控制点数-1，不用此字段）
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

    // ---- 绘制 ----
    raster::LineStyle currentStyle() const;
    Color currentFillColor() const;
    IPoint lineDragEnd() const; // 直线拖拽终点（Shift：方向吸附 45° 整数倍）
    int dragRadius() const;     // 圆/弧拖拽半径（Ctrl：kStepPx 步进）
    void drawShape(FrameBuffer& fb, const Shape& s) const;
    void drawOutline(FrameBuffer& fb, const Shape& s, const Color& c,
                     const raster::LineStyle& ls) const; // 线/圆/弧/多边形/曲线的轮廓重描
    void drawClipWindow(FrameBuffer& fb, const Color& c, bool dashed) const;
    void drawPreview(FrameBuffer& fb) const;
    void commitRender(); // 图形列表 → committed_
    void copyCommittedClipped(FrameBuffer& fb); // 常驻裁剪：窗口内照抄，窗口外保持背景
    void rebuildClipCache();                    // 只在提交层/裁剪窗变化时重建掩码缓存
    bool clipLine(IPoint& a, IPoint& b) const;
    const char* clipAlgoName() const;

    // ---- 变换（实验三） ----
    bool shapeSelectable(const Shape& s) const;
    FPoint shapeCentroid(const Shape& s) const;    // 线=中点 圆/弧=圆心 多边形=顶点平均
    FPoint combinedCentroid() const;               // 选中集合的整体参考点
    FPoint activeRefPoint() const;                 // 手动参考点（未指定时回退重心）
    void transformSelected(const geom::Mat3& m, float rotDeg, float scaleF);
    void beginXformDrag(IPoint anchor); // 开始整体拖拽：记录锚点并快照选中集合
    void snapshotSelection();           // 选中图形参数快照（拖拽期间从快照重算，防整型漂移）
    static void applyToShape(Shape& s, const geom::Mat3& m, float rotDeg, float scaleF);
    // ---- 种子填充绑定（圆/多边形缩放时种子随图形走） ----
    void addShape(Shape s);                              // 入列表并分配稳定 id
    std::uint64_t findSeedParent(IPoint p) const;        // 包含 p 的最上层闭合图形 id
    void snapshotAttachedSeeds();                        // 拖拽开始：快照选中图形的附加种子
    void transformSeedsOfParent(std::uint64_t parentId, const geom::Mat3& m,
                                bool useSnapshot);       // 变换某图形的附加种子
    void transformAttachedSeedsForSelection(const geom::Mat3& m, bool useSnapshot);
    IPoint clampSeedToParent(IPoint seed, const Shape& parent) const; // 防取整落到图形外
    int hitShape(IPoint p) const;                  // 返回命中的图形下标，无命中 -1
    bool shapeIntersectsRect(const Shape& s, const raster::Rect& r) const; // 框选：图形与矩形相交
    // 点击点吸附到某图形轮廓的最近点（对接点），返回该图形下标；skipIdx 用于选 B 时跳过 A
    int pickOutlineShape(IPoint p, FPoint& outPt, FPoint& outTan, int skipIdx = -1) const;

    // ---- 画板式选择编辑（实验三增强：外接矩形 + 控制柄 + 高亮） ----
    // 选中控制柄：线段端点柄（仅单选线段时出现），或选中集合外接矩形的 8 向柄
    // （选中含圆/弧时只给 4 角柄 = 等比缩放，无椭圆图元表达非等比结果）
    enum class SelHandle { None = 0, EndA, EndB, NW, N, NE, E, SE, S, SW, W };
    raster::Rect shapeBounds(const Shape& s) const;       // 单图形最小外接矩形
    raster::Rect selectionBounds() const;                 // 选中集合外接矩形（并集）
    bool selectionUniformOnly() const;                    // 选中含圆/弧 → 只允许等比
    bool selEndpointsMode() const;                        // 恰好单选一条线段 → 端点编辑模式
    SelHandle pickHandle(IPoint p) const;                 // 命中控制柄（先于图形命中）
    void drawSelection(FrameBuffer& fb) const;            // 高亮重描 + 虚线外接矩形 + 控制柄
    void drawHandleBox(FrameBuffer& fb, IPoint c, bool hot) const; // 实心方柄
    void applyHandleDrag(IPoint pos);                     // 柄拖拽中：从快照重算选中图形

    // ---- 状态变更 ----
    void markDirty() { dirty_ = true; }
    void switchMode(Mode m); // 切换模式并结束所有进行中的输入
    void closePolygon();
    void cancelInProgress();
    void setRectClipWindow(IPoint p1, IPoint p2);

    // ---- 曲线（实验四） ----
    void commitCurve();                                    // 待定控制点 → 已提交曲线
    std::vector<FPoint> curveSamples(const Shape& s) const; // 曲线采样折线（渲染/命中共用）
    std::pair<int, int> pickCurveCtrl(IPoint p) const;      // 命中的(图形下标,控制点下标)
    void drawCurveHandles(FrameBuffer& fb) const;           // 曲线模式：控制多边形+控制点手柄

    // ---- 任务栏 ----
    void handleAction(Act a);                        // 键盘与按钮的统一操作入口
    void buildToolbar(std::vector<ToolButton>& btns) const;
    std::uint64_t toolbarKey(int width) const;       // 影响任务栏布局/文字的状态指纹
    void renderToolbar(FrameBuffer& fb, const std::vector<ToolButton>& btns) const;
    void drawToolbar(FrameBuffer& fb) const;
    bool toolbarPress(IPoint p);                     // 命中按钮则执行并返回 true

    Mode mode_ = Mode::Line;
    int styleIdx_ = 0;     // 0/1/2 → 实线/虚线/点线
    int widthIdx_ = 0;     // 0/1/2 → 1/3/5
    int fillColorIdx_ = 0; // 填充色调色板下标
    std::vector<Shape> shapes_;
    std::uint64_t nextShapeId_ = 1; // 稳定 id 分配器（不随删除/下标变化）
    bool shiftHeld_ = false; // 最近一次事件的 Shift 状态（多选 / 45° 吸附）
    bool ctrlHeld_ = false;  // 最近一次事件的 Ctrl 状态（定步长缩放）

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

    // 曲线状态（实验四）
    std::vector<IPoint> pendingCurve_; // 连点中的控制点（左键加点，右键提交）
    bool curveBSpline_ = false;        // 新曲线类型：false=Bezier true=B 样条（B 键切换）
    bool curveDrag_ = false;           // 拖拽已提交曲线的控制点进行中
    int dragShapeIdx_ = -1;            // 被拖拽控制点所属图形下标
    int dragCtrlIdx_ = -1;             // 被拖拽的控制点下标

    // 裁剪状态
    ClipWindow clipWin_;
    bool clipOn_ = false;       // 常驻裁剪开关：开=所有模式只显示窗口内内容（V/任务栏切换）
    int clipAlgoIdx_ = 0;       // 0=C-S，1=中点分割（矩形窗口时有效）
    bool clipRectEdit_ = false; // K：拖拽定义矩形窗口
    bool clipPolyEdit_ = false; // P：连点定义凸多边形窗口
    std::vector<IPoint> pendingClipWin_;

    // 变换状态（实验三）
    std::vector<int> selected_;  // 选中图形的下标（shapes_ 内）
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

    // 画板式选择编辑状态
    bool handleDrag_ = false;              // 控制柄拖拽进行中
    SelHandle activeHandle_ = SelHandle::None;
    int handleShapeIdx_ = -1;              // 端点柄所属线段下标
    raster::Rect selBounds0_{};            // 柄拖拽开始时的选中集合外接矩形
    std::vector<Shape> selSnapshot_;       // 柄拖拽开始时选中图形快照（每帧从快照重算，防整型烘焙漂移）
    // 拖拽开始时，选中图形所绑定的种子填充快照：(shapes_ 下标, 初始种子点)
    std::vector<std::pair<int, IPoint>> attachedSeedSnapshot_;

    // 提交层缓存
    std::unique_ptr<FrameBuffer> committed_;
    bool dirty_ = true;

    // 常驻裁剪的显示层缓存：窗口内内容 + 窗口外黑底。只在内容/窗口/尺寸变化时重建，
    // 其余帧直接整块 memcpy 到显示缓冲，避免每帧逐像素掩码。
    std::unique_ptr<FrameBuffer> clipCache_;
    bool clipCacheDirty_ = true;

    // 任务栏缓存：布局和文字只在状态变化时重建，其余帧逐行整段上屏。
    mutable std::unique_ptr<FrameBuffer> toolbarCache_;
    mutable std::vector<ToolButton> toolbarLayout_;
    mutable std::uint64_t toolbarCacheKey_ = ~0ull;
    // 标题缓存：只在键盘/鼠标按键事件后重建，主循环每帧读取不再拼接长字符串。
    mutable std::string titleCache_;
    mutable bool titleDirty_ = true;
};

} // namespace cg
