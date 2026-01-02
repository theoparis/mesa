# Copyright © 2024 Luca Mignatti
# SPDX-License-Identifier: MIT

# libgl_interpose.dylib - intercepts dlsym calls to redirect GL lookups to EGL
# This is macOS-specific and uses DYLD_INTERPOSE

libgl_interpose = shared_library(
  'gl_interpose',
  'libgl_interpose.c',
  include_directories : [inc_include],
  dependencies : [dep_dl],
  install : true,
  name_prefix : 'lib',
  name_suffix : 'dylib',
)

# libGL.dylib - wrapper that provides GL function symbols forwarded to EGL
# This is for applications that load libGL and dlsym for GL functions

libgl_wrapper = shared_library(
  'GL',
  'libgl_wrapper.c',
  include_directories : [inc_include],
  dependencies : [dep_dl],
  # These functions are intentionally exported without prototypes
  c_args : ['-Wno-missing-prototypes'],
  install : true,
  name_prefix : 'lib',
  name_suffix : 'dylib',
)
