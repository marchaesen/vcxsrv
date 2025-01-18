/*
 * Copyright © 2014 Connor Abbott
 * SPDX-License-Identifier: MIT
 */

/*
 * This file is split off from nir.h to allow #include'ing these defines from
 * OpenCL code.
 */

#ifndef NIR_DEFINES_H
#define NIR_DEFINES_H

/** NIR sized and unsized types
 *
 * The values in this enum are carefully chosen so that the sized type is
 * just the unsized type OR the number of bits.
 */
/* clang-format off */
typedef enum ENUM_PACKED {
   nir_type_invalid =   0, /* Not a valid type */
   nir_type_int =       2,
   nir_type_uint =      4,
   nir_type_bool =      6,
   nir_type_float =     128,
   nir_type_bool1 =     1  | nir_type_bool,
   nir_type_bool8 =     8  | nir_type_bool,
   nir_type_bool16 =    16 | nir_type_bool,
   nir_type_bool32 =    32 | nir_type_bool,
   nir_type_int1 =      1  | nir_type_int,
   nir_type_int8 =      8  | nir_type_int,
   nir_type_int16 =     16 | nir_type_int,
   nir_type_int32 =     32 | nir_type_int,
   nir_type_int64 =     64 | nir_type_int,
   nir_type_uint1 =     1  | nir_type_uint,
   nir_type_uint8 =     8  | nir_type_uint,
   nir_type_uint16 =    16 | nir_type_uint,
   nir_type_uint32 =    32 | nir_type_uint,
   nir_type_uint64 =    64 | nir_type_uint,
   nir_type_float16 =   16 | nir_type_float,
   nir_type_float32 =   32 | nir_type_float,
   nir_type_float64 =   64 | nir_type_float,
} nir_alu_type;
/* clang-format on */

#define NIR_ALU_TYPE_SIZE_MASK      0x79
#define NIR_ALU_TYPE_BASE_TYPE_MASK 0x86

static inline unsigned
nir_alu_type_get_type_size(nir_alu_type type)
{
   return type & NIR_ALU_TYPE_SIZE_MASK;
}

static inline nir_alu_type
nir_alu_type_get_base_type(nir_alu_type type)
{
   return (nir_alu_type)(type & NIR_ALU_TYPE_BASE_TYPE_MASK);
}

#endif
