# AGENTS.md —— 给在本仓库工作的 AI 编程助手

> 本文件面向组员使用的 AI 编程助手（ZCode / Claude Code / Codex / Cursor 等，多数会自动读取本文件）。
> 人类组员看 [README](README.md) 和 [CONTRIBUTING](CONTRIBUTING.md)。仓库主管：@zmdld11。

## 仓库背景

- 上海大学《计算机图形学》(08306027) 课程仓库：**实验一~四做成同一个"简易绘图引擎"**，期末项目是在引擎之上做二维动画。
- 技术栈：C++17 / CMake(≥3.20) / OpenGL 3.3（GLFW + GLAD，CMake FetchContent 自动拉取，首次 configure 需联网）。
- **课程红线：光栅化算法必须手写**（Bresenham、扫描线填充、裁剪、de Casteljau 等），禁止用图形库现成画线/填充函数顶替实验算法——这是挂科级错误。

## 架构红线

```
src/core     framebuffer（putPixel 是唯一画点入口）、Canvas 上屏
src/raster   图元生成算法        ┐
src/geom     变换矩阵            ├─ 算法层：不得 include src/ui 的任何头文件
src/curves   Bezier/B样条        ┘
src/ui       GLFW 窗口、事件转发、菜单 —— 只调下层接口，不写算法
```

上层依赖下层，禁止反向依赖。期末动画模块只能通过公开接口驱动引擎。

## 必须遵守的工作流（详见 CONTRIBUTING.md）

1. **动手前先看 issue**：领任务 = 认领 GitHub issue；没有对应 issue 就先建（写清 目标 / 范围 / 验收标准），标题动词开头。**主任务（实验一~五、期末）之外的修改也必须建 issue，打 `extension` 标签**。
2. **一个 issue 一个短分支**：`feat/xxx`、`fix/xxx`、`docs/xxx`，从最新 `main` 切出。
3. **commit 规范**：Conventional Commits + 引用 issue 号，如 `feat(raster): 实现任意斜率 Bresenham #2`。保持每个 commit 可编译。
4. **过程留痕**：卡点、设计取舍、与课件不一致的约定，评论到对应 issue。
5. **完成后**：issue 里写「小结」（改动内容 / 关键文件 / 踩坑 / 遗留），再开 PR（描述含 `Closes #N`）合入 main。
6. **main 随时可构建**：禁止推送编译不过的代码；合并前必须本地完整构建 + 自测通过。main 已开分支保护：禁止强推/删除，只接受 squash 或 rebase 合并（保持线性历史）。
7. 改别人负责的模块（实验分工见 issue 认领）前，先在 issue 里 @ 对方说明。

## 构建与自测（Windows；MSVC 或 MinGW 皆可）

```bash
cmake -B build                        # 首次会联网拉 GLFW/glad
cmake --build build --config Release
./build/cg-engine                     # 出现画布窗口 + 自检图案（绿色十字+四角红点）
./build/cg-engine --smoke             # 无窗口自检，退出码 0 = 通过
```

- CLion：直接 Open 仓库根目录（自带 CMake/Ninja，工具链选 MSVC 或 MinGW）。
- Visual Studio：文件 → 打开 → CMake。

## 代码约定

- 注释、issue、commit message 用中文；标识符用英文。
- 算法实现放 `raster/geom/curves`；想画任何东西最终都走 `FrameBuffer::putPixel`。
- 占位头文件里的接口是初稿，实验实现时可调整，**调整理由写进对应 issue**。
- **禁引入 Qt / MFC / ImGui / WebView 等任何界面或前端框架**（老师强调算法与前端低耦合）；外部依赖只有 GLFW（窗口事件）与 OpenGL/glad（上屏），见 README 依赖白名单。不改动 `CMakeLists.txt` 的依赖部分，除非 issue 里说明。
- 每个实验（任务）实现完成并合入 main 后打 tag：`lab1`、`lab2`…（老师要求所有实验在一个程序里，tag 即分任务提交节点）。

## 安全红线

- 永不提交：构建产物（`.gitignore` 已覆盖）、任何人的 token/密码、老师的课件原件（PPT/PDF 不进仓库）。
- token 只放在本机凭据管理（如 `gh auth login`），不写进代码、配置或聊天记录。
