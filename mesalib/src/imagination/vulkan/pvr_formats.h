/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef PVR_FORMATS_H
#define PVR_FORMATS_H

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

enum pvr_pbe_accum_format {
   PVR_PBE_ACCUM_FORMAT_INVALID = 0, /* Explicitly treat 0 as invalid. */
   PVR_PBE_ACCUM_FORMAT_U8,
   PVR_PBE_ACCUM_FORMAT_S8,
   PVR_PBE_ACCUM_FORMAT_U16,
   PVR_PBE_ACCUM_FORMAT_S16,
   PVR_PBE_ACCUM_FORMAT_F16,
   PVR_PBE_ACCUM_FORMAT_F32,
   PVR_PBE_ACCUM_FORMAT_UINT8,
   PVR_PBE_ACCUM_FORMAT_UINT16,
   PVR_PBE_ACCUM_FORMAT_UINT32,
   PVR_PBE_ACCUM_FORMAT_SINT8,
   PVR_PBE_ACCUM_FORMAT_SINT16,
   PVR_PBE_ACCUM_FORMAT_SINT32,
   /* Formats with medp shader output precision. */
   PVR_PBE_ACCUM_FORMAT_UINT32_MEDP,
   PVR_PBE_ACCUM_FORMAT_SINT32_MEDP,
   PVR_PBE_ACCUM_FORMAT_U1010102,
   PVR_PBE_ACCUM_FORMAT_U24,
};

const uint8_t *pvr_get_format_swizzle(VkFormat vk_format);
uint32_t pvr_get_tex_format(VkFormat vk_format);
uint32_t pvr_get_pbe_packmode(VkFormat vk_format);
uint32_t pvr_get_pbe_accum_format(VkFormat vk_format);
bool pvr_format_is_pbe_downscalable(VkFormat vk_format);

#endif /* PVR_FORMATS_H */
