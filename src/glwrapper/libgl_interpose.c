/*
 * Mesa libGL wrapper for macOS EGL using dlsym interposition
 * 
 * This library intercepts dlsym calls to redirect GL function lookups
 * to Mesa's EGL implementation. It finds libEGL.dylib relative to itself,
 * making the install location-independent.
 * 
 * It also sets MESA_EGL_LIBRARY and MESA_VULKAN_LIBRARY environment variables
 * so that GLFW and Zink can find the libraries when DYLD_LIBRARY_PATH is 
 * stripped by SIP on macOS.
 */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
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
static int env_vars_set = 0;
static char lib_dir[1024] = {0};
static char egl_lib_path[1024] = {0};

/* Determine the library directory and EGL path */
static void determine_paths(void) {
    if (lib_dir[0] != '\0') return;
    
    Dl_info info;
    if (dladdr((void*)determine_paths, &info) && info.dli_fname) {
        const char *last_slash = strrchr(info.dli_fname, '/');
        if (last_slash) {
            size_t dir_len = last_slash - info.dli_fname;
            snprintf(lib_dir, sizeof(lib_dir), "%.*s", (int)dir_len, info.dli_fname);
            snprintf(egl_lib_path, sizeof(egl_lib_path), "%s/libEGL.dylib", lib_dir);
        } else {
            snprintf(egl_lib_path, sizeof(egl_lib_path), "libEGL.dylib");
        }
    } else {
        snprintf(egl_lib_path, sizeof(egl_lib_path), "libEGL.dylib");
    }
}

/* Set environment variables for library paths (called lazily) */
static void set_env_vars(void) {
    if (env_vars_set) return;
    env_vars_set = 1;
    
    determine_paths();
    
    /* Set EGL library path */
    setenv("MESA_EGL_LIBRARY", egl_lib_path, 1);
    fprintf(stderr, "libGL interpose: Set MESA_EGL_LIBRARY=%s\n", egl_lib_path);
    
    /* Set Vulkan library path */
    if (lib_dir[0] != '\0') {
        char vk_lib_path[1024];
        snprintf(vk_lib_path, sizeof(vk_lib_path), "%s/libvulkan.1.dylib", lib_dir);
        setenv("MESA_VULKAN_LIBRARY", vk_lib_path, 1);
        fprintf(stderr, "libGL interpose: Set MESA_VULKAN_LIBRARY=%s\n", vk_lib_path);
    }
}

static void ensure_initialized(void) {
    if (initialized) return;
    initialized = 1;
    
    determine_paths();
    set_env_vars();
    
    egl_handle = dlopen(egl_lib_path, RTLD_NOW | RTLD_LOCAL);
    if (egl_handle) {
        fprintf(stderr, "libGL interpose: Loaded Mesa EGL from %s\n", egl_lib_path);
        mesa_eglGetProcAddress = (PFNEGLGETPROCADDRESSPROC)dlsym(egl_handle, "eglGetProcAddress");
        if (mesa_eglGetProcAddress) {
            fprintf(stderr, "libGL interpose: Ready to forward GL calls\n");
        }
    } else {
        fprintf(stderr, "libGL interpose: Failed to load %s: %s\n", egl_lib_path, dlerror());
    }
}

/* Interpose dlsym to catch GL function lookups */
static void *my_dlsym(void *handle, const char *symbol) {
    /* Set env vars early so other libraries can use them */
    if (!env_vars_set) {
        set_env_vars();
    }
    
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
