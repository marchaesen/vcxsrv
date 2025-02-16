/*
 * Copyright © 2021 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#include "fd_pps_driver.h"

#include <cstring>
#include <iostream>
#include <perfetto.h>

#include "common/freedreno_dev_info.h"
#include "drm/freedreno_drmif.h"
#include "drm/freedreno_ringbuffer.h"
#include "perfcntrs/freedreno_dt.h"
#include "perfcntrs/freedreno_perfcntr.h"

#include "pps/pps.h"
#include "pps/pps_algorithm.h"

namespace pps
{

double
safe_div(uint64_t a, uint64_t b)
{
   if (b == 0)
      return 0;

   return a / static_cast<double>(b);
}

float
percent(uint64_t a, uint64_t b)
{
   /* Sometimes we get bogus values but we want for the timeline
    * to look nice without higher than 100% values.
    */
   if (b == 0 || a > b)
      return 0;

   return 100.f * (a / static_cast<double>(b));
}

bool
FreedrenoDriver::is_dump_perfcnt_preemptible() const
{
   return false;
}

uint64_t
FreedrenoDriver::get_min_sampling_period_ns()
{
   return 100000;
}

/*
TODO this sees like it would be largely the same for a5xx as well
(ie. same countable names)..
 */
void
FreedrenoDriver::setup_a6xx_counters()
{
   /* TODO is there a reason to want more than one group? */
   CounterGroup group = {};
   group.name = "counters";
   groups.clear();
   counters.clear();
   countables.clear();
   enabled_counters.clear();
   groups.emplace_back(std::move(group));

   /*
    * Create the countables that we'll be using.
    */

   auto PERF_CP_ALWAYS_COUNT = countable("CP", "PERF_CP_ALWAYS_COUNT");
   auto PERF_CP_BUSY_CYCLES  = countable("CP", "PERF_CP_BUSY_CYCLES");
   auto PERF_RB_3D_PIXELS    = countable("RB", "PERF_RB_3D_PIXELS");
   auto PERF_TP_L1_CACHELINE_MISSES = countable("TP", "PERF_TP_L1_CACHELINE_MISSES");
   auto PERF_TP_L1_CACHELINE_REQUESTS = countable("TP", "PERF_TP_L1_CACHELINE_REQUESTS");

   auto PERF_TP_OUTPUT_PIXELS  = countable("TP", "PERF_TP_OUTPUT_PIXELS");
   auto PERF_TP_OUTPUT_PIXELS_ANISO  = countable("TP", "PERF_TP_OUTPUT_PIXELS_ANISO");
   auto PERF_TP_OUTPUT_PIXELS_BILINEAR = countable("TP", "PERF_TP_OUTPUT_PIXELS_BILINEAR");
   auto PERF_TP_OUTPUT_PIXELS_POINT = countable("TP", "PERF_TP_OUTPUT_PIXELS_POINT");
   auto PERF_TP_OUTPUT_PIXELS_ZERO_LOD = countable("TP", "PERF_TP_OUTPUT_PIXELS_ZERO_LOD");

   auto PERF_TSE_INPUT_PRIM  = countable("TSE", "PERF_TSE_INPUT_PRIM");
   auto PERF_TSE_CLIPPED_PRIM  = countable("TSE", "PERF_TSE_CLIPPED_PRIM");
   auto PERF_TSE_TRIVAL_REJ_PRIM  = countable("TSE", "PERF_TSE_TRIVAL_REJ_PRIM");
   auto PERF_TSE_OUTPUT_VISIBLE_PRIM = countable("TSE", "PERF_TSE_OUTPUT_VISIBLE_PRIM");

   auto PERF_SP_BUSY_CYCLES  = countable("SP", "PERF_SP_BUSY_CYCLES");
   auto PERF_SP_ALU_WORKING_CYCLES = countable("SP", "PERF_SP_ALU_WORKING_CYCLES");
   auto PERF_SP_EFU_WORKING_CYCLES = countable("SP", "PERF_SP_EFU_WORKING_CYCLES");
   auto PERF_SP_VS_STAGE_EFU_INSTRUCTIONS = countable("SP", "PERF_SP_VS_STAGE_EFU_INSTRUCTIONS");
   auto PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS = countable("SP", "PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS");
   auto PERF_SP_VS_STAGE_TEX_INSTRUCTIONS = countable("SP", "PERF_SP_VS_STAGE_TEX_INSTRUCTIONS");
   auto PERF_SP_FS_STAGE_EFU_INSTRUCTIONS = countable("SP", "PERF_SP_FS_STAGE_EFU_INSTRUCTIONS");
   auto PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS = countable("SP", "PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS");
   auto PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS = countable("SP", "PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS");
   auto PERF_SP_STALL_CYCLES_TP = countable("SP", "PERF_SP_STALL_CYCLES_TP");
   auto PERF_SP_ANY_EU_WORKING_FS_STAGE = countable("SP", "PERF_SP_ANY_EU_WORKING_FS_STAGE");
   auto PERF_SP_ANY_EU_WORKING_VS_STAGE = countable("SP", "PERF_SP_ANY_EU_WORKING_VS_STAGE");
   auto PERF_SP_ANY_EU_WORKING_CS_STAGE = countable("SP", "PERF_SP_ANY_EU_WORKING_CS_STAGE");

   auto PERF_UCHE_STALL_CYCLES_ARBITER = countable("UCHE", "PERF_UCHE_STALL_CYCLES_ARBITER");
   auto PERF_UCHE_VBIF_READ_BEATS_TP = countable("UCHE", "PERF_UCHE_VBIF_READ_BEATS_TP");
   auto PERF_UCHE_VBIF_READ_BEATS_VFD = countable("UCHE", "PERF_UCHE_VBIF_READ_BEATS_VFD");
   auto PERF_UCHE_VBIF_READ_BEATS_SP = countable("UCHE", "PERF_UCHE_VBIF_READ_BEATS_SP");
   auto PERF_UCHE_READ_REQUESTS_TP = countable("UCHE", "PERF_UCHE_READ_REQUESTS_TP");

   auto PERF_PC_STALL_CYCLES_VFD = countable("PC", "PERF_PC_STALL_CYCLES_VFD");
   auto PERF_PC_VS_INVOCATIONS = countable("PC", "PERF_PC_VS_INVOCATIONS");
   auto PERF_PC_VERTEX_HITS = countable("PC", "PERF_PC_VERTEX_HITS");

   auto PERF_HLSQ_QUADS = countable("HLSQ", "PERF_HLSQ_QUADS"); /* Quads (fragments / 4) produced */

   auto PERF_CP_NUM_PREEMPTIONS = countable("CP", "PERF_CP_NUM_PREEMPTIONS");
   auto PERF_CP_PREEMPTION_REACTION_DELAY = countable("CP", "PERF_CP_PREEMPTION_REACTION_DELAY");

   /* TODO: resolve() tells there is no PERF_CMPDECMP_VBIF_READ_DATA */
   // auto PERF_CMPDECMP_VBIF_READ_DATA = countable("PERF_CMPDECMP_VBIF_READ_DATA");

   /*
    * And then setup the derived counters that we are exporting to
    * pps based on the captured countable values.
    *
    * We try to expose the same counters as blob:
    * https://gpuinspector.dev/docs/gpu-counters/qualcomm
    */

   counter("GPU Frequency", Counter::Units::Hertz, [=]() {
         return PERF_CP_ALWAYS_COUNT / time;
      }
   );

   counter("GPU % Utilization", Counter::Units::Percent, [=]() {
         return percent(PERF_CP_BUSY_CYCLES / time, max_freq);
      }
   );

   counter("TP L1 Cache Misses", Counter::Units::None, [=]() {
         return PERF_TP_L1_CACHELINE_MISSES / time;
      }
   );

   counter("Shader Core Utilization", Counter::Units::Percent, [=]() {
         return percent(PERF_SP_BUSY_CYCLES / time, max_freq * info->num_sp_cores);
      }
   );

   /* TODO: verify */
   counter("(?) % Texture Fetch Stall", Counter::Units::Percent, [=]() {
         return percent(PERF_SP_STALL_CYCLES_TP / time, max_freq * info->num_sp_cores);
      }
   );

   /* TODO: verify */
   counter("(?) % Vertex Fetch Stall", Counter::Units::Percent, [=]() {
         return percent(PERF_PC_STALL_CYCLES_VFD / time, max_freq * info->num_sp_cores);
      }
   );

   counter("L1 Texture Cache Miss Per Pixel", Counter::Units::None, [=]() {
         return safe_div(PERF_TP_L1_CACHELINE_MISSES, PERF_HLSQ_QUADS * 4);
      }
   );

   counter("% Texture L1 Miss", Counter::Units::Percent, [=]() {
         return percent(PERF_TP_L1_CACHELINE_MISSES, PERF_TP_L1_CACHELINE_REQUESTS);
      }
   );

   counter("% Texture L2 Miss", Counter::Units::Percent, [=]() {
         return percent(PERF_UCHE_VBIF_READ_BEATS_TP / 2, PERF_UCHE_READ_REQUESTS_TP);
      }
   );

   /* TODO: verify */
   counter("(?) % Stalled on System Memory", Counter::Units::Percent, [=]() {
         return percent(PERF_UCHE_STALL_CYCLES_ARBITER / time, max_freq * info->num_sp_cores);
      }
   );

   counter("Pre-clipped Polygons / Second", Counter::Units::None, [=]() {
         return PERF_TSE_INPUT_PRIM * (1.f / time);
      }
   );

   counter("% Prims Trivially Rejected", Counter::Units::Percent, [=]() {
         return percent(PERF_TSE_TRIVAL_REJ_PRIM, PERF_TSE_INPUT_PRIM);
      }
   );

   counter("% Prims Clipped", Counter::Units::Percent, [=]() {
         return percent(PERF_TSE_CLIPPED_PRIM, PERF_TSE_INPUT_PRIM);
      }
   );

   counter("Average Vertices / Polygon", Counter::Units::None, [=]() {
         return PERF_PC_VS_INVOCATIONS / PERF_TSE_INPUT_PRIM;
      }
   );

   counter("Reused Vertices / Second", Counter::Units::None, [=]() {
         return PERF_PC_VERTEX_HITS * (1.f / time);
      }
   );

   counter("Average Polygon Area", Counter::Units::None, [=]() {
         return safe_div(PERF_HLSQ_QUADS * 4, PERF_TSE_OUTPUT_VISIBLE_PRIM);
      }
   );

   /* TODO: find formula */
   // counter("% Shaders Busy", Counter::Units::Percent, [=]() {
   //       return 100.0 * 0;
   //    }
   // );

   counter("Vertices Shaded / Second", Counter::Units::None, [=]() {
         return PERF_PC_VS_INVOCATIONS * (1.f / time);
      }
   );

   counter("Fragments Shaded / Second", Counter::Units::None, [=]() {
         return PERF_HLSQ_QUADS * 4 * (1.f / time);
      }
   );

   counter("Vertex Instructions / Second", Counter::Units::None, [=]() {
         return (PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS +
                 PERF_SP_VS_STAGE_EFU_INSTRUCTIONS) * (1.f / time);
      }
   );

   counter("Fragment Instructions / Second", Counter::Units::None, [=]() {
         return (PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS +
                 PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS / 2 +
                 PERF_SP_FS_STAGE_EFU_INSTRUCTIONS) * (1.f / time);
      }
   );

   counter("Fragment ALU Instructions / Sec (Full)", Counter::Units::None, [=]() {
         return PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS * (1.f / time);
      }
   );

   counter("Fragment ALU Instructions / Sec (Half)", Counter::Units::None, [=]() {
         return PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS * (1.f / time);
      }
   );

   counter("Fragment EFU Instructions / Second", Counter::Units::None, [=]() {
         return PERF_SP_FS_STAGE_EFU_INSTRUCTIONS * (1.f / time);
      }
   );

   counter("Textures / Vertex", Counter::Units::None, [=]() {
         return safe_div(PERF_SP_VS_STAGE_TEX_INSTRUCTIONS, PERF_PC_VS_INVOCATIONS);
      }
   );

   counter("Textures / Fragment", Counter::Units::None, [=]() {
         return safe_div(PERF_TP_OUTPUT_PIXELS, PERF_HLSQ_QUADS * 4);
      }
   );

   counter("ALU / Vertex", Counter::Units::None, [=]() {
         return safe_div(PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS, PERF_PC_VS_INVOCATIONS);
      }
   );

   counter("EFU / Vertex", Counter::Units::None, [=]() {
         return safe_div(PERF_SP_VS_STAGE_EFU_INSTRUCTIONS, PERF_PC_VS_INVOCATIONS);
      }
   );

   counter("ALU / Fragment", Counter::Units::None, [=]() {
         return safe_div(PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS +
                         PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS / 2, PERF_HLSQ_QUADS);
      }
   );

   counter("EFU / Fragment", Counter::Units::None, [=]() {
         return safe_div(PERF_SP_FS_STAGE_EFU_INSTRUCTIONS, PERF_HLSQ_QUADS);
      }
   );

   counter("% Time Shading Vertices", Counter::Units::Percent, [=]() {
         return percent(PERF_SP_ANY_EU_WORKING_VS_STAGE,
                        (PERF_SP_ANY_EU_WORKING_VS_STAGE +
                         PERF_SP_ANY_EU_WORKING_FS_STAGE +
                         PERF_SP_ANY_EU_WORKING_CS_STAGE));
      }
   );

   counter("% Time Shading Fragments", Counter::Units::Percent, [=]() {
         return percent(PERF_SP_ANY_EU_WORKING_FS_STAGE,
                        (PERF_SP_ANY_EU_WORKING_VS_STAGE +
                         PERF_SP_ANY_EU_WORKING_FS_STAGE +
                         PERF_SP_ANY_EU_WORKING_CS_STAGE));
      }
   );

   counter("% Time Compute", Counter::Units::Percent, [=]() {
         return percent(PERF_SP_ANY_EU_WORKING_CS_STAGE,
                        (PERF_SP_ANY_EU_WORKING_VS_STAGE +
                         PERF_SP_ANY_EU_WORKING_FS_STAGE +
                         PERF_SP_ANY_EU_WORKING_CS_STAGE));
      }
   );

   counter("% Shader ALU Capacity Utilized", Counter::Units::Percent, [=]() {
         return percent((PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS +
                         PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS +
                         PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS / 2) / 64,
                        PERF_SP_BUSY_CYCLES);
      }
   );

   counter("% Time ALUs Working", Counter::Units::Percent, [=]() {
         return percent(PERF_SP_ALU_WORKING_CYCLES / 2, PERF_SP_BUSY_CYCLES);
      }
   );

   counter("% Time EFUs Working", Counter::Units::Percent, [=]() {
         return percent(PERF_SP_EFU_WORKING_CYCLES / 2, PERF_SP_BUSY_CYCLES);
      }
   );

   counter("% Anisotropic Filtered", Counter::Units::Percent, [=]() {
         return percent(PERF_TP_OUTPUT_PIXELS_ANISO, PERF_TP_OUTPUT_PIXELS);
      }
   );

   counter("% Linear Filtered", Counter::Units::Percent, [=]() {
         return percent(PERF_TP_OUTPUT_PIXELS_BILINEAR, PERF_TP_OUTPUT_PIXELS);
      }
   );

   counter("% Nearest Filtered", Counter::Units::Percent, [=]() {
         return percent(PERF_TP_OUTPUT_PIXELS_POINT, PERF_TP_OUTPUT_PIXELS);
      }
   );

   counter("% Non-Base Level Textures", Counter::Units::Percent, [=]() {
         return percent(PERF_TP_OUTPUT_PIXELS_ZERO_LOD, PERF_TP_OUTPUT_PIXELS);
      }
   );

   /* Reads from KGSL_PERFCOUNTER_GROUP_VBIF countable=63 */
   // counter("Read Total (Bytes/sec)", Counter::Units::Byte, [=]() {
   //       return  * (1.f / time);
   //    }
   // );

   /* Reads from KGSL_PERFCOUNTER_GROUP_VBIF countable=84 */
   // counter("Write Total (Bytes/sec)", Counter::Units::Byte, [=]() {
   //       return  * (1.f / time);
   //    }
   // );

   /* Cannot get PERF_CMPDECMP_VBIF_READ_DATA countable */
   // counter("Texture Memory Read BW (Bytes/Second)", Counter::Units::Byte, [=]() {
   //       return (PERF_CMPDECMP_VBIF_READ_DATA + PERF_UCHE_VBIF_READ_BEATS_TP) * (1.f / time);
   //    }
   // );

   /* TODO: verify */
   counter("(?) Vertex Memory Read (Bytes/Second)", Counter::Units::Byte, [=]() {
         return PERF_UCHE_VBIF_READ_BEATS_VFD * 32 * (1.f / time);
      }
   );

   /* TODO: verify */
   counter("SP Memory Read (Bytes/Second)", Counter::Units::Byte, [=]() {
         return PERF_UCHE_VBIF_READ_BEATS_SP * 32 * (1.f / time);
      }
   );

   counter("Avg Bytes / Fragment", Counter::Units::Byte, [=]() {
         return safe_div(PERF_UCHE_VBIF_READ_BEATS_TP * 32, PERF_HLSQ_QUADS * 4);
      }
   );

   counter("Avg Bytes / Vertex", Counter::Units::Byte, [=]() {
         return safe_div(PERF_UCHE_VBIF_READ_BEATS_VFD * 32, PERF_PC_VS_INVOCATIONS);
      }
   );

   counter("Preemptions / second", Counter::Units::None, [=]() {
         return PERF_CP_NUM_PREEMPTIONS * (1.f / time);
      }
   );

   counter("Avg Preemption Delay", Counter::Units::None, [=]() {
         return PERF_CP_PREEMPTION_REACTION_DELAY * (1.f / time);
      }
   );
}

void
FreedrenoDriver::setup_a7xx_counters()
{
   /* TODO is there a reason to want more than one group? */
   CounterGroup group = {};
   group.name = "counters";
   groups.clear();
   counters.clear();
   countables.clear();
   enabled_counters.clear();
   groups.emplace_back(std::move(group));

   /* So far, all a7xx devices seem to have two uSPTPs in each SP core
    * and 128 ALUs in each uSPTP.
    */
   const unsigned number_of_usptp = info->num_sp_cores * 2;
   const unsigned number_of_alus_per_usptp = 128;

   /* The enumeration and two helper lambdas serve to handle countables
    * that can be sampled from either rendering or visibility bins.
    */
   enum {
      BR = 0,
      BV = 1,
   };

   auto cbCountable = [=](std::string group, std::string name) {
      return std::array<Countable, 2> {
         countable(group, name),
         countable("BV_" + group, name),
      };
   };

   auto cbSum = [](const std::array<Countable, 2>& countable) {
      return countable[BR] + countable[BV];
   };

   /* This is a helper no-op lambda to handle known and understood counters
    * that we can't currently implement for a variety of reasons.
    */
   auto disabledCounter = [](std::string, Counter::Units, std::function<int64_t()>) { };

   /* CP: 3/14 counters */
   auto PERF_CP_ALWAYS_COUNT = countable("CP", "PERF_CP_ALWAYS_COUNT");
   auto PERF_CP_NUM_PREEMPTIONS = countable("CP", "PERF_CP_NUM_PREEMPTIONS");
   auto PERF_CP_PREEMPTION_REACTION_DELAY = countable("CP", "PERF_CP_PREEMPTION_REACTION_DELAY");

   /* RBBM: 1/4 counters */
   auto PERF_RBBM_STATUS_MASKED = countable("RBBM", "PERF_RBBM_STATUS_MASKED");

   /* PC: 3/8 counters, BV_PC: 3/8 counters */
   auto PERF_PC_STALL_CYCLES_VFD = cbCountable("PC", "PERF_PC_STALL_CYCLES_VFD");
   auto PERF_PC_VERTEX_HITS = cbCountable("PC", "PERF_PC_VERTEX_HITS");
   auto PERF_PC_VS_INVOCATIONS = cbCountable("PC", "PERF_PC_VS_INVOCATIONS");

   /* TSE: 4/8 counters */
   auto PERF_TSE_INPUT_PRIM = countable("TSE", "PERF_TSE_INPUT_PRIM");
   auto PERF_TSE_TRIVAL_REJ_PRIM = countable("TSE", "PERF_TSE_TRIVAL_REJ_PRIM");
   auto PERF_TSE_CLIPPED_PRIM = countable("TSE", "PERF_TSE_CLIPPED_PRIM");
   auto PERF_TSE_OUTPUT_VISIBLE_PRIM = countable("TSE", "PERF_TSE_OUTPUT_VISIBLE_PRIM");

   /* UCHE: 8/12 counters */
   auto PERF_UCHE_STALL_CYCLES_ARBITER = countable("UCHE", "PERF_UCHE_STALL_CYCLES_ARBITER");
   auto PERF_UCHE_VBIF_READ_BEATS_TP = countable("UCHE", "PERF_UCHE_VBIF_READ_BEATS_TP");
   auto PERF_UCHE_VBIF_READ_BEATS_VFD = countable("UCHE", "PERF_UCHE_VBIF_READ_BEATS_VFD");
   auto PERF_UCHE_VBIF_READ_BEATS_SP = countable("UCHE", "PERF_UCHE_VBIF_READ_BEATS_SP");
   auto PERF_UCHE_READ_REQUESTS_TP = countable("UCHE", "PERF_UCHE_READ_REQUESTS_TP");
   auto PERF_UCHE_READ_REQUESTS_SP = countable("UCHE", "PERF_UCHE_READ_REQUESTS_SP");
   auto PERF_UCHE_WRITE_REQUESTS_SP = countable("UCHE", "PERF_UCHE_WRITE_REQUESTS_SP");
   auto PERF_UCHE_EVICTS = countable("UCHE", "PERF_UCHE_EVICTS");

   /* TP: 7/12 counters, BV_TP: 6/6 counters */
   auto PERF_TP_BUSY_CYCLES = countable("TP", "PERF_TP_BUSY_CYCLES");
   auto PERF_TP_L1_CACHELINE_REQUESTS = cbCountable("TP", "PERF_TP_L1_CACHELINE_REQUESTS");
   auto PERF_TP_L1_CACHELINE_MISSES = cbCountable("TP", "PERF_TP_L1_CACHELINE_MISSES");
   auto PERF_TP_OUTPUT_PIXELS = cbCountable("TP", "PERF_TP_OUTPUT_PIXELS");
   auto PERF_TP_OUTPUT_PIXELS_POINT = cbCountable("TP", "PERF_TP_OUTPUT_PIXELS_POINT");
   auto PERF_TP_OUTPUT_PIXELS_BILINEAR = cbCountable("TP", "PERF_TP_OUTPUT_PIXELS_BILINEAR");
   auto PERF_TP_OUTPUT_PIXELS_ANISO = cbCountable("TP", "PERF_TP_OUTPUT_PIXELS_ANISO");

   /* SP: 24/24 counters, BV_SP: 7/12 counters */
   auto PERF_SP_BUSY_CYCLES = countable("SP", "PERF_SP_BUSY_CYCLES");
   auto PERF_SP_ALU_WORKING_CYCLES = countable("SP", "PERF_SP_ALU_WORKING_CYCLES");
   auto PERF_SP_EFU_WORKING_CYCLES = countable("SP", "PERF_SP_EFU_WORKING_CYCLES");
   auto PERF_SP_STALL_CYCLES_TP = cbCountable("SP", "PERF_SP_STALL_CYCLES_TP");
   auto PERF_SP_NON_EXECUTION_CYCLES = countable("SP", "PERF_SP_NON_EXECUTION_CYCLES");
   auto PERF_SP_VS_STAGE_TEX_INSTRUCTIONS = cbCountable("SP", "PERF_SP_VS_STAGE_TEX_INSTRUCTIONS");
   auto PERF_SP_VS_STAGE_EFU_INSTRUCTIONS = cbCountable("SP", "PERF_SP_VS_STAGE_EFU_INSTRUCTIONS");
   auto PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS = cbCountable("SP", "PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS");
   auto PERF_SP_FS_STAGE_EFU_INSTRUCTIONS = countable("SP", "PERF_SP_FS_STAGE_EFU_INSTRUCTIONS");
   auto PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS = countable("SP", "PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS");
   auto PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS = countable("SP", "PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS");
   auto PERF_SP_ICL1_REQUESTS = cbCountable("SP", "PERF_SP_ICL1_REQUESTS");
   auto PERF_SP_ICL1_MISSES = cbCountable("SP", "PERF_SP_ICL1_MISSES");
   auto PERF_SP_ANY_EU_WORKING_FS_STAGE = countable("SP", "PERF_SP_ANY_EU_WORKING_FS_STAGE");
   auto PERF_SP_ANY_EU_WORKING_VS_STAGE = cbCountable("SP", "PERF_SP_ANY_EU_WORKING_VS_STAGE");
   auto PERF_SP_ANY_EU_WORKING_CS_STAGE = countable("SP", "PERF_SP_ANY_EU_WORKING_CS_STAGE");
   auto PERF_SP_PIXELS = countable("SP", "PERF_SP_PIXELS");
   auto PERF_SP_RAY_QUERY_INSTRUCTIONS = countable("SP", "PERF_SP_RAY_QUERY_INSTRUCTIONS");
   auto PERF_SP_RTU_BUSY_CYCLES = countable("SP", "PERF_SP_RTU_BUSY_CYCLES");
   auto PERF_SP_RTU_BVH_FETCH_LATENCY_CYCLES = countable("SP", "PERF_SP_RTU_BVH_FETCH_LATENCY_CYCLES");
   auto PERF_SP_RTU_BVH_FETCH_LATENCY_SAMPLES = countable("SP", "PERF_SP_RTU_BVH_FETCH_LATENCY_SAMPLES");
   auto PERF_SP_RTU_RAY_BOX_INTERSECTIONS = countable("SP", "PERF_SP_RTU_RAY_BOX_INTERSECTIONS");
   auto PERF_SP_RTU_RAY_TRIANGLE_INTERSECTIONS = countable("SP", "PERF_SP_RTU_RAY_TRIANGLE_INTERSECTIONS");
   auto PERF_SP_SCH_STALL_CYCLES_RTU = countable("SP", "PERF_SP_SCH_STALL_CYCLES_RTU");

   /* CMP: 1/4 counters */
   auto PERF_CMPDECMP_VBIF_READ_DATA = countable("CMP", "PERF_CMPDECMP_VBIF_READ_DATA");

   /**
    * GPU Compute
    */
   disabledCounter("Avg Load-Store Instructions Per Cycle", Counter::Units::None, [=]() {
         /* Number of average Load-Store instructions per cycle. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_27 = PERF_SP_LM_LOAD_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_28 = PERF_SP_LM_STORE_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_30 = PERF_SP_GM_LOAD_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_31 = PERF_SP_GM_STORE_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_0 = PERF_SP_BUSY_CYCLES
          * Notes:
          *   - FIXME: disabled due to lack of SP counter capacity
          *   - Equation: 4*sum(PERF_SP_{LM,GM}_{LOAD,STORE}_INSTRUCTIONS) / PERF_SP_BUSY_CYCLES
          */
         return 42;
      }
   );
   counter("Bytes Data Actually Written", Counter::Units::Byte, [=]() {
         /* Number of bytes requested to be written by the GPU. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_18 = PERF_UCHE_EVICTS
          * Notes:
          *   - Equation: PERF_UCHE_EVICTS * 64
          */
         return PERF_UCHE_EVICTS * 64;
      }
   );
   counter("Bytes Data Write Requested", Counter::Units::Byte, [=]() {
         /* Number of bytes requested to be written by the GPU. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_15 = PERF_UCHE_WRITE_REQUESTS_SP
          * Notes:
          *   - Equation: PERF_UCHE_WRITE_REQUESTS_SP * 16
          */
         return PERF_UCHE_WRITE_REQUESTS_SP * 16;
      }
   );
   counter("Global Buffer Data Read BW (Bytes/sec)", Counter::Units::Byte, [=]() {
         /* Number of bytes of global buffer data read in by the GPU, per second from the system memory (when the data is not found in L2 cache). */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_8 = PERF_UCHE_VBIF_READ_BEATS_SP
          * Notes:
          *   - Equation: (PERF_UCHE_VBIF_READ_BEATS_SP * 32) / time
          */
         return (PERF_UCHE_VBIF_READ_BEATS_SP * 32) / time;
      }
   );
   counter("Global Buffer Data Read Request BW (Bytes/sec)", Counter::Units::Byte, [=]() {
         /* Number of bytes of global buffer read requests, made by a compute kernel to the L2 cache, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_13 = PERF_UCHE_READ_REQUESTS_SP
          * Notes:
          *   - Equation: (PERF_UCHE_READ_REQUESTS_SP * 16) / time
          */
         return (PERF_UCHE_READ_REQUESTS_SP * 16) / time;
      }
   );
   counter("% Global Buffer Read L2 Hit", Counter::Units::Percent, [=]() {
         /* Percentage of total global buffer read requests that were fulfilled by L2 cache hit which is populated by looking at the number of read requests that were forwarded to VBIF to read from the system memory. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_8 = PERF_UCHE_VBIF_READ_BEATS_SP
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_13 = PERF_UCHE_READ_REQUESTS_SP
          * Notes:
          *   - Equation: (PERF_UCHE_READ_REQUESTS_SP - (PERF_UCHE_VBIF_READ_BEATS_SP / 2)) / PERF_UCHE_READ_REQUESTS_SP
          */
         return percent(PERF_UCHE_READ_REQUESTS_SP - (PERF_UCHE_VBIF_READ_BEATS_SP / 2), PERF_UCHE_READ_REQUESTS_SP);
      }
   );
   counter("% Global Buffer Write L2 Hit", Counter::Units::Percent, [=]() {
         /* Percentage of global write L2 Hit. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_18 = PERF_UCHE_EVICTS
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_15 = PERF_UCHE_WRITE_REQUESTS_SP
          * Notes:
          *   - Equation: (PERF_UCHE_WRITE_REQUESTS_SP - PERF_UCHE_EVICTS) / PERF_UCHE_WRITE_REQUESTS_SP
          */
         return percent(PERF_UCHE_WRITE_REQUESTS_SP - PERF_UCHE_EVICTS, PERF_UCHE_WRITE_REQUESTS_SP);
      }
   );
   counter("Global Image Compressed Data Read BW (Bytes/sec)", Counter::Units::Byte, [=]() {
         /* Number of bytes of global Image data (compressed) read in by the GPU per second from the system memory (when the data is not found in L2 cache). */
         /* Countables:
          * PERFCOUNTER_GROUP_CMP::COUNTABLE_7 = PERF_CMPDECMP_VBIF_READ_DATA
          * Notes:
          *   - Equation: (PERF_CMPDECMP_VBIF_READ_DATA * 32) / time
          */
         return (PERF_CMPDECMP_VBIF_READ_DATA * 32) / time;
      }
   );
   counter("Global Image Data Read Request BW (Bytes/sec)", Counter::Units::Byte, [=]() {
         /* Number of bytes of image buffer read requests, made by a compute kernel to the L2 cache, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_9 = PERF_UCHE_READ_REQUESTS_TP
          * Notes:
          *   - Equation: (PERF_UCHE_READ_REQUESTS_TP * 16) / time
          */
         return (PERF_UCHE_READ_REQUESTS_TP * 16) / time;
      }
   );
   counter("Global Image Uncompressed Data Read BW (Bytes/sec)", Counter::Units::Byte, [=]() {
         /* Number of bytes of global Image data (uncompressed) read in by the GPU per second from the system memory (when the data is not found in L2 cache). */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_4 = PERF_UCHE_VBIF_READ_BEATS_TP
          * Notes:
          *   - Equation: (PERF_UCHE_VBIF_READ_BEATS_TP * 32) / time
          */
         return (PERF_UCHE_VBIF_READ_BEATS_TP * 32) / time;
      }
   );
   disabledCounter("Global Memory Atomic Instructions", Counter::Units::None, [=]() {
         /* Number of Global Memory Atomic Instructions executed by SP during a given sample period. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_32 = PERF_SP_GM_ATOMICS
          * Notes:
          *   - FIXME: disabled due to lack of SP counter capacity
          *   - Equation: PERF_SP_GM_ATOMICS * 4
          */
         return 42;
      }
   );
   disabledCounter("Global Memory Load Instructions", Counter::Units::None, [=]() {
         /* Number of Global Memory Load Instructions executed by SP during a given sample period. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_30 = PERF_SP_GM_LOAD_INSTRUCTIONS
          * Notes:
          *   - FIXME: disabled due to lack of SP counter capacity
          *   - Equation: PERF_SP_GM_LOAD_INSTRUCTIONS * 4
          */
         return 42;
      }
   );
   disabledCounter("Global Memory Store Instructions", Counter::Units::None, [=]() {
         /* Number of Global Memory Store Instructions executed by SP during a given sample period. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_31 = PERF_SP_GM_STORE_INSTRUCTIONS
          * Notes:
          *   - FIXME: disabled due to lack of SP counter capacity
          *   - Equation: PERF_SP_GM_STORE_INSTRUCTIONS * 4
          */
         return 42;
      }
   );
   counter("% Image Read L2 Hit", Counter::Units::Percent, [=]() {
         /* Percentage of total image read requests that were fulfilled by L2 cache hit which is populated by looking at the number of read requests that were forwarded to VBIF to read from the system memory. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_4 = PERF_UCHE_VBIF_READ_BEATS_TP
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_9 = PERF_UCHE_READ_REQUESTS_TP
          * Notes:
          *   - Equation: (PERF_UCHE_READ_REQUESTS_TP - (PERF_UCHE_VBIF_READ_BEATS_TP / 2)) / PERF_UCHE_READ_REQUESTS_TP
          */
         return percent(PERF_UCHE_READ_REQUESTS_TP - (PERF_UCHE_VBIF_READ_BEATS_TP / 2), PERF_UCHE_READ_REQUESTS_TP);
      }
   );
   counter("% Kernel Load Cycles", Counter::Units::Percent, [=]() {
         /* Percentage of cycles used for a compute kernel loading; excludes execution cycles. */
         /* Countables:
          * PERFCOUNTER_GROUP_CP::COUNTABLE_0 = PERF_CP_ALWAYS_COUNT
          * PERFCOUNTER_GROUP_SP::COUNTABLE_0 = PERF_SP_BUSY_CYCLES
          * PERFCOUNTER_GROUP_RBBM::COUNTABLE_6 = PERF_RBBM_STATUS_MASKED
          * Notes:
          *   - Equation: (PERF_RBBM_STATUS_MASKED - (PERF_SP_BUSY_CYCLES * #uSPTP)) / PERF_CP_ALWAYS_COUNT
          */
         return percent(PERF_RBBM_STATUS_MASKED - (PERF_SP_BUSY_CYCLES * number_of_usptp), PERF_CP_ALWAYS_COUNT);
      }
   );
   counter("% L1 Hit", Counter::Units::Percent, [=]() {
         /* Percentage of L1 texture cache requests that were hits. */
         /* Countables:
          * PERFCOUNTER_GROUP_TP::COUNTABLE_6 = PERF_TP_L1_CACHELINE_REQUESTS
          * PERFCOUNTER_GROUP_TP::COUNTABLE_7 = PERF_TP_L1_CACHELINE_MISSES
          * Notes:
          *   - Equation: (PERF_TP_L1_CACHELINE_REQUESTS - PERF_TP_L1_CACHELINE_MISSES) / PERF_TP_L1_CACHELINE_REQUESTS
          */
         return percent(PERF_TP_L1_CACHELINE_REQUESTS[BR] - PERF_TP_L1_CACHELINE_MISSES[BR], PERF_TP_L1_CACHELINE_REQUESTS[BR]);
      }
   );
   disabledCounter("Load-Store Utilization", Counter::Units::Percent, [=]() {
         /* Percentage of the Load-Store unit is utilized compared to theoretical Load/Store throughput. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_63 = PERF_SP_LOAD_CONTROL_WORKING_CYCLES
          * PERFCOUNTER_GROUP_SP::COUNTABLE_0 = PERF_SP_BUSY_CYCLES
          * Notes:
          *   - FIXME: disabled due to lack of SP counter capacity
          *   - Equation: PERF_SP_LOAD_CONTROL_WORKING_CYCLES / PERF_SP_BUSY_CYCLES
          */
         return 42;
      }
   );
   disabledCounter("Local Memory Atomic Instructions", Counter::Units::None, [=]() {
         /* Number of Local Memory Atomic Instructions executed by SP during a given sample period. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_29 = PERF_SP_LM_ATOMICS
          * Notes:
          *   - FIXME: disabled due to lack of SP counter capacity
          *   - Equation: PERF_SP_LM_ATOMICS * 4
          */
         return 42;
      }
   );
   disabledCounter("Local Memory Load Instructions", Counter::Units::None, [=]() {
         /* Number of Local Memory Load Instructions executed by SP during a given sample period. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_27 = PERF_SP_LM_LOAD_INSTRUCTIONS
          * Notes:
          *   - FIXME: disabled due to lack of SP counter capacity
          *   - Equation: PERF_SP_LM_LOAD_INSTRUCTIONS * 4
          */
         return 42;
      }
   );
   disabledCounter("Local Memory Store Instructions", Counter::Units::None, [=]() {
         /* Number of Local Memory Store Instructions executed by SP during a given sample period. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_28 = PERF_SP_LM_STORE_INSTRUCTIONS
          * Notes:
          *   - FIXME: disabled due to lack of SP counter capacity
          *   - Equation: PERF_SP_LM_STORE_INSTRUCTIONS * 4
          */
         return 42;
      }
   );

   /**
    * GPU General
    */
   disabledCounter("Clocks / Second", Counter::Units::None, [=]() {
         /* Number of GPU clocks per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_CP::COUNTABLE_0 = PERF_CP_ALWAYS_COUNT
          * Notes:
          *   - TODO: with Adaptive Clock Distribution, the measured values are much more varied
          *     than the constant GPU frequency value we currently get, so this counter is disabled
          *     for now in favor of the GPU Frequency counter below.
          *   - Equation: PERF_CP_ALWAYS_COUNT / time
          */
         return 42;
      }
   );
   disabledCounter("GPU % Bus Busy", Counter::Units::Percent, [=]() {
         /* Approximate Percentage of time the GPU's bus to system memory is busy. */
         /* Countables:
          * PERFCOUNTER_GROUP_RBBM::COUNTABLE_6 = PERF_RBBM_STATUS_MASKED
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_1 = PERF_UCHE_STALL_CYCLES_ARBITER
          * PERFCOUNTER_GROUP_VBIF::COUNTABLE_34 = PERF_GBIF_AXI0_READ_DATA_BEATS_TOTAL
          * PERFCOUNTER_GROUP_VBIF::COUNTABLE_35 = PERF_GBIF_AXI1_READ_DATA_BEATS_TOTAL
          * PERFCOUNTER_GROUP_VBIF::COUNTABLE_46 = PERF_GBIF_AXI0_WRITE_DATA_BEATS_TOTAL
          * PERFCOUNTER_GROUP_VBIF::COUNTABLE_47 = PERF_GBIF_AXI1_WRITE_DATA_BEATS_TOTAL
          * Notes:
          *   - TODO: requires VBIF perfcounter group exposure which isn't trivial because of
          *     more complex way that those counters are enabled
          *   - Equation: (PERF_UCHE_STALL_CYCLES_ARBITER + sum(PERF_GBIF_AXI{0,1}_{READ,WRITE}_DATA_BEATS_TOTAL)) / (4 * PERF_RBBM_STATUS_MASKED)
          */
         return 42;
      }
   );
   counter("GPU Frequency", Counter::Units::None, [=]() {
         /* Notes:
          *   - TODO: Should read from (an equivalent of) /sys/class/kgsl/kgsl-3d0/gpuclk
          *   - Same value can be retrieved through PERF_CP_ALWAYS_COUNT, until ACD enables adaptive
          *     GPU frequencies that would be covered by the Clocks / Second counter above.
          */
         return PERF_CP_ALWAYS_COUNT / time;
      }
   );
   disabledCounter("GPU Temperature", Counter::Units::None, [=]() {
         /* TODO: Should read from (an equivalent of) /sys/class/kgsl/kgsl-3d0/temp */
         return 42;
      }
   );
   counter("GPU % Utilization", Counter::Units::Percent, [=]() {
         /* Percentage utilization of the GPU. */
         /* Countables:
          * PERFCOUNTER_GROUP_RBBM::COUNTABLE_6 = PERF_RBBM_STATUS_MASKED
          */
         return percent(PERF_RBBM_STATUS_MASKED, max_freq);
      }
   );

   /**
    * GPU Memory Stats
    */
   counter("Avg Bytes / Fragment", Counter::Units::Byte, [=]() {
         /* Average number of bytes transferred from main memory for each fragment. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_4 = PERF_UCHE_VBIF_READ_BEATS_TP
          * PERFCOUNTER_GROUP_SP::COUNTABLE_101 = PERF_SP_PIXELS
          */
         return safe_div(PERF_UCHE_VBIF_READ_BEATS_TP * 32, PERF_SP_PIXELS);
      }
   );
   counter("Avg Bytes / Vertex", Counter::Units::Byte, [=]() {
         /* Average number of bytes transferred from main memory for each vertex. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_5 = PERF_UCHE_VBIF_READ_BEATS_VFD
          * PERFCOUNTER_GROUP_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          * PERFCOUNTER_GROUP_BV_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          */
         return safe_div(PERF_UCHE_VBIF_READ_BEATS_VFD * 32, cbSum(PERF_PC_VS_INVOCATIONS));
      }
   );
   disabledCounter("Read Total (Bytes/sec)", Counter::Units::Byte, [=]() {
         /* Total number of bytes read by the GPU from memory, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_VBIF::COUNTABLE_34 = PERF_GBIF_AXI0_READ_DATA_BEATS_TOTAL
          * PERFCOUNTER_GROUP_VBIF::COUNTABLE_35 = PERF_GBIF_AXI1_READ_DATA_BEATS_TOTAL
          * Notes:
          *   - TODO: requires VBIF perfcounter group exposure which isn't trivial because of
          *     more complex way that those counters are enabled
          *   - Equation: (PERF_GBIF_AXI0_READ_DATA_BEATS_TOTAL + PERF_GBIF_AXI1_READ_DATA_BEATS_TOTAL) * 32 / time
          */
         return 42;
      }
   );
   counter("SP Memory Read (Bytes/sec)", Counter::Units::Byte, [=]() {
         /* Bytes of data read from memory by the Shader Processors, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_8 = PERF_UCHE_VBIF_READ_BEATS_SP
          */
         return (PERF_UCHE_VBIF_READ_BEATS_SP * 32) / time;
      }
   );
   counter("Texture Memory Read BW (Bytes/sec)", Counter::Units::Byte, [=]() {
         /* Bytes of texture data read from memory per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_4 = PERF_UCHE_VBIF_READ_BEATS_TP
          * PERFCOUNTER_GROUP_CMP::COUNTABLE_7 = PERF_CMPDECMP_VBIF_READ_DATA
          */
         return ((PERF_UCHE_VBIF_READ_BEATS_TP + PERF_CMPDECMP_VBIF_READ_DATA) * 32) / time;
      }
   );
   counter("Vertex Memory Read (Bytes/sec)", Counter::Units::Byte, [=]() {
         /* Bytes of vertex data read from memory per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_5 = PERF_UCHE_VBIF_READ_BEATS_VFD
          */
         return (PERF_UCHE_VBIF_READ_BEATS_VFD * 32) / time;
      }
   );
   disabledCounter("Write Total (Bytes/sec)", Counter::Units::Byte, [=]() {
         /* Total number of bytes written by the GPU to memory, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_VBIF::COUNTABLE_46 = PERF_GBIF_AXI0_WRITE_DATA_BEATS_TOTAL
          * PERFCOUNTER_GROUP_VBIF::COUNTABLE_47 = PERF_GBIF_AXI1_WRITE_DATA_BEATS_TOTAL
          * Notes:
          *   - TODO: requires VBIF perfcounter group exposure which isn't trivial because of
          *     more complex way that those counters are enabled
          *   - Equation: (PERF_GBIF_AXI0_WRITE_DATA_BEATS_TOTAL + PERF_GBIF_AXI1_WRITE_DATA_BEATS_TOTAL) * 32 / time
          */
         return 42;
      }
   );

   /**
    * GPU Preemption
    */
   counter("Avg Preemption Delay", Counter::Units::None, [=]() {
         /* Average time (us) from the preemption request to preemption start. */
         /* Countables:
          * PERFCOUNTER_GROUP_CP::COUNTABLE_4 = PERF_CP_PREEMPTION_REACTION_DELAY
          * PERFCOUNTER_GROUP_CP::COUNTABLE_3 = PERF_CP_NUM_PREEMPTIONS
          * PERFCOUNTER_GROUP_CP::COUNTABLE_0 = PERF_CP_ALWAYS_COUNT
          * Note:
          *   - PERF_CP_NUM_PREEMPTIONS has to be divided by 2
          */
         if (!PERF_CP_ALWAYS_COUNT || !PERF_CP_NUM_PREEMPTIONS)
            return 0.0;

         double clocks_per_us = (double)PERF_CP_ALWAYS_COUNT / (time * 1000000);
         double delay_us = PERF_CP_PREEMPTION_REACTION_DELAY / clocks_per_us;
         return delay_us / ((double)PERF_CP_NUM_PREEMPTIONS / 2);
      }
   );
   counter("Preemptions / second", Counter::Units::None, [=]() {
         /* The number of GPU preemptions that occurred, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_CP::COUNTABLE_3 = PERF_CP_NUM_PREEMPTIONS
          * Note:
          *   - PERF_CP_NUM_PREEMPTIONS has to be divided by 2
          */
         return PERF_CP_NUM_PREEMPTIONS / (2 * time);
      }
   );

   /**
    * GPU Primitive Processing
    */
   counter("Average Polygon Area", Counter::Units::None, [=]() {
         /* Average number of pixels per polygon. */
         /* Countables:
          * PERFCOUNTER_GROUP_TSE::COUNTABLE_14 = PERF_TSE_OUTPUT_VISIBLE_PRIM
          * PERFCOUNTER_GROUP_SP::COUNTABLE_101 = PERF_SP_PIXELS
          */
         return safe_div(PERF_SP_PIXELS, PERF_TSE_OUTPUT_VISIBLE_PRIM);
      }
   );
   counter("Average Vertices / Polygon", Counter::Units::None, [=]() {
         /* Average number of vertices per polygon. */
         /* Countables:
          * PERFCOUNTER_GROUP_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          * PERFCOUNTER_GROUP_BV_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          * PERFCOUNTER_GROUP_TSE::COUNTABLE_6 = PERF_TSE_INPUT_PRIM
          */
         return safe_div(cbSum(PERF_PC_VS_INVOCATIONS), PERF_TSE_INPUT_PRIM);
      }
   );
   counter("Pre-clipped Polygons / Second", Counter::Units::None, [=]() {
         /* Number of polygons submitted to the GPU, per second, before any hardware clipping. */
         /* Countables:
          * PERFCOUNTER_GROUP_TSE::COUNTABLE_6 = PERF_TSE_INPUT_PRIM
          */
         return PERF_TSE_INPUT_PRIM / time;
      }
   );
   counter("% Prims Clipped", Counter::Units::Percent, [=]() {
         /* Percentage of primitives clipped by the GPU (where new primitives are generated). */
         /* Countables:
          * PERFCOUNTER_GROUP_TSE::COUNTABLE_9 = PERF_TSE_CLIPPED_PRIM
          * PERFCOUNTER_GROUP_TSE::COUNTABLE_6 = PERF_TSE_INPUT_PRIM
          */
         return percent(PERF_TSE_CLIPPED_PRIM, PERF_TSE_INPUT_PRIM);
      }
   );
   counter("% Prims Trivially Rejected", Counter::Units::Percent, [=]() {
         /* Percentage of primitives that are trivially rejected. */
         /* Countables:
          * PERFCOUNTER_GROUP_TSE::COUNTABLE_8 = PERF_TSE_TRIVAL_REJ_PRIM
          * PERFCOUNTER_GROUP_TSE::COUNTABLE_6 = PERF_TSE_INPUT_PRIM
          */
         return percent(PERF_TSE_TRIVAL_REJ_PRIM, PERF_TSE_INPUT_PRIM);
      }
   );
   counter("Reused Vertices / Second", Counter::Units::None, [=]() {
         /* Number of vertices used from the post-transform vertex buffer cache, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_PC::COUNTABLE_19 = PERF_PC_VERTEX_HITS
          * PERFCOUNTER_GROUP_BV_PC::COUNTABLE_19 = PERF_PC_VERTEX_HITS
          */
         return cbSum(PERF_PC_VERTEX_HITS) / time;
      }
   );

   /**
    * GPU Shader Processing
    */
   counter("ALU / Fragment", Counter::Units::None, [=]() {
         /* Average number of scalar fragment shader ALU instructions issued per shaded fragment, expressed as full precision ALUs (2 mediump = 1 fullp). */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_40 = PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_41 = PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_99 = PERF_SP_QUADS
          * Notes:
          *   - PERF_SP_PIXELS is used instead of PERF_SP_QUADS to avoid SP counter group overcapacity.
          *   - PERF_SP_PIXELS ~ PERF_SP_QUADS * 4
          *   - original equation uses unmultiplied QUADS as denominator, we use PIXELS ~ QUADS * 4
          *     to match other per-fragment counters.
          */
         return safe_div(PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS + PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS / 2,
            PERF_SP_PIXELS);
      }
   );
   counter("ALU / Vertex", Counter::Units::None, [=]() {
         /* Average number of vertex scalar shader ALU instructions issued per shaded vertex. */
         /* Countables:
          * PERFCOUNTER_GROUP_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          * PERFCOUNTER_GROUP_BV_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_35 = PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_35 = PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS
          * Notes:
          *   - Numerator has to be multiplied by four.
          *   - For some reason half-precision ALUs are not counted.
          */
         return safe_div(4 * cbSum(PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS), cbSum(PERF_PC_VS_INVOCATIONS));
      }
   );
   counter("% Anisotropic Filtered", Counter::Units::Percent, [=]() {
         /* Percent of texels filtered using the 'Anisotropic' sampling method. */
         /* Countables:
          * PERFCOUNTER_GROUP_TP::COUNTABLE_10 = PERF_TP_OUTPUT_PIXELS
          * PERFCOUNTER_GROUP_BV_TP::COUNTABLE_10 = PERF_TP_OUTPUT_PIXELS
          * PERFCOUNTER_GROUP_TP::COUNTABLE_28 = PERF_TP_OUTPUT_PIXELS_ANISO
          * PERFCOUNTER_GROUP_BV_TP::COUNTABLE_28 = PERF_TP_OUTPUT_PIXELS_ANISO
          */
         return safe_div(cbSum(PERF_TP_OUTPUT_PIXELS_ANISO), cbSum(PERF_TP_OUTPUT_PIXELS));
      }
   );
   counter("Average BVH Fetch Latency Cycles", Counter::Units::None, [=]() {
         /* The Average BVH Fetch Latency cycles is the latency counted from start of BVH query request till getting BVH Query result back. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_139 = PERF_SP_RTU_BVH_FETCH_LATENCY_CYCLES
          * PERFCOUNTER_GROUP_SP::COUNTABLE_140 = PERF_SP_RTU_BVH_FETCH_LATENCY_SAMPLES
          * Notes:
          *   - TODO: provisional implementation, wasn't able to verify.
          */
         return safe_div(PERF_SP_RTU_BVH_FETCH_LATENCY_CYCLES, PERF_SP_RTU_BVH_FETCH_LATENCY_SAMPLES);
      }
   );
   counter("EFU / Fragment", Counter::Units::None, [=]() {
         /* Average number of scalar fragment shader EFU instructions issued per shaded fragment. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_39 = PERF_SP_FS_STAGE_EFU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_99 = PERF_SP_QUADS
          * Notes:
          *   - PERF_SP_PIXELS is used instead of PERF_SP_QUADS to avoid SP counter group overcapacity.
          *   - PERF_SP_PIXELS ~ PERF_SP_QUADS * 4
          *   - original equation uses unmultiplied QUADS as denominator, we use PIXELS ~ QUADS * 4
          *     to match other per-fragment counters.
          */
         return safe_div(PERF_SP_FS_STAGE_EFU_INSTRUCTIONS, PERF_SP_PIXELS);
      }
   );
   counter("EFU / Vertex", Counter::Units::None, [=]() {
         /* Average number of scalar vertex shader EFU instructions issued per shaded vertex. */
         /* Countables:
          * PERFCOUNTER_GROUP_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          * PERFCOUNTER_GROUP_BV_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_34 = PERF_SP_VS_STAGE_EFU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_34 = PERF_SP_VS_STAGE_EFU_INSTRUCTIONS
          * Notes:
          *   - Numerator has to be multiplied by four.
          */
         return safe_div(4 * cbSum(PERF_SP_VS_STAGE_EFU_INSTRUCTIONS), cbSum(PERF_PC_VS_INVOCATIONS));
      }
   );
   counter("Fragment ALU Instructions / Sec (Full)", Counter::Units::None, [=]() {
         /* Total number of full precision fragment shader instructions issued, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_40 = PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS
          * Notes:
          *   - Numerator has to be multiplied by four.
          */
         return (PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS * 4) / time;
      }
   );
   counter("Fragment ALU Instructions / Sec (Half)", Counter::Units::None, [=]() {
         /* Total number of half precision Scalar fragment shader instructions issued, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_41 = PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS
          * Notes:
          *   - Numerator has to be multiplied by four.
          */
         return (PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS * 4) / time;
      }
   );
   counter("Fragment EFU Instructions / Second", Counter::Units::None, [=]() {
         /* Total number of Scalar fragment shader Elementary Function Unit (EFU) instructions issued, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_39 = PERF_SP_FS_STAGE_EFU_INSTRUCTIONS
          * Notes:
          *   - Numerator has to be multiplied by four.
          */
         return (PERF_SP_FS_STAGE_EFU_INSTRUCTIONS * 4) / time;
      }
   );
   counter("Fragment Instructions / Second", Counter::Units::None, [=]() {
         /* Total number of fragment shader instructions issued, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_39 = PERF_SP_FS_STAGE_EFU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_40 = PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_41 = PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS
          * Notes:
          *   - Numerator has to be multiplied by four.
          */
         return (4 * (PERF_SP_FS_STAGE_EFU_INSTRUCTIONS + PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS +
            + PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS / 2)) / time;
      }
   );
   counter("Fragments Shaded / Second", Counter::Units::None, [=]() {
         /* Number of fragments submitted to the shader engine, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_101 = PERF_SP_PIXELS
          */
         return PERF_SP_PIXELS / time;
      }
   );
   counter("% Linear Filtered", Counter::Units::Percent, [=]() {
         /* Percent of texels filtered using the 'Linear' sampling method. */
         /* Countables:
          * PERFCOUNTER_GROUP_TP::COUNTABLE_10 = PERF_TP_OUTPUT_PIXELS
          * PERFCOUNTER_GROUP_BV_TP::COUNTABLE_10 = PERF_TP_OUTPUT_PIXELS
          * PERFCOUNTER_GROUP_TP::COUNTABLE_26 = PERF_TP_OUTPUT_PIXELS_BILINEAR
          * PERFCOUNTER_GROUP_BV_TP::COUNTABLE_26 = PERF_TP_OUTPUT_PIXELS_BILINEAR
          */
         return safe_div(cbSum(PERF_TP_OUTPUT_PIXELS_BILINEAR), cbSum(PERF_TP_OUTPUT_PIXELS));
      }
   );
   counter("% Nearest Filtered", Counter::Units::Percent, [=]() {
         /* Percent of texels filtered using the 'Nearest' sampling method. */
         /* Countables:
          * PERFCOUNTER_GROUP_TP::COUNTABLE_10 = PERF_TP_OUTPUT_PIXELS
          * PERFCOUNTER_GROUP_BV_TP::COUNTABLE_10 = PERF_TP_OUTPUT_PIXELS
          * PERFCOUNTER_GROUP_TP::COUNTABLE_25 = PERF_TP_OUTPUT_PIXELS_POINT
          * PERFCOUNTER_GROUP_BV_TP::COUNTABLE_25 = PERF_TP_OUTPUT_PIXELS_POINT
          */
         return safe_div(cbSum(PERF_TP_OUTPUT_PIXELS_POINT), cbSum(PERF_TP_OUTPUT_PIXELS));
      }
   );
   disabledCounter("% Non-Base Level Textures", Counter::Units::Percent, [=]() {
         /* Percent of texels coming from a non-base MIP level. */
         /* Countables:
          * PERFCOUNTER_GROUP_TP::COUNTABLE_10 = PERF_TP_OUTPUT_PIXELS
          * PERFCOUNTER_GROUP_BV_TP::COUNTABLE_10 = PERF_TP_OUTPUT_PIXELS
          * PERFCOUNTER_GROUP_TP::COUNTABLE_29 = PERF_TP_OUTPUT_PIXELS_ZERO_LOD
          * PERFCOUNTER_GROUP_BV_TP::COUNTABLE_29 = PERF_TP_OUTPUT_PIXELS_ZERO_LOD
          * Notes:
          *   - FIXME: disabled due to lack of TP counter capacity
          *   - Equation: 100.0 - percent(cbSum(PERF_TP_OUTPUT_PIXELS_ZERO_LOD), cbSum(PERF_TP_OUTPUT_PIXELS));
          */
         return 42;
      }
   );
   counter("% RTU Busy", Counter::Units::Percent, [=]() {
         /* Percentage of time that Ray Tracing Unit in SP is busy compared to whole SP. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_125 = PERF_SP_RTU_BUSY_CYCLES
          * PERFCOUNTER_GROUP_SP::COUNTABLE_0 = PERF_SP_BUSY_CYCLES
          * Notes:
          *   - TODO: provisional implementation, wasn't able to verify.
          */
         return percent(PERF_SP_RTU_BUSY_CYCLES, PERF_SP_BUSY_CYCLES);
      }
   );
   counter("RTU Ray Box Intersections Per Instruction", Counter::Units::None, [=]() {
         /* Number of Ray Box intersections per instruction. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_148 = PERF_SP_RTU_RAY_BOX_INTERSECTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_122 = PERF_SP_RAY_QUERY_INSTRUCTIONS
          * Notes:
          *   - TODO: provisional implementation, wasn't able to verify.
          */
         return safe_div(PERF_SP_RTU_RAY_BOX_INTERSECTIONS, PERF_SP_RAY_QUERY_INSTRUCTIONS);
      }
   );
   counter("RTU Ray Triangle Intersections Per Instruction", Counter::Units::None, [=]() {
         /* Number of Ray Triangle intersections per instruction. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_149 = PERF_SP_RTU_RAY_TRIANGLE_INTERSECTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_122 = PERF_SP_RAY_QUERY_INSTRUCTIONS
          * Notes:
          *   - TODO: provisional implementation, wasn't able to verify.
          */
         return safe_div(PERF_SP_RTU_RAY_TRIANGLE_INTERSECTIONS, PERF_SP_RAY_QUERY_INSTRUCTIONS);
      }
   );
   counter("% Shader ALU Capacity Utilized", Counter::Units::Percent, [=]() {
         /* Percent of maximum shader capacity (ALU operations) utilized. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_0 = PERF_SP_BUSY_CYCLES
          * PERFCOUNTER_GROUP_SP::COUNTABLE_35 = PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_35 = PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_40 = PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_41 = PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS
          * Notes:
          *   - Numerator has to be multiplied by four.
          */
         int64_t numerator = cbSum(PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS) +
            PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS + PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS / 2;
         int64_t denominator = PERF_SP_BUSY_CYCLES * number_of_alus_per_usptp;
         return percent(numerator, denominator);
      }
   );
   counter("% Shaders Busy", Counter::Units::Percent, [=]() {
         /* Percentage of time that all Shader cores are busy. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_0 = PERF_SP_BUSY_CYCLES
          * PERFCOUNTER_GROUP_TP::COUNTABLE_0 = PERF_TP_BUSY_CYCLES
          * PERFCOUNTER_GROUP_RBBM::COUNTABLE_6 = PERF_RBBM_STATUS_MASKED
          * Notes:
          *   - SP_BUSY_CYCLES seems to be used as the numerator -- unless it's zero,
          *     at which point TP_BUSY_CYLCES seems to be used instead.
          */

         int64_t numerator = PERF_SP_BUSY_CYCLES;
         if (!numerator)
            numerator = PERF_TP_BUSY_CYCLES;
         return percent(numerator, number_of_usptp * PERF_RBBM_STATUS_MASKED);
      }
   );
   counter("% Shaders Stalled", Counter::Units::Percent, [=]() {
         /* Percentage of time that all shader cores are idle with at least one active wave. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_7 = PERF_SP_NON_EXECUTION_CYCLES
          * PERFCOUNTER_GROUP_RBBM::COUNTABLE_6 = PERF_RBBM_STATUS_MASKED
          */
         return percent(PERF_SP_NON_EXECUTION_CYCLES, number_of_usptp * PERF_RBBM_STATUS_MASKED);
      }
   );
   counter("% Texture Pipes Busy", Counter::Units::Percent, [=]() {
         /* Percentage of time that any texture pipe is busy. */
         /* Countables:
          * PERFCOUNTER_GROUP_TP::COUNTABLE_0 = PERF_TP_BUSY_CYCLES
          * PERFCOUNTER_GROUP_RBBM::COUNTABLE_6 = PERF_RBBM_STATUS_MASKED
          */
         return percent(PERF_TP_BUSY_CYCLES, number_of_usptp * PERF_RBBM_STATUS_MASKED);
      }
   );
   counter("Textures / Fragment", Counter::Units::None, [=]() {
         /* Average number of textures referenced per fragment. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_33 = PERF_SP_VS_STAGE_TEX_INSTRUCTIONS
          * PERFCOUNTER_GROUP_TP::COUNTABLE_10 = PERF_TP_OUTPUT_PIXELS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_101 = PERF_SP_PIXELS
          */
         return safe_div(PERF_TP_OUTPUT_PIXELS[BR], PERF_SP_PIXELS);
      }
   );
   counter("Textures / Vertex", Counter::Units::None, [=]() {
         /* Average number of textures referenced per vertex. */
         /* Countables:
          * PERFCOUNTER_GROUP_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          * PERFCOUNTER_GROUP_BV_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_33 = PERF_SP_VS_STAGE_TEX_INSTRUCTIONS
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_33 = PERF_SP_VS_STAGE_TEX_INSTRUCTIONS
          * Notes:
          *   - Numerator has to be multiplied by four.
          */
         return safe_div(4 * cbSum(PERF_SP_VS_STAGE_TEX_INSTRUCTIONS), cbSum(PERF_PC_VS_INVOCATIONS));
      }
   );
   counter("% Time ALUs Working", Counter::Units::Percent, [=]() {
         /* Percentage of time the ALUs are working while the Shaders are busy. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_0 = PERF_SP_BUSY_CYCLES
          * PERFCOUNTER_GROUP_SP::COUNTABLE_1 = PERF_SP_ALU_WORKING_CYCLES
          * Notes:
          *   - ALU working cycles have to be halved.
          */
         return percent(PERF_SP_ALU_WORKING_CYCLES / 2, PERF_SP_BUSY_CYCLES);
      }
   );
   counter("% Time Compute", Counter::Units::Percent, [=]() {
         /* Amount of time spent in compute work compared to the total time spent shading everything. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_74 = PERF_SP_ANY_EU_WORKING_FS_STAGE
          * PERFCOUNTER_GROUP_SP::COUNTABLE_76 = PERF_SP_ANY_EU_WORKING_VS_STAGE
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_76 = PERF_SP_ANY_EU_WORKING_VS_STAGE
          * PERFCOUNTER_GROUP_SP::COUNTABLE_78 = PERF_SP_ANY_EU_WORKING_CS_STAGE
          * CS_STAGE amount is also counted in FS_STAGE, so it shouldn't be summed into the total value.
          */
         int64_t total = PERF_SP_ANY_EU_WORKING_FS_STAGE +
            cbSum(PERF_SP_ANY_EU_WORKING_VS_STAGE);
         return percent(PERF_SP_ANY_EU_WORKING_CS_STAGE, total);
      }
   );
   counter("% Time EFUs Working", Counter::Units::Percent, [=]() {
         /* Percentage of time the EFUs are working while the Shaders are busy. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_0 = PERF_SP_BUSY_CYCLES
          * PERFCOUNTER_GROUP_SP::COUNTABLE_2 = PERF_SP_EFU_WORKING_CYCLES
          */
         return percent(PERF_SP_EFU_WORKING_CYCLES, PERF_SP_BUSY_CYCLES);
      }
   );
   counter("% Time Shading Fragments", Counter::Units::Percent, [=]() {
         /* Amount of time spent shading fragments compared to the total time spent shading everything. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_74 = PERF_SP_ANY_EU_WORKING_FS_STAGE
          * PERFCOUNTER_GROUP_SP::COUNTABLE_76 = PERF_SP_ANY_EU_WORKING_VS_STAGE
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_76 = PERF_SP_ANY_EU_WORKING_VS_STAGE
          * PERFCOUNTER_GROUP_SP::COUNTABLE_78 = PERF_SP_ANY_EU_WORKING_CS_STAGE
          * Notes:
          *   - CS_STAGE amount is also counted in FS_STAGE, so fragment time has to be retrieved
          *     through subtraction and the compute time shouldn't be summed into the total value.
          */
         int64_t fragments = PERF_SP_ANY_EU_WORKING_FS_STAGE - PERF_SP_ANY_EU_WORKING_CS_STAGE;
         int64_t total = PERF_SP_ANY_EU_WORKING_FS_STAGE +
            cbSum(PERF_SP_ANY_EU_WORKING_VS_STAGE);
         return percent(fragments, total);
      }
   );
   counter("% Time Shading Vertices", Counter::Units::Percent, [=]() {
         /* Amount of time spent shading vertices compared to the total time spent shading everything. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_74 = PERF_SP_ANY_EU_WORKING_FS_STAGE
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_74 = PERF_SP_ANY_EU_WORKING_FS_STAGE
          * PERFCOUNTER_GROUP_SP::COUNTABLE_76 = PERF_SP_ANY_EU_WORKING_VS_STAGE
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_76 = PERF_SP_ANY_EU_WORKING_VS_STAGE
          * Notes:
          *   - CS_STAGE amount is also counted in FS_STAGE, so it shouldn't be summed into the total value.
          */
         int64_t total = PERF_SP_ANY_EU_WORKING_FS_STAGE +
            cbSum(PERF_SP_ANY_EU_WORKING_VS_STAGE);
         return percent(cbSum(PERF_SP_ANY_EU_WORKING_VS_STAGE), total);
      }
   );
   counter("Vertex Instructions / Second", Counter::Units::None, [=]() {
         /* Total number of scalar vertex shader instructions issued, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_34 = PERF_SP_VS_STAGE_EFU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_35 = PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_34 = PERF_SP_VS_STAGE_EFU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_35 = PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS
          * Notes:
              - Numerator has to be multiplied by four.
          */
         return (4 * (cbSum(PERF_SP_VS_STAGE_EFU_INSTRUCTIONS) + cbSum(PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS))) / time;
      }
   );
   counter("Vertices Shaded / Second", Counter::Units::None, [=]() {
         /* Number of vertices submitted to the shader engine, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          * PERFCOUNTER_GROUP_BV_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          */
         return cbSum(PERF_PC_VS_INVOCATIONS) / time;
      }
   );
   disabledCounter("% Wave Context Occupancy", Counter::Units::Percent, [=]() {
         /* Average percentage of wave context occupancy per cycle. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_8 = PERF_SP_WAVE_CONTEXTS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_9 = PERF_SP_WAVE_CONTEXT_CYCLES
          * Note:
          *   - FIXME: disabled due to lack of SP counter capacity
          *   - the quotient has to be divided by the number of execution wave slots per SP (16 on a7xx)
          *   - Equation: (PERF_SP_WAVE_CONTEXTS / PERF_SP_WAVE_CONTEXT_CYCLES) / number_of_execution_wave_slots_per_sp;
          */
         return 42;
      }
   );

   /**
    * GPU Stalls
    */
   counter("% BVH Fetch Stall", Counter::Units::Percent, [=]() {
         /* Percentage of clock cycles where the RTU could not make any more requests for BVH fetch from scheduler. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_150 = PERF_SP_SCH_STALL_CYCLES_RTU
          * PERFCOUNTER_GROUP_RBBM::COUNTABLE_6 = PERF_RBBM_STATUS_MASKED
          * Notes:
          *   - TODO: provisional implementation, wasn't able to verify.
          */
         return percent(PERF_SP_SCH_STALL_CYCLES_RTU, PERF_RBBM_STATUS_MASKED);
      }
   );
   counter("% Instruction Cache Miss", Counter::Units::Percent, [=]() {
         /* Number of L1 instruction cache misses divided by L1 instruction cache requests. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_51 = PERF_SP_ICL1_REQUESTS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_52 = PERF_SP_ICL1_MISSES
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_51 = PERF_SP_ICL1_REQUESTS
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_52 = PERF_SP_ICL1_MISSES
          */
         return percent(cbSum(PERF_SP_ICL1_MISSES), cbSum(PERF_SP_ICL1_REQUESTS));
      }
   );
   counter("L1 Texture Cache Miss Per Pixel", Counter::Units::None, [=]() {
         /* Average number of Texture L1 cache misses per pixel. */
         /* Countables:
          * PERFCOUNTER_GROUP_TP::COUNTABLE_7 = PERF_TP_L1_CACHELINE_MISSES
          * PERFCOUNTER_GROUP_BV_TP::COUNTABLE_7 = PERF_TP_L1_CACHELINE_MISSES
          * PERFCOUNTER_GROUP_SP::COUNTABLE_101 = PERF_SP_PIXELS
          */
         return safe_div(cbSum(PERF_TP_L1_CACHELINE_MISSES), PERF_SP_PIXELS);
      }
   );
   counter("% Stalled On System Memory", Counter::Units::Percent, [=]() {
         /* Percentage of cycles the L2 cache is stalled waiting for data from system memory. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_1 = PERF_UCHE_STALL_CYCLES_ARBITER
          * PERFCOUNTER_GROUP_RBBM::COUNTABLE_6 = PERF_RBBM_STATUS_MASKED
          * Notes:
          *   - denominator has to be multiplied by four, for unknown reasons.
          */
         return safe_div(PERF_UCHE_STALL_CYCLES_ARBITER, 4 * PERF_RBBM_STATUS_MASKED);
      }
   );
   counter("% Texture Fetch Stall", Counter::Units::Percent, [=]() {
         /* Percentage of clock cycles where the shader processors cannot make any more requests for texture data. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_4 = PERF_SP_STALL_CYCLES_TP
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_4 = PERF_SP_STALL_CYCLES_TP
          * PERFCOUNTER_GROUP_RBBM::COUNTABLE_6 = PERF_RBBM_STATUS_MASKED
          */
         return percent(cbSum(PERF_SP_STALL_CYCLES_TP), number_of_usptp * PERF_RBBM_STATUS_MASKED);
      }
   );
   counter("% Texture L1 Miss", Counter::Units::Percent, [=]() {
         /* Number of L1 texture cache misses divided by L1 texture cache requests. */
         /* Countables:
          * PERFCOUNTER_GROUP_TP::COUNTABLE_6 = PERF_TP_L1_CACHELINE_REQUESTS
          * PERFCOUNTER_GROUP_TP::COUNTABLE_7 = PERF_TP_L1_CACHELINE_MISSES
          * PERFCOUNTER_GROUP_BV_TP::COUNTABLE_6 = PERF_TP_L1_CACHELINE_REQUESTS
          * PERFCOUNTER_GROUP_BV_TP::COUNTABLE_7 = PERF_TP_L1_CACHELINE_MISSES
          */
         return percent(cbSum(PERF_TP_L1_CACHELINE_MISSES), cbSum(PERF_TP_L1_CACHELINE_REQUESTS));
      }
   );
   counter("% Texture L2 Miss", Counter::Units::Percent, [=]() {
         /* Number of L2 texture cache misses divided by L2 texture cache requests. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_4 = PERF_UCHE_VBIF_READ_BEATS_TP
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_9 = PERF_UCHE_READ_REQUESTS_TP
          * Notes:
          *   - ratio has to be multiplied by two. Unsure how this constant comes up.
          */
         return percent(2 * PERF_UCHE_VBIF_READ_BEATS_TP, PERF_UCHE_READ_REQUESTS_TP);
      }
   );
   counter("% Vertex Fetch Stall", Counter::Units::Percent, [=]() {
         /* Percentage of clock cycles where the GPU cannot make any more requests for vertex data. */
         /* Countables:
          * PERFCOUNTER_GROUP_PC::COUNTABLE_2 = PERF_PC_STALL_CYCLES_VFD
          * PERFCOUNTER_GROUP_BV_PC::COUNTABLE_2 = PERF_PC_STALL_CYCLES_VFD
          * PERFCOUNTER_GROUP_RBBM::COUNTABLE_6 = PERF_RBBM_STATUS_MASKED
          */
         return percent(cbSum(PERF_PC_STALL_CYCLES_VFD), PERF_RBBM_STATUS_MASKED);
      }
   );
}

/**
 * Generate an submit the cmdstream to configure the counter/countable
 * muxing
 */
void
FreedrenoDriver::configure_counters(bool reset, bool wait)
{
   struct fd_submit *submit = fd_submit_new(pipe);
   enum fd_ringbuffer_flags flags =
      (enum fd_ringbuffer_flags)(FD_RINGBUFFER_PRIMARY | FD_RINGBUFFER_GROWABLE);
   struct fd_ringbuffer *ring = fd_submit_new_ringbuffer(submit, 0x1000, flags);

   for (const auto &countable : countables)
      countable.configure(ring, reset);

   struct fd_fence *fence = fd_submit_flush(submit, -1, false);

   fd_fence_flush(fence);
   fd_fence_del(fence);

   fd_ringbuffer_del(ring);
   fd_submit_del(submit);

   if (wait)
      fd_pipe_wait(pipe, fence);
}

/**
 * Read the current counter values and record the time.
 */
void
FreedrenoDriver::collect_countables()
{
   last_dump_ts = perfetto::base::GetBootTimeNs().count();

   for (const auto &countable : countables)
      countable.collect();
}

bool
FreedrenoDriver::init_perfcnt()
{
   uint64_t val;

   if (dev)
      fd_device_del(dev);

   dev = fd_device_new(drm_device.fd);
   pipe = fd_pipe_new2(dev, FD_PIPE_3D, 0);
   dev_id = fd_pipe_dev_id(pipe);

   if (fd_pipe_get_param(pipe, FD_MAX_FREQ, &val)) {
      PERFETTO_FATAL("Could not get MAX_FREQ");
      return false;
   }
   max_freq = val;

   if (fd_pipe_get_param(pipe, FD_SUSPEND_COUNT, &val)) {
      PERFETTO_ILOG("Could not get SUSPEND_COUNT");
   } else {
      suspend_count = val;
      has_suspend_count = true;
   }

   fd_pipe_set_param(pipe, FD_SYSPROF, 1);

   perfcntrs = fd_perfcntrs(fd_pipe_dev_id(pipe), &num_perfcntrs);
   if (num_perfcntrs == 0) {
      PERFETTO_FATAL("No hw counters available");
      return false;
   }

   assigned_counters.resize(num_perfcntrs);
   assigned_counters.assign(assigned_counters.size(), 0);

   info = fd_dev_info_raw(dev_id);

   switch (fd_dev_gen(dev_id)) {
   case 6:
      setup_a6xx_counters();
      break;
   case 7:
      setup_a7xx_counters();
      break;
   default:
      PERFETTO_FATAL("Unsupported GPU: a%03u", fd_dev_gpu_id(dev_id));
      return false;
   }

   state.resize(next_countable_id);

   for (const auto &countable : countables)
      countable.resolve();

   io = fd_dt_find_io();
   if (!io) {
      PERFETTO_FATAL("Could not map GPU I/O space");
      return false;
   }

   configure_counters(true, true);
   collect_countables();

   return true;
}

void
FreedrenoDriver::enable_counter(const uint32_t counter_id)
{
   enabled_counters.push_back(counters[counter_id]);
}

void
FreedrenoDriver::enable_all_counters()
{
   enabled_counters.reserve(counters.size());
   for (auto &counter : counters) {
      enabled_counters.push_back(counter);
   }
}

void
FreedrenoDriver::enable_perfcnt(const uint64_t /* sampling_period_ns */)
{
}

bool
FreedrenoDriver::dump_perfcnt()
{
   if (has_suspend_count) {
      uint64_t val;

      fd_pipe_get_param(pipe, FD_SUSPEND_COUNT, &val);

      if (suspend_count != val) {
         PERFETTO_ILOG("Device had suspended!");

         suspend_count = val;

         configure_counters(true, true);
         collect_countables();

         /* We aren't going to have anything sensible by comparing
          * current values to values from prior to the suspend, so
          * just skip this sampling period.
          */
         return false;
      }
   }

   auto last_ts = last_dump_ts;

   /* Capture the timestamp from the *start* of the sampling period: */
   last_capture_ts = last_dump_ts;

   collect_countables();

   auto elapsed_time_ns = last_dump_ts - last_ts;

   time = (float)elapsed_time_ns / 1000000000.0;

   /* On older kernels that dont' support querying the suspend-
    * count, just send configuration cmdstream regularly to keep
    * the GPU alive and correctly configured for the countables
    * we want
    */
   if (!has_suspend_count) {
      configure_counters(false, false);
   }

   return true;
}

uint64_t FreedrenoDriver::next()
{
   auto ret = last_capture_ts;
   last_capture_ts = 0;
   return ret;
}

void FreedrenoDriver::disable_perfcnt()
{
   /* There isn't really any disable, only reconfiguring which countables
    * get muxed to which counters
    */
}

/*
 * Countable
 */

FreedrenoDriver::Countable
FreedrenoDriver::countable(std::string group, std::string name)
{
   auto countable = Countable(this, group, name);
   countables.emplace_back(countable);
   return countable;
}

FreedrenoDriver::Countable::Countable(FreedrenoDriver *d, std::string group, std::string name)
   : id {d->next_countable_id++}, d {d}, group {group}, name {name}
{
}

/* Emit register writes on ring to configure counter/countable muxing: */
void
FreedrenoDriver::Countable::configure(struct fd_ringbuffer *ring, bool reset) const
{
   const struct fd_perfcntr_countable *countable = d->state[id].countable;
   const struct fd_perfcntr_counter   *counter   = d->state[id].counter;

   OUT_PKT7(ring, CP_WAIT_FOR_IDLE, 0);

   if (counter->enable && reset) {
      OUT_PKT4(ring, counter->enable, 1);
      OUT_RING(ring, 0);
   }

   if (counter->clear && reset) {
      OUT_PKT4(ring, counter->clear, 1);
      OUT_RING(ring, 1);

      OUT_PKT4(ring, counter->clear, 1);
      OUT_RING(ring, 0);
   }

   OUT_PKT4(ring, counter->select_reg, 1);
   OUT_RING(ring, countable->selector);

   if (counter->enable && reset) {
      OUT_PKT4(ring, counter->enable, 1);
      OUT_RING(ring, 1);
   }
}

/* Collect current counter value and calculate delta since last sample: */
void
FreedrenoDriver::Countable::collect() const
{
   const struct fd_perfcntr_counter *counter = d->state[id].counter;

   d->state[id].last_value = d->state[id].value;

   /* this is true on a5xx and later */
   assert(counter->counter_reg_lo + 1 == counter->counter_reg_hi);
   uint64_t *reg = (uint64_t *)((uint32_t *)d->io + counter->counter_reg_lo);

   d->state[id].value = *reg;
}

/* Resolve the countable and assign next counter from it's group: */
void
FreedrenoDriver::Countable::resolve() const
{
   for (unsigned i = 0; i < d->num_perfcntrs; i++) {
      const struct fd_perfcntr_group *g = &d->perfcntrs[i];
      if (group != g->name)
         continue;

      for (unsigned j = 0; j < g->num_countables; j++) {
         const struct fd_perfcntr_countable *c = &g->countables[j];
         if (name != c->name)
            continue;

         d->state[id].countable = c;

         /* Assign a counter from the same group: */
         assert(d->assigned_counters[i] < g->num_counters);
         d->state[id].counter = &g->counters[d->assigned_counters[i]++];

         std::cout << "Countable: " << name << ", group=" << g->name <<
               ", counter=" << d->assigned_counters[i] - 1 << "\n";

         return;
      }
   }
   unreachable("no such countable!");
}

uint64_t
FreedrenoDriver::Countable::get_value() const
{
   return d->state[id].value - d->state[id].last_value;
}

/*
 * DerivedCounter
 */

FreedrenoDriver::DerivedCounter::DerivedCounter(FreedrenoDriver *d, std::string name,
                                                Counter::Units units,
                                                std::function<int64_t()> derive)
   : Counter(d->next_counter_id++, name, 0)
{
   std::cout << "DerivedCounter: " << name << ", id=" << id << "\n";
   this->units = units;
   set_getter([=](const Counter &c, const Driver &d) {
         return derive();
      }
   );
}

FreedrenoDriver::DerivedCounter
FreedrenoDriver::counter(std::string name, Counter::Units units,
                         std::function<int64_t()> derive)
{
   auto counter = DerivedCounter(this, name, units, derive);
   counters.emplace_back(counter);
   return counter;
}

uint32_t
FreedrenoDriver::gpu_clock_id() const
{
   return perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME;
}

uint64_t
FreedrenoDriver::gpu_timestamp() const
{
   return perfetto::base::GetBootTimeNs().count();
}

bool
FreedrenoDriver::cpu_gpu_timestamp(uint64_t &, uint64_t &) const
{
   /* Not supported */
   return false;
}

} // namespace pps
