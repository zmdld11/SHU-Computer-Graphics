# 协作规范（CONTRIBUTING）

> 本仓库的核心工作流：**issue 驱动**。任何修改（新功能、重构、修 bug、写文档）都从一条 GitHub issue 开始。

## 一、修改流程（必须遵守）

```
建 issue → 建 issue → 建 issue → …… → 逐项完成 → 写小结 → 关闭 issue → 合并 main
```

### 1. 先建 issue（一个修改计划 = 一个 issue）

动手改代码**之前**，在 GitHub 上建 issue，写清：

- **目标**：这次要改成什么样
- **范围**：动哪些模块/文件，不动什么
- **验收标准**：怎么算做完（功能演示点、测试、性能等）

issue 标题用动词开头，如 `实现任意斜率 Bresenham 直线算法`。

### 2. 过程记录在 issue 里

实现过程中遇到的**任何卡点、设计取舍、意外发现**（算法 bug、环境坑、与课件不一致的约定），及时以**评论**形式追加到对应 issue 下——这是给队友和期末写报告留素材，报告的"算法设计""结果分析"章节直接受益。

### 3. 关闭前写小结

完成时在 issue 里追加一条**小结评论**，固定格式：

```markdown
## 小结
- 改动内容：（列点，对应 issue 目标逐条勾销）
- 关键文件：`src/raster/line.cpp` 等
- 踩坑记录：（无则写"无"）
- 遗留问题：（无则写"无"）
```

然后关闭 issue。

## 二、分支与合并

- `main` 分支**随时可构建、可运行**，禁止提交编译不过的代码
- 每个 issue 开一个短分支：`feat/lab1-line`、`fix/clip-bug`、`docs/report` 等
- 完成后合回 `main`（PR 或本地 merge 后 push 均可）；merge 前自测通过
- 期末项目代码同样走此流程（如 `feat/anim-scene1`）

## 三、Commit 规范

Conventional Commits，**必须引用对应 issue 号**：

```
feat(raster): 实现任意斜率 Bresenham 算法 #12
fix(ui): 修复多选图形后旋转参考点错误 #18
docs: 补充实验二验收记录 #7
chore: 调整 CMake 警告级别 #3
```

类型：`feat` / `fix` / `docs` / `refactor` / `test` / `chore`。一个 issue 通常对应 1~5 个 commit，每个 commit 保持可编译。

## 四、角色分工

- **仓库主管**：组长（owner）。负责建库、成员权限、main 保护、issue 分派与验收、最终提交物打包。
- **成员**：按分派认领 issue，遵守上述流程。
- **AI 助手（ZCode）**：受主管委托执行修改；可自主 commit / push；同样严格遵守 issue 流程（改动前建 issue、过程记录、小结关闭）。

## 五、代码约定（骨架建立时在此补充）

- 命名、目录分层见 README「目录结构」；算法层不 include UI 层头文件
- （待补充：代码风格、测试方式、评测脚本等）
