/*
 * Mesa 3-D graphics library
 *
 * Copyright (c) 2014 The Chromium OS Authors.
 * Copyright © 2011 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "pipe/p_screen.h"
#include "util/libdrm.h"
#include <sys/stat.h>
#include <sys/types.h>

#include "dri_screen.h"
#include "dri_util.h"
#include "egl_dri2.h"
#include "eglglobals.h"
#include "kopper_interface.h"
#include "loader.h"
#include "loader_dri_helper.h"

#if defined(__APPLE__) && defined(VK_USE_PLATFORM_METAL_EXT)
#include <execinfo.h>
#include <pthread.h>
#include <signal.h>
#include <dispatch/dispatch.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <vulkan/vulkan_metal.h>

static void
crash_handler(int sig)
{
   void *array[50];
   int size = backtrace(array, 50);
   fprintf(stderr, "\n\n=== CRASH HANDLER: Signal %d ===\n", sig);
   fprintf(stderr, "Stack trace:\n");
   backtrace_symbols_fd(array, size, STDERR_FILENO);
   fprintf(stderr, "=== END STACK TRACE ===\n\n");
   signal(sig, SIG_DFL);
   raise(sig);
}

__attribute__((constructor)) static void
install_crash_handler(void)
{
   signal(SIGSEGV, crash_handler);
   signal(SIGBUS, crash_handler);
   signal(SIGABRT, crash_handler);
}
#endif

static struct dri_image *
surfaceless_alloc_image(struct dri2_egl_display *dri2_dpy,
                        struct dri2_egl_surface *dri2_surf)
{
   return dri_create_image(dri2_dpy->dri_screen_render_gpu,
                           dri2_surf->base.Width, dri2_surf->base.Height,
                           dri2_surf->visual, NULL, 0, 0, NULL);
}

static void
surfaceless_free_images(struct dri2_egl_surface *dri2_surf)
{
   if (dri2_surf->front) {
      dri2_destroy_image(dri2_surf->front);
      dri2_surf->front = NULL;
   }

   free(dri2_surf->swrast_device_buffer);
   dri2_surf->swrast_device_buffer = NULL;
}

static int
surfaceless_image_get_buffers(struct dri_drawable *driDrawable,
                              unsigned int format, uint32_t *stamp,
                              void *loaderPrivate, uint32_t buffer_mask,
                              struct __DRIimageList *buffers)
{
   struct dri2_egl_surface *dri2_surf = loaderPrivate;
   struct dri2_egl_display *dri2_dpy =
      dri2_egl_display(dri2_surf->base.Resource.Display);

   buffers->image_mask = 0;
   buffers->front = NULL;
   buffers->back = NULL;

   /* The EGL 1.5 spec states that pbuffers are single-buffered. Specifically,
    * the spec states that they have a back buffer but no front buffer, in
    * contrast to pixmaps, which have a front buffer but no back buffer.
    *
    * Single-buffered surfaces with no front buffer confuse Mesa; so we deviate
    * from the spec, following the precedent of Mesa's EGL X11 platform. The
    * X11 platform correctly assigns pbuffers to single-buffered configs, but
    * assigns the pbuffer a front buffer instead of a back buffer.
    *
    * Pbuffers in the X11 platform mostly work today, so let's just copy its
    * behavior instead of trying to fix (and hence potentially breaking) the
    * world.
    */

   if (buffer_mask & __DRI_IMAGE_BUFFER_FRONT) {

      if (!dri2_surf->front) {
         dri2_surf->front = surfaceless_alloc_image(dri2_dpy, dri2_surf);
         if (!dri2_surf->front)
            return 0;
      }

      buffers->image_mask |= __DRI_IMAGE_BUFFER_FRONT;
      buffers->front = dri2_surf->front;
   }

   return 1;
}

static _EGLSurface *
dri2_surfaceless_create_surface(_EGLDisplay *disp, EGLint type,
                                _EGLConfig *conf, const EGLint *attrib_list)
{
   struct dri2_egl_display *dri2_dpy = dri2_egl_display(disp);
   struct dri2_egl_config *dri2_conf = dri2_egl_config(conf);
   struct dri2_egl_surface *dri2_surf;
   const struct dri_config *config;

   /* Make sure to calloc so all pointers
    * are originally NULL.
    */
   dri2_surf = calloc(1, sizeof *dri2_surf);

   if (!dri2_surf) {
      _eglError(EGL_BAD_ALLOC, "eglCreatePbufferSurface");
      return NULL;
   }

   if (!dri2_init_surface(&dri2_surf->base, disp, type, conf, attrib_list,
                          false, NULL))
      goto cleanup_surface;

   config = dri2_get_dri_config(dri2_conf, type, dri2_surf->base.GLColorspace);

   if (!config) {
      _eglError(EGL_BAD_MATCH,
                "Unsupported surfacetype/colorspace configuration");
      goto cleanup_surface;
   }

   dri2_surf->visual = dri2_image_format_for_pbuffer_config(dri2_dpy, config);
   if (dri2_surf->visual == PIPE_FORMAT_NONE)
      goto cleanup_surface;

   if (!dri2_create_drawable(dri2_dpy, config, dri2_surf, dri2_surf))
      goto cleanup_surface;

   return &dri2_surf->base;

cleanup_surface:
   free(dri2_surf);
   return NULL;
}

static EGLBoolean
surfaceless_destroy_surface(_EGLDisplay *disp, _EGLSurface *surf)
{
   struct dri2_egl_surface *dri2_surf = dri2_egl_surface(surf);

   surfaceless_free_images(dri2_surf);

   driDestroyDrawable(dri2_surf->dri_drawable);

   dri2_fini_surface(surf);
   free(dri2_surf);
   return EGL_TRUE;
}

static _EGLSurface *
dri2_surfaceless_create_pbuffer_surface(_EGLDisplay *disp, _EGLConfig *conf,
                                        const EGLint *attrib_list)
{
   return dri2_surfaceless_create_surface(disp, EGL_PBUFFER_BIT, conf,
                                          attrib_list);
}

static const struct dri2_egl_display_vtbl dri2_surfaceless_display_vtbl = {
   .create_pbuffer_surface = dri2_surfaceless_create_pbuffer_surface,
   .destroy_surface = surfaceless_destroy_surface,
   .create_image = dri2_create_image_khr,
   .get_dri_drawable = dri2_surface_get_dri_drawable,
};

#ifdef __APPLE__
/* macOS window surface functions for Kopper/Metal */

static _EGLSurface *
dri2_surfaceless_create_window_surface(_EGLDisplay *disp, _EGLConfig *conf,
                                       void *native_window,
                                       const EGLint *attrib_list)
{
   struct dri2_egl_display *dri2_dpy = dri2_egl_display(disp);
   struct dri2_egl_config *dri2_conf = dri2_egl_config(conf);
   struct dri2_egl_surface *dri2_surf;
   const struct dri_config *config;

   dri2_surf = calloc(1, sizeof *dri2_surf);
   if (!dri2_surf) {
      _eglError(EGL_BAD_ALLOC, "eglCreateWindowSurface");
      return NULL;
   }

   if (!dri2_init_surface(&dri2_surf->base, disp, EGL_WINDOW_BIT, conf,
                          attrib_list, false, native_window))
      goto cleanup_surface;

   config = dri2_get_dri_config(dri2_conf, EGL_WINDOW_BIT,
                                dri2_surf->base.GLColorspace);
   if (!config) {
      _eglError(EGL_BAD_MATCH,
                "Unsupported surfacetype/colorspace configuration");
      goto cleanup_surface;
   }

   dri2_surf->visual = dri2_image_format_for_pbuffer_config(dri2_dpy, config);
   if (dri2_surf->visual == PIPE_FORMAT_NONE)
      goto cleanup_surface;

   if (!dri2_create_drawable(dri2_dpy, config, dri2_surf, dri2_surf))
      goto cleanup_surface;

   return &dri2_surf->base;

cleanup_surface:
   free(dri2_surf);
   return NULL;
}

static EGLBoolean
dri2_surfaceless_kopper_swap_buffers(_EGLDisplay *disp, _EGLSurface *draw)
{
   struct dri2_egl_surface *dri2_surf = dri2_egl_surface(draw);
   kopperSwapBuffers(dri2_surf->dri_drawable,
                     __DRI2_FLUSH_CONTEXT | __DRI2_FLUSH_INVALIDATE_ANCILLARY);
   return EGL_TRUE;
}

static EGLBoolean
dri2_surfaceless_kopper_swap_interval(_EGLDisplay *disp, _EGLSurface *surf,
                                      EGLint interval)
{
   struct dri2_egl_surface *dri2_surf = dri2_egl_surface(surf);
   kopperSetSwapInterval(dri2_surf->dri_drawable, interval);
   return EGL_TRUE;
}

static EGLint
dri2_surfaceless_kopper_query_buffer_age(_EGLDisplay *disp, _EGLSurface *surf)
{
   struct dri2_egl_surface *dri2_surf = dri2_egl_surface(surf);
   return kopperQueryBufferAge(dri2_surf->dri_drawable);
}

static const struct dri2_egl_display_vtbl dri2_surfaceless_metal_display_vtbl = {
   .create_window_surface = dri2_surfaceless_create_window_surface,
   .create_pbuffer_surface = dri2_surfaceless_create_pbuffer_surface,
   .destroy_surface = surfaceless_destroy_surface,
   .create_image = dri2_create_image_khr,
   .swap_buffers = dri2_surfaceless_kopper_swap_buffers,
   .swap_interval = dri2_surfaceless_kopper_swap_interval,
   .query_buffer_age = dri2_surfaceless_kopper_query_buffer_age,
   .get_dri_drawable = dri2_surface_get_dri_drawable,
};
#endif /* __APPLE__ */

static void
surfaceless_flush_front_buffer(struct dri_drawable *driDrawable,
                               void *loaderPrivate)
{
}

static unsigned
surfaceless_get_capability(void *loaderPrivate, enum dri_loader_cap cap)
{
   /* Note: loaderPrivate is _EGLDisplay* */
   switch (cap) {
   case DRI_LOADER_CAP_FP16:
      return 1;
   case DRI_LOADER_CAP_RGBA_ORDERING:
      return 1;
   default:
      return 0;
   }
}

static const __DRIimageLoaderExtension image_loader_extension = {
   .base = {__DRI_IMAGE_LOADER, 2},
   .getBuffers = surfaceless_image_get_buffers,
   .flushFrontBuffer = surfaceless_flush_front_buffer,
   .getCapability = surfaceless_get_capability,
};

static const __DRIextension *image_loader_extensions[] = {
   &image_loader_extension.base,
   &image_lookup_extension.base,
   NULL,
};

static const __DRIextension *swrast_loader_extensions[] = {
   &swrast_pbuffer_loader_extension.base,
   &image_loader_extension.base,
   &image_lookup_extension.base,
   NULL,
};

static const __DRIextension *kopper_loader_extensions[] = {
   &kopper_pbuffer_loader_extension.base,
   &image_lookup_extension.base,
   &image_lookup_extension.base,
   NULL,
};

#ifdef __APPLE__
/* macOS Metal window surface support for Kopper */

#ifdef VK_USE_PLATFORM_METAL_EXT

struct get_size_ctx {
   void *layer;
   double w;
   double h;
};

static void
get_drawable_size_main_thread(void *data)
{
   struct get_size_ctx *ctx = data;
   typedef struct {
      double width;
      double height;
   } MGLSize;

   /* Check superlayer to verify attachment */
   id superlayer = ((id(*)(id, SEL))objc_msgSend)(
      (id)ctx->layer, sel_registerName("superlayer"));

   MGLSize (*msgSendSize)(id, SEL) = (MGLSize(*)(id, SEL))objc_msgSend;
   MGLSize size = msgSendSize((id)ctx->layer, sel_registerName("drawableSize"));
   ctx->w = size.width;
   ctx->h = size.height;
}

static void
surfaceless_metal_kopper_get_drawable_info(struct dri_drawable *draw, int *w,
                                           int *h, void *loaderPrivate)
{
   struct dri2_egl_surface *dri2_surf = loaderPrivate;
   void *layer = dri2_surf->base.NativeSurface;

   if (layer) {
      /* Debugging SIGBUS: Validate layer state */

      /* Check class */
      const char *cls = object_getClassName((id)layer);

      /* Check device property */
      id device =
         ((id(*)(id, SEL))objc_msgSend)((id)layer, sel_registerName("device"));

      /* [layer drawableSize] */
      /* Query on Main Thread to avoid race conditions with CoreAnimation which
       * can cause SIGBUS */

      struct get_size_ctx ctx;
      ctx.layer = layer;
      ctx.w = 0;
      ctx.h = 0;

      if (pthread_main_np()) {
         get_drawable_size_main_thread(&ctx);
      } else {
         dispatch_sync_f(dispatch_get_main_queue(), &ctx,
                         get_drawable_size_main_thread);
      }

      *w = (int)ctx.w;
      *h = (int)ctx.h;
   } else {
      *w = dri2_surf->base.Width;
      *h = dri2_surf->base.Height;
   }
}

#include <objc/message.h>
#include <objc/runtime.h>

struct swap_layer_ctx {
   void *layer;
   void *result_layer;
};

static void
swap_layer_on_main_thread(void *data)
{
   struct swap_layer_ctx *ctx = data;
   void *lblayer = ctx->layer;
   if (!lblayer)
      return;

   const char *className = object_getClassName((id)lblayer);

   if (strcmp(className, "NSViewBackingLayer") == 0 ||
       strcmp(className, "_NSViewBackingLayer") == 0) {
      /* Get the view from the layer's delegate */
      id view = ((id(*)(id, SEL))objc_msgSend)((id)lblayer,
                                               sel_registerName("delegate"));

      if (view) {
         /* [view setWantsLayer:YES] */
         ((void (*)(id, SEL, BOOL))objc_msgSend)(
            view, sel_registerName("setWantsLayer:"), 1);

         /* id newLayer = [CAMetalLayer layer] */
         Class CAMetalLayerClass = objc_getClass("CAMetalLayer");
         if (CAMetalLayerClass) {
            id newLayer = ((id(*)(id, SEL))objc_msgSend)(
               (id)CAMetalLayerClass, sel_registerName("layer"));

            if (newLayer) {
               /* Configure the layer to match the view's dimensions and
                * scale */
               /* CGRect bounds = [view bounds] */
               typedef struct {
                  double x, y, w, h;
               } MGLRect;
               MGLRect (*msgSendRect)(id, SEL) =
                  (MGLRect(*)(id, SEL))objc_msgSend;
               MGLRect viewBounds =
                  msgSendRect(view, sel_registerName("bounds"));

               /* [newLayer setFrame:viewBounds] */
               void (*msgSendRect_v)(id, SEL, MGLRect) =
                  (void (*)(id, SEL, MGLRect))objc_msgSend;
               msgSendRect_v(newLayer, sel_registerName("setFrame:"),
                             viewBounds);

               /* [newLayer setOpaque:YES] - prevents alpha blending
                * with desktop */
               void (*msgSendBool_v)(id, SEL, BOOL) =
                  (void (*)(id, SEL, BOOL))objc_msgSend;
               msgSendBool_v(newLayer, sel_registerName("setOpaque:"), YES);

               /* Get window's backingScaleFactor for Retina display
                * support */
               /* id window = [view window] */
               id window = ((id(*)(id, SEL))objc_msgSend)(
                  view, sel_registerName("window"));
               double scale = 1.0;
               if (window) {
                  /* CGFloat scale = [window backingScaleFactor] */
                  double (*msgSendDouble)(id, SEL) =
                     (double (*)(id, SEL))objc_msgSend;
                  scale = msgSendDouble(window,
                                        sel_registerName("backingScaleFactor"));

                  /* [newLayer setContentsScale:scale] */
                  void (*msgSendDouble_v)(id, SEL, double) =
                     (void (*)(id, SEL, double))objc_msgSend;
                  msgSendDouble_v(newLayer,
                                  sel_registerName("setContentsScale:"), scale);
               }

               /* Explicitly set drawableSize to match the backing store
                * size. CAMetalLayer.drawableSize = view.bounds.size *
                * contentsScale If we don't set this, drawableSize might
                * return 1x1 until the next layout pass.
                */
               typedef struct {
                  double w, h;
               } MGLSize;
               MGLSize drawableSize;
               drawableSize.w = viewBounds.w * scale;
               drawableSize.h = viewBounds.h * scale;
               void (*msgSendSize_v)(id, SEL, MGLSize) =
                  (void (*)(id, SEL, MGLSize))objc_msgSend;
               msgSendSize_v(newLayer, sel_registerName("setDrawableSize:"),
                             drawableSize);

               /* [view setLayer:newLayer] */
               ((void (*)(id, SEL, id))objc_msgSend)(
                  view, sel_registerName("setLayer:"), newLayer);

               /* Explicitly RETAIN the layer to ensure it survives.
                * platform_surfaceless doesn't normally own the window,
                * but here we created the layer. We need to ensure
                * dri2_surf->base.NativeSurface remains valid.
                */
               ((id(*)(id, SEL))objc_msgSend)(newLayer,
                                              sel_registerName("retain"));

               ctx->result_layer = newLayer; /* Pass back the new layer */
            } else {
            }
         } else {
         }
      }
   }
}

static void
surfaceless_metal_kopper_set_surface_create_info(void *_draw,
                                                 struct kopper_loader_info *ci)
{
   struct dri2_egl_surface *dri2_surf = _draw;
   VkMetalSurfaceCreateInfoEXT *metal = (VkMetalSurfaceCreateInfoEXT *)&ci->bos;

   if (dri2_surf->base.Type != EGL_WINDOW_BIT)
      return;

   metal->sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
   metal->pNext = NULL;
   metal->flags = 0;

   void *layer = dri2_surf->base.NativeSurface;

   /* Fix for KosmicKrisp/wsi_metal_surface crash:
    * If we receive an NSViewBackingLayer (generic default layer), we need to
    * force the view to use a CAMetalLayer instead.
    * This MUST be done on the Main Thread to avoid SIGBUS/race conditions.
    */
   if (layer) {
      struct swap_layer_ctx ctx;
      ctx.layer = layer;
      ctx.result_layer = layer;

      /* Avoid deadlock if we are already on the main thread */
      if (pthread_main_np()) {
         swap_layer_on_main_thread(&ctx);
      } else {
         dispatch_sync_f(dispatch_get_main_queue(), &ctx,
                         swap_layer_on_main_thread);
      }

      if (layer != ctx.result_layer) {
         layer = ctx.result_layer;
         dri2_surf->base.NativeSurface =
            layer; /* Update state for git_drawable_info */
      }
   }

   /* The native window is the CAMetalLayer pointer */
   metal->pLayer = layer;
   ci->has_alpha = true; /* Assume alpha support */
   /* Force opaque presentation on Metal - don't blend with desktop */
   ci->present_opaque = true;
}

static const __DRIkopperLoaderExtension kopper_metal_loader_extension = {
   .base = {__DRI_KOPPER_LOADER, 1},

   .SetSurfaceCreateInfo = surfaceless_metal_kopper_set_surface_create_info,
   .GetDrawableInfo = surfaceless_metal_kopper_get_drawable_info,
};

static const __DRIextension *kopper_metal_loader_extensions[] = {
   &kopper_metal_loader_extension.base,
   &image_lookup_extension.base,
   NULL,
};
#endif /* VK_USE_PLATFORM_METAL_EXT */
#endif /* __APPLE__ */

static bool
surfaceless_probe_device(_EGLDisplay *disp, bool swrast, bool zink)
{
   const unsigned node_type = swrast ? DRM_NODE_PRIMARY : DRM_NODE_RENDER;
   struct dri2_egl_display *dri2_dpy = dri2_egl_display(disp);
   _EGLDevice *dev_list = _eglGlobal.DeviceList;
   drmDevicePtr device;

   while (dev_list) {
      if (!_eglDeviceSupports(dev_list, _EGL_DEVICE_DRM))
         goto next;

      if (_eglHasAttrib(disp, EGL_DEVICE_EXT) && dev_list != disp->Device) {
         goto next;
      }

      device = _eglDeviceDrm(dev_list);
      assert(device);

      if (!(device->available_nodes & (1 << node_type)))
         goto next;

      dri2_dpy->fd_render_gpu = loader_open_device(device->nodes[node_type]);
      if (dri2_dpy->fd_render_gpu < 0)
         goto next;

#ifdef HAVE_WAYLAND_PLATFORM
      loader_get_user_preferred_fd(&dri2_dpy->fd_render_gpu,
                                   &dri2_dpy->fd_display_gpu);

      if (dri2_dpy->fd_render_gpu != dri2_dpy->fd_display_gpu) {
         free(dri2_dpy->device_name);
         dri2_dpy->device_name =
            loader_get_device_name_for_fd(dri2_dpy->fd_render_gpu);
         if (!dri2_dpy->device_name) {
            _eglError(EGL_BAD_ALLOC,
                      "surfaceless-egl: failed to get device name "
                      "for requested GPU");
            goto retry;
         }
      }

      /* we have to do the check now, because loader_get_user_preferred_fd
       * will return a render-node when the requested gpu is different
       * to the server, but also if the client asks for the same gpu than
       * the server by requesting its pci-id */
      dri2_dpy->is_render_node =
         drmGetNodeTypeFromFd(dri2_dpy->fd_render_gpu) == DRM_NODE_RENDER;
#endif
      char *driver_name = loader_get_driver_for_fd(dri2_dpy->fd_render_gpu);

      disp->Device = dev_list;
      if (swrast) {
         /* Use kms swrast only with vgem / virtio_gpu.
          * virtio-gpu fallbacks to software rendering when 3D features
          * are unavailable since 6c5ab, and kms_swrast is more
          * feature complete than swrast.
          */
         if (driver_name && (strcmp(driver_name, "vgem") == 0 ||
                             strcmp(driver_name, "virtio_gpu") == 0))
            dri2_dpy->driver_name = strdup("kms_swrast");
         free(driver_name);
      } else {
         /* Use the given hardware driver */
         dri2_dpy->driver_name = driver_name;
      }

      if (dri2_dpy->driver_name) {
         dri2_detect_swrast_kopper(disp);
         if (dri2_dpy->kopper)
            dri2_dpy->loader_extensions = kopper_loader_extensions;
         else if (swrast)
            dri2_dpy->loader_extensions = swrast_loader_extensions;
         else
            dri2_dpy->loader_extensions = image_loader_extensions;

         if (!dri2_create_screen(disp)) {
            _eglLog(_EGL_WARNING, "DRI2: failed to create screen");
            goto retry;
         }

         if (!dri2_dpy->dri_screen_render_gpu->base.screen->caps.graphics) {

            _eglLog(_EGL_DEBUG,
                    "DRI2: Driver %s doesn't support graphics, skipping.",
                    dri2_dpy->driver_name);

            if (dri2_dpy->dri_screen_display_gpu !=
                dri2_dpy->dri_screen_render_gpu) {
               driDestroyScreen(dri2_dpy->dri_screen_display_gpu);
               dri2_dpy->dri_screen_display_gpu = NULL;
            }

            driDestroyScreen(dri2_dpy->dri_screen_render_gpu);
            dri2_dpy->dri_screen_render_gpu = NULL;

            dri2_dpy->own_dri_screen = false;

            goto retry;
         }

         break;
      }

   retry:
      free(dri2_dpy->driver_name);
      dri2_dpy->driver_name = NULL;
      if (dri2_dpy->fd_display_gpu != dri2_dpy->fd_render_gpu)
         close(dri2_dpy->fd_display_gpu);
      dri2_dpy->fd_display_gpu = -1;
      close(dri2_dpy->fd_render_gpu);
      dri2_dpy->fd_render_gpu = -1;

   next:
      dev_list = _eglDeviceNext(dev_list);
   }

   if (!dev_list)
      return false;

   return true;
}

static bool
surfaceless_probe_device_sw(_EGLDisplay *disp)
{
   struct dri2_egl_display *dri2_dpy = dri2_egl_display(disp);
   struct _egl_device *device = _eglFindDevice(dri2_dpy->fd_render_gpu, true);

   dri2_dpy->fd_render_gpu = -1;

   if (_eglHasAttrib(disp, EGL_DEVICE_EXT) && disp->Device != device) {
      return false;
   }

   disp->Device = device;
   assert(disp->Device);

   dri2_dpy->driver_name = strdup(disp->Options.Zink ? "zink" : "swrast");
   if (!dri2_dpy->driver_name)
      return false;

   dri2_detect_swrast_kopper(disp);

   if (dri2_dpy->kopper) {
#if defined(__APPLE__) && defined(VK_USE_PLATFORM_METAL_EXT)
      dri2_dpy->loader_extensions = kopper_metal_loader_extensions;
#else
      dri2_dpy->loader_extensions = kopper_loader_extensions;
#endif
   } else
      dri2_dpy->loader_extensions = swrast_loader_extensions;

   dri2_dpy->fd_display_gpu = dri2_dpy->fd_render_gpu;

   if (!dri2_create_screen(disp)) {
      _eglLog(_EGL_WARNING, "DRI2: failed to create screen");
      free(dri2_dpy->driver_name);
      dri2_dpy->driver_name = NULL;
      return false;
   }

   return true;
}

EGLBoolean
dri2_initialize_surfaceless(_EGLDisplay *disp)
{
   const char *err;
   bool driver_loaded = false;
   struct dri2_egl_display *dri2_dpy = dri2_egl_display(disp);

   /* When ForceSoftware is false, we try the HW driver.  When ForceSoftware
    * is true, we try kms_swrast and swrast in order.
    */
   driver_loaded = surfaceless_probe_device(disp, disp->Options.ForceSoftware,
                                            disp->Options.Zink);
   /* On macOS (Darwin) or when ForceSoftware is set, fall back to swrast/zink
    * if no DRM devices were found.
    */
   if (!driver_loaded &&
       (disp->Options.ForceSoftware
#ifdef __APPLE__
        ||
        true /* Always try fallback on macOS since there are no DRM devices */
#endif
        )) {
      _eglLog(_EGL_DEBUG, "Falling back to surfaceless swrast without DRM.");
      driver_loaded = surfaceless_probe_device_sw(disp);
   }

   if (!driver_loaded) {
      err = "DRI2: failed to load driver";
      goto cleanup;
   }

   dri2_setup_screen(disp);
#ifdef HAVE_WAYLAND_PLATFORM
   dri2_dpy->device_name =
      loader_get_device_name_for_fd(dri2_dpy->fd_render_gpu);
#endif
   dri2_set_WL_bind_wayland_display(disp);

   dri2_add_pbuffer_configs_for_visuals(disp);

#ifdef __APPLE__
   /* On macOS, also add window configs when kopper is enabled so that
    * window surfaces can be created via the kopper/Metal presentation path.
    */
   if (dri2_dpy->kopper) {
      for (unsigned i = 0; dri2_dpy->driver_configs[i] != NULL; i++) {
         dri2_add_config(disp, dri2_dpy->driver_configs[i],
                         EGL_WINDOW_BIT | EGL_PBUFFER_BIT, NULL);
      }
   }
#endif

   /* Fill vtbl last to prevent accidentally calling virtual function during
    * initialization.
    */
#if defined(__APPLE__) && defined(VK_USE_PLATFORM_METAL_EXT)
   if (dri2_dpy->kopper)
      dri2_dpy->vtbl = &dri2_surfaceless_metal_display_vtbl;
   else
#endif
      dri2_dpy->vtbl = &dri2_surfaceless_display_vtbl;

   return EGL_TRUE;

cleanup:
   return _eglError(EGL_NOT_INITIALIZED, err);
}
