/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 * All Rights Reserved.
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
 */

#include "si_build_pm4.h"
#include "si_query.h"
#include "util/u_memory.h"

enum si_pc_block_flags
{
   /* This block is part of the shader engine */
   SI_PC_BLOCK_SE = (1 << 0),

   /* Expose per-instance groups instead of summing all instances (within
    * an SE). */
   SI_PC_BLOCK_INSTANCE_GROUPS = (1 << 1),

   /* Expose per-SE groups instead of summing instances across SEs. */
   SI_PC_BLOCK_SE_GROUPS = (1 << 2),

   /* Shader block */
   SI_PC_BLOCK_SHADER = (1 << 3),

   /* Non-shader block with perfcounters windowed by shaders. */
   SI_PC_BLOCK_SHADER_WINDOWED = (1 << 4),
};

enum si_pc_reg_layout
{
   /* All secondary selector dwords follow as one block after the primary
    * selector dwords for the counters that have secondary selectors.
    *
    * Example:
    *    PERFCOUNTER0_SELECT
    *    PERFCOUNTER1_SELECT
    *    PERFCOUNTER0_SELECT1
    *    PERFCOUNTER1_SELECT1
    *    PERFCOUNTER2_SELECT
    *    PERFCOUNTER3_SELECT
    */
   SI_PC_MULTI_BLOCK = 0,

   /* Each secondary selector dword follows immediately after the
    * corresponding primary.
    *
    * Example:
    *    PERFCOUNTER0_SELECT
    *    PERFCOUNTER0_SELECT1
    *    PERFCOUNTER1_SELECT
    *    PERFCOUNTER1_SELECT1
    *    PERFCOUNTER2_SELECT
    *    PERFCOUNTER3_SELECT
    */
   SI_PC_MULTI_ALTERNATE = 1,

   /* All secondary selector dwords follow as one block after all primary
    * selector dwords.
    *
    * Example:
    *    PERFCOUNTER0_SELECT
    *    PERFCOUNTER1_SELECT
    *    PERFCOUNTER2_SELECT
    *    PERFCOUNTER3_SELECT
    *    PERFCOUNTER0_SELECT1
    *    PERFCOUNTER1_SELECT1
    */
   SI_PC_MULTI_TAIL = 2,

   /* Free-form arrangement of selector registers. */
   SI_PC_MULTI_CUSTOM = 3,

   SI_PC_MULTI_MASK = 3,

   /* Registers are laid out in decreasing rather than increasing order. */
   SI_PC_REG_REVERSE = 4,

   SI_PC_FAKE = 8,
};

struct si_pc_block_base {
   const char *name;
   unsigned num_counters;
   unsigned flags;

   unsigned select_or;
   unsigned select0;
   unsigned counter0_lo;
   unsigned *select;
   unsigned *counters;
   unsigned num_multi;
   unsigned num_prelude;
   unsigned layout;
};

struct si_pc_block_gfxdescr {
   struct si_pc_block_base *b;
   unsigned selectors;
   unsigned instances;
};

struct si_pc_block {
   const struct si_pc_block_gfxdescr *b;
   unsigned num_instances;

   unsigned num_groups;
   char *group_names;
   unsigned group_name_stride;

   char *selector_names;
   unsigned selector_name_stride;
};

/* The order is chosen to be compatible with GPUPerfStudio's hardcoding of
 * performance counter group IDs.
 */
static const char *const si_pc_shader_type_suffixes[] = {"",    "_ES", "_GS", "_VS",
                                                         "_PS", "_LS", "_HS", "_CS"};

static const unsigned si_pc_shader_type_bits[] = {
   0x7f,
   S_036780_ES_EN(1),
   S_036780_GS_EN(1),
   S_036780_VS_EN(1),
   S_036780_PS_EN(1),
   S_036780_LS_EN(1),
   S_036780_HS_EN(1),
   S_036780_CS_EN(1),
};

/* Max counters per HW block */
#define SI_QUERY_MAX_COUNTERS 16

#define SI_PC_SHADERS_WINDOWING (1u << 31)

struct si_query_group {
   struct si_query_group *next;
   struct si_pc_block *block;
   unsigned sub_gid;     /* only used during init */
   unsigned result_base; /* only used during init */
   int se;
   int instance;
   unsigned num_counters;
   unsigned selectors[SI_QUERY_MAX_COUNTERS];
};

struct si_query_counter {
   unsigned base;
   unsigned qwords;
   unsigned stride; /* in uint64s */
};

struct si_query_pc {
   struct si_query b;
   struct si_query_buffer buffer;

   /* Size of the results in memory, in bytes. */
   unsigned result_size;

   unsigned shaders;
   unsigned num_counters;
   struct si_query_counter *counters;
   struct si_query_group *groups;
};

static struct si_pc_block_base cik_CB = {
   .name = "CB",
   .num_counters = 4,
   .flags = SI_PC_BLOCK_SE | SI_PC_BLOCK_INSTANCE_GROUPS,

   .select0 = R_037000_CB_PERFCOUNTER_FILTER,
   .counter0_lo = R_035018_CB_PERFCOUNTER0_LO,
   .num_multi = 1,
   .num_prelude = 1,
   .layout = SI_PC_MULTI_ALTERNATE,
};

static unsigned cik_CPC_select[] = {
   R_036024_CPC_PERFCOUNTER0_SELECT,
   R_036010_CPC_PERFCOUNTER0_SELECT1,
   R_03600C_CPC_PERFCOUNTER1_SELECT,
};
static struct si_pc_block_base cik_CPC = {
   .name = "CPC",
   .num_counters = 2,

   .select = cik_CPC_select,
   .counter0_lo = R_034018_CPC_PERFCOUNTER0_LO,
   .num_multi = 1,
   .layout = SI_PC_MULTI_CUSTOM | SI_PC_REG_REVERSE,
};

static struct si_pc_block_base cik_CPF = {
   .name = "CPF",
   .num_counters = 2,

   .select0 = R_03601C_CPF_PERFCOUNTER0_SELECT,
   .counter0_lo = R_034028_CPF_PERFCOUNTER0_LO,
   .num_multi = 1,
   .layout = SI_PC_MULTI_ALTERNATE | SI_PC_REG_REVERSE,
};

static struct si_pc_block_base cik_CPG = {
   .name = "CPG",
   .num_counters = 2,

   .select0 = R_036008_CPG_PERFCOUNTER0_SELECT,
   .counter0_lo = R_034008_CPG_PERFCOUNTER0_LO,
   .num_multi = 1,
   .layout = SI_PC_MULTI_ALTERNATE | SI_PC_REG_REVERSE,
};

static struct si_pc_block_base cik_DB = {
   .name = "DB",
   .num_counters = 4,
   .flags = SI_PC_BLOCK_SE | SI_PC_BLOCK_INSTANCE_GROUPS,

   .select0 = R_037100_DB_PERFCOUNTER0_SELECT,
   .counter0_lo = R_035100_DB_PERFCOUNTER0_LO,
   .num_multi = 3, // really only 2, but there's a gap between registers
   .layout = SI_PC_MULTI_ALTERNATE,
};

static struct si_pc_block_base cik_GDS = {
   .name = "GDS",
   .num_counters = 4,

   .select0 = R_036A00_GDS_PERFCOUNTER0_SELECT,
   .counter0_lo = R_034A00_GDS_PERFCOUNTER0_LO,
   .num_multi = 1,
   .layout = SI_PC_MULTI_TAIL,
};

static unsigned cik_GRBM_counters[] = {
   R_034100_GRBM_PERFCOUNTER0_LO,
   R_03410C_GRBM_PERFCOUNTER1_LO,
};
static struct si_pc_block_base cik_GRBM = {
   .name = "GRBM",
   .num_counters = 2,

   .select0 = R_036100_GRBM_PERFCOUNTER0_SELECT,
   .counters = cik_GRBM_counters,
};

static struct si_pc_block_base cik_GRBMSE = {
   .name = "GRBMSE",
   .num_counters = 4,

   .select0 = R_036108_GRBM_SE0_PERFCOUNTER_SELECT,
   .counter0_lo = R_034114_GRBM_SE0_PERFCOUNTER_LO,
};

static struct si_pc_block_base cik_IA = {
   .name = "IA",
   .num_counters = 4,

   .select0 = R_036210_IA_PERFCOUNTER0_SELECT,
   .counter0_lo = R_034220_IA_PERFCOUNTER0_LO,
   .num_multi = 1,
   .layout = SI_PC_MULTI_TAIL,
};

static struct si_pc_block_base cik_PA_SC = {
   .name = "PA_SC",
   .num_counters = 8,
   .flags = SI_PC_BLOCK_SE,

   .select0 = R_036500_PA_SC_PERFCOUNTER0_SELECT,
   .counter0_lo = R_034500_PA_SC_PERFCOUNTER0_LO,
   .num_multi = 1,
   .layout = SI_PC_MULTI_ALTERNATE,
};

/* According to docs, PA_SU counters are only 48 bits wide. */
static struct si_pc_block_base cik_PA_SU = {
   .name = "PA_SU",
   .num_counters = 4,
   .flags = SI_PC_BLOCK_SE,

   .select0 = R_036400_PA_SU_PERFCOUNTER0_SELECT,
   .counter0_lo = R_034400_PA_SU_PERFCOUNTER0_LO,
   .num_multi = 2,
   .layout = SI_PC_MULTI_ALTERNATE,
};

static struct si_pc_block_base cik_SPI = {
   .name = "SPI",
   .num_counters = 6,
   .flags = SI_PC_BLOCK_SE,

   .select0 = R_036600_SPI_PERFCOUNTER0_SELECT,
   .counter0_lo = R_034604_SPI_PERFCOUNTER0_LO,
   .num_multi = 4,
   .layout = SI_PC_MULTI_BLOCK,
};

static struct si_pc_block_base cik_SQ = {
   .name = "SQ",
   .num_counters = 16,
   .flags = SI_PC_BLOCK_SE | SI_PC_BLOCK_SHADER,

   .select0 = R_036700_SQ_PERFCOUNTER0_SELECT,
   .select_or = S_036700_SQC_BANK_MASK(15) | S_036700_SQC_CLIENT_MASK(15) | S_036700_SIMD_MASK(15),
   .counter0_lo = R_034700_SQ_PERFCOUNTER0_LO,
};

static struct si_pc_block_base cik_SX = {
   .name = "SX",
   .num_counters = 4,
   .flags = SI_PC_BLOCK_SE,

   .select0 = R_036900_SX_PERFCOUNTER0_SELECT,
   .counter0_lo = R_034900_SX_PERFCOUNTER0_LO,
   .num_multi = 2,
   .layout = SI_PC_MULTI_TAIL,
};

static struct si_pc_block_base cik_TA = {
   .name = "TA",
   .num_counters = 2,
   .flags = SI_PC_BLOCK_SE | SI_PC_BLOCK_INSTANCE_GROUPS | SI_PC_BLOCK_SHADER_WINDOWED,

   .select0 = R_036B00_TA_PERFCOUNTER0_SELECT,
   .counter0_lo = R_034B00_TA_PERFCOUNTER0_LO,
   .num_multi = 1,
   .layout = SI_PC_MULTI_ALTERNATE,
};

static struct si_pc_block_base cik_TD = {
   .name = "TD",
   .num_counters = 2,
   .flags = SI_PC_BLOCK_SE | SI_PC_BLOCK_INSTANCE_GROUPS | SI_PC_BLOCK_SHADER_WINDOWED,

   .select0 = R_036C00_TD_PERFCOUNTER0_SELECT,
   .counter0_lo = R_034C00_TD_PERFCOUNTER0_LO,
   .num_multi = 1,
   .layout = SI_PC_MULTI_ALTERNATE,
};

static struct si_pc_block_base cik_TCA = {
   .name = "TCA",
   .num_counters = 4,
   .flags = SI_PC_BLOCK_INSTANCE_GROUPS,

   .select0 = R_036E40_TCA_PERFCOUNTER0_SELECT,
   .counter0_lo = R_034E40_TCA_PERFCOUNTER0_LO,
   .num_multi = 2,
   .layout = SI_PC_MULTI_ALTERNATE,
};

static struct si_pc_block_base cik_TCC = {
   .name = "TCC",
   .num_counters = 4,
   .flags = SI_PC_BLOCK_INSTANCE_GROUPS,

   .select0 = R_036E00_TCC_PERFCOUNTER0_SELECT,
   .counter0_lo = R_034E00_TCC_PERFCOUNTER0_LO,
   .num_multi = 2,
   .layout = SI_PC_MULTI_ALTERNATE,
};

static struct si_pc_block_base cik_TCP = {
   .name = "TCP",
   .num_counters = 4,
   .flags = SI_PC_BLOCK_SE | SI_PC_BLOCK_INSTANCE_GROUPS | SI_PC_BLOCK_SHADER_WINDOWED,

   .select0 = R_036D00_TCP_PERFCOUNTER0_SELECT,
   .counter0_lo = R_034D00_TCP_PERFCOUNTER0_LO,
   .num_multi = 2,
   .layout = SI_PC_MULTI_ALTERNATE,
};

static struct si_pc_block_base cik_VGT = {
   .name = "VGT",
   .num_counters = 4,
   .flags = SI_PC_BLOCK_SE,

   .select0 = R_036230_VGT_PERFCOUNTER0_SELECT,
   .counter0_lo = R_034240_VGT_PERFCOUNTER0_LO,
   .num_multi = 1,
   .layout = SI_PC_MULTI_TAIL,
};

static struct si_pc_block_base cik_WD = {
   .name = "WD",
   .num_counters = 4,

   .select0 = R_036200_WD_PERFCOUNTER0_SELECT,
   .counter0_lo = R_034200_WD_PERFCOUNTER0_LO,
};

static struct si_pc_block_base cik_MC = {
   .name = "MC",
   .num_counters = 4,

   .layout = SI_PC_FAKE,
};

static struct si_pc_block_base cik_SRBM = {
   .name = "SRBM",
   .num_counters = 2,

   .layout = SI_PC_FAKE,
};

static struct si_pc_block_base gfx10_CHA = {
   .name = "CHA",
   .num_counters = 4,

   .select0 = R_037780_CHA_PERFCOUNTER0_SELECT,
   .counter0_lo = R_035800_CHA_PERFCOUNTER0_LO,
   .num_multi = 1,
   .layout = SI_PC_MULTI_ALTERNATE,
};

static struct si_pc_block_base gfx10_CHCG = {
   .name = "CHCG",
   .num_counters = 4,

   .select0 = R_036F18_CHCG_PERFCOUNTER0_SELECT,
   .counter0_lo = R_034F20_CHCG_PERFCOUNTER0_LO,
   .num_multi = 1,
   .layout = SI_PC_MULTI_ALTERNATE,
};

static struct si_pc_block_base gfx10_CHC = {
   .name = "CHC",
   .num_counters = 4,

   .select0 = R_036F00_CHC_PERFCOUNTER0_SELECT,
   .counter0_lo = R_034F00_CHC_PERFCOUNTER0_LO,
   .num_multi = 1,
   .layout = SI_PC_MULTI_ALTERNATE,
};

static struct si_pc_block_base gfx10_GCR = {
   .name = "GCR",
   .num_counters = 2,

   .select0 = R_037580_GCR_PERFCOUNTER0_SELECT,
   .counter0_lo = R_035480_GCR_PERFCOUNTER0_LO,
   .num_multi = 1,
   .layout = SI_PC_MULTI_ALTERNATE,
};

static struct si_pc_block_base gfx10_GE = {
   .name = "GE",
   .num_counters = 12,

   .select0 = R_036200_GE_PERFCOUNTER0_SELECT,
   .counter0_lo = R_034200_GE_PERFCOUNTER0_LO,
   .num_multi = 4,
   .layout = SI_PC_MULTI_ALTERNATE,
};

static struct si_pc_block_base gfx10_GL1A = {
   .name = "GL1A",
   .num_counters = 4,
   .flags = SI_PC_BLOCK_SE | SI_PC_BLOCK_SHADER_WINDOWED,

   .select0 = R_037700_GL1A_PERFCOUNTER0_SELECT,
   .counter0_lo = R_035700_GL1A_PERFCOUNTER0_LO,
   .num_multi = 1,
   .layout = SI_PC_MULTI_ALTERNATE,
};

static struct si_pc_block_base gfx10_GL1C = {
   .name = "GL1C",
   .num_counters = 4,
   .flags = SI_PC_BLOCK_SE | SI_PC_BLOCK_SHADER_WINDOWED,

   .select0 = R_036E80_GL1C_PERFCOUNTER0_SELECT,
   .counter0_lo = R_034E80_GL1C_PERFCOUNTER0_LO,
   .num_multi = 1,
   .layout = SI_PC_MULTI_ALTERNATE,
};

static struct si_pc_block_base gfx10_GL2A = {
   .name = "GL2A",
   .num_counters = 4,

   .select0 = R_036E40_GL2A_PERFCOUNTER0_SELECT,
   .counter0_lo = R_034E40_GL2A_PERFCOUNTER0_LO,
   .num_multi = 2,
   .layout = SI_PC_MULTI_ALTERNATE,
};

static struct si_pc_block_base gfx10_GL2C = {
   .name = "GL2C",
   .num_counters = 4,

   .select0 = R_036E00_GL2C_PERFCOUNTER0_SELECT,
   .counter0_lo = R_034E00_GL2C_PERFCOUNTER0_LO,
   .num_multi = 2,
   .layout = SI_PC_MULTI_ALTERNATE,
};

static unsigned gfx10_PA_PH_select[] = {
   R_037600_PA_PH_PERFCOUNTER0_SELECT,
   R_037604_PA_PH_PERFCOUNTER0_SELECT1,
   R_037608_PA_PH_PERFCOUNTER1_SELECT,
   R_037640_PA_PH_PERFCOUNTER1_SELECT1,
   R_03760C_PA_PH_PERFCOUNTER2_SELECT,
   R_037644_PA_PH_PERFCOUNTER2_SELECT1,
   R_037610_PA_PH_PERFCOUNTER3_SELECT,
   R_037648_PA_PH_PERFCOUNTER3_SELECT1,
   R_037614_PA_PH_PERFCOUNTER4_SELECT,
   R_037618_PA_PH_PERFCOUNTER5_SELECT,
   R_03761C_PA_PH_PERFCOUNTER6_SELECT,
   R_037620_PA_PH_PERFCOUNTER7_SELECT,
};
static struct si_pc_block_base gfx10_PA_PH = {
   .name = "PA_PH",
   .num_counters = 8,
   .flags = SI_PC_BLOCK_SE,

   .select = gfx10_PA_PH_select,
   .counter0_lo = R_035600_PA_PH_PERFCOUNTER0_LO,
   .num_multi = 4,
   .layout = SI_PC_MULTI_CUSTOM,
};

static struct si_pc_block_base gfx10_PA_SU = {
   .name = "PA_SU",
   .num_counters = 4,
   .flags = SI_PC_BLOCK_SE,

   .select0 = R_036400_PA_SU_PERFCOUNTER0_SELECT,
   .counter0_lo = R_034400_PA_SU_PERFCOUNTER0_LO,
   .num_multi = 4,
   .layout = SI_PC_MULTI_ALTERNATE,
};

static struct si_pc_block_base gfx10_RLC = {
   .name = "RLC",
   .num_counters = 2,

   .select0 = R_037304_RLC_PERFCOUNTER0_SELECT,
   .counter0_lo = R_035200_RLC_PERFCOUNTER0_LO,
   .num_multi = 0,
   .layout = SI_PC_MULTI_ALTERNATE,
};

static struct si_pc_block_base gfx10_RMI = {
   .name = "RMI",
   /* Actually 4, but the 2nd counter is missing the secondary selector while
    * the 3rd counter has it, which complicates the register layout. */
   .num_counters = 2,
   .flags = SI_PC_BLOCK_SE | SI_PC_BLOCK_INSTANCE_GROUPS,

   .select0 = R_037400_RMI_PERFCOUNTER0_SELECT,
   .counter0_lo = R_035300_RMI_PERFCOUNTER0_LO,
   .num_multi = 1,
   .layout = SI_PC_MULTI_ALTERNATE,
};

static struct si_pc_block_base gfx10_UTCL1 = {
   .name = "UTCL1",
   .num_counters = 2,
   .flags = SI_PC_BLOCK_SE | SI_PC_BLOCK_SHADER_WINDOWED,

   .select0 = R_03758C_UTCL1_PERFCOUNTER0_SELECT,
   .counter0_lo = R_035470_UTCL1_PERFCOUNTER0_LO,
   .num_multi = 0,
   .layout = SI_PC_MULTI_ALTERNATE,
};

/* Both the number of instances and selectors varies between chips of the same
 * class. We only differentiate by class here and simply expose the maximum
 * number over all chips in a class.
 *
 * Unfortunately, GPUPerfStudio uses the order of performance counter groups
 * blindly once it believes it has identified the hardware, so the order of
 * blocks here matters.
 */
static struct si_pc_block_gfxdescr groups_CIK[] = {
   {&cik_CB, 226},     {&cik_CPF, 17},    {&cik_DB, 257},  {&cik_GRBM, 34},   {&cik_GRBMSE, 15},
   {&cik_PA_SU, 153},  {&cik_PA_SC, 395}, {&cik_SPI, 186}, {&cik_SQ, 252},    {&cik_SX, 32},
   {&cik_TA, 111},     {&cik_TCA, 39, 2}, {&cik_TCC, 160}, {&cik_TD, 55},     {&cik_TCP, 154},
   {&cik_GDS, 121},    {&cik_VGT, 140},   {&cik_IA, 22},   {&cik_MC, 22},     {&cik_SRBM, 19},
   {&cik_WD, 22},      {&cik_CPG, 46},    {&cik_CPC, 22},

};

static struct si_pc_block_gfxdescr groups_VI[] = {
   {&cik_CB, 405},     {&cik_CPF, 19},    {&cik_DB, 257},  {&cik_GRBM, 34},   {&cik_GRBMSE, 15},
   {&cik_PA_SU, 154},  {&cik_PA_SC, 397}, {&cik_SPI, 197}, {&cik_SQ, 273},    {&cik_SX, 34},
   {&cik_TA, 119},     {&cik_TCA, 35, 2}, {&cik_TCC, 192}, {&cik_TD, 55},     {&cik_TCP, 180},
   {&cik_GDS, 121},    {&cik_VGT, 147},   {&cik_IA, 24},   {&cik_MC, 22},     {&cik_SRBM, 27},
   {&cik_WD, 37},      {&cik_CPG, 48},    {&cik_CPC, 24},

};

static struct si_pc_block_gfxdescr groups_gfx9[] = {
   {&cik_CB, 438},     {&cik_CPF, 32},    {&cik_DB, 328},  {&cik_GRBM, 38},   {&cik_GRBMSE, 16},
   {&cik_PA_SU, 292},  {&cik_PA_SC, 491}, {&cik_SPI, 196}, {&cik_SQ, 374},    {&cik_SX, 208},
   {&cik_TA, 119},     {&cik_TCA, 35, 2}, {&cik_TCC, 256}, {&cik_TD, 57},     {&cik_TCP, 85},
   {&cik_GDS, 121},    {&cik_VGT, 148},   {&cik_IA, 32},   {&cik_WD, 58},     {&cik_CPG, 59},
   {&cik_CPC, 35},
};

static struct si_pc_block_gfxdescr groups_gfx10[] = {
   {&cik_CB, 461},
   {&gfx10_CHA, 45},
   {&gfx10_CHCG, 35},
   {&gfx10_CHC, 35},
   {&cik_CPC, 47},
   {&cik_CPF, 40},
   {&cik_CPG, 82},
   {&cik_DB, 370},
   {&gfx10_GCR, 94},
   {&cik_GDS, 123},
   {&gfx10_GE, 315},
   {&gfx10_GL1A, 36},
   {&gfx10_GL1C, 64},
   {&gfx10_GL2A, 91},
   {&gfx10_GL2C, 235},
   {&cik_GRBM, 47},
   {&cik_GRBMSE, 19},
   {&gfx10_PA_PH, 960},
   {&cik_PA_SC, 552},
   {&gfx10_PA_SU, 266},
   {&gfx10_RLC, 7},
   {&gfx10_RMI, 258},
   {&cik_SPI, 329},
   {&cik_SQ, 509},
   {&cik_SX, 225},
   {&cik_TA, 226},
   {&cik_TCP, 77},
   {&cik_TD, 61},
   {&gfx10_UTCL1, 15},
};

static bool si_pc_block_has_per_se_groups(const struct si_perfcounters *pc,
                                          const struct si_pc_block *block)
{
   return block->b->b->flags & SI_PC_BLOCK_SE_GROUPS ||
          (block->b->b->flags & SI_PC_BLOCK_SE && pc->separate_se);
}

static bool si_pc_block_has_per_instance_groups(const struct si_perfcounters *pc,
                                                const struct si_pc_block *block)
{
   return block->b->b->flags & SI_PC_BLOCK_INSTANCE_GROUPS ||
          (block->num_instances > 1 && pc->separate_instance);
}

static struct si_pc_block *lookup_counter(struct si_perfcounters *pc, unsigned index,
                                          unsigned *base_gid, unsigned *sub_index)
{
   struct si_pc_block *block = pc->blocks;
   unsigned bid;

   *base_gid = 0;
   for (bid = 0; bid < pc->num_blocks; ++bid, ++block) {
      unsigned total = block->num_groups * block->b->selectors;

      if (index < total) {
         *sub_index = index;
         return block;
      }

      index -= total;
      *base_gid += block->num_groups;
   }

   return NULL;
}

static struct si_pc_block *lookup_group(struct si_perfcounters *pc, unsigned *index)
{
   unsigned bid;
   struct si_pc_block *block = pc->blocks;

   for (bid = 0; bid < pc->num_blocks; ++bid, ++block) {
      if (*index < block->num_groups)
         return block;
      *index -= block->num_groups;
   }

   return NULL;
}

static void si_pc_emit_instance(struct si_context *sctx, int se, int instance)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   unsigned value = S_030800_SH_BROADCAST_WRITES(1);

   if (se >= 0) {
      value |= S_030800_SE_INDEX(se);
   } else {
      value |= S_030800_SE_BROADCAST_WRITES(1);
   }

   if (sctx->chip_class >= GFX10) {
      /* TODO: Expose counters from each shader array separately if needed. */
      value |= S_030800_SA_BROADCAST_WRITES(1);
   }

   if (instance >= 0) {
      value |= S_030800_INSTANCE_INDEX(instance);
   } else {
      value |= S_030800_INSTANCE_BROADCAST_WRITES(1);
   }

   radeon_begin(cs);
   radeon_set_uconfig_reg(cs, R_030800_GRBM_GFX_INDEX, value);
   radeon_end();
}

static void si_pc_emit_shaders(struct si_context *sctx, unsigned shaders)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   radeon_begin(cs);
   radeon_set_uconfig_reg_seq(cs, R_036780_SQ_PERFCOUNTER_CTRL, 2, false);
   radeon_emit(cs, shaders & 0x7f);
   radeon_emit(cs, 0xffffffff);
   radeon_end();
}

static void si_pc_emit_select(struct si_context *sctx, struct si_pc_block *block, unsigned count,
                              unsigned *selectors)
{
   struct si_pc_block_base *regs = block->b->b;
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   unsigned idx;
   unsigned layout_multi = regs->layout & SI_PC_MULTI_MASK;
   unsigned dw;

   assert(count <= regs->num_counters);

   if (regs->layout & SI_PC_FAKE)
      return;

   radeon_begin(cs);

   if (layout_multi == SI_PC_MULTI_BLOCK) {
      assert(!(regs->layout & SI_PC_REG_REVERSE));

      dw = count + regs->num_prelude;
      if (count >= regs->num_multi)
         dw += regs->num_multi;
      radeon_set_uconfig_reg_seq(cs, regs->select0, dw, false);
      for (idx = 0; idx < regs->num_prelude; ++idx)
         radeon_emit(cs, 0);
      for (idx = 0; idx < MIN2(count, regs->num_multi); ++idx)
         radeon_emit(cs, selectors[idx] | regs->select_or);

      if (count < regs->num_multi) {
         unsigned select1 = regs->select0 + 4 * regs->num_multi;
         radeon_set_uconfig_reg_seq(cs, select1, count, false);
      }

      for (idx = 0; idx < MIN2(count, regs->num_multi); ++idx)
         radeon_emit(cs, 0);

      if (count > regs->num_multi) {
         for (idx = regs->num_multi; idx < count; ++idx)
            radeon_emit(cs, selectors[idx] | regs->select_or);
      }
   } else if (layout_multi == SI_PC_MULTI_TAIL) {
      unsigned select1, select1_count;

      assert(!(regs->layout & SI_PC_REG_REVERSE));

      radeon_set_uconfig_reg_seq(cs, regs->select0, count + regs->num_prelude, false);
      for (idx = 0; idx < regs->num_prelude; ++idx)
         radeon_emit(cs, 0);
      for (idx = 0; idx < count; ++idx)
         radeon_emit(cs, selectors[idx] | regs->select_or);

      select1 = regs->select0 + 4 * regs->num_counters;
      select1_count = MIN2(count, regs->num_multi);
      radeon_set_uconfig_reg_seq(cs, select1, select1_count, false);
      for (idx = 0; idx < select1_count; ++idx)
         radeon_emit(cs, 0);
   } else if (layout_multi == SI_PC_MULTI_CUSTOM) {
      unsigned *reg = regs->select;
      for (idx = 0; idx < count; ++idx) {
         radeon_set_uconfig_reg(cs, *reg++, selectors[idx] | regs->select_or);
         if (idx < regs->num_multi)
            radeon_set_uconfig_reg(cs, *reg++, 0);
      }
   } else {
      assert(layout_multi == SI_PC_MULTI_ALTERNATE);

      unsigned reg_base = regs->select0;
      unsigned reg_count = count + MIN2(count, regs->num_multi);
      reg_count += regs->num_prelude;

      if (!(regs->layout & SI_PC_REG_REVERSE)) {
         radeon_set_uconfig_reg_seq(cs, reg_base, reg_count, false);

         for (idx = 0; idx < regs->num_prelude; ++idx)
            radeon_emit(cs, 0);
         for (idx = 0; idx < count; ++idx) {
            radeon_emit(cs, selectors[idx] | regs->select_or);
            if (idx < regs->num_multi)
               radeon_emit(cs, 0);
         }
      } else {
         reg_base -= (reg_count - 1) * 4;
         radeon_set_uconfig_reg_seq(cs, reg_base, reg_count, false);

         for (idx = count; idx > 0; --idx) {
            if (idx <= regs->num_multi)
               radeon_emit(cs, 0);
            radeon_emit(cs, selectors[idx - 1] | regs->select_or);
         }
         for (idx = 0; idx < regs->num_prelude; ++idx)
            radeon_emit(cs, 0);
      }
   }
   radeon_end();
}

static void si_pc_emit_start(struct si_context *sctx, struct si_resource *buffer, uint64_t va)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   si_cp_copy_data(sctx, &sctx->gfx_cs, COPY_DATA_DST_MEM, buffer, va - buffer->gpu_address,
                   COPY_DATA_IMM, NULL, 1);

   radeon_begin(cs);
   radeon_set_uconfig_reg(cs, R_036020_CP_PERFMON_CNTL,
                          S_036020_PERFMON_STATE(V_036020_CP_PERFMON_STATE_DISABLE_AND_RESET));
   radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
   radeon_emit(cs, EVENT_TYPE(V_028A90_PERFCOUNTER_START) | EVENT_INDEX(0));
   radeon_set_uconfig_reg(cs, R_036020_CP_PERFMON_CNTL,
                          S_036020_PERFMON_STATE(V_036020_CP_PERFMON_STATE_START_COUNTING));
   radeon_end();
}

/* Note: The buffer was already added in si_pc_emit_start, so we don't have to
 * do it again in here. */
static void si_pc_emit_stop(struct si_context *sctx, struct si_resource *buffer, uint64_t va)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   si_cp_release_mem(sctx, cs, V_028A90_BOTTOM_OF_PIPE_TS, 0, EOP_DST_SEL_MEM, EOP_INT_SEL_NONE,
                     EOP_DATA_SEL_VALUE_32BIT, buffer, va, 0, SI_NOT_QUERY);
   si_cp_wait_mem(sctx, cs, va, 0, 0xffffffff, WAIT_REG_MEM_EQUAL);

   radeon_begin(cs);
   radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
   radeon_emit(cs, EVENT_TYPE(V_028A90_PERFCOUNTER_SAMPLE) | EVENT_INDEX(0));
   radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
   radeon_emit(cs, EVENT_TYPE(V_028A90_PERFCOUNTER_STOP) | EVENT_INDEX(0));
   radeon_set_uconfig_reg(
      cs, R_036020_CP_PERFMON_CNTL,
      S_036020_PERFMON_STATE(V_036020_CP_PERFMON_STATE_STOP_COUNTING) | S_036020_PERFMON_SAMPLE_ENABLE(1));
   radeon_end();
}

static void si_pc_emit_read(struct si_context *sctx, struct si_pc_block *block, unsigned count,
                            uint64_t va)
{
   struct si_pc_block_base *regs = block->b->b;
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   unsigned idx;
   unsigned reg = regs->counter0_lo;
   unsigned reg_delta = 8;

   radeon_begin(cs);

   if (!(regs->layout & SI_PC_FAKE)) {
      if (regs->layout & SI_PC_REG_REVERSE)
         reg_delta = -reg_delta;

      for (idx = 0; idx < count; ++idx) {
         if (regs->counters)
            reg = regs->counters[idx];

         radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
         radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_PERF) | COPY_DATA_DST_SEL(COPY_DATA_DST_MEM) |
                            COPY_DATA_COUNT_SEL); /* 64 bits */
         radeon_emit(cs, reg >> 2);
         radeon_emit(cs, 0); /* unused */
         radeon_emit(cs, va);
         radeon_emit(cs, va >> 32);
         va += sizeof(uint64_t);
         reg += reg_delta;
      }
   } else {
      for (idx = 0; idx < count; ++idx) {
         radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
         radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_IMM) | COPY_DATA_DST_SEL(COPY_DATA_DST_MEM) |
                            COPY_DATA_COUNT_SEL);
         radeon_emit(cs, 0); /* immediate */
         radeon_emit(cs, 0);
         radeon_emit(cs, va);
         radeon_emit(cs, va >> 32);
         va += sizeof(uint64_t);
      }
   }
   radeon_end();
}

static void si_pc_query_destroy(struct si_context *sctx, struct si_query *squery)
{
   struct si_query_pc *query = (struct si_query_pc *)squery;

   while (query->groups) {
      struct si_query_group *group = query->groups;
      query->groups = group->next;
      FREE(group);
   }

   FREE(query->counters);

   si_query_buffer_destroy(sctx->screen, &query->buffer);
   FREE(query);
}

void si_inhibit_clockgating(struct si_context *sctx, struct radeon_cmdbuf *cs, bool inhibit)
{
   radeon_begin(&sctx->gfx_cs);

   if (sctx->chip_class >= GFX10) {
      radeon_set_uconfig_reg(cs, R_037390_RLC_PERFMON_CLK_CNTL,
                             S_037390_PERFMON_CLOCK_STATE(inhibit));
   } else if (sctx->chip_class >= GFX8) {
      radeon_set_uconfig_reg(cs, R_0372FC_RLC_PERFMON_CLK_CNTL,
                             S_0372FC_PERFMON_CLOCK_STATE(inhibit));
   }
   radeon_end();
}

static void si_pc_query_resume(struct si_context *sctx, struct si_query *squery)
/*
                                   struct si_query_hw *hwquery,
                                   struct si_resource *buffer, uint64_t va)*/
{
   struct si_query_pc *query = (struct si_query_pc *)squery;
   int current_se = -1;
   int current_instance = -1;

   if (!si_query_buffer_alloc(sctx, &query->buffer, NULL, query->result_size))
      return;
   si_need_gfx_cs_space(sctx, 0);

   if (query->shaders)
      si_pc_emit_shaders(sctx, query->shaders);

   si_inhibit_clockgating(sctx, &sctx->gfx_cs, true);

   for (struct si_query_group *group = query->groups; group; group = group->next) {
      struct si_pc_block *block = group->block;

      if (group->se != current_se || group->instance != current_instance) {
         current_se = group->se;
         current_instance = group->instance;
         si_pc_emit_instance(sctx, group->se, group->instance);
      }

      si_pc_emit_select(sctx, block, group->num_counters, group->selectors);
   }

   if (current_se != -1 || current_instance != -1)
      si_pc_emit_instance(sctx, -1, -1);

   uint64_t va = query->buffer.buf->gpu_address + query->buffer.results_end;
   si_pc_emit_start(sctx, query->buffer.buf, va);
}

static void si_pc_query_suspend(struct si_context *sctx, struct si_query *squery)
{
   struct si_query_pc *query = (struct si_query_pc *)squery;

   if (!query->buffer.buf)
      return;

   uint64_t va = query->buffer.buf->gpu_address + query->buffer.results_end;
   query->buffer.results_end += query->result_size;

   si_pc_emit_stop(sctx, query->buffer.buf, va);

   for (struct si_query_group *group = query->groups; group; group = group->next) {
      struct si_pc_block *block = group->block;
      unsigned se = group->se >= 0 ? group->se : 0;
      unsigned se_end = se + 1;

      if ((block->b->b->flags & SI_PC_BLOCK_SE) && (group->se < 0))
         se_end = sctx->screen->info.max_se;

      do {
         unsigned instance = group->instance >= 0 ? group->instance : 0;

         do {
            si_pc_emit_instance(sctx, se, instance);
            si_pc_emit_read(sctx, block, group->num_counters, va);
            va += sizeof(uint64_t) * group->num_counters;
         } while (group->instance < 0 && ++instance < block->num_instances);
      } while (++se < se_end);
   }

   si_pc_emit_instance(sctx, -1, -1);

   si_inhibit_clockgating(sctx, &sctx->gfx_cs, false);
}

static bool si_pc_query_begin(struct si_context *ctx, struct si_query *squery)
{
   struct si_query_pc *query = (struct si_query_pc *)squery;

   si_query_buffer_reset(ctx, &query->buffer);

   list_addtail(&query->b.active_list, &ctx->active_queries);
   ctx->num_cs_dw_queries_suspend += query->b.num_cs_dw_suspend;

   si_pc_query_resume(ctx, squery);

   return true;
}

static bool si_pc_query_end(struct si_context *ctx, struct si_query *squery)
{
   struct si_query_pc *query = (struct si_query_pc *)squery;

   si_pc_query_suspend(ctx, squery);

   list_del(&squery->active_list);
   ctx->num_cs_dw_queries_suspend -= squery->num_cs_dw_suspend;

   return query->buffer.buf != NULL;
}

static void si_pc_query_add_result(struct si_query_pc *query, void *buffer,
                                   union pipe_query_result *result)
{
   uint64_t *results = buffer;
   unsigned i, j;

   for (i = 0; i < query->num_counters; ++i) {
      struct si_query_counter *counter = &query->counters[i];

      for (j = 0; j < counter->qwords; ++j) {
         uint32_t value = results[counter->base + j * counter->stride];
         result->batch[i].u64 += value;
      }
   }
}

static bool si_pc_query_get_result(struct si_context *sctx, struct si_query *squery, bool wait,
                                   union pipe_query_result *result)
{
   struct si_query_pc *query = (struct si_query_pc *)squery;

   memset(result, 0, sizeof(result->batch[0]) * query->num_counters);

   for (struct si_query_buffer *qbuf = &query->buffer; qbuf; qbuf = qbuf->previous) {
      unsigned usage = PIPE_MAP_READ | (wait ? 0 : PIPE_MAP_DONTBLOCK);
      unsigned results_base = 0;
      void *map;

      if (squery->b.flushed)
         map = sctx->ws->buffer_map(qbuf->buf->buf, NULL, usage);
      else
         map = si_buffer_map(sctx, qbuf->buf, usage);

      if (!map)
         return false;

      while (results_base != qbuf->results_end) {
         si_pc_query_add_result(query, map + results_base, result);
         results_base += query->result_size;
      }
   }

   return true;
}

static const struct si_query_ops batch_query_ops = {
   .destroy = si_pc_query_destroy,
   .begin = si_pc_query_begin,
   .end = si_pc_query_end,
   .get_result = si_pc_query_get_result,

   .suspend = si_pc_query_suspend,
   .resume = si_pc_query_resume,
};

static struct si_query_group *get_group_state(struct si_screen *screen, struct si_query_pc *query,
                                              struct si_pc_block *block, unsigned sub_gid)
{
   struct si_query_group *group = query->groups;

   while (group) {
      if (group->block == block && group->sub_gid == sub_gid)
         return group;
      group = group->next;
   }

   group = CALLOC_STRUCT(si_query_group);
   if (!group)
      return NULL;

   group->block = block;
   group->sub_gid = sub_gid;

   if (block->b->b->flags & SI_PC_BLOCK_SHADER) {
      unsigned sub_gids = block->num_instances;
      unsigned shader_id;
      unsigned shaders;
      unsigned query_shaders;

      if (si_pc_block_has_per_se_groups(screen->perfcounters, block))
         sub_gids = sub_gids * screen->info.max_se;
      shader_id = sub_gid / sub_gids;
      sub_gid = sub_gid % sub_gids;

      shaders = si_pc_shader_type_bits[shader_id];

      query_shaders = query->shaders & ~SI_PC_SHADERS_WINDOWING;
      if (query_shaders && query_shaders != shaders) {
         fprintf(stderr, "si_perfcounter: incompatible shader groups\n");
         FREE(group);
         return NULL;
      }
      query->shaders = shaders;
   }

   if (block->b->b->flags & SI_PC_BLOCK_SHADER_WINDOWED && !query->shaders) {
      // A non-zero value in query->shaders ensures that the shader
      // masking is reset unless the user explicitly requests one.
      query->shaders = SI_PC_SHADERS_WINDOWING;
   }

   if (si_pc_block_has_per_se_groups(screen->perfcounters, block)) {
      group->se = sub_gid / block->num_instances;
      sub_gid = sub_gid % block->num_instances;
   } else {
      group->se = -1;
   }

   if (si_pc_block_has_per_instance_groups(screen->perfcounters, block)) {
      group->instance = sub_gid;
   } else {
      group->instance = -1;
   }

   group->next = query->groups;
   query->groups = group;

   return group;
}

struct pipe_query *si_create_batch_query(struct pipe_context *ctx, unsigned num_queries,
                                         unsigned *query_types)
{
   struct si_screen *screen = (struct si_screen *)ctx->screen;
   struct si_perfcounters *pc = screen->perfcounters;
   struct si_pc_block *block;
   struct si_query_group *group;
   struct si_query_pc *query;
   unsigned base_gid, sub_gid, sub_index;
   unsigned i, j;

   if (!pc)
      return NULL;

   query = CALLOC_STRUCT(si_query_pc);
   if (!query)
      return NULL;

   query->b.ops = &batch_query_ops;

   query->num_counters = num_queries;

   /* Collect selectors per group */
   for (i = 0; i < num_queries; ++i) {
      unsigned sub_gid;

      if (query_types[i] < SI_QUERY_FIRST_PERFCOUNTER)
         goto error;

      block =
         lookup_counter(pc, query_types[i] - SI_QUERY_FIRST_PERFCOUNTER, &base_gid, &sub_index);
      if (!block)
         goto error;

      sub_gid = sub_index / block->b->selectors;
      sub_index = sub_index % block->b->selectors;

      group = get_group_state(screen, query, block, sub_gid);
      if (!group)
         goto error;

      if (group->num_counters >= block->b->b->num_counters) {
         fprintf(stderr, "perfcounter group %s: too many selected\n", block->b->b->name);
         goto error;
      }
      group->selectors[group->num_counters] = sub_index;
      ++group->num_counters;
   }

   /* Compute result bases and CS size per group */
   query->b.num_cs_dw_suspend = pc->num_stop_cs_dwords;
   query->b.num_cs_dw_suspend += pc->num_instance_cs_dwords;

   i = 0;
   for (group = query->groups; group; group = group->next) {
      struct si_pc_block *block = group->block;
      unsigned read_dw;
      unsigned instances = 1;

      if ((block->b->b->flags & SI_PC_BLOCK_SE) && group->se < 0)
         instances = screen->info.max_se;
      if (group->instance < 0)
         instances *= block->num_instances;

      group->result_base = i;
      query->result_size += sizeof(uint64_t) * instances * group->num_counters;
      i += instances * group->num_counters;

      read_dw = 6 * group->num_counters;
      query->b.num_cs_dw_suspend += instances * read_dw;
      query->b.num_cs_dw_suspend += instances * pc->num_instance_cs_dwords;
   }

   if (query->shaders) {
      if (query->shaders == SI_PC_SHADERS_WINDOWING)
         query->shaders = 0xffffffff;
   }

   /* Map user-supplied query array to result indices */
   query->counters = CALLOC(num_queries, sizeof(*query->counters));
   for (i = 0; i < num_queries; ++i) {
      struct si_query_counter *counter = &query->counters[i];
      struct si_pc_block *block;

      block =
         lookup_counter(pc, query_types[i] - SI_QUERY_FIRST_PERFCOUNTER, &base_gid, &sub_index);

      sub_gid = sub_index / block->b->selectors;
      sub_index = sub_index % block->b->selectors;

      group = get_group_state(screen, query, block, sub_gid);
      assert(group != NULL);

      for (j = 0; j < group->num_counters; ++j) {
         if (group->selectors[j] == sub_index)
            break;
      }

      counter->base = group->result_base + j;
      counter->stride = group->num_counters;

      counter->qwords = 1;
      if ((block->b->b->flags & SI_PC_BLOCK_SE) && group->se < 0)
         counter->qwords = screen->info.max_se;
      if (group->instance < 0)
         counter->qwords *= block->num_instances;
   }

   return (struct pipe_query *)query;

error:
   si_pc_query_destroy((struct si_context *)ctx, &query->b);
   return NULL;
}

static bool si_init_block_names(struct si_screen *screen, struct si_pc_block *block)
{
   bool per_instance_groups = si_pc_block_has_per_instance_groups(screen->perfcounters, block);
   bool per_se_groups = si_pc_block_has_per_se_groups(screen->perfcounters, block);
   unsigned i, j, k;
   unsigned groups_shader = 1, groups_se = 1, groups_instance = 1;
   unsigned namelen;
   char *groupname;
   char *p;

   if (per_instance_groups)
      groups_instance = block->num_instances;
   if (per_se_groups)
      groups_se = screen->info.max_se;
   if (block->b->b->flags & SI_PC_BLOCK_SHADER)
      groups_shader = ARRAY_SIZE(si_pc_shader_type_bits);

   namelen = strlen(block->b->b->name);
   block->group_name_stride = namelen + 1;
   if (block->b->b->flags & SI_PC_BLOCK_SHADER)
      block->group_name_stride += 3;
   if (per_se_groups) {
      assert(groups_se <= 10);
      block->group_name_stride += 1;

      if (per_instance_groups)
         block->group_name_stride += 1;
   }
   if (per_instance_groups) {
      assert(groups_instance <= 100);
      block->group_name_stride += 2;
   }

   block->group_names = MALLOC(block->num_groups * block->group_name_stride);
   if (!block->group_names)
      return false;

   groupname = block->group_names;
   for (i = 0; i < groups_shader; ++i) {
      const char *shader_suffix = si_pc_shader_type_suffixes[i];
      unsigned shaderlen = strlen(shader_suffix);
      for (j = 0; j < groups_se; ++j) {
         for (k = 0; k < groups_instance; ++k) {
            strcpy(groupname, block->b->b->name);
            p = groupname + namelen;

            if (block->b->b->flags & SI_PC_BLOCK_SHADER) {
               strcpy(p, shader_suffix);
               p += shaderlen;
            }

            if (per_se_groups) {
               p += sprintf(p, "%d", j);
               if (per_instance_groups)
                  *p++ = '_';
            }

            if (per_instance_groups)
               p += sprintf(p, "%d", k);

            groupname += block->group_name_stride;
         }
      }
   }

   assert(block->b->selectors <= 1000);
   block->selector_name_stride = block->group_name_stride + 4;
   block->selector_names =
      MALLOC(block->num_groups * block->b->selectors * block->selector_name_stride);
   if (!block->selector_names)
      return false;

   groupname = block->group_names;
   p = block->selector_names;
   for (i = 0; i < block->num_groups; ++i) {
      for (j = 0; j < block->b->selectors; ++j) {
         sprintf(p, "%s_%03d", groupname, j);
         p += block->selector_name_stride;
      }
      groupname += block->group_name_stride;
   }

   return true;
}

int si_get_perfcounter_info(struct si_screen *screen, unsigned index,
                            struct pipe_driver_query_info *info)
{
   struct si_perfcounters *pc = screen->perfcounters;
   struct si_pc_block *block;
   unsigned base_gid, sub;

   if (!pc)
      return 0;

   if (!info) {
      unsigned bid, num_queries = 0;

      for (bid = 0; bid < pc->num_blocks; ++bid) {
         num_queries += pc->blocks[bid].b->selectors * pc->blocks[bid].num_groups;
      }

      return num_queries;
   }

   block = lookup_counter(pc, index, &base_gid, &sub);
   if (!block)
      return 0;

   if (!block->selector_names) {
      if (!si_init_block_names(screen, block))
         return 0;
   }
   info->name = block->selector_names + sub * block->selector_name_stride;
   info->query_type = SI_QUERY_FIRST_PERFCOUNTER + index;
   info->max_value.u64 = 0;
   info->type = PIPE_DRIVER_QUERY_TYPE_UINT64;
   info->result_type = PIPE_DRIVER_QUERY_RESULT_TYPE_AVERAGE;
   info->group_id = base_gid + sub / block->b->selectors;
   info->flags = PIPE_DRIVER_QUERY_FLAG_BATCH;
   if (sub > 0 && sub + 1 < block->b->selectors * block->num_groups)
      info->flags |= PIPE_DRIVER_QUERY_FLAG_DONT_LIST;
   return 1;
}

int si_get_perfcounter_group_info(struct si_screen *screen, unsigned index,
                                  struct pipe_driver_query_group_info *info)
{
   struct si_perfcounters *pc = screen->perfcounters;
   struct si_pc_block *block;

   if (!pc)
      return 0;

   if (!info)
      return pc->num_groups;

   block = lookup_group(pc, &index);
   if (!block)
      return 0;

   if (!block->group_names) {
      if (!si_init_block_names(screen, block))
         return 0;
   }
   info->name = block->group_names + index * block->group_name_stride;
   info->num_queries = block->b->selectors;
   info->max_active_queries = block->b->b->num_counters;
   return 1;
}

void si_destroy_perfcounters(struct si_screen *screen)
{
   struct si_perfcounters *pc = screen->perfcounters;
   unsigned i;

   if (!pc)
      return;

   for (i = 0; i < pc->num_blocks; ++i) {
      FREE(pc->blocks[i].group_names);
      FREE(pc->blocks[i].selector_names);
   }
   FREE(pc->blocks);
   FREE(pc);
   screen->perfcounters = NULL;
}

void si_init_perfcounters(struct si_screen *screen)
{
   struct si_perfcounters *pc;
   const struct si_pc_block_gfxdescr *blocks;
   unsigned num_blocks;
   unsigned i;

   switch (screen->info.chip_class) {
   case GFX7:
      blocks = groups_CIK;
      num_blocks = ARRAY_SIZE(groups_CIK);
      break;
   case GFX8:
      blocks = groups_VI;
      num_blocks = ARRAY_SIZE(groups_VI);
      break;
   case GFX9:
      blocks = groups_gfx9;
      num_blocks = ARRAY_SIZE(groups_gfx9);
      break;
   case GFX10:
   case GFX10_3:
      blocks = groups_gfx10;
      num_blocks = ARRAY_SIZE(groups_gfx10);
      break;
   case GFX6:
   default:
      return; /* not implemented */
   }

   screen->perfcounters = pc = CALLOC_STRUCT(si_perfcounters);
   if (!pc)
      return;

   pc->num_stop_cs_dwords = 14 + si_cp_write_fence_dwords(screen);
   pc->num_instance_cs_dwords = 3;

   pc->separate_se = debug_get_bool_option("RADEON_PC_SEPARATE_SE", false);
   pc->separate_instance = debug_get_bool_option("RADEON_PC_SEPARATE_INSTANCE", false);

   pc->blocks = CALLOC(num_blocks, sizeof(struct si_pc_block));
   if (!pc->blocks)
      goto error;
   pc->num_blocks = num_blocks;

   for (i = 0; i < num_blocks; ++i) {
      struct si_pc_block *block = &pc->blocks[i];
      block->b = &blocks[i];
      block->num_instances = MAX2(1, block->b->instances);

      if (!strcmp(block->b->b->name, "CB") ||
          !strcmp(block->b->b->name, "DB") ||
          !strcmp(block->b->b->name, "RMI"))
         block->num_instances = screen->info.max_se;
      else if (!strcmp(block->b->b->name, "TCC"))
         block->num_instances = screen->info.max_tcc_blocks;
      else if (!strcmp(block->b->b->name, "IA"))
         block->num_instances = MAX2(1, screen->info.max_se / 2);
      else if (!strcmp(block->b->b->name, "TA") ||
               !strcmp(block->b->b->name, "TCP") ||
               !strcmp(block->b->b->name, "TD")) {
         block->num_instances = MAX2(1, screen->info.max_good_cu_per_sa);
      }

      if (si_pc_block_has_per_instance_groups(pc, block)) {
         block->num_groups = block->num_instances;
      } else {
         block->num_groups = 1;
      }

      if (si_pc_block_has_per_se_groups(pc, block))
         block->num_groups *= screen->info.max_se;
      if (block->b->b->flags & SI_PC_BLOCK_SHADER)
         block->num_groups *= ARRAY_SIZE(si_pc_shader_type_bits);

      pc->num_groups += block->num_groups;
   }

   return;

error:
   si_destroy_perfcounters(screen);
}
