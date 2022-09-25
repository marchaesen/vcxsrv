#
# Copyright © 2021 Google, Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

from mako.template import Template
import sys

def max_bitfield_val(high, low, shift):
    return ((1 << (high - low)) - 1) << shift

class State(object):
    def __init__(self):
        # List of unique device-info structs, multiple different GPU ids
        # can map to a single info struct in cases where the differences
        # are not sw visible, or the only differences are parameters
        # queried from the kernel (like GMEM size)
        self.gpu_infos = []

        # Table mapping GPU id to device-info struct
        self.gpus = {}

    def info_index(self, gpu_info):
        i = 0
        for info in self.gpu_infos:
            if gpu_info == info:
                return i
            i += 1
        raise Error("invalid info")

s = State()

def add_gpus(ids, info):
    for id in ids:
        s.gpus[id] = info

class GPUId(object):
    def __init__(self, gpu_id = None, chip_id = None, name=None):
        if chip_id == None:
            assert(gpu_id != None)
            val = gpu_id
            core = int(val / 100)
            val -= (core * 100);
            major = int(val / 10);
            val -= (major * 10)
            minor = val
            chip_id = (core << 24) | (major << 16) | (minor << 8) | 0xff
        self.chip_id = chip_id
        if gpu_id == None:
            gpu_id = 0
        self.gpu_id = gpu_id
        if name == None:
            assert(gpu_id != 0)
            name = "FD%d" % gpu_id
        self.name = name

class Struct(object):
    """A helper class that stringifies itself to a 'C' struct initializer
    """
    def __str__(self):
        s = "{"
        for name, value in vars(self).items():
            s += "." + name + "=" + str(value) + ","
        return s + "}"

class GPUInfo(Struct):
    """Base class for any generation of adreno, consists of GMEM layout
       related parameters

       Note that tile_max_h is normally only constrained by corresponding
       bitfield size/shift (ie. VSC_BIN_SIZE, or similar), but tile_max_h
       tends to have lower limits, in which case a comment will describe
       the bitfield size/shift
    """
    def __init__(self, gmem_align_w, gmem_align_h,
                 tile_align_w, tile_align_h,
                 tile_max_w, tile_max_h, num_vsc_pipes):
        self.gmem_align_w  = gmem_align_w
        self.gmem_align_h  = gmem_align_h
        self.tile_align_w  = tile_align_w
        self.tile_align_h  = tile_align_h
        self.tile_max_w    = tile_max_w
        self.tile_max_h    = tile_max_h
        self.num_vsc_pipes = num_vsc_pipes

        s.gpu_infos.append(self)


class A6xxGPUInfo(GPUInfo):
    """The a6xx generation has a lot more parameters, and is broken down
       into distinct sub-generations.  The template parameter avoids
       duplication of parameters that are unique to the sub-generation.
    """
    def __init__(self, template, num_sp_cores, num_ccu, magic_regs):
        super().__init__(gmem_align_w = 16, gmem_align_h = 4,
                         tile_align_w = 32, tile_align_h = 32,
                         tile_max_w   = 1024, # max_bitfield_val(5, 0, 5)
                         tile_max_h   = max_bitfield_val(14, 8, 4),
                         num_vsc_pipes = 32)
        assert(num_sp_cores == num_ccu)

        self.num_sp_cores = num_sp_cores

        # 96 tile alignment seems correlated to 3 CCU
        if num_ccu == 3:
            self.tile_align_w = 96

        self.a6xx = Struct()
        self.a6xx.magic = Struct()

        for name, val in magic_regs.items():
            setattr(self.a6xx.magic, name, val)

        # Things that earlier gens have and later gens remove, provide
        # defaults here and let them be overridden by sub-gen template:
        self.a6xx.has_cp_reg_write = True
        self.a6xx.has_8bpp_ubwc = True

        for name, val in template.items():
            if name == "magic": # handled above
                continue
            setattr(self.a6xx, name, val)

# a2xx is really two sub-generations, a20x and a22x, but we don't currently
# capture that in the device-info tables
add_gpus([
        GPUId(200),
        GPUId(201),
        GPUId(205),
        GPUId(220),
    ], GPUInfo(
        gmem_align_w = 32,  gmem_align_h = 32,
        tile_align_w = 32,  tile_align_h = 32,
        tile_max_w   = 512,
        tile_max_h   = ~0, # TODO
        num_vsc_pipes = 8,
    ))

add_gpus([
        GPUId(305),
        GPUId(307),
        GPUId(320),
        GPUId(330),
    ], GPUInfo(
        gmem_align_w = 32,  gmem_align_h = 32,
        tile_align_w = 32,  tile_align_h = 32,
        tile_max_w   = 992, # max_bitfield_val(4, 0, 5)
        tile_max_h   = max_bitfield_val(9, 5, 5),
        num_vsc_pipes = 8,
    ))

add_gpus([
        GPUId(405),
        GPUId(420),
        GPUId(430),
    ], GPUInfo(
        gmem_align_w = 32,  gmem_align_h = 32,
        tile_align_w = 32,  tile_align_h = 32,
        tile_max_w   = 1024, # max_bitfield_val(4, 0, 5)
        tile_max_h   = max_bitfield_val(9, 5, 5),
        num_vsc_pipes = 8,
    ))

add_gpus([
        GPUId(508),
        GPUId(509),
        GPUId(510),
        GPUId(512),
        GPUId(530),
        GPUId(540),
    ], GPUInfo(
        gmem_align_w = 64,  gmem_align_h = 32,
        tile_align_w = 64,  tile_align_h = 32,
        tile_max_w   = 1024, # max_bitfield_val(7, 0, 5)
        tile_max_h   = max_bitfield_val(16, 9, 5),
        num_vsc_pipes = 16,
    ))

# a6xx can be divided into distinct sub-generations, where certain device-
# info parameters are keyed to the sub-generation.  These templates reduce
# the copypaste

# a615, a616, a618, a619, a620 and a630:
a6xx_gen1 = dict(
        fibers_per_sp = 128 * 16,
        reg_size_vec4 = 96,
        instr_cache_size = 64,
        concurrent_resolve = True,
        indirect_draw_wfm_quirk = True,
        depth_bounds_require_depth_test_quirk = True,
    )

# a640, a680:
a6xx_gen2 = dict(
        fibers_per_sp = 128 * 4 * 16,
        reg_size_vec4 = 96,
        instr_cache_size = 64, # TODO
        supports_multiview_mask = True,
        has_z24uint_s8uint = True,
        indirect_draw_wfm_quirk = True,
        depth_bounds_require_depth_test_quirk = True, # TODO: check if true
        has_dp2acc = False, # TODO: check if true
        has_8bpp_ubwc = False,
    )

# a650:
a6xx_gen3 = dict(
        fibers_per_sp = 128 * 2 * 16,
        reg_size_vec4 = 64,
        # Blob limits it to 128 but we hang with 128
        instr_cache_size = 127,
        supports_multiview_mask = True,
        has_z24uint_s8uint = True,
        tess_use_shared = True,
        storage_16bit = True,
        has_tex_filter_cubic = True,
        has_sample_locations = True,
        has_ccu_flush_bug = True,
        has_8bpp_ubwc = False,
        has_dp2acc = True,
        has_lrz_dir_tracking = True,
        enable_lrz_fast_clear = True,
        lrz_track_quirk = True,
    )

# a635, a660:
a6xx_gen4 = dict(
        fibers_per_sp = 128 * 2 * 16,
        reg_size_vec4 = 64,
        # Blob limits it to 128 but we hang with 128
        instr_cache_size = 127,
        supports_multiview_mask = True,
        has_z24uint_s8uint = True,
        tess_use_shared = True,
        storage_16bit = True,
        has_tex_filter_cubic = True,
        has_sample_locations = True,
        has_ccu_flush_bug = True,
        has_cp_reg_write = False,
        has_8bpp_ubwc = False,
        has_lpac = True,
        has_shading_rate = True,
        has_getfiberid = True,
        has_dp2acc = True,
        has_dp4acc = True,
        enable_lrz_fast_clear = True,
        has_lrz_dir_tracking = True,
    )

add_gpus([
        GPUId(615),
        GPUId(616),
        GPUId(618),
        GPUId(619),
    ], A6xxGPUInfo(
        a6xx_gen1,
        num_sp_cores = 1,
        num_ccu = 1,
        magic_regs = dict(
            PC_POWER_CNTL = 0,
            TPL1_DBG_ECO_CNTL = 0x00108000,
            GRAS_DBG_ECO_CNTL = 0x00000880,
            SP_CHICKEN_BITS = 0x00000430,
            UCHE_CLIENT_PF = 0x00000004,
            PC_MODE_CNTL = 0x1f,
            SP_DBG_ECO_CNTL = 0x0,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            HLSQ_DBG_ECO_CNTL = 0x00080000,
            RB_UNKNOWN_8E01 = 0x00000001,
            VPC_DBG_ECO_CNTL = 0x0,
            UCHE_UNKNOWN_0E12 = 0x00000001
        )
    ))

add_gpus([
        GPUId(620),
    ], A6xxGPUInfo(
        a6xx_gen1,
        num_sp_cores = 1,
        num_ccu = 1,
        magic_regs = dict(
            PC_POWER_CNTL = 0,
            TPL1_DBG_ECO_CNTL = 0x01008000,
            GRAS_DBG_ECO_CNTL = 0x0,
            SP_CHICKEN_BITS = 0x00000400,
            UCHE_CLIENT_PF = 0x00000004,
            PC_MODE_CNTL = 0x1f,
            SP_DBG_ECO_CNTL = 0x01000000,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            HLSQ_DBG_ECO_CNTL = 0x0,
            RB_UNKNOWN_8E01 = 0x0,
            VPC_DBG_ECO_CNTL = 0x02000000,
            UCHE_UNKNOWN_0E12 = 0x00000001
        )
    ))

add_gpus([
        GPUId(630),
    ], A6xxGPUInfo(
        a6xx_gen1,
        num_sp_cores = 2,
        num_ccu = 2,
        magic_regs = dict(
            PC_POWER_CNTL = 1,
            TPL1_DBG_ECO_CNTL = 0x00108000,
            GRAS_DBG_ECO_CNTL = 0x00000880,
            SP_CHICKEN_BITS = 0x00001430,
            UCHE_CLIENT_PF = 0x00000004,
            PC_MODE_CNTL = 0x1f,
            SP_DBG_ECO_CNTL = 0x0,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            HLSQ_DBG_ECO_CNTL = 0x00080000,
            RB_UNKNOWN_8E01 = 0x00000001,
            VPC_DBG_ECO_CNTL = 0x0,
            UCHE_UNKNOWN_0E12 = 0x10000001
        )
    ))

add_gpus([
        GPUId(640),
    ], A6xxGPUInfo(
        a6xx_gen2,
        num_sp_cores = 2,
        num_ccu = 2,
        magic_regs = dict(
            PC_POWER_CNTL = 1,
            TPL1_DBG_ECO_CNTL = 0x00008000,
            GRAS_DBG_ECO_CNTL = 0x0,
            SP_CHICKEN_BITS = 0x00000420,
            UCHE_CLIENT_PF = 0x00000004,
            PC_MODE_CNTL = 0x1f,
            SP_DBG_ECO_CNTL = 0x0,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            HLSQ_DBG_ECO_CNTL = 0x0,
            RB_UNKNOWN_8E01 = 0x00000001,
            VPC_DBG_ECO_CNTL = 0x02000000,
            UCHE_UNKNOWN_0E12 = 0x00000001
        )
    ))

add_gpus([
        GPUId(680),
    ], A6xxGPUInfo(
        a6xx_gen2,
        num_sp_cores = 4,
        num_ccu = 4,
        magic_regs = dict(
            PC_POWER_CNTL = 3,
            TPL1_DBG_ECO_CNTL = 0x00108000,
            GRAS_DBG_ECO_CNTL = 0x0,
            SP_CHICKEN_BITS = 0x00001430,
            UCHE_CLIENT_PF = 0x00000004,
            PC_MODE_CNTL = 0x1f,
            SP_DBG_ECO_CNTL = 0x0,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            HLSQ_DBG_ECO_CNTL = 0x0,
            RB_UNKNOWN_8E01 = 0x00000001,
            VPC_DBG_ECO_CNTL = 0x02000000,
            UCHE_UNKNOWN_0E12 = 0x00000001
        )
    ))

add_gpus([
        GPUId(650),
    ], A6xxGPUInfo(
        a6xx_gen3,
        num_sp_cores = 3,
        num_ccu = 3,
        magic_regs = dict(
            PC_POWER_CNTL = 2,
            # this seems to be a chicken bit that fixes cubic filtering:
            TPL1_DBG_ECO_CNTL = 0x01008000,
            GRAS_DBG_ECO_CNTL = 0x0,
            SP_CHICKEN_BITS = 0x00001400,
            UCHE_CLIENT_PF = 0x00000004,
            PC_MODE_CNTL = 0x1f,
            SP_DBG_ECO_CNTL = 0x01000000,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            HLSQ_DBG_ECO_CNTL = 0x0,
            RB_UNKNOWN_8E01 = 0x0,
            VPC_DBG_ECO_CNTL = 0x02000000,
            UCHE_UNKNOWN_0E12 = 0x00000001
        )
    ))

add_gpus([
        GPUId(chip_id=0x00be06030500, name="Adreno 8c Gen 3"),
        GPUId(chip_id=0x007506030500, name="Adreno 7c+ Gen 3"),
        GPUId(chip_id=0x006006030500, name="Adreno 7c+ Gen 3 Lite"),
        # fallback wildcard entry should be last:
        GPUId(chip_id=0xffff06030500, name="Adreno 7c+ Gen 3"),
    ], A6xxGPUInfo(
        a6xx_gen4,
        num_sp_cores = 2,
        num_ccu = 2,
        magic_regs = dict(
            PC_POWER_CNTL = 1,
            TPL1_DBG_ECO_CNTL = 0x05008000,
            GRAS_DBG_ECO_CNTL = 0x0,
            SP_CHICKEN_BITS = 0x00001400,
            UCHE_CLIENT_PF = 0x00000084,
            PC_MODE_CNTL = 0x1f,
            SP_DBG_ECO_CNTL = 0x00000006,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            HLSQ_DBG_ECO_CNTL = 0x0,
            RB_UNKNOWN_8E01 = 0x0,
            VPC_DBG_ECO_CNTL = 0x02000000,
            UCHE_UNKNOWN_0E12 = 0x00000001
        )
    ))

add_gpus([
        GPUId(660),
    ], A6xxGPUInfo(
        a6xx_gen4,
        num_sp_cores = 3,
        num_ccu = 3,
        magic_regs = dict(
            PC_POWER_CNTL = 2,
            TPL1_DBG_ECO_CNTL = 0x05008000,
            GRAS_DBG_ECO_CNTL = 0x0,
            SP_CHICKEN_BITS = 0x00001400,
            UCHE_CLIENT_PF = 0x00000084,
            PC_MODE_CNTL = 0x1f,
            SP_DBG_ECO_CNTL = 0x01000000,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            HLSQ_DBG_ECO_CNTL = 0x0,
            RB_UNKNOWN_8E01 = 0x0,
            VPC_DBG_ECO_CNTL = 0x02000000,
            UCHE_UNKNOWN_0E12 = 0x00000001
        )
    ))

template = """\
/* Copyright (C) 2021 Google, Inc.
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

#include "freedreno_dev_info.h"

/* Map python to C: */
#define True true
#define False false

%for info in s.gpu_infos:
static const struct fd_dev_info __info${s.info_index(info)} = ${str(info)};
%endfor

static const struct fd_dev_rec fd_dev_recs[] = {
%for id, info in s.gpus.items():
   { {${id.gpu_id}, ${hex(id.chip_id)}}, "${id.name}", &__info${s.info_index(info)} },
%endfor
};
"""

print(Template(template).render(s=s))

