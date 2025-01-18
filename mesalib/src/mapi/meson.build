# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

genCommon_py = files('new/genCommon.py')
glapi_gen_mapi_py = files('new/gen_gldispatch_mapi.py')
mapi_abi_py = files('mapi_abi.py')

subdir('glapi')
if with_shared_glapi
  subdir('shared-glapi')
else
  libglapi = []
endif
if not with_glvnd
  if with_gles1
    subdir('es1api')
  endif
  if with_gles2
    subdir('es2api')
  endif
endif
