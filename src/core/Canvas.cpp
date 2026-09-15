#include "core/Canvas.hpp"
#include "core/FrameBuffer.hpp"
#include <glad/gl.h>
#include <iostream>

namespace cg {
namespace {

// ---------------- 两个"着色器"：跑在 GPU 里的小程序 ----------------
// 顶点着色器：决定"画在哪"。我们只画一个铺满整个窗口的矩形，
// 所以直接把矩形角点的坐标交给 GPU，不做别的计算。
constexpr const char* kVertexShaderSource = R"glsl(#version 330 core
layout(location = 0) in vec2 aPos;   // 矩形角点的屏幕坐标（-1..1）
layout(location = 1) in vec2 aUV;    // 这个角点对应贴图上的位置（0..1）
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)glsl";

// 片段着色器：决定"每个像素涂什么色"。它从贴图（也就是我们的 FrameBuffer）里取色。
// 注意 v 要取反：我们数组第 0 行在画面顶部，OpenGL 贴图第 0 行在底部，方向相反。
constexpr const char* kFragmentShaderSource = R"glsl(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;   // 贴图 = 我们已经画好像素的 FrameBuffer
void main() {
    fragColor = texture(uTex, vec2(vUV.x, 1.0 - vUV.y));
}
)glsl";

// 编译一个着色器小程序；失败时打印显卡驱动给出的原因，返回 0 表示失败
unsigned compileShader(unsigned shaderType, const char* source) {
    const unsigned shaderId = glCreateShader(shaderType); // 向驱动申请一个"着色器"空壳
    glShaderSource(shaderId, 1, &source, nullptr);        // 把上面的源码文本装进去
    glCompileShader(shaderId);                            // 编译
    int ok = 0;
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &ok);      // 查询编译是否成功
    if (ok == 0) {
        char log[512];
        glGetShaderInfoLog(shaderId, sizeof(log), nullptr, log); // 取错误详情
        std::cerr << "[Canvas] shader compile failed: " << log << std::endl;
        return 0;
    }
    return shaderId;
}

} // namespace

Canvas::Canvas(int width, int height, LoaderFn loader) {
    // glad 初始化：把几百个 OpenGL 函数的地址从显卡驱动里查出来。
    // 不做这一步，下面所有 gl 开头的函数都调不动。
    if (loader == nullptr || gladLoadGL((GLADloadfunc)loader) == 0) {
        std::cerr << "[Canvas] gladLoadGL failed" << std::endl;
        return;
    }

    // ① 编译两个着色器，链接成一个"程序"（一条完整的画图流水线）
    const unsigned vertexShader = compileShader(GL_VERTEX_SHADER, kVertexShaderSource);
    const unsigned fragmentShader = compileShader(GL_FRAGMENT_SHADER, kFragmentShaderSource);
    if (vertexShader == 0 || fragmentShader == 0) {
        return;
    }

    program_ = glCreateProgram();             // 创建一个空程序
    glAttachShader(program_, vertexShader);   // 把两个着色器挂进去
    glAttachShader(program_, fragmentShader);
    glLinkProgram(program_);                  // 链接成可执行整体
    glDeleteShader(vertexShader);             // 链接完成后源码对象没用了，释放
    glDeleteShader(fragmentShader);
    int ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok); // 查询链接是否成功
    if (ok == 0) {
        char log[512];
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        std::cerr << "[Canvas] program link failed: " << log << std::endl;
        return;
    }

    // ② 准备"铺满窗口的大矩形"的顶点数据：两个三角形拼成一个矩形。
    // 每行 4 个数 = (x, y, u, v)：x/y 是屏幕位置（-1..1），u/v 是贴图上的取色位置（0..1）
    const float rectangleVertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
    };
    unsigned vertexBuffer = 0;
    glGenVertexArrays(1, &vao_);          // VAO：一个"备忘盒"，记住顶点数据长什么样
    glGenBuffers(1, &vertexBuffer);      // VBO：真正存放顶点数字的显存块
    glBindVertexArray(vao_);             // 打开备忘盒，接下来的配置都记在里面
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(rectangleVertices), rectangleVertices,
                 GL_STATIC_DRAW);        // 把矩形数据从内存搬进显存（只搬这一次）
    // 告诉 GPU 第 0 号属性（位置）：每 2 个 float 一组，从每行开头读
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    // 第 1 号属性（贴图坐标）：跳过开头 2 个 float，再读 2 个
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // ③ 创建贴图（纹理）：GPU 显存里的一张"图片"，尺寸与画板一致
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    // 放大/缩小时取最近的那个像素：保持锐利的像素风，不糊
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // 取色位置超出贴图边界时，夹在边缘不重复
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);                // 先按尺寸占好位，内容每帧再填

    valid_ = true;
}

Canvas::~Canvas() {
    if (program_ != 0) glDeleteProgram(program_);      // 释放显存里的画图程序
    if (texture_ != 0) glDeleteTextures(1, &texture_); // 释放贴图
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);     // 释放顶点备忘盒
}

void Canvas::resize(int width, int height) {
    if (!valid_) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);                // 按新窗口尺寸重建贴图存储
}

void Canvas::present(const FrameBuffer& fb) {
    if (!valid_) {
        return;
    }

    // ⓪ 把我们画好的像素数组从内存上传进贴图——这是 CPU→GPU 的唯一数据通道
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fb.width(), fb.height(), GL_RGBA,
                    GL_UNSIGNED_BYTE, fb.data());

    // ① 让 GPU 画那个大矩形
    glViewport(0, 0, fb.width(), fb.height()); // 输出区域 = 整个窗口
    glClear(GL_COLOR_BUFFER_BIT);              // 擦掉上一帧
    glUseProgram(program_);                    // 启用我们的画图流水线
    glActiveTexture(GL_TEXTURE0);              // 选定 0 号贴图槽
    glBindTexture(GL_TEXTURE_2D, texture_);    // 把贴图放进这个槽
    glUniform1i(glGetUniformLocation(program_, "uTex"),
                0); // 告诉流水线：着色器里的 uTex 就用 0 号槽
    glBindVertexArray(vao_);                   // 使用 ② 里配好的矩形顶点
    glDrawArrays(GL_TRIANGLES, 0, 6);          // 画 6 个顶点 = 2 个三角形 = 整块矩形

    // ② 显示：main 里紧接着调用 glfwSwapBuffers，把这一帧翻到屏幕上（见文档）
}

} // namespace cg
