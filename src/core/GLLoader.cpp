#include "core/GLLoader.hpp"

// 函数指针的定义（声明见 GLLoader.hpp 的 gl 命名空间）
namespace gl {

void (*glActiveTexture)(GLenum) = nullptr;
void (*glAttachShader)(GLuint, GLuint) = nullptr;
void (*glBindBuffer)(GLenum, GLuint) = nullptr;
void (*glBindTexture)(GLenum, GLuint) = nullptr;
void (*glBindVertexArray)(GLuint) = nullptr;
void (*glBufferData)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
void (*glClear)(GLbitfield) = nullptr;
void (*glCompileShader)(GLuint) = nullptr;
GLuint (*glCreateProgram)(void) = nullptr;
GLuint (*glCreateShader)(GLenum) = nullptr;
void (*glDeleteProgram)(GLuint) = nullptr;
void (*glDeleteShader)(GLuint) = nullptr;
void (*glDeleteTextures)(GLsizei, const GLuint*) = nullptr;
void (*glDeleteVertexArrays)(GLsizei, const GLuint*) = nullptr;
void (*glDrawArrays)(GLenum, GLint, GLsizei) = nullptr;
void (*glEnableVertexAttribArray)(GLuint) = nullptr;
void (*glGenBuffers)(GLsizei, GLuint*) = nullptr;
void (*glGenTextures)(GLsizei, GLuint*) = nullptr;
void (*glGenVertexArrays)(GLsizei, GLuint*) = nullptr;
void (*glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
void (*glGetProgramiv)(GLuint, GLenum, GLint*) = nullptr;
void (*glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
void (*glGetShaderiv)(GLuint, GLenum, GLint*) = nullptr;
GLint (*glGetUniformLocation)(GLuint, const GLchar*) = nullptr;
void (*glLinkProgram)(GLuint) = nullptr;
void (*glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*) = nullptr;
void (*glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                     const void*) = nullptr;
void (*glTexParameteri)(GLenum, GLenum, GLint) = nullptr;
void (*glTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                        const void*) = nullptr;
void (*glUniform1i)(GLint, GLint) = nullptr;
void (*glUseProgram)(GLuint) = nullptr;
void (*glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) = nullptr;
void (*glViewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;

bool loadGL(cg::LoaderFn getProcAddress) {
    if (getProcAddress == nullptr) {
        return false;
    }

    // 按名字查地址；查不到任何一个就整体失败。
    // 宏展开后等价于：glActiveTexture = reinterpret_cast<decltype(glActiveTexture)>(getProcAddress("glActiveTexture"));
#define CG_LOAD(fn)                                                                                \
    do {                                                                                           \
        fn = reinterpret_cast<decltype(fn)>(getProcAddress(#fn));                                  \
        if (fn == nullptr) {                                                                       \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

    CG_LOAD(glActiveTexture);
    CG_LOAD(glAttachShader);
    CG_LOAD(glBindBuffer);
    CG_LOAD(glBindTexture);
    CG_LOAD(glBindVertexArray);
    CG_LOAD(glBufferData);
    CG_LOAD(glClear);
    CG_LOAD(glCompileShader);
    CG_LOAD(glCreateProgram);
    CG_LOAD(glCreateShader);
    CG_LOAD(glDeleteProgram);
    CG_LOAD(glDeleteShader);
    CG_LOAD(glDeleteTextures);
    CG_LOAD(glDeleteVertexArrays);
    CG_LOAD(glDrawArrays);
    CG_LOAD(glEnableVertexAttribArray);
    CG_LOAD(glGenBuffers);
    CG_LOAD(glGenTextures);
    CG_LOAD(glGenVertexArrays);
    CG_LOAD(glGetProgramInfoLog);
    CG_LOAD(glGetProgramiv);
    CG_LOAD(glGetShaderInfoLog);
    CG_LOAD(glGetShaderiv);
    CG_LOAD(glGetUniformLocation);
    CG_LOAD(glLinkProgram);
    CG_LOAD(glShaderSource);
    CG_LOAD(glTexImage2D);
    CG_LOAD(glTexParameteri);
    CG_LOAD(glTexSubImage2D);
    CG_LOAD(glUniform1i);
    CG_LOAD(glUseProgram);
    CG_LOAD(glVertexAttribPointer);
    CG_LOAD(glViewport);
#undef CG_LOAD

    return true;
}

} // namespace gl
