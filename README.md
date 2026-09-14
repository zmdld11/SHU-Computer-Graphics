# SHU-Computer-Graphics —— 计算机图形学实验与课程项目

> 上海大学 2026-2027 秋季学期《计算机图形学》08306027

本仓库承载 **实验一~四（必做）+ 实验五（选做）** 的统一"简易绘图引擎"，以及在此基础上开发的**期末课程项目（二维动画程序）**。

## 技术栈

| 项 | 选择 | 说明 |
|---|---|---|
| 语言 | C++17/20 | |
| 构建 | CMake ≥ 3.20 | 一份 `CMakeLists.txt`，所有 IDE/命令行通用 |
| 图形库 | OpenGL 3.3 + GLFW + GLAD | GLFW 仅作"显示画布"，**所有光栅化算法手写**（Bresenham 等），符合实验考察点 |
| 依赖管理 | CMake FetchContent | 队友克隆即用，无需手动装库 |

## 构建方式（任选其一）

```bash
# 方式一：命令行
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 方式二：CLion —— 直接用 Open 打开本目录（自带 CMake/Ninja）
# 方式三：Visual Studio —— 文件→打开→CMake，选择本目录
```

> Windows 工具链：MSVC (VS2022) 或 MinGW 皆可。

## 目录结构（骨架建立后生效）

```
cg-labs/
├── docs/          # 实验要求摘要、设计文档
├── src/
│   ├── core/      # framebuffer、画布、图元基类（底层，与 UI 低耦合）
│   ├── raster/    # 图元生成：直线/圆弧/填充/裁剪算法（实验一、二）
│   ├── geom/      # 变换：矩阵、复合变换（实验三）
│   ├── curves/    # Bezier / de Casteljau（实验四）
│   ├── ui/        # 鼠标键盘交互、界面层（调 core 接口，不写算法）
│   └── main.cpp
├── assets/        # 测试模型文件（实验五）
└── CMakeLists.txt
```

**架构红线**（实验要求原文精神）：算法层不依赖 UI 层；上层动画（期末项目）只调底层接口。开始写代码前先读 `docs/lab1.md` 的架构要求。

## 实验验收清单

- [ ] 实验一：中点/Bresenham 直线（任意斜率）、圆弧中点算法、线型线宽
- [ ] 实验二：扫描线多边形填充、扫描线种子填充、线段裁剪
- [ ] 实验三：平移/放缩/旋转，多选图形，重心及任意参考点
- [ ] 实验四：n 阶 Bezier（de Casteljau）+ 鼠标控制调整
- [ ] 实验五（选做）：模型导入 + Lambert/Phong 渲染

每个实验验收后：源码 + 可运行文件打包上传**超星平台**。

## 关键时间线

| 节点 | 事项 |
|---|---|
| 第 6 周 | 提交项目选题简要说明 + 课堂汇报 |
| 约 2026 年 11 月 | 提交：源码 + 报告 PDF（≤15 页，模板见课程附件）+ 演示解说录屏 + 纸质报告 |
| 期末 | 书面考试 |

评分：**程序 50 分 + 报告 50 分**。

## 协作规范

见 [CONTRIBUTING.md](CONTRIBUTING.md) —— 修改一律先建 GitHub issue，一个修改计划一个 issue。
