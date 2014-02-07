typedef struct glamor_gl_dispatch {
    /* Transformation functions */
    void (*glMatrixMode) (GLenum mode);
    void (*glLoadIdentity) (void);
    void (*glViewport) (GLint x, GLint y, GLsizei width, GLsizei height);
    /* Drawing functions */
    void (*glRasterPos2i) (GLint x, GLint y);

    /* Vertex Array */
    void (*glDrawArrays) (GLenum mode, GLint first, GLsizei count);

    /* Elements Array */
    void (*glDrawElements) (GLenum mode, GLsizei count, GLenum type,
                            const GLvoid * indices);
    void (*glDrawRangeElements) (GLenum mode, GLuint start, GLuint end,
                                 GLsizei count, GLenum type,
                                 const GLvoid * indices);

    /* Raster functions */
    void (*glReadPixels) (GLint x, GLint y,
                          GLsizei width, GLsizei height,
                          GLenum format, GLenum type, GLvoid * pixels);

    void (*glDrawPixels) (GLsizei width, GLsizei height,
                          GLenum format, GLenum type, const GLvoid * pixels);
    void (*glPixelStorei) (GLenum pname, GLint param);
    /* Texture Mapping */

    void (*glTexParameteri) (GLenum target, GLenum pname, GLint param);
    void (*glTexImage2D) (GLenum target, GLint level,
                          GLint internalFormat,
                          GLsizei width, GLsizei height,
                          GLint border, GLenum format, GLenum type,
                          const GLvoid * pixels);
    /* 1.1 */
    void (*glGenTextures) (GLsizei n, GLuint * textures);
    void (*glDeleteTextures) (GLsizei n, const GLuint * textures);
    void (*glBindTexture) (GLenum target, GLuint texture);
    void (*glTexSubImage2D) (GLenum target, GLint level,
                             GLint xoffset, GLint yoffset,
                             GLsizei width, GLsizei height,
                             GLenum format, GLenum type, const GLvoid * pixels);
    /* MISC */
    void (*glFlush) (void);
    void (*glFinish) (void);
    void (*glGetIntegerv) (GLenum pname, GLint * params);
    const GLubyte *(*glGetString) (GLenum name);
    void (*glScissor) (GLint x, GLint y, GLsizei width, GLsizei height);
    void (*glEnable) (GLenum cap);
    void (*glDisable) (GLenum cap);
    void (*glBlendFunc) (GLenum sfactor, GLenum dfactor);
    void (*glLogicOp) (GLenum opcode);

    /* 1.3 */
    void (*glActiveTexture) (GLenum texture);

    /* GL Extentions */
    void (*glGenBuffers) (GLsizei n, GLuint * buffers);
    void (*glBufferData) (GLenum target, GLsizeiptr size,
                          const GLvoid * data, GLenum usage);
    GLvoid *(*glMapBuffer) (GLenum target, GLenum access);
    GLvoid *(*glMapBufferRange) (GLenum target, GLintptr offset,
                                 GLsizeiptr length, GLbitfield access);
     GLboolean(*glUnmapBuffer) (GLenum target);
    void (*glBindBuffer) (GLenum target, GLuint buffer);
    void (*glDeleteBuffers) (GLsizei n, const GLuint * buffers);

    void (*glFramebufferTexture2D) (GLenum target, GLenum attachment,
                                    GLenum textarget, GLuint texture,
                                    GLint level);
    void (*glBindFramebuffer) (GLenum target, GLuint framebuffer);
    void (*glDeleteFramebuffers) (GLsizei n, const GLuint * framebuffers);
    void (*glGenFramebuffers) (GLsizei n, GLuint * framebuffers);
     GLenum(*glCheckFramebufferStatus) (GLenum target);
    void (*glBlitFramebuffer) (GLint srcX0, GLint srcY0, GLint srcX1,
                               GLint srcY1, GLint dstX0, GLint dstY0,
                               GLint dstX1, GLint dstY1,
                               GLbitfield mask, GLenum filter);

    void (*glVertexAttribPointer) (GLuint index, GLint size,
                                   GLenum type, GLboolean normalized,
                                   GLsizei stride, const GLvoid * pointer);
    void (*glDisableVertexAttribArray) (GLuint index);
    void (*glEnableVertexAttribArray) (GLuint index);
    void (*glBindAttribLocation) (GLuint program, GLuint index,
                                  const GLchar * name);

    void (*glLinkProgram) (GLuint program);
    void (*glShaderSource) (GLuint shader, GLsizei count,
                            const GLchar * *string, const GLint * length);
    void (*glUseProgram) (GLuint program);
    void (*glUniform1i) (GLint location, GLint v0);
    void (*glUniform1f) (GLint location, GLfloat v0);
    void (*glUniform4f) (GLint location, GLfloat v0, GLfloat v1,
                         GLfloat v2, GLfloat v3);
    void (*glUniform1fv) (GLint location, GLsizei count, const GLfloat * value);
    void (*glUniform2fv) (GLint location, GLsizei count, const GLfloat * value);
    void (*glUniform4fv) (GLint location, GLsizei count, const GLfloat * value);
    void (*glUniformMatrix3fv) (GLint location, GLsizei count,
                                GLboolean transpose, const GLfloat * value);
     GLuint(*glCreateProgram) (void);
     GLuint(*glDeleteProgram) (GLuint);
     GLuint(*glCreateShader) (GLenum type);
    void (*glCompileShader) (GLuint shader);
    void (*glAttachShader) (GLuint program, GLuint shader);
    void (*glDeleteShader) (GLuint shader);
    void (*glGetShaderiv) (GLuint shader, GLenum pname, GLint * params);
    void (*glGetShaderInfoLog) (GLuint shader, GLsizei bufSize,
                                GLsizei * length, GLchar * infoLog);
    void (*glGetProgramiv) (GLuint program, GLenum pname, GLint * params);
    void (*glGetProgramInfoLog) (GLuint program, GLsizei bufSize,
                                 GLsizei * length, GLchar * infoLog);
     GLint(*glGetUniformLocation) (GLuint program, const GLchar * name);

} glamor_gl_dispatch;

typedef void *(*get_proc_address_t) (const char *);

_X_EXPORT Bool

glamor_gl_dispatch_init_impl(struct glamor_gl_dispatch *dispatch,
                             int gl_version,
                             get_proc_address_t get_proc_address);

_X_EXPORT Bool

glamor_gl_dispatch_init(ScreenPtr screen,
                        struct glamor_gl_dispatch *dispatch, int gl_version);
