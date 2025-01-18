/* Copyright 2024 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "vpe_assert.h"
#include "common.h"
#include "reg_helper.h"
#include "vpe10_vpe_desc_writer.h"
#include "vpe11_vpe_desc_writer.h"
#include "vpe11_command.h"

void vpe11_construct_vpe_desc_writer(struct vpe_desc_writer *writer)
{
    writer->init            = vpe11_vpe_desc_writer_init;
    writer->add_plane_desc  = vpe10_vpe_desc_writer_add_plane_desc;
    writer->add_config_desc = vpe10_vpe_desc_writer_add_config_desc;
    writer->complete        = vpe10_vpe_desc_writer_complete;
}

enum vpe_status vpe11_vpe_desc_writer_init(
    struct vpe_desc_writer *writer, struct vpe_buf *buf, int cd)
{
    uint32_t *cmd_space;
    uint64_t  size = sizeof(uint32_t);

    writer->base_cpu_va      = buf->cpu_va;
    writer->base_gpu_va      = buf->gpu_va;
    writer->buf              = buf;
    writer->num_config_desc  = 0;
    writer->plane_desc_added = false;
    writer->status           = VPE_STATUS_OK;

    if (buf->size < size) {
        writer->status = VPE_STATUS_BUFFER_OVERFLOW;
        return writer->status;
    }

    if (writer->status == VPE_STATUS_OK) {
        cmd_space = (uint32_t *)(uintptr_t)writer->buf->cpu_va;
        *cmd_space++ = VPE_DESC_CMD_HEADER(cd);

        writer->buf->cpu_va += size;
        writer->buf->gpu_va += size;
        writer->buf->size -= size;
    }

    return writer->status;
}

