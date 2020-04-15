/*
 * Copyright (C) 2020 Collabora Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors (Collabora):
 *      Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#include "bit.h"
#include "panfrost/pandecode/decode.h"
#include "drm-uapi/panfrost_drm.h"
#include "panfrost/encoder/pan_encoder.h"

/* Standalone compiler tests submitting jobs directly to the hardware. Uses the
 * `bit` prefix for `BIfrost Tests` and because bit sounds wicked cool. */

static struct panfrost_bo *
bit_bo_create(struct panfrost_device *dev, size_t size)
{
        struct panfrost_bo *bo = panfrost_bo_create(dev, size, PAN_BO_EXECUTE);
        pandecode_inject_mmap(bo->gpu, bo->cpu, bo->size, NULL);
        return bo;
}

struct panfrost_device *
bit_initialize(void *memctx)
{
        int fd = drmOpenWithType("panfrost", NULL, DRM_NODE_RENDER);

        if (fd < 0)
                unreachable("No panfrost device found. Try chmod?");

        struct panfrost_device *dev = rzalloc(memctx, struct panfrost_device);
        panfrost_open_device(memctx, fd, dev);

        pandecode_initialize(true);
        printf("%X\n", dev->gpu_id);

        return dev;
}

static bool
bit_submit(struct panfrost_device *dev,
                enum mali_job_type T,
                void *payload, size_t payload_size,
                struct panfrost_bo **bos, size_t bo_count, enum bit_debug debug)
{
        struct mali_job_descriptor_header header = {
                .job_descriptor_size = MALI_JOB_64,
                .job_type = T,
                .job_index = 1
        };

        struct panfrost_bo *job = bit_bo_create(dev, 4096);
        memcpy(job->cpu, &header, sizeof(header));
        memcpy(job->cpu + sizeof(header), payload, payload_size);

        uint32_t *bo_handles = calloc(sizeof(uint32_t), bo_count);

        for (unsigned i = 0; i < bo_count; ++i)
                bo_handles[i] = bos[i]->gem_handle;

        uint32_t syncobj = 0;
        int ret = 0;

        ret = drmSyncobjCreate(dev->fd, DRM_SYNCOBJ_CREATE_SIGNALED, &syncobj);
        assert(!ret);

        struct drm_panfrost_submit submit = {
                .jc = job->gpu,
                .bo_handles = (uintptr_t) bo_handles,
                .bo_handle_count = bo_count,
                .out_sync = syncobj,
        };

        ret = drmIoctl(dev->fd, DRM_IOCTL_PANFROST_SUBMIT, &submit);
        assert(!ret);
        free(bo_handles);

        drmSyncobjWait(dev->fd, &syncobj, 1, INT64_MAX, 0, NULL);
        if (debug >= BIT_DEBUG_ALL)
                pandecode_jc(submit.jc, true, dev->gpu_id, false);
        return true;
}

/* Checks that the device is alive and responding to basic jobs as a sanity
 * check - prerequisite to running code on the device. We test this via a
 * WRITE_VALUE job */

bool
bit_sanity_check(struct panfrost_device *dev)
{
        struct panfrost_bo *scratch = bit_bo_create(dev, 65536);
        ((uint32_t *) scratch->cpu)[0] = 0xAA;

        struct mali_payload_write_value payload = {
                .address = scratch->gpu,
                .value_descriptor = MALI_WRITE_VALUE_ZERO
        };

        struct panfrost_bo *bos[] = { scratch };
        bool success = bit_submit(dev, JOB_TYPE_WRITE_VALUE,
                        &payload, sizeof(payload), bos, 1, false);

        return success && (((uint8_t *) scratch->cpu)[0] == 0x0);
}

/* Constructs a vertex job */

bool
bit_vertex(struct panfrost_device *dev, panfrost_program prog,
                uint32_t *iubo, size_t sz_ubo,
                uint32_t *iattr, size_t sz_attr,
                uint32_t *expected, size_t sz_expected, enum bit_debug debug)
{

        struct panfrost_bo *scratchpad = bit_bo_create(dev, 4096);
        struct panfrost_bo *shader = bit_bo_create(dev, prog.compiled.size);
        struct panfrost_bo *shader_desc = bit_bo_create(dev, 4096);
        struct panfrost_bo *ubo = bit_bo_create(dev, 4096);
        struct panfrost_bo *var = bit_bo_create(dev, 4096);
        struct panfrost_bo *attr = bit_bo_create(dev, 4096);

        struct mali_attr_meta vmeta = {
                .index = 0,
                .format = MALI_RGBA32UI
        };

        union mali_attr vary = {
                .elements = (var->gpu + 1024) | MALI_ATTR_LINEAR,
                .size = 1024
        };

        union mali_attr attr_ = {
                .elements = (attr->gpu + 1024) | MALI_ATTR_LINEAR,
                .size = 1024
        };

        uint64_t my_ubo = MALI_MAKE_UBO(64, ubo->gpu + 1024);

        memcpy(ubo->cpu, &my_ubo, sizeof(my_ubo));
        memcpy(var->cpu, &vmeta, sizeof(vmeta));

        vmeta.unknown1 = 0x2; /* XXX: only attrib? */
        memcpy(attr->cpu, &vmeta, sizeof(vmeta));
        memcpy(var->cpu + 256, &vary, sizeof(vary));
        memcpy(attr->cpu + 256, &attr_, sizeof(vary));

        if (sz_ubo)
                memcpy(ubo->cpu + 1024, iubo, sz_ubo);

        if (sz_attr)
                memcpy(attr->cpu + 1024, iattr, sz_attr);

        struct panfrost_bo *shmem = bit_bo_create(dev, 4096);
        struct mali_shared_memory shmemp = {
                .scratchpad = scratchpad->gpu,
                .shared_workgroup_count = 0x1f,
        };

        memcpy(shmem->cpu, &shmemp, sizeof(shmemp));

        struct mali_shader_meta meta = {
                .shader = shader->gpu,
                .attribute_count = 1,
                .varying_count = 1,
                .bifrost1 = {
                        .unk1 = 0x800200,
                        .uniform_buffer_count = 1,
                },
                .bifrost2 = {
                        .unk3 = 0x0,
                        .preload_regs = 0xc0,
                        .uniform_count = sz_ubo / 16,
                        .unk4 = 0x0,
                },
        };

        memcpy(shader_desc->cpu, &meta, sizeof(meta));
        memcpy(shader->cpu, prog.compiled.data, prog.compiled.size);

        struct bifrost_payload_vertex payload = {
                .prefix = {
                },
                .postfix = {
                        .gl_enables = 0x2,
                        .shared_memory = shmem->gpu,
                        .shader = shader_desc->gpu,
                        .uniforms = ubo->gpu + 1024,
                        .uniform_buffers = ubo->gpu,
                        .attribute_meta = attr->gpu,
                        .attributes = attr->gpu + 256,
                        .varying_meta = var->gpu,
                        .varyings = var->gpu + 256,
                },
        };

        panfrost_pack_work_groups_compute(&payload.prefix,
                        1, 1, 1,
                        1, 1, 1,
                        true);

        payload.prefix.workgroups_x_shift_3 = 5;

        struct panfrost_bo *bos[] = {
                scratchpad, shmem, shader, shader_desc, ubo, var, attr
        };

        bool succ = bit_submit(dev, JOB_TYPE_VERTEX, &payload,
                        sizeof(payload), bos, ARRAY_SIZE(bos), debug);

        /* Check the output varyings */

        uint32_t *output = (uint32_t *) (var->cpu + 1024);
        float *foutput = (float *) output;
        float *fexpected = (float *) expected;

        if (sz_expected) {
                unsigned comp = memcmp(output, expected, sz_expected);
                succ &= (comp == 0);

                if (comp && (debug >= BIT_DEBUG_FAIL)) {
                        fprintf(stderr, "expected [");

                        for (unsigned i = 0; i < (sz_expected >> 2); ++i)
                                fprintf(stderr, "%08X /* %f */ ", expected[i], fexpected[i]);

                        fprintf(stderr, "], got [");

                        for (unsigned i = 0; i < (sz_expected >> 2); ++i)
                                fprintf(stderr, "%08X /* %f */ ", output[i], foutput[i]);

                        fprintf(stderr, "\n");
                }
        } else if (debug == BIT_DEBUG_ALL) {
                fprintf(stderr, "got [");

                for (unsigned i = 0; i < 4; ++i)
                        fprintf(stderr, "%08X /* %f */ ", output[i], foutput[i]);

                fprintf(stderr, "\n");
        }

        return succ;
}
