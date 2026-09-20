#pragma once
#include "core/FrameBuffer.hpp"
#include "core/Point.hpp"
#include "raster/Clip.hpp"
#include "raster/Rasterizer.hpp"
#include <memory>
#include <string>
#include <vector>

namespace cg {

// 交互控制器：把鼠标键盘事件翻译成"图形/填充/裁剪"操作，并按两层渲染：
//   · 提交层（committed_）：图形列表的完整重绘结果，只在内容变化时重算一次
//     （种子填充这类重算法不必每帧重放；窗口缩放、重绘都不丢内容）
//   · 预览层：每帧在提交层之上叠加当前操作的实时预览与光标十字
// 图形列表按顺序重放 —— 后续实验（选中、变换、曲线）都基于这个列表。
class PaintController {
public:
    // key 用 GLFW 键码；button: 0=左键 1=右键；action: 0=释放 1=按下
    void onKey(int key, int action);
    void onMouseButton(int button, int action);
    void onMouseMove(IPoint pos);

    // 每帧调用：必要时重绘提交层，然后拷贝 + 叠加预览
    void render(FrameBuffer& fb);

    // 窗口标题（当前模式/属性/快捷键提示）
    std::string title() const;

private:
    enum class Mode { Line = 0, Circle = 1, Arc = 2, Polygon = 3, SeedFill = 4, Clip = 5 };

    // 一个已完成、可重放的图形
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

    // ---- 绘制 ----
    raster::LineStyle currentStyle() const;
    Color currentFillColor() const;
    void drawShape(FrameBuffer& fb, const Shape& s) const;
    void drawClipWindow(FrameBuffer& fb) const;
    void drawPreview(FrameBuffer& fb) const;
    void commitRender(); // 图形列表 → committed_
    bool clipLine(IPoint& a, IPoint& b) const;
    const char* clipAlgoName() const;

    // ---- 状态变更 ----
    void markDirty() { dirty_ = true; }
    void closePolygon();
    void cancelInProgress();
    void setRectClipWindow(IPoint p1, IPoint p2);

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
    int clipAlgoIdx_ = 0;       // 0=C-S，1=中点分割（矩形窗口时有效）
    bool clipRectEdit_ = false; // K：拖拽定义矩形窗口
    bool clipPolyEdit_ = false; // P：连点定义凸多边形窗口
    std::vector<IPoint> pendingClipWin_;

    // 提交层缓存
    std::unique_ptr<FrameBuffer> committed_;
    bool dirty_ = true;
};

} // namespace cg
