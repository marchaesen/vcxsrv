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
#include "panfrost/lib/decode.h"
#include "drm-uapi/panfrost_drm.h"
#include "panfrost/lib/pan_encoder.h"

/* Standalone compiler tests submitting jobs directly to the hardware. Uses the
 * `bit` prefix for `BIfrost Tests` and because bit sounds wicked cool. */

static struct panfrost_bo *
bit_bo_create(struct panfrost_device *dev, size_t size)
{
        struct panfrost_bo *bo = panfrost_bo_create(dev, size, PAN_BO_EXECUTE);
        pandecode_inject_mmap(bo->ptr.gpu, bo->ptr.cpu, bo->size, NULL);
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
        struct panfrost_bo *job = bit_bo_create(dev, 4096);
        pan_pack(job->ptr.cpu, JOB_HEADER, cfg) {
                cfg.type = T;
                cfg.index = 1;
        }
        memcpy(job->ptr.cpu + MALI_JOB_HEADER_LENGTH, payload, payload_size);

        uint32_t *bo_handles = calloc(sizeof(uint32_t), bo_count);

        for (unsigned i = 0; i < bo_count; ++i)
                bo_handles[i] = bos[i]->gem_handle;

        uint32_t syncobj = 0;
        int ret = 0;

        ret = drmSyncobjCreate(dev->fd, DRM_SYNCOBJ_CREATE_SIGNALED, &syncobj);
        assert(!ret);

        struct drm_panfrost_submit submit = {
                .jc = job->ptr.gpu,
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
        ((uint32_t *) scratch->ptr.cpu)[0] = 0xAA;

        struct mali_write_value_job_payload_packed payload;

        pan_pack(&payload, WRITE_VALUE_JOB_PAYLOAD, cfg) {
                cfg.address = scratch->ptr.gpu;
                cfg.type = MALI_WRITE_VALUE_TYPE_ZERO;
        };

        struct panfrost_bo *bos[] = { scratch };
        bool success = bit_submit(dev, MALI_JOB_TYPE_WRITE_VALUE,
                        &payload, sizeof(payload), bos, 1, false);

        return success && (((uint8_t *) scratch->ptr.cpu)[0] == 0x0);
}

/* Constructs a vertex job */

bool
bit_vertex(struct panfrost_device *dev, panfrost_program *prog,
                uint32_t *iubo, size_t sz_ubo,
                uint32_t *iattr, size_t sz_attr,
                uint32_t *expected, size_t sz_expected, enum bit_debug debug)
{
        struct panfrost_bo *shader = bit_bo_create(dev, prog->compiled.size);
        struct panfrost_bo *shader_desc = bit_bo_create(dev, 4096);
        struct panfrost_bo *ubo = bit_bo_create(dev, 4096);
        struct panfrost_bo *var = bit_bo_create(dev, 4096);
        struct panfrost_bo *attr = bit_bo_create(dev, 4096);

        pan_pack(attr->ptr.cpu, ATTRIBUTE, cfg) {
                cfg.format = (MALI_RGBA32UI << 12);
                cfg.offset_enable = true;
        }

        pan_pack(var->ptr.cpu, ATTRIBUTE, cfg) {
                cfg.format = (MALI_RGBA32UI << 12);
                cfg.offset_enable = false;
        }

        pan_pack(var->ptr.cpu + 256, ATTRIBUTE_BUFFER, cfg) {
                cfg.pointer = (var->ptr.gpu + 1024);
                cfg.size = 1024;
        }

        pan_pack(attr->ptr.cpu + 256, ATTRIBUTE_BUFFER, cfg) {
                cfg.pointer = (attr->ptr.gpu + 1024);
                cfg.size = 1024;
        }

        pan_pack(ubo->ptr.cpu, UNIFORM_BUFFER, cfg) {
                cfg.entries = sz_ubo / 16;
                cfg.pointer = ubo->ptr.gpu + 1024;
        }

        if (sz_ubo)
                memcpy(ubo->ptr.cpu + 1024, iubo, sz_ubo);

        if (sz_attr)
                memcpy(attr->ptr.cpu + 1024, iattr, sz_attr);

        struct panfrost_bo *shmem = bit_bo_create(dev, 4096);

        pan_pack(shmem->ptr.cpu, LOCAL_STORAGE, cfg) {
                cfg.wls_instances = MALI_LOCAL_STORAGE_NO_WORKGROUP_MEM;
        }

        pan_pack(shader_desc->ptr.cpu, RENDERER_STATE, cfg) {
                cfg.shader.shader = shader->ptr.gpu;
                cfg.shader.attribute_count = cfg.shader.varying_count = 1;
                cfg.properties.uniform_buffer_count = 1;
                cfg.properties.bifrost.zs_update_operation = MALI_PIXEL_KILL_STRONG_EARLY;
                cfg.preload.vertex.vertex_id = true;
                cfg.preload.vertex.instance_id = true;
                cfg.preload.uniform_count = (sz_ubo / 16);
        }

        memcpy(shader->ptr.cpu, prog->compiled.data, prog->compiled.size);

        struct mali_compute_job_packed job;

        pan_section_pack(&job, COMPUTE_JOB, PARAMETERS, cfg) {
                cfg.job_task_split = 5;
        }

        pan_section_pack(&job, COMPUTE_JOB, DRAW, cfg) {
                cfg.draw_descriptor_is_64b = true;
                cfg.thread_storage = shmem->ptr.gpu;
                cfg.state = shader_desc->ptr.gpu;
                cfg.push_uniforms = ubo->ptr.gpu + 1024;
                cfg.uniform_buffers = ubo->ptr.gpu;
                cfg.attributes = attr->ptr.gpu;
                cfg.attribute_buffers = attr->ptr.gpu + 256;
                cfg.varyings = var->ptr.gpu;
                cfg.varying_buffers = var->ptr.gpu + 256;
        }
 
        void *invocation = pan_section_ptr(&job, COMPUTE_JOB, INVOCATION);
        panfrost_pack_work_groups_compute(invocation,
                                          1, 1, 1,
                                          1, 1, 1,
                                          true);

        struct panfrost_bo *bos[] = {
                shmem, shader, shader_desc, ubo, var, attr
        };

        bool succ = bit_submit(dev, MALI_JOB_TYPE_VERTEX,
                               ((void *)&job) + MALI_JOB_HEADER_LENGTH,
                               MALI_COMPUTE_JOB_LENGTH - MALI_JOB_HEADER_LENGTH,
                               bos, ARRAY_SIZE(bos), debug);

        /* Check the output varyings */

        uint32_t *output = (uint32_t *) (var->ptr.cpu + 1024);
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
