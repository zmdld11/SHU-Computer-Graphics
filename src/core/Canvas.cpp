#include "core/Canvas.hpp"
#include "core/FrameBuffer.hpp"
#include <glad/gl.h>
#include <iostream>

namespace cg {
namespace {

constexpr const char* kVertSrc = R"glsl(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)glsl";

// FrameBuffer 行 0 在顶部，纹理 v=0 在底部，采样时翻转 v
constexpr const char* kFragSrc = R"glsl(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
void main() {
    fragColor = texture(uTex, vec2(vUV.x, 1.0 - vUV.y));
}
)glsl";

unsigned compileShader(unsigned type, const char* src) {
    const unsigned s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (ok == 0) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "[Canvas] shader compile failed: " << log << std::endl;
        return 0;
    }
    return s;
}

} // namespace

Canvas::Canvas(int width, int height, LoaderFn loader) {
    if (loader == nullptr || gladLoadGL((GLADloadfunc)loader) == 0) {
        std::cerr << "[Canvas] gladLoadGL failed" << std::endl;
        return;
    }

    const unsigned vs = compileShader(GL_VERTEX_SHADER, kVertSrc);
    const unsigned fs = compileShader(GL_FRAGMENT_SHADER, kFragSrc);
    if (vs == 0 || fs == 0) {
        return;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    int ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (ok == 0) {
        char log[512];
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        std::cerr << "[Canvas] program link failed: " << log << std::endl;
        return;
    }

    // 全屏四边形（两个三角形）：x, y, u, v
    const float quad[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
    };
    unsigned vbo = 0;
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    valid_ = true;
}

Canvas::~Canvas() {
    if (program_ != 0) glDeleteProgram(program_);
    if (texture_ != 0) glDeleteTextures(1, &texture_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
}

void Canvas::present(const FrameBuffer& fb) {
    if (!valid_) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fb.width(), fb.height(), GL_RGBA,
                    GL_UNSIGNED_BYTE, fb.data());

    glViewport(0, 0, fb.width(), fb.height());
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(program_, "uTex"), 0);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

} // namespace cg
