/*
 * Copyright 2024 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_SHADER_DEBUG_INFO_H
#define AC_SHADER_DEBUG_INFO_H

#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

enum ac_shader_debug_info_type {
   ac_shader_debug_info_src_loc,
};

/*
 * ac_shader_debug_info holds information about a sequence of hardware instructions starting
 * at ac_shader_debug_info::offset and ending at the offset of the next ac_shader_debug_info.
 */
struct ac_shader_debug_info {
   enum ac_shader_debug_info_type type;

   union {
      struct {
         /* Line number and spirv offset this instruction sequence was generated from. */
         char *file;
         uint32_t line;
         uint32_t column;
         uint32_t spirv_offset;
      } src_loc;
   };

   /* Offset into the shader binary: */
   uint32_t offset;
};

#ifdef __cplusplus
}
#endif

#endif
