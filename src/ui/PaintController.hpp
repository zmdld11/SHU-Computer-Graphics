// PaintController.hpp — 交互控制器：事件 → 绘图/填充/裁剪/变换。
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

// 两层渲染：提交层 committed_ 只在内容变化时重绘，预览层每帧叠加当前操作。
// 图形按参数存储、按顺序重放：变换改参数，像素重算。
class PaintController {
public:
    // key 为 GLFW 键码；button: 0=左键 1=右键；action: 0=释放 1=按下；mods 为修饰键位集。
    void onKey(int key, int action, int mods = 0);
    void onMouseButton(int button, int action, int mods = 0);
    void onMouseMove(IPoint pos, int mods = 0);

    // 每帧调用：必要时重绘提交层，然后拷贝 + 叠加预览
    void render(FrameBuffer& fb);

    // 窗口标题（模式/属性/快捷键），返回缓存引用。
    const std::string& title() const;

    // ---- 自绘任务栏：键盘与按钮共用 handleAction ----
    // 按钮布局公开，测试按标签定位。
    enum class Act {
        None = 0,
        ModeLine, ModeCircle, ModeArc, ModePolygon, ModeSeed, ModeClip, ModeXform, ModeCurve,
        Style, Width, Color, Clear, Undo,
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
        bool active = false;
    };
    std::vector<ToolButton> toolbarButtons() const {
        std::vector<ToolButton> btns;
        buildToolbar(btns);
        return btns;
    }
    int toolbarHeight() const;

private:
    enum class Mode { Line = 0, Circle = 1, Arc = 2, Polygon = 3, SeedFill = 4, Clip = 5,
                      Transform = 6, Curve = 7 };

    // 已完成、可重放的参数化图形。
    struct Shape {
        Mode mode = Mode::Line;
        std::uint64_t id = 0;       // 稳定标识（0=未分配）
        std::uint64_t parentId = 0; // SeedFill 的父图形 id（0=自由种子）
        IPoint a{};                  // Line 起点；Circle/Arc 圆心；SeedFill 种子点
        IPoint b{};                  // Line 终点；Clip 原线段终点
        int radius = 0;
        float startDeg = 0.0f;
        float endDeg = 360.0f;
        std::vector<IPoint> polygon;
        bool filled = true;
        std::vector<FPoint> curve;
        bool bspline = false;        // false=Bezier true=B 样条
        int curveDeg = 3;
        raster::LineStyle style;
        Color color = Color::White();
    };

    // 裁剪窗口：矩形或凸多边形。
    struct ClipWindow {
        enum class Kind { Rect, ConvexPoly };
        Kind kind = Kind::Rect;
        raster::Rect rect;
        std::vector<FPoint> poly;
        bool valid = false;
    };

    // 撤销快照：图形列表是值类型，整份文档深拷贝即可，无需改现有结构。
    struct DocState {
        std::vector<Shape> shapes;
        std::uint64_t nextShapeId = 1;
        ClipWindow clipWin;
        bool clipOn = false;
    };

    // ---- 绘制 ----
    raster::LineStyle currentStyle() const;
    Color currentFillColor() const;
    IPoint lineDragEnd() const; // Shift 吸附 45°
    int dragRadius() const;     // Ctrl 按 kStepPx 步进
    void drawShape(FrameBuffer& fb, const Shape& s) const;
    void drawOutline(FrameBuffer& fb, const Shape& s, const Color& c,
                     const raster::LineStyle& ls) const;
    void drawClipWindow(FrameBuffer& fb, const Color& c, bool dashed) const;
    void drawPreview(FrameBuffer& fb) const;
    void commitRender(); // 列表 → committed_
    void copyCommittedClipped(FrameBuffer& fb);
    void rebuildClipCache();
    bool clipLine(IPoint& a, IPoint& b) const;
    const char* clipAlgoName() const;

    // ---- 变换 ----
    bool shapeSelectable(const Shape& s) const;
    FPoint shapeCentroid(const Shape& s) const;    // 线=中点 圆/弧=圆心 多边形=顶点平均
    FPoint combinedCentroid() const;
    FPoint activeRefPoint() const;                 // 未指定时回退重心
    void transformSelected(const geom::Mat3& m, float rotDeg, float scaleF);
    void beginXformDrag(IPoint anchor);
    void beginRotateDrag();              // 右键拖拽旋转：记录参考点与起始极角
    void applyRotateDrag(IPoint pos);    // 从拖拽起点快照重算旋转姿态
    void snapshotSelection();           // 选中参数快照（防整数漂移）
    static void applyToShape(Shape& s, const geom::Mat3& m, float rotDeg, float scaleF);
    // ---- 种子填充绑定 ----
    void addShape(Shape s);
    std::uint64_t findSeedParent(IPoint p) const;        // 包含 p 的最上层闭合图形
    void snapshotAttachedSeeds();
    void transformSeedsOfParent(std::uint64_t parentId, const geom::Mat3& m,
                                bool useSnapshot);       // 变换某图形的附加种子
    void transformAttachedSeedsForSelection(const geom::Mat3& m, bool useSnapshot);
    IPoint clampSeedToParent(IPoint seed, const Shape& parent) const; // 钳回父图形内
    int hitShape(IPoint p) const;                  // 命中的图形下标，无则 -1
    bool shapeIntersectsRect(const Shape& s, const raster::Rect& r) const; // 框选相交
    // 吸附到轮廓最近点（对接用）；skipIdx 用于选 B 时跳过 A。
    int pickOutlineShape(IPoint p, FPoint& outPt, FPoint& outTan, int skipIdx = -1) const;

    // ---- 画板式选择编辑：外接矩形 + 控制柄 ----
    // 控制柄：线段端点柄（仅单选线段），或外接矩形的 8 向柄
    enum class SelHandle { None = 0, EndA, EndB, NW, N, NE, E, SE, S, SW, W };
    raster::Rect shapeBounds(const Shape& s) const;
    raster::Rect selectionBounds() const;
    bool selectionUniformOnly() const;                    // 含圆/弧 → 只允许等比
    bool selEndpointsMode() const;                        // 单选线段 → 端点编辑
    SelHandle pickHandle(IPoint p) const;
    void drawSelection(FrameBuffer& fb) const;            // 高亮 + 外接矩形 + 控制柄
    void drawHandleBox(FrameBuffer& fb, IPoint c, bool hot) const;
    void applyHandleDrag(IPoint pos);                     // 从快照重算选中图形

    // ---- 状态变更 ----
    void markDirty() { dirty_ = true; }
    void switchMode(Mode m); // 切换模式并结束所有进行中的输入
    void closePolygon();
    void cancelInProgress();
    void setRectClipWindow(IPoint p1, IPoint p2);

    // ---- 撤销（文档快照栈） ----
    void pushUndo();          // 改动前记录一份文档
    void undo();              // 恢复上一份文档
    void beginGesture();      // 手势开始：下一次真正改动时才压栈
    void ensureGestureUndo(); // 手势内首次改动前压栈
    std::vector<DocState> undoStack_;
    bool gestureUndoPushed_ = false;

    // ---- 曲线 ----
    void commitCurve();
    std::vector<FPoint> curveSamples(const Shape& s) const; // 采样折线（渲染/命中共用）
    std::pair<int, int> pickCurveCtrl(IPoint p) const;
    void drawCurveHandles(FrameBuffer& fb) const;           // 控制多边形 + 手柄

    // ---- 任务栏 ----
    void handleAction(Act a);                        // 键盘与按钮的统一入口
    void buildToolbar(std::vector<ToolButton>& btns) const;
    std::uint64_t toolbarKey(int width) const;
    void renderToolbar(FrameBuffer& fb, const std::vector<ToolButton>& btns) const;
    void drawToolbar(FrameBuffer& fb) const;
    bool toolbarPress(IPoint p);

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

    // 曲线状态
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

    // 变换状态
    std::vector<int> selected_;  // 选中图形的下标（shapes_ 内）
    bool xformDrag_ = false;     // 拖拽平移进行中
    IPoint lastDrag_{};          // 上一次增量平移的参考位置
    bool rotDrag_ = false;       // 右键拖拽旋转进行中
    FPoint rotPivot_{};          // 旋转参考点：拖拽开始时锁定，避免中途重心漂移
    float rotStartAngle_ = 0.0f; // 按下时的极角（弧度，屏幕系顺时针为正）
    bool rotArmed_ = false;      // 光标离参考点足够远后才锁定起始角（防近心抖动）
    float rotDeg_ = 0.0f;        // 当前旋转角度（度，仅用于画布读数）
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

    // 常驻裁剪显示层缓存：窗口内内容 + 黑底；只在内容/窗口/尺寸变化时重建。
    std::unique_ptr<FrameBuffer> clipCache_;
    bool clipCacheDirty_ = true;

    // 任务栏缓存：状态不变时逐行上屏。
    mutable std::unique_ptr<FrameBuffer> toolbarCache_;
    mutable std::vector<ToolButton> toolbarLayout_;
    mutable std::uint64_t toolbarCacheKey_ = ~0ull;
    // 标题缓存：输入事件后才重建。
    mutable std::string titleCache_;
    mutable bool titleDirty_ = true;
};

} // namespace cg
