#pragma once
#include "core/FrameBuffer.hpp"
#include "core/Point.hpp"
#include "raster/Rasterizer.hpp"
#include <string>
#include <vector>

namespace cg {

// 实验一交互控制器：输入状态机（拖拽/圆弧两阶段）+ 图形列表重绘。
// 图形列表按序重画——这是后续实验（选中、变换、曲线）复用的基础。
class PaintController {
public:
    PaintController() = default;

    // key 用 GLFW 键码；button: 0=左键 1=右键；action: 0=释放 1=按下
    void onKey(int key, int action);
    void onMouseButton(int button, int action);
    void onMouseMove(IPoint pos);

    // 每帧调用：清屏后重绘全部图形 + 当前操作预览 + 光标十字
    void render(FrameBuffer& fb) const;

    // 窗口标题（当前模式与操作提示）
    std::string title() const;

private:
    enum class Mode { Line = 0, Circle = 1, Arc = 2 };

    struct Shape {
        Mode mode;
        IPoint a{};   // Line: 起点；Circle/Arc: 圆心
        IPoint b{};   // Line: 终点
        int radius = 0;          // Circle/Arc
        float startDeg = 0.0f;   // Arc
        float endDeg = 360.0f;   // Arc / Circle（整圆）
        raster::LineStyle style;
    };

    raster::LineStyle currentStyle() const;
    void drawShape(FrameBuffer& fb, const Shape& s, const Color& color) const;

    Mode mode_ = Mode::Line;
    int styleIdx_ = 0; // 0 实线 1 虚线 2 点线
    int widthIdx_ = 0; // 0/1/2 -> 1/3/5
    std::vector<Shape> shapes_;

    bool dragging_ = false;    // 左键按下拖拽中
    bool arcAdjust_ = false;   // 圆弧第二阶段：半径已定，调终止角
    IPoint press_{};
    IPoint curr_{};
    IPoint arcCenter_{};
    int arcRadius_ = 0;
    float arcStart_ = 0.0f;
};

} // namespace cg
