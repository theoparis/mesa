/*
 * Mesa libGL wrapper for macOS EGL
 * 
 * This library provides libGL.dylib compatibility for applications that load
 * OpenGL functions via dlsym on libGL, but the actual OpenGL context is
 * created via EGL/Zink.
 * 
 * It forwards all OpenGL function lookups to eglGetProcAddress.
 */

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <EGL/egl.h>

/* Type definitions */
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned char GLboolean;
typedef float GLfloat;
typedef double GLdouble;
typedef void GLvoid;
typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;

typedef void (*PFNGLPROC)(void);

static void *egl_handle = NULL;
static PFNEGLGETPROCADDRESSPROC mesa_eglGetProcAddress = NULL;

__attribute__((constructor))
static void init_egl_loader(void) {
    const char *egl_paths[] = {
        "/Users/lucamignatti/mesa-native/lib/libEGL.dylib",
        "libEGL.dylib",
        NULL
    };
    
    for (int i = 0; egl_paths[i] != NULL; i++) {
        egl_handle = dlopen(egl_paths[i], RTLD_NOW | RTLD_LOCAL);
        if (egl_handle) {
            fprintf(stderr, "libGL wrapper: Loaded EGL from %s\n", egl_paths[i]);
            break;
        }
    }
    
    if (!egl_handle) {
        fprintf(stderr, "libGL wrapper: Failed to load libEGL.dylib\n");
        return;
    }
    
    mesa_eglGetProcAddress = (PFNEGLGETPROCADDRESSPROC)dlsym(egl_handle, "eglGetProcAddress");
    if (!mesa_eglGetProcAddress) {
        fprintf(stderr, "libGL wrapper: Failed to find eglGetProcAddress\n");
    } else {
        fprintf(stderr, "libGL wrapper: Successfully initialized EGL function loader\n");
    }
}

static PFNGLPROC get_gl_proc(const char *name) {
    if (mesa_eglGetProcAddress) {
        return (PFNGLPROC)mesa_eglGetProcAddress(name);
    }
    return NULL;
}

/* ============== Core OpenGL Functions ============== */

/* glGetError */
GLenum glGetError(void) {
    static GLenum (*fn)(void) = NULL;
    if (!fn) {
        fn = (GLenum (*)(void))get_gl_proc("glGetError");
        if (fn) fprintf(stderr, "libGL wrapper: glGetError loaded\n");
    }
    return fn ? fn() : 0;
}

/* glGetString */
const GLubyte *glGetString(GLenum name) {
    static const GLubyte *(*fn)(GLenum) = NULL;
    if (!fn) {
        fn = (const GLubyte *(*)(GLenum))get_gl_proc("glGetString");
        if (fn) fprintf(stderr, "libGL wrapper: glGetString loaded\n");
    }
    return fn ? fn(name) : NULL;
}

/* glGetStringi - Required by LWJGL for capabilities */
const GLubyte *glGetStringi(GLenum name, GLuint index) {
    static const GLubyte *(*fn)(GLenum, GLuint) = NULL;
    if (!fn) {
        fn = (const GLubyte *(*)(GLenum, GLuint))get_gl_proc("glGetStringi");
        if (fn) fprintf(stderr, "libGL wrapper: glGetStringi loaded\n");
    }
    return fn ? fn(name, index) : NULL;
}

/* glGetIntegerv */
void glGetIntegerv(GLenum pname, GLint *params) {
    static void (*fn)(GLenum, GLint*) = NULL;
    if (!fn) {
        fn = (void (*)(GLenum, GLint*))get_gl_proc("glGetIntegerv");
    }
    if (fn) fn(pname, params);
}

/* glGetFloatv */
void glGetFloatv(GLenum pname, GLfloat *params) {
    static void (*fn)(GLenum, GLfloat*) = NULL;
    if (!fn) {
        fn = (void (*)(GLenum, GLfloat*))get_gl_proc("glGetFloatv");
    }
    if (fn) fn(pname, params);
}

/* glGetDoublev */
void glGetDoublev(GLenum pname, GLdouble *params) {
    static void (*fn)(GLenum, GLdouble*) = NULL;
    if (!fn) {
        fn = (void (*)(GLenum, GLdouble*))get_gl_proc("glGetDoublev");
    }
    if (fn) fn(pname, params);
}

/* glGetBooleanv */
void glGetBooleanv(GLenum pname, GLboolean *params) {
    static void (*fn)(GLenum, GLboolean*) = NULL;
    if (!fn) {
        fn = (void (*)(GLenum, GLboolean*))get_gl_proc("glGetBooleanv");
    }
    if (fn) fn(pname, params);
}

/* glEnable */
void glEnable(GLenum cap) {
    static void (*fn)(GLenum) = NULL;
    if (!fn) fn = (void (*)(GLenum))get_gl_proc("glEnable");
    if (fn) fn(cap);
}

/* glDisable */
void glDisable(GLenum cap) {
    static void (*fn)(GLenum) = NULL;
    if (!fn) fn = (void (*)(GLenum))get_gl_proc("glDisable");
    if (fn) fn(cap);
}

/* glIsEnabled */
GLboolean glIsEnabled(GLenum cap) {
    static GLboolean (*fn)(GLenum) = NULL;
    if (!fn) fn = (GLboolean (*)(GLenum))get_gl_proc("glIsEnabled");
    return fn ? fn(cap) : 0;
}

/* glViewport */
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    static void (*fn)(GLint, GLint, GLsizei, GLsizei) = NULL;
    if (!fn) fn = (void (*)(GLint, GLint, GLsizei, GLsizei))get_gl_proc("glViewport");
    if (fn) fn(x, y, width, height);
}

/* glClear */
void glClear(GLuint mask) {
    static void (*fn)(GLuint) = NULL;
    if (!fn) fn = (void (*)(GLuint))get_gl_proc("glClear");
    if (fn) fn(mask);
}

/* glClearColor */
void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    static void (*fn)(GLfloat, GLfloat, GLfloat, GLfloat) = NULL;
    if (!fn) fn = (void (*)(GLfloat, GLfloat, GLfloat, GLfloat))get_gl_proc("glClearColor");
    if (fn) fn(red, green, blue, alpha);
}

/* glFlush */
void glFlush(void) {
    static void (*fn)(void) = NULL;
    if (!fn) fn = (void (*)(void))get_gl_proc("glFlush");
    if (fn) fn();
}

/* glFinish */
void glFinish(void) {
    static void (*fn)(void) = NULL;
    if (!fn) fn = (void (*)(void))get_gl_proc("glFinish");
    if (fn) fn();
}

/* glDepthMask */
void glDepthMask(GLboolean flag) {
    static void (*fn)(GLboolean) = NULL;
    if (!fn) fn = (void (*)(GLboolean))get_gl_proc("glDepthMask");
    if (fn) fn(flag);
}

/* glColorMask */
void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) {
    static void (*fn)(GLboolean, GLboolean, GLboolean, GLboolean) = NULL;
    if (!fn) fn = (void (*)(GLboolean, GLboolean, GLboolean, GLboolean))get_gl_proc("glColorMask");
    if (fn) fn(red, green, blue, alpha);
}

/* glBlendFunc */
void glBlendFunc(GLenum sfactor, GLenum dfactor) {
    static void (*fn)(GLenum, GLenum) = NULL;
    if (!fn) fn = (void (*)(GLenum, GLenum))get_gl_proc("glBlendFunc");
    if (fn) fn(sfactor, dfactor);
}

/* glDepthFunc */
void glDepthFunc(GLenum func) {
    static void (*fn)(GLenum) = NULL;
    if (!fn) fn = (void (*)(GLenum))get_gl_proc("glDepthFunc");
    if (fn) fn(func);
}

/* glCullFace */
void glCullFace(GLenum mode) {
    static void (*fn)(GLenum) = NULL;
    if (!fn) fn = (void (*)(GLenum))get_gl_proc("glCullFace");
    if (fn) fn(mode);
}

/* glFrontFace */
void glFrontFace(GLenum mode) {
    static void (*fn)(GLenum) = NULL;
    if (!fn) fn = (void (*)(GLenum))get_gl_proc("glFrontFace");
    if (fn) fn(mode);
}

/* glPolygonMode */
void glPolygonMode(GLenum face, GLenum mode) {
    static void (*fn)(GLenum, GLenum) = NULL;
    if (!fn) fn = (void (*)(GLenum, GLenum))get_gl_proc("glPolygonMode");
    if (fn) fn(face, mode);
}

/* glScissor */
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    static void (*fn)(GLint, GLint, GLsizei, GLsizei) = NULL;
    if (!fn) fn = (void (*)(GLint, GLint, GLsizei, GLsizei))get_gl_proc("glScissor");
    if (fn) fn(x, y, width, height);
}

/* glPixelStorei */
void glPixelStorei(GLenum pname, GLint param) {
    static void (*fn)(GLenum, GLint) = NULL;
    if (!fn) fn = (void (*)(GLenum, GLint))get_gl_proc("glPixelStorei");
    if (fn) fn(pname, param);
}

/* glReadPixels */
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels) {
    static void (*fn)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = NULL;
    if (!fn) fn = (void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))get_gl_proc("glReadPixels");
    if (fn) fn(x, y, width, height, format, type, pixels);
}

/* glDrawArrays */
void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    static void (*fn)(GLenum, GLint, GLsizei) = NULL;
    if (!fn) fn = (void (*)(GLenum, GLint, GLsizei))get_gl_proc("glDrawArrays");
    if (fn) fn(mode, first, count);
}

/* glDrawElements */
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    static void (*fn)(GLenum, GLsizei, GLenum, const void*) = NULL;
    if (!fn) fn = (void (*)(GLenum, GLsizei, GLenum, const void*))get_gl_proc("glDrawElements");
    if (fn) fn(mode, count, type, indices);
}

/* glBindTexture */
void glBindTexture(GLenum target, GLuint texture) {
    static void (*fn)(GLenum, GLuint) = NULL;
    if (!fn) fn = (void (*)(GLenum, GLuint))get_gl_proc("glBindTexture");
    if (fn) fn(target, texture);
}

/* glGenTextures */
void glGenTextures(GLsizei n, GLuint *textures) {
    static void (*fn)(GLsizei, GLuint*) = NULL;
    if (!fn) fn = (void (*)(GLsizei, GLuint*))get_gl_proc("glGenTextures");
    if (fn) fn(n, textures);
}

/* glDeleteTextures */
void glDeleteTextures(GLsizei n, const GLuint *textures) {
    static void (*fn)(GLsizei, const GLuint*) = NULL;
    if (!fn) fn = (void (*)(GLsizei, const GLuint*))get_gl_proc("glDeleteTextures");
    if (fn) fn(n, textures);
}

/* glTexParameteri */
void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    static void (*fn)(GLenum, GLenum, GLint) = NULL;
    if (!fn) fn = (void (*)(GLenum, GLenum, GLint))get_gl_proc("glTexParameteri");
    if (fn) fn(target, pname, param);
}

/* glTexImage2D */
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels) {
    static void (*fn)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) = NULL;
    if (!fn) fn = (void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*))get_gl_proc("glTexImage2D");
    if (fn) fn(target, level, internalformat, width, height, border, format, type, pixels);
}

/* glTexSubImage2D */
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels) {
    static void (*fn)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*) = NULL;
    if (!fn) fn = (void (*)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*))get_gl_proc("glTexSubImage2D");
    if (fn) fn(target, level, xoffset, yoffset, width, height, format, type, pixels);
}

/* glActiveTexture */
void glActiveTexture(GLenum texture) {
    static void (*fn)(GLenum) = NULL;
    if (!fn) fn = (void (*)(GLenum))get_gl_proc("glActiveTexture");
    if (fn) fn(texture);
}

/* glLineWidth */
void glLineWidth(GLfloat width) {
    static void (*fn)(GLfloat) = NULL;
    if (!fn) fn = (void (*)(GLfloat))get_gl_proc("glLineWidth");
    if (fn) fn(width);
}

/* glPointSize */
void glPointSize(GLfloat size) {
    static void (*fn)(GLfloat) = NULL;
    if (!fn) fn = (void (*)(GLfloat))get_gl_proc("glPointSize");
    if (fn) fn(size);
}

/* ============== Modern OpenGL / Extension query ============== */

/* glGetProcAddress - Important for LWJGL to load extension functions */
void *glXGetProcAddress(const char *procName) {
    return (void*)get_gl_proc(procName);
}

void *glXGetProcAddressARB(const char *procName) {
    return (void*)get_gl_proc(procName);
}

/* Create aliases for the function lookup */
__attribute__((visibility("default")))
void *wglGetProcAddress(const char *procName) {
    return (void*)get_gl_proc(procName);
}
