// Canvas.cpp — 把 FrameBuffer 上屏：像素上传成 OpenGL 纹理，贴满窗口。
#include "core/Canvas.hpp"
#include "core/FrameBuffer.hpp"
#include <iostream>

// GL 函数指针都在 gl 命名空间（GLLoader.hpp）。
using namespace gl;

namespace cg {
namespace {

// 顶点着色器：把矩形角点坐标直接输出到裁剪空间。
constexpr const char* kVertexShaderSource = R"glsl(#version 330 core
layout(location = 0) in vec2 aPos;   // 矩形角点的屏幕坐标（-1..1）
layout(location = 1) in vec2 aUV;    // 这个角点对应贴图上的位置（0..1）
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)glsl";

// 片段着色器：从贴图取色；v 取反（数组第 0 行在顶部，贴图第 0 行在底部）。
constexpr const char* kFragmentShaderSource = R"glsl(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;   // 贴图 = FrameBuffer
void main() {
    fragColor = texture(uTex, vec2(vUV.x, 1.0 - vUV.y));
}
)glsl";

// 编译着色器，失败时打印驱动日志并返回 0。
unsigned compileShader(unsigned shaderType, const char* source) {
    const unsigned shaderId = glCreateShader(shaderType);
    glShaderSource(shaderId, 1, &source, nullptr);
    glCompileShader(shaderId);
    int ok = 0;
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &ok);
    if (ok == 0) {
        char log[512];
        glGetShaderInfoLog(shaderId, sizeof(log), nullptr, log);
        std::cerr << "[Canvas] shader compile failed: " << log << std::endl;
        return 0;
    }
    return shaderId;
}

} // namespace

Canvas::Canvas(int width, int height, LoaderFn loader) {
    // 从驱动加载本工程用到的 GL 函数地址。
    if (loader == nullptr || !gl::loadGL(loader)) {
        std::cerr << "[Canvas] GL load failed（是否不支持 OpenGL 3.3？）" << std::endl;
        return;
    }

    // 编译两个着色器并链接成程序。
    const unsigned vertexShader = compileShader(GL_VERTEX_SHADER, kVertexShaderSource);
    const unsigned fragmentShader = compileShader(GL_FRAGMENT_SHADER, kFragmentShaderSource);
    if (vertexShader == 0 || fragmentShader == 0) {
        return;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vertexShader);
    glAttachShader(program_, fragmentShader);
    glLinkProgram(program_);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    int ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (ok == 0) {
        char log[512];
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        std::cerr << "[Canvas] program link failed: " << log << std::endl;
        return;
    }
    texUniform_ = glGetUniformLocation(program_, "uTex");

    // 铺满窗口的矩形：两个三角形，每顶点 (x, y, u, v)。
    const float rectangleVertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
    };
    unsigned vertexBuffer = 0;
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vertexBuffer);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(rectangleVertices), rectangleVertices,
                 GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // 贴图：尺寸与画板一致。
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    // 最近邻采样：保持像素锐利。
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // 超出边界按边缘夹取。
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);

    valid_ = true;
}

Canvas::~Canvas() {
    if (program_ != 0) glDeleteProgram(program_);
    if (texture_ != 0) glDeleteTextures(1, &texture_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
}

void Canvas::resize(int width, int height) {
    if (!valid_) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
}

void Canvas::present(const FrameBuffer& fb) {
    if (!valid_) {
        return;
    }

    // 上传像素到贴图（CPU→GPU 唯一通道）。
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fb.width(), fb.height(), GL_RGBA,
                    GL_UNSIGNED_BYTE, fb.data());

    // 让 GPU 画那个大矩形。
    glViewport(0, 0, fb.width(), fb.height());
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    if (texUniform_ >= 0) {
        glUniform1i(texUniform_, 0);
    }
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);

}

} // namespace cg
