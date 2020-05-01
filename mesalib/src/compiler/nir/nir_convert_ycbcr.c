/*
 * Copyright © 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "nir_vulkan.h"
#include <math.h>

static nir_ssa_def *
y_range(nir_builder *b,
        nir_ssa_def *y_channel,
        int bpc,
        VkSamplerYcbcrRange range)
{
   switch (range) {
   case VK_SAMPLER_YCBCR_RANGE_ITU_FULL:
      return y_channel;
   case VK_SAMPLER_YCBCR_RANGE_ITU_NARROW:
      return nir_fmul(b,
                      nir_fadd(b,
                               nir_fmul(b, y_channel,
                                        nir_imm_float(b, pow(2, bpc) - 1)),
                               nir_imm_float(b, -16.0f * pow(2, bpc - 8))),
                      nir_frcp(b, nir_imm_float(b, 219.0f * pow(2, bpc - 8))));
   default:
      unreachable("missing Ycbcr range");
      return NULL;
   }
}

static nir_ssa_def *
chroma_range(nir_builder *b,
             nir_ssa_def *chroma_channel,
             int bpc,
             VkSamplerYcbcrRange range)
{
   switch (range) {
   case VK_SAMPLER_YCBCR_RANGE_ITU_FULL:
      return nir_fadd(b, chroma_channel,
                      nir_imm_float(b, -pow(2, bpc - 1) / (pow(2, bpc) - 1.0f)));
   case VK_SAMPLER_YCBCR_RANGE_ITU_NARROW:
      return nir_fmul(b,
                      nir_fadd(b,
                               nir_fmul(b, chroma_channel,
                                        nir_imm_float(b, pow(2, bpc) - 1)),
                               nir_imm_float(b, -128.0f * pow(2, bpc - 8))),
                      nir_frcp(b, nir_imm_float(b, 224.0f * pow(2, bpc - 8))));
   default:
      unreachable("missing Ycbcr range");
      return NULL;
   }
}

typedef struct nir_const_value_3_4 {
   nir_const_value v[3][4];
} nir_const_value_3_4;

static const nir_const_value_3_4 *
ycbcr_model_to_rgb_matrix(VkSamplerYcbcrModelConversion model)
{
   switch (model) {
   case VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601: {
      static const nir_const_value_3_4 bt601 = { {
         { { .f32 =  1.402f             }, { .f32 = 1.0f }, { .f32 =  0.0f               }, { .f32 = 0.0f } },
         { { .f32 = -0.714136286201022f }, { .f32 = 1.0f }, { .f32 = -0.344136286201022f }, { .f32 = 0.0f } },
         { { .f32 =  0.0f               }, { .f32 = 1.0f }, { .f32 =  1.772f             }, { .f32 = 0.0f } },
      } };

      return &bt601;
   }
   case VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709: {
      static const nir_const_value_3_4 bt709 = { {
         { { .f32 =  1.5748031496063f   }, { .f32 = 1.0f }, { .f32 =  0.0f               }, { .f32 = 0.0f } },
         { { .f32 = -0.468125209181067f }, { .f32 = 1.0f }, { .f32 = -0.187327487470334f }, { .f32 = 0.0f } },
         { { .f32 =  0.0f               }, { .f32 = 1.0f }, { .f32 =  1.85563184264242f  }, { .f32 = 0.0f } },
      } };

      return &bt709;
   }
   case VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_2020: {
      static const nir_const_value_3_4 bt2020 = { {
         { { .f32 =  1.4746f            }, { .f32 = 1.0f }, { .f32 =  0.0f               }, { .f32 = 0.0f } },
         { { .f32 = -0.571353126843658f }, { .f32 = 1.0f }, { .f32 = -0.164553126843658f }, { .f32 = 0.0f } },
         { { .f32 =  0.0f               }, { .f32 = 1.0f }, { .f32 =  1.8814f            }, { .f32 = 0.0f } },
      } };

      return &bt2020;
   }
   default:
      unreachable("missing Ycbcr model");
      return NULL;
   }
}

nir_ssa_def *
nir_convert_ycbcr_to_rgb(nir_builder *b,
                         VkSamplerYcbcrModelConversion model,
                         VkSamplerYcbcrRange range,
                         nir_ssa_def *raw_channels,
                         uint32_t *bpcs)
{
   nir_ssa_def *expanded_channels =
      nir_vec4(b,
               chroma_range(b, nir_channel(b, raw_channels, 0), bpcs[0], range),
               y_range(b, nir_channel(b, raw_channels, 1), bpcs[1], range),
               chroma_range(b, nir_channel(b, raw_channels, 2), bpcs[2], range),
               nir_channel(b, raw_channels, 3));

   if (model == VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_IDENTITY)
      return expanded_channels;

   const nir_const_value_3_4 *conversion_matrix =
      ycbcr_model_to_rgb_matrix(model);

   nir_ssa_def *converted_channels[] = {
      nir_fdot4(b, expanded_channels, nir_build_imm(b, 4, 32, conversion_matrix->v[0])),
      nir_fdot4(b, expanded_channels, nir_build_imm(b, 4, 32, conversion_matrix->v[1])),
      nir_fdot4(b, expanded_channels, nir_build_imm(b, 4, 32, conversion_matrix->v[2]))
   };

   return nir_vec4(b,
                   converted_channels[0], converted_channels[1],
                   converted_channels[2], nir_channel(b, raw_channels, 3));
}
