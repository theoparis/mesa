/*
 * Mesa libGL wrapper for macOS EGL using dlsym interposition
 */

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <EGL/egl.h>

/* DYLD_INTERPOSE macro - define it ourselves */
#define DYLD_INTERPOSE(_replacement,_replacee) \
   __attribute__((used)) static struct{ const void* replacement; const void* replacee; } _interpose_##_replacee \
   __attribute__ ((section ("__DATA,__interpose"))) = { (const void*)(unsigned long)&_replacement, (const void*)(unsigned long)&_replacee };

static void *egl_handle = NULL;
static PFNEGLGETPROCADDRESSPROC mesa_eglGetProcAddress = NULL;
static int initialized = 0;

static void ensure_initialized(void) {
    if (initialized) return;
    initialized = 1;
    
    egl_handle = dlopen("/Users/lucamignatti/mesa-native/lib/libEGL.dylib", RTLD_NOW | RTLD_LOCAL);
    if (egl_handle) {
        fprintf(stderr, "libGL interpose: Loaded Mesa EGL\n");
        mesa_eglGetProcAddress = (PFNEGLGETPROCADDRESSPROC)dlsym(egl_handle, "eglGetProcAddress");
        if (mesa_eglGetProcAddress) {
            fprintf(stderr, "libGL interpose: Ready to forward GL calls\n");
        }
    }
}

/* Interpose dlsym to catch GL function lookups */
void *my_dlsym(void *handle, const char *symbol) {
    ensure_initialized();
    
    /* If looking for a GL function, use eglGetProcAddress */
    if (symbol && strncmp(symbol, "gl", 2) == 0 && mesa_eglGetProcAddress) {
        void *proc = (void*)mesa_eglGetProcAddress(symbol);
        if (proc) {
            return proc;
        }
    }
    
    /* Fall back to real dlsym */
    return dlsym(handle, symbol);
}

DYLD_INTERPOSE(my_dlsym, dlsym)
