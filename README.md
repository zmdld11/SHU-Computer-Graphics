# SHU-Computer-Graphics —— 计算机图形学实验与课程项目

> 上海大学 2026-2027 秋季学期《计算机图形学》08306027

本仓库承载 **实验一~四（必做）+ 实验五（选做）** 的统一"简易绘图引擎"，以及在此基础上开发的**期末课程项目（二维动画程序）**。

## 技术栈

| 项 | 选择 | 说明 |
|---|---|---|
| 语言 | C++17 | |
| 构建 | CMake ≥ 3.20 | 一份 `CMakeLists.txt`，所有 IDE/命令行通用 |
| 图形库 | OpenGL 3.3 + GLFW + GLAD | GLFW 仅作"显示画布"，**所有光栅化算法手写**（Bresenham 等），符合实验考察点 |
| 依赖管理 | FetchContent(GLFW) + 入库(glad) | GLFW 首次构建联网自动拉取；glad 源码已在 `third_party/`，无 Python 依赖 |

## 构建与运行

```bash
# 方式一：命令行（首次会联网下载 GLFW）
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 方式二：CLion —— 直接用 Open 打开本目录（自带 CMake/Ninja）
# 方式三：Visual Studio —— 文件→打开→CMake，选择本目录
```

```bash
./build/cg-engine            # 交互绘图（Windows 下为 build/cg-engine.exe）
./build/cg-engine --smoke    # 无窗口算法自测（11 项断言，退出码 0 = 通过）
```

> Windows 工具链：MSVC (VS2022) 或 MinGW 皆可。

### 操作说明（实验一）

| 输入 | 功能 |
|---|---|
| `1` / `2` / `3` | 切换 直线 / 圆 / 圆弧 模式 |
| 左键拖拽 | 画线、画圆（实时预览）；圆弧模式下定圆心、半径、起始角 |
| 移动鼠标 + 左键 | 圆弧第二阶段：预览终止角，左键确认 |
| `S` | 线型：实线 / 虚线 / 点线 |
| `W` | 线宽：1 / 3 / 5（方形刷子） |
| `E` / 右键 | 清屏 / 取消当前操作 |
| 拖窗口边缘 | 调整画板大小（内容清空） |
| `ESC` | 退出 |

当前模式与属性实时显示在窗口标题栏。

## 目录结构

```
SHU-Computer-Graphics/
├── AGENTS.md          # 组员 AI 助手工作指引（AI 编程工具自动读取）
├── CONTRIBUTING.md    # 协作规范：issue 驱动工作流
├── docs/              # 实验要求摘要、架构设计（类图与依赖）
├── src/
│   ├── core/          # FrameBuffer（putPixel 唯一画点入口）、Canvas 上屏
│   ├── raster/        # 图元生成算法：直线/圆/圆弧已实现（实验一）；填充/裁剪待做（实验二）
│   ├── geom/          # 变换矩阵（实验三，接口占位）
│   ├── curves/        # Bezier / de Casteljau（实验四，接口占位）
│   ├── ui/            # GLFW 窗口 + PaintController 交互状态机
│   └── main.cpp
└── third_party/
    └── glad/          # 预生成的 OpenGL 函数加载器源码
```

**关于 `third_party/`**：存放不由我们维护、但随仓库一起编译的第三方源码。目前只有 glad——Windows 上 OpenGL 函数地址须运行时获取，glad 负责加载这些函数指针（`Canvas` 是引擎里唯一使用它的地方）。官方用法要求构建时用生成器现生成（每台机器都得装 Python + jinja2），故改为**把生成好的 3 个文件直接入库**，队友克隆即可构建；换 GL 版本的重新生成方法写在 `CMakeLists.txt` 注释里。

**架构红线**：算法层（raster/geom/curves）不依赖 UI 层，绘图只经 `FrameBuffer::putPixel`；期末动画项目只调底层接口。

**课程要求原文**（老师实验注意事项，做任何改动前对照一遍）：

> 在进行实验一到四的过程中，充分考虑整个系统底层数据存储类型和结构层次设计，基础功能的封装和接口设计，使其成为一个简易绘图引擎。后续能够支持实现上层具体动画效果，即最终的课程设计需要依赖前期基础架构。考虑具体功能函数与前端界面的低耦合。

**依赖白名单**（低耦合的落地红线：全项目只允许以下外部依赖，**禁引入 Qt / MFC / ImGui / WebView 等任何界面或前端框架**，UI 只做事件转发）：

| 依赖 | 用途 | 使用边界 |
|---|---|---|
| GLFW | 窗口与鼠标键盘事件 | 仅 `src/ui/Window.cpp` 使用 |
| OpenGL 3.3 + glad | 把 framebuffer 贴上屏 | 仅 `src/core/Canvas.cpp` 使用 |
| CMake | 构建 | — |

## 实验验收清单

- [x] 实验一：中点/Bresenham 直线（任意斜率）、圆弧中点算法、线型线宽（已完成，tag `lab1`）
- [ ] 实验二：扫描线多边形填充、扫描线种子填充、线段裁剪
- [ ] 实验三：平移/放缩/旋转，多选图形，重心及任意参考点
- [ ] 实验四：n 阶 Bezier（de Casteljau）+ 鼠标控制调整
- [ ] 实验五（选做）：模型导入 + Lambert/Phong 渲染

四个任务**最后统一验收**（老师确认）；统一验收通过后源码 + 可运行文件打包上传**超星平台**。

## 关键时间线

| 节点 | 事项 |
|---|---|
| 第 6 周 | 提交项目选题简要说明 + 课堂汇报 |
| 约 2026 年 11 月 | 提交：源码 + 报告 PDF（≤15 页，模板见课程附件）+ 演示解说录屏 + 纸质报告 |
| 期末 | 书面考试 |

评分：**程序 50 分 + 报告 50 分**。

## 协作规范

见 [CONTRIBUTING.md](CONTRIBUTING.md) —— 修改一律先建 GitHub issue，一个修改计划一个 issue。
组员使用 AI 编程助手协作时，助手会自动读取 [AGENTS.md](AGENTS.md) 的工作指引。
