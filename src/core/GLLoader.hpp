#pragma once
#include <cstddef>

namespace cg {

// 进程地址加载器：按函数名向显卡驱动查询函数地址（由 Window 提供，如 glfwGetProcAddress）
using LoaderFn = void* (*)(const char* name);

} // namespace cg

// ===================== OpenGL 加载器（自写，替代第三方 glad） =====================
// 以下类型/常量/函数指针都放在全局的 gl 命名空间——官方 OpenGL 就是全局 C API，
// 这样调用处的写法与使用官方头文件完全一致。
// 数值与签名抄自 OpenGL 官方规范（曾与生成器产物逐一核对）。
namespace gl {

// ---- 基础类型（与官方 KHR 平台头一致）----
using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLchar = char;
using GLsizeiptr = std::ptrdiff_t;

// ---- 常量（只列本工程用到的）----
constexpr GLenum GL_FALSE = 0;
constexpr GLenum GL_TRUE = 1;
constexpr GLenum GL_FLOAT = 0x1406;
constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum GL_TRIANGLES = 0x0004;
constexpr GLenum GL_COLOR_BUFFER_BIT = 0x00004000;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
constexpr GLenum GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
constexpr GLenum GL_STATIC_DRAW = 0x88E4;
constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
constexpr GLenum GL_RGBA8 = 0x8058;
constexpr GLenum GL_RGBA = 0x1908;
constexpr GLenum GL_NEAREST = 0x2600;
constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_TEXTURE0 = 0x84C0;

// ---- 函数指针 ----
// Windows 上 OpenGL 1.1 之后的函数编译时链接不到，只能运行时按名字向驱动要地址，
// 存进下面的指针再调用。名字与官方 OpenGL 完全一致，调用处写法不变。
extern void (*glActiveTexture)(GLenum texture);
extern void (*glAttachShader)(GLuint program, GLuint shader);
extern void (*glBindBuffer)(GLenum target, GLuint buffer);
extern void (*glBindTexture)(GLenum target, GLuint texture);
extern void (*glBindVertexArray)(GLuint array);
extern void (*glBufferData)(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
extern void (*glClear)(GLbitfield mask);
extern void (*glCompileShader)(GLuint shader);
extern GLuint (*glCreateProgram)(void);
extern GLuint (*glCreateShader)(GLenum type);
extern void (*glDeleteProgram)(GLuint program);
extern void (*glDeleteShader)(GLuint shader);
extern void (*glDeleteTextures)(GLsizei n, const GLuint* textures);
extern void (*glDeleteVertexArrays)(GLsizei n, const GLuint* arrays);
extern void (*glDrawArrays)(GLenum mode, GLint first, GLsizei count);
extern void (*glEnableVertexAttribArray)(GLuint index);
extern void (*glGenBuffers)(GLsizei n, GLuint* buffers);
extern void (*glGenTextures)(GLsizei n, GLuint* textures);
extern void (*glGenVertexArrays)(GLsizei n, GLuint* arrays);
extern void (*glGetProgramInfoLog)(GLuint program, GLsizei bufSize, GLsizei* length,
                                   GLchar* infoLog);
extern void (*glGetProgramiv)(GLuint program, GLenum pname, GLint* params);
extern void (*glGetShaderInfoLog)(GLuint shader, GLsizei bufSize, GLsizei* length,
                                  GLchar* infoLog);
extern void (*glGetShaderiv)(GLuint shader, GLenum pname, GLint* params);
extern GLint (*glGetUniformLocation)(GLuint program, const GLchar* name);
extern void (*glLinkProgram)(GLuint program);
extern void (*glShaderSource)(GLuint shader, GLsizei count, const GLchar* const* string,
                              const GLint* length);
extern void (*glTexImage2D)(GLenum target, GLint level, GLint internalformat, GLsizei width,
                            GLsizei height, GLint border, GLenum format, GLenum type,
                            const void* pixels);
extern void (*glTexParameteri)(GLenum target, GLenum pname, GLint param);
extern void (*glTexSubImage2D)(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                               GLsizei width, GLsizei height, GLenum format, GLenum type,
                               const void* pixels);
extern void (*glUniform1i)(GLint location, GLint v0);
extern void (*glUseProgram)(GLuint program);
extern void (*glVertexAttribPointer)(GLuint index, GLint size, GLenum type,
                                     GLboolean normalized, GLsizei stride,
                                     const void* pointer);
extern void (*glViewport)(GLint x, GLint y, GLsizei width, GLsizei height);

// 把上面 33 个函数的地址逐个从驱动查出来。
// 任何一个查不到都返回 false（说明这台机器不支持 OpenGL 3.3，程序会提示并退出）。
bool loadGL(cg::LoaderFn getProcAddress);

} // namespace gl
