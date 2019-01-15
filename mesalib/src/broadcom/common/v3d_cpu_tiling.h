/*
 * Copyright © 2017 Broadcom
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

/** @file v3d_cpu_tiling.h
 *
 * Contains load/store functions common to both v3d and vc4.  The utile layout
 * stayed the same, though the way utiles get laid out has changed.
 */

static inline void
v3d_load_utile(void *cpu, uint32_t cpu_stride,
               void *gpu, uint32_t gpu_stride)
{
#if defined(V3D_BUILD_NEON) && defined(PIPE_ARCH_ARM)
        if (gpu_stride == 8) {
                __asm__ volatile (
                        /* Load from the GPU in one shot, no interleave, to
                         * d0-d7.
                         */
                        "vldm %0, {q0, q1, q2, q3}\n"
                        /* Store each 8-byte line to cpu-side destination,
                         * incrementing it by the stride each time.
                         */
                        "vst1.8 d0, [%1], %2\n"
                        "vst1.8 d1, [%1], %2\n"
                        "vst1.8 d2, [%1], %2\n"
                        "vst1.8 d3, [%1], %2\n"
                        "vst1.8 d4, [%1], %2\n"
                        "vst1.8 d5, [%1], %2\n"
                        "vst1.8 d6, [%1], %2\n"
                        "vst1.8 d7, [%1]\n"
                        :
                        : "r"(gpu), "r"(cpu), "r"(cpu_stride)
                        : "q0", "q1", "q2", "q3");
                return;
        } else if (gpu_stride == 16) {
                __asm__ volatile (
                        /* Load from the GPU in one shot, no interleave, to
                         * d0-d7.
                         */
                        "vldm %0, {q0, q1, q2, q3};\n"
                        /* Store each 16-byte line in 2 parts to the cpu-side
                         * destination.  (vld1 can only store one d-register
                         * at a time).
                         */
                        "vst1.8 d0, [%1], %3\n"
                        "vst1.8 d1, [%2], %3\n"
                        "vst1.8 d2, [%1], %3\n"
                        "vst1.8 d3, [%2], %3\n"
                        "vst1.8 d4, [%1], %3\n"
                        "vst1.8 d5, [%2], %3\n"
                        "vst1.8 d6, [%1]\n"
                        "vst1.8 d7, [%2]\n"
                        :
                        : "r"(gpu), "r"(cpu), "r"(cpu + 8), "r"(cpu_stride)
                        : "q0", "q1", "q2", "q3");
                return;
        }
#elif defined (PIPE_ARCH_AARCH64)
        if (gpu_stride == 8) {
                __asm__ volatile (
                        /* Load from the GPU in one shot, no interleave, to
                         * d0-d7.
                         */
                        "ld1 {v0.2d, v1.2d, v2.2d, v3.2d}, [%0]\n"
                        /* Store each 8-byte line to cpu-side destination,
                         * incrementing it by the stride each time.
                         */
                        "st1 {v0.D}[0], [%1], %2\n"
                        "st1 {v0.D}[1], [%1], %2\n"
                        "st1 {v1.D}[0], [%1], %2\n"
                        "st1 {v1.D}[1], [%1], %2\n"
                        "st1 {v2.D}[0], [%1], %2\n"
                        "st1 {v2.D}[1], [%1], %2\n"
                        "st1 {v3.D}[0], [%1], %2\n"
                        "st1 {v3.D}[1], [%1]\n"
                        :
                        : "r"(gpu), "r"(cpu), "r"(cpu_stride)
                        : "v0", "v1", "v2", "v3");
                return;
        } else if (gpu_stride == 16) {
                __asm__ volatile (
                        /* Load from the GPU in one shot, no interleave, to
                         * d0-d7.
                         */
                        "ld1 {v0.2d, v1.2d, v2.2d, v3.2d}, [%0]\n"
                        /* Store each 16-byte line in 2 parts to the cpu-side
                         * destination.  (vld1 can only store one d-register
                         * at a time).
                         */
                        "st1 {v0.D}[0], [%1], %3\n"
                        "st1 {v0.D}[1], [%2], %3\n"
                        "st1 {v1.D}[0], [%1], %3\n"
                        "st1 {v1.D}[1], [%2], %3\n"
                        "st1 {v2.D}[0], [%1], %3\n"
                        "st1 {v2.D}[1], [%2], %3\n"
                        "st1 {v3.D}[0], [%1]\n"
                        "st1 {v3.D}[1], [%2]\n"
                        :
                        : "r"(gpu), "r"(cpu), "r"(cpu + 8), "r"(cpu_stride)
                        : "v0", "v1", "v2", "v3");
                return;
        }
#endif

        for (uint32_t gpu_offset = 0; gpu_offset < 64; gpu_offset += gpu_stride) {
                memcpy(cpu, gpu + gpu_offset, gpu_stride);
                cpu += cpu_stride;
        }
}

static inline void
v3d_store_utile(void *gpu, uint32_t gpu_stride,
                void *cpu, uint32_t cpu_stride)
{
#if defined(V3D_BUILD_NEON) && defined(PIPE_ARCH_ARM)
        if (gpu_stride == 8) {
                __asm__ volatile (
                        /* Load each 8-byte line from cpu-side source,
                         * incrementing it by the stride each time.
                         */
                        "vld1.8 d0, [%1], %2\n"
                        "vld1.8 d1, [%1], %2\n"
                        "vld1.8 d2, [%1], %2\n"
                        "vld1.8 d3, [%1], %2\n"
                        "vld1.8 d4, [%1], %2\n"
                        "vld1.8 d5, [%1], %2\n"
                        "vld1.8 d6, [%1], %2\n"
                        "vld1.8 d7, [%1]\n"
                        /* Load from the GPU in one shot, no interleave, to
                         * d0-d7.
                         */
                        "vstm %0, {q0, q1, q2, q3}\n"
                        :
                        : "r"(gpu), "r"(cpu), "r"(cpu_stride)
                        : "q0", "q1", "q2", "q3");
                return;
        } else if (gpu_stride == 16) {
                __asm__ volatile (
                        /* Load each 16-byte line in 2 parts from the cpu-side
                         * destination.  (vld1 can only store one d-register
                         * at a time).
                         */
                        "vld1.8 d0, [%1], %3\n"
                        "vld1.8 d1, [%2], %3\n"
                        "vld1.8 d2, [%1], %3\n"
                        "vld1.8 d3, [%2], %3\n"
                        "vld1.8 d4, [%1], %3\n"
                        "vld1.8 d5, [%2], %3\n"
                        "vld1.8 d6, [%1]\n"
                        "vld1.8 d7, [%2]\n"
                        /* Store to the GPU in one shot, no interleave. */
                        "vstm %0, {q0, q1, q2, q3}\n"
                        :
                        : "r"(gpu), "r"(cpu), "r"(cpu + 8), "r"(cpu_stride)
                        : "q0", "q1", "q2", "q3");
                return;
        }
#elif defined (PIPE_ARCH_AARCH64)
        if (gpu_stride == 8) {
                __asm__ volatile (
                        /* Load each 8-byte line from cpu-side source,
                         * incrementing it by the stride each time.
                         */
                        "ld1 {v0.D}[0], [%1], %2\n"
                        "ld1 {v0.D}[1], [%1], %2\n"
                        "ld1 {v1.D}[0], [%1], %2\n"
                        "ld1 {v1.D}[1], [%1], %2\n"
                        "ld1 {v2.D}[0], [%1], %2\n"
                        "ld1 {v2.D}[1], [%1], %2\n"
                        "ld1 {v3.D}[0], [%1], %2\n"
                        "ld1 {v3.D}[1], [%1]\n"
                        /* Store to the GPU in one shot, no interleave. */
                        "st1 {v0.2d, v1.2d, v2.2d, v3.2d}, [%0]\n"
                        :
                        : "r"(gpu), "r"(cpu), "r"(cpu_stride)
                        : "v0", "v1", "v2", "v3");
                return;
        } else if (gpu_stride == 16) {
                __asm__ volatile (
                        /* Load each 16-byte line in 2 parts from the cpu-side
                         * destination.  (vld1 can only store one d-register
                         * at a time).
                         */
                        "ld1 {v0.D}[0], [%1], %3\n"
                        "ld1 {v0.D}[1], [%2], %3\n"
                        "ld1 {v1.D}[0], [%1], %3\n"
                        "ld1 {v1.D}[1], [%2], %3\n"
                        "ld1 {v2.D}[0], [%1], %3\n"
                        "ld1 {v2.D}[1], [%2], %3\n"
                        "ld1 {v3.D}[0], [%1]\n"
                        "ld1 {v3.D}[1], [%2]\n"
                        /* Store to the GPU in one shot, no interleave. */
                        "st1 {v0.2d, v1.2d, v2.2d, v3.2d}, [%0]\n"
                        :
                        : "r"(gpu), "r"(cpu), "r"(cpu + 8), "r"(cpu_stride)
                        : "v0", "v1", "v2", "v3");
                return;
        }
#endif

        for (uint32_t gpu_offset = 0; gpu_offset < 64; gpu_offset += gpu_stride) {
                memcpy(gpu + gpu_offset, cpu, gpu_stride);
                cpu += cpu_stride;
        }
}
