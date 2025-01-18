/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_debug.h"
#include "sid.h"
#include "sid_tables.h"
#include "ac_vcn.h"
#include "ac_vcn_dec.h"
#include "ac_vcn_enc.h"

#include "util/compiler.h"
#include "util/hash_table.h"
#include "util/u_debug.h"
#include "util/u_math.h"
#include "util/memstream.h"
#include "util/u_string.h"

#include <stdlib.h>

#ifdef HAVE_VALGRIND
#include <memcheck.h>
#include <valgrind.h>
#endif

DEBUG_GET_ONCE_BOOL_OPTION(color, "AMD_COLOR", true);

/* Parsed IBs are difficult to read without colors. Use "less -R file" to
 * read them, or use "aha -b -f file" to convert them to html.
 */
#define COLOR_RESET  "\033[0m"
#define COLOR_RED    "\033[31m"
#define COLOR_GREEN  "\033[1;32m"
#define COLOR_YELLOW "\033[1;33m"
#define COLOR_CYAN   "\033[1;36m"
#define COLOR_PURPLE "\033[1;35m"

#define O_COLOR_RESET  (debug_get_option_color() ? COLOR_RESET : "")
#define O_COLOR_RED    (debug_get_option_color() ? COLOR_RED : "")
#define O_COLOR_GREEN  (debug_get_option_color() ? COLOR_GREEN : "")
#define O_COLOR_YELLOW (debug_get_option_color() ? COLOR_YELLOW : "")
#define O_COLOR_CYAN   (debug_get_option_color() ? COLOR_CYAN : "")
#define O_COLOR_PURPLE (debug_get_option_color() ? COLOR_PURPLE : "")

#define INDENT_PKT 8

static void parse_gfx_compute_ib(FILE *f, struct ac_ib_parser *ib);

static void print_spaces(FILE *f, unsigned num)
{
   fprintf(f, "%*s", num, "");
}

static void print_value(FILE *file, uint32_t value, int bits)
{
   /* Guess if it's int or float */
   if (value <= (1 << 15)) {
      if (value <= 9)
         fprintf(file, "%u\n", value);
      else
         fprintf(file, "%u (0x%0*x)\n", value, bits / 4, value);
   } else {
      float f = uif(value);

      if (fabs(f) < 100000 && f * 10 == floor(f * 10))
         fprintf(file, "%.1ff (0x%0*x)\n", f, bits / 4, value);
      else
         /* Don't print more leading zeros than there are bits. */
         fprintf(file, "0x%0*x\n", bits / 4, value);
   }
}

static void print_data_dword(FILE *file, uint32_t value, const char *comment)
{
   print_spaces(file, INDENT_PKT);
   fprintf(file, "(%s)\n", comment);
}

static void print_named_value(FILE *file, const char *name, uint32_t value, int bits)
{
   print_spaces(file, INDENT_PKT);
   fprintf(file, "%s%s%s <- ",
           O_COLOR_YELLOW, name,
           O_COLOR_RESET);
   print_value(file, value, bits);
}

static void print_string_value(FILE *file, const char *name, const char *value)
{
   print_spaces(file, INDENT_PKT);
   fprintf(file, "%s%s%s <- ",
           O_COLOR_YELLOW, name,
           O_COLOR_RESET);
   fprintf(file, "%s\n", value);
}

void ac_dump_reg(FILE *file, enum amd_gfx_level gfx_level, enum radeon_family family,
                 unsigned offset, uint32_t value, uint32_t field_mask)
{
   const struct si_reg *reg = ac_find_register(gfx_level, family, offset);

   if (reg) {
      const char *reg_name = sid_strings + reg->name_offset;

      print_spaces(file, INDENT_PKT);
      fprintf(file, "%s%s%s <- ",
              O_COLOR_YELLOW, reg_name,
              O_COLOR_RESET);

      print_value(file, value, 32);

      for (unsigned f = 0; f < reg->num_fields; f++) {
         const struct si_field *field = sid_fields_table + reg->fields_offset + f;
         const int *values_offsets = sid_strings_offsets + field->values_offset;
         uint32_t val = (value & field->mask) >> (ffs(field->mask) - 1);

         if (!(field->mask & field_mask))
            continue;

         /* Indent the field. */
         print_spaces(file, INDENT_PKT + strlen(reg_name) + 4);

         /* Print the field. */
         fprintf(file, "%s = ", sid_strings + field->name_offset);

         if (val < field->num_values && values_offsets[val] >= 0)
            fprintf(file, "%s\n", sid_strings + values_offsets[val]);
         else
            print_value(file, val, util_bitcount(field->mask));
      }
      return;
   }

   print_spaces(file, INDENT_PKT);
   fprintf(file, "%s0x%05x%s <- 0x%08x\n",
           O_COLOR_YELLOW, offset,
           O_COLOR_RESET, value);
}

static uint32_t ac_ib_get(struct ac_ib_parser *ib)
{
   uint32_t v = 0;

   if (ib->cur_dw < ib->num_dw) {
      v = ib->ib[ib->cur_dw];
#ifdef HAVE_VALGRIND
      /* Help figure out where garbage data is written to IBs.
       *
       * Arguably we should do this already when the IBs are written,
       * see RADEON_VALGRIND. The problem is that client-requests to
       * Valgrind have an overhead even when Valgrind isn't running,
       * and radeon_emit is performance sensitive...
       */
      if (VALGRIND_CHECK_VALUE_IS_DEFINED(v))
         fprintf(ib->f, "%sValgrind: The next DWORD is garbage%s\n",
                 debug_get_option_color() ? COLOR_RED : "", O_COLOR_RESET);
#endif
      fprintf(ib->f, "\n\035#%08x ", v);
   } else {
      fprintf(ib->f, "\n\035#???????? ");
   }

   ib->cur_dw++;
   return v;
}

static uint64_t ac_ib_get64(struct ac_ib_parser *ib)
{
   uint32_t lo = ac_ib_get(ib);
   uint32_t hi = ac_ib_get(ib);
   return ((uint64_t)hi << 32) | lo;
}

static uint64_t ac_sext_addr48(uint64_t addr)
{
   if (addr & (1llu << 47))
      return addr | (0xFFFFllu << 48);
   else
      return addr & (~(0xFFFFllu << 48));
}

static void ac_parse_set_reg_packet(FILE *f, unsigned count, unsigned reg_offset,
                                    struct ac_ib_parser *ib)
{
   unsigned reg_dw = ac_ib_get(ib);
   unsigned reg = ((reg_dw & 0xFFFF) << 2) + reg_offset;
   unsigned index = reg_dw >> 28;
   int i;

   if (index != 0)
      print_named_value(f, "INDEX", index, 32);

   for (i = 0; i < count; i++)
      ac_dump_reg(f, ib->gfx_level, ib->family, reg + i * 4, ac_ib_get(ib), ~0);
}

static void ac_parse_set_reg_pairs_packet(FILE *f, unsigned count, unsigned reg_base,
                                          struct ac_ib_parser *ib)
{
   for (unsigned i = 0; i < (count + 1) / 2; i++) {
      unsigned reg_offset = (ac_ib_get(ib) << 2) + reg_base;
      ac_dump_reg(f, ib->gfx_level, ib->family, reg_offset, ac_ib_get(ib), ~0);
   }
}

static void ac_parse_set_reg_pairs_packed_packet(FILE *f, unsigned count, unsigned reg_base,
                                                 struct ac_ib_parser *ib)
{
   unsigned reg_offset0 = 0, reg_offset1 = 0;

   print_named_value(f, "REG_COUNT", ac_ib_get(ib), 32);

   for (unsigned i = 0; i < count; i++) {
      if (i % 3 == 0) {
         unsigned tmp = ac_ib_get(ib);
         reg_offset0 = ((tmp & 0xffff) << 2) + reg_base;
         reg_offset1 = ((tmp >> 16) << 2) + reg_base;
      } else if (i % 3 == 1) {
         ac_dump_reg(f, ib->gfx_level, ib->family, reg_offset0, ac_ib_get(ib), ~0);
      } else {
         ac_dump_reg(f, ib->gfx_level, ib->family, reg_offset1, ac_ib_get(ib), ~0);
      }
   }
}

#define AC_ADDR_SIZE_NOT_MEMORY 0xFFFFFFFF

static void print_addr(struct ac_ib_parser *ib, const char *name, uint64_t addr, uint32_t size)
{
   FILE *f = ib->f;

   print_spaces(f, INDENT_PKT);
   fprintf(f, "%s%s%s <- ",
           O_COLOR_YELLOW, name,
           O_COLOR_RESET);

   fprintf(f, "0x%llx", (unsigned long long)addr);

   if (ib->addr_callback && size != AC_ADDR_SIZE_NOT_MEMORY) {
      struct ac_addr_info addr_info;
      ib->addr_callback(ib->addr_callback_data, addr, &addr_info);

      struct ac_addr_info addr_info2 = addr_info;
      if (size)
         ib->addr_callback(ib->addr_callback_data, addr + size - 1, &addr_info2);

      uint32_t invalid_count = !addr_info.valid + !addr_info2.valid;

      if (addr_info.use_after_free && addr_info2.use_after_free)
         fprintf(f, " used after free");
      else if (invalid_count == 2)
         fprintf(f, " invalid");
      else if (invalid_count == 1)
         fprintf(f, " out of bounds");
   }

   fprintf(f, "\n");
}

static void ac_parse_packet3(FILE *f, uint32_t header, struct ac_ib_parser *ib,
                             int *current_trace_id)
{
   unsigned first_dw = ib->cur_dw;
   int count = PKT_COUNT_G(header);
   unsigned op = PKT3_IT_OPCODE_G(header);
   const char *shader_type = PKT3_SHADER_TYPE_G(header) ? "(shader_type=compute)" : "";
   const char *predicated = PKT3_PREDICATE(header) ? "(predicated)" : "";
   const char *reset_filter_cam = PKT3_RESET_FILTER_CAM_G(header) ? "(reset_filter_cam)" : "";
   int i;
   unsigned tmp;

   /* Print the name first. */
   for (i = 0; i < ARRAY_SIZE(packet3_table); i++)
      if (packet3_table[i].op == op)
         break;

   char unknown_name[32];
   const char *pkt_name;

   if (i < ARRAY_SIZE(packet3_table)) {
      pkt_name = sid_strings + packet3_table[i].name_offset;
   } else {
      snprintf(unknown_name, sizeof(unknown_name), "UNKNOWN(0x%02X)", op);
      pkt_name = unknown_name;
   }
   const char *color;

   if (strstr(pkt_name, "DRAW") || strstr(pkt_name, "DISPATCH"))
      color = O_COLOR_PURPLE;
   else if (strstr(pkt_name, "SET") == pkt_name && strstr(pkt_name, "REG"))
      color = O_COLOR_CYAN;
   else if (i >= ARRAY_SIZE(packet3_table))
      color = O_COLOR_RED;
   else
      color = O_COLOR_GREEN;

   fprintf(f, "%s%s%s%s%s%s:\n", color, pkt_name, O_COLOR_RESET,
           shader_type, predicated, reset_filter_cam);

   /* Print the contents. */
   switch (op) {
   case PKT3_SET_CONTEXT_REG:
      ac_parse_set_reg_packet(f, count, SI_CONTEXT_REG_OFFSET, ib);
      break;
   case PKT3_SET_CONFIG_REG:
      ac_parse_set_reg_packet(f, count, SI_CONFIG_REG_OFFSET, ib);
      break;
   case PKT3_SET_UCONFIG_REG:
   case PKT3_SET_UCONFIG_REG_INDEX:
      ac_parse_set_reg_packet(f, count, CIK_UCONFIG_REG_OFFSET, ib);
      break;
   case PKT3_SET_SH_REG:
   case PKT3_SET_SH_REG_INDEX:
      ac_parse_set_reg_packet(f, count, SI_SH_REG_OFFSET, ib);
      break;
   case PKT3_SET_UCONFIG_REG_PAIRS:
      ac_parse_set_reg_pairs_packet(f, count, CIK_UCONFIG_REG_OFFSET, ib);
      break;
   case PKT3_SET_CONTEXT_REG_PAIRS:
      ac_parse_set_reg_pairs_packet(f, count, SI_CONTEXT_REG_OFFSET, ib);
      break;
   case PKT3_SET_SH_REG_PAIRS:
      ac_parse_set_reg_pairs_packet(f, count, SI_SH_REG_OFFSET, ib);
      break;
   case PKT3_SET_CONTEXT_REG_PAIRS_PACKED:
      ac_parse_set_reg_pairs_packed_packet(f, count, SI_CONTEXT_REG_OFFSET, ib);
      break;
   case PKT3_SET_SH_REG_PAIRS_PACKED:
   case PKT3_SET_SH_REG_PAIRS_PACKED_N:
      ac_parse_set_reg_pairs_packed_packet(f, count, SI_SH_REG_OFFSET, ib);
      break;
   case PKT3_ACQUIRE_MEM:
      if (ib->gfx_level >= GFX11) {
         if (G_585_PWS_ENA(ib->ib[ib->cur_dw + 5])) {
            ac_dump_reg(f, ib->gfx_level, ib->family, R_580_ACQUIRE_MEM_PWS_2, ac_ib_get(ib), ~0);
            print_named_value(f, "GCR_SIZE", ac_ib_get(ib), 32);
            print_named_value(f, "GCR_SIZE_HI", ac_ib_get(ib), 25);
            print_named_value(f, "GCR_BASE_LO", ac_ib_get(ib), 32);
            print_named_value(f, "GCR_BASE_HI", ac_ib_get(ib), 32);
            ac_dump_reg(f, ib->gfx_level, ib->family, R_585_ACQUIRE_MEM_PWS_7, ac_ib_get(ib), ~0);
            ac_dump_reg(f, ib->gfx_level, ib->family, R_586_GCR_CNTL, ac_ib_get(ib), ~0);
         } else {
            print_string_value(f, "ENGINE_SEL", ac_ib_get(ib) & 0x80000000 ? "ME" : "PFP");
            print_named_value(f, "GCR_SIZE", ac_ib_get(ib), 32);
            print_named_value(f, "GCR_SIZE_HI", ac_ib_get(ib), 25);
            print_named_value(f, "GCR_BASE_LO", ac_ib_get(ib), 32);
            print_named_value(f, "GCR_BASE_HI", ac_ib_get(ib), 32);
            print_named_value(f, "POLL_INTERVAL", ac_ib_get(ib), 16);
            ac_dump_reg(f, ib->gfx_level, ib->family, R_586_GCR_CNTL, ac_ib_get(ib), ~0);
         }
      } else {
         tmp = ac_ib_get(ib);
         ac_dump_reg(f, ib->gfx_level, ib->family, R_0301F0_CP_COHER_CNTL, tmp, 0x7fffffff);
         print_string_value(f, "ENGINE_SEL", tmp & 0x80000000 ? "ME" : "PFP");
         ac_dump_reg(f, ib->gfx_level, ib->family, R_0301F4_CP_COHER_SIZE, ac_ib_get(ib), ~0);
         ac_dump_reg(f, ib->gfx_level, ib->family, R_030230_CP_COHER_SIZE_HI, ac_ib_get(ib), ~0);
         ac_dump_reg(f, ib->gfx_level, ib->family, R_0301F8_CP_COHER_BASE, ac_ib_get(ib), ~0);
         ac_dump_reg(f, ib->gfx_level, ib->family, R_0301E4_CP_COHER_BASE_HI, ac_ib_get(ib), ~0);
         print_named_value(f, "POLL_INTERVAL", ac_ib_get(ib), 16);
         if (ib->gfx_level >= GFX10)
            ac_dump_reg(f, ib->gfx_level, ib->family, R_586_GCR_CNTL, ac_ib_get(ib), ~0);
      }
      break;
   case PKT3_SURFACE_SYNC:
      if (ib->gfx_level >= GFX7) {
         ac_dump_reg(f, ib->gfx_level, ib->family, R_0301F0_CP_COHER_CNTL, ac_ib_get(ib), ~0);
         ac_dump_reg(f, ib->gfx_level, ib->family, R_0301F4_CP_COHER_SIZE, ac_ib_get(ib), ~0);
         ac_dump_reg(f, ib->gfx_level, ib->family, R_0301F8_CP_COHER_BASE, ac_ib_get(ib), ~0);
      } else {
         ac_dump_reg(f, ib->gfx_level, ib->family, R_0085F0_CP_COHER_CNTL, ac_ib_get(ib), ~0);
         ac_dump_reg(f, ib->gfx_level, ib->family, R_0085F4_CP_COHER_SIZE, ac_ib_get(ib), ~0);
         ac_dump_reg(f, ib->gfx_level, ib->family, R_0085F8_CP_COHER_BASE, ac_ib_get(ib), ~0);
      }
      print_named_value(f, "POLL_INTERVAL", ac_ib_get(ib), 16);
      break;
   case PKT3_EVENT_WRITE: {
      uint32_t event_dw = ac_ib_get(ib);
      ac_dump_reg(f, ib->gfx_level, ib->family, R_028A90_VGT_EVENT_INITIATOR, event_dw,
                  S_028A90_EVENT_TYPE(~0));
      print_named_value(f, "EVENT_INDEX", (event_dw >> 8) & 0xf, 4);
      print_named_value(f, "INV_L2", (event_dw >> 20) & 0x1, 1);
      if (count > 0)
         print_addr(ib, "ADDR", ac_ib_get64(ib), 0);

      break;
   }
   case PKT3_EVENT_WRITE_EOP: {
      uint32_t event_dw = ac_ib_get(ib);
      ac_dump_reg(f, ib->gfx_level, ib->family, R_028A90_VGT_EVENT_INITIATOR, event_dw,
                  S_028A90_EVENT_TYPE(~0));
      print_named_value(f, "EVENT_INDEX", (event_dw >> 8) & 0xf, 4);
      print_named_value(f, "TCL1_VOL_ACTION_ENA", (event_dw >> 12) & 0x1, 1);
      print_named_value(f, "TC_VOL_ACTION_ENA", (event_dw >> 13) & 0x1, 1);
      print_named_value(f, "TC_WB_ACTION_ENA", (event_dw >> 15) & 0x1, 1);
      print_named_value(f, "TCL1_ACTION_ENA", (event_dw >> 16) & 0x1, 1);
      print_named_value(f, "TC_ACTION_ENA", (event_dw >> 17) & 0x1, 1);
      uint64_t addr = ac_ib_get64(ib);
      uint32_t data_sel = addr >> 61;
      uint32_t data_size;
      switch (data_sel) {
      case EOP_DATA_SEL_VALUE_32BIT:
         data_size = 4;
         break;
      case EOP_DATA_SEL_VALUE_64BIT:
      case EOP_DATA_SEL_TIMESTAMP:
         data_size = 8;
         break;
      default:
         data_size = AC_ADDR_SIZE_NOT_MEMORY;
         break;
      }
      print_addr(ib, "ADDR", ac_sext_addr48(addr), data_size);
      print_named_value(f, "DST_SEL", (addr >> 48) & 0x3, 2);
      print_named_value(f, "INT_SEL", (addr >> 56) & 0x7, 3);
      print_named_value(f, "DATA_SEL", data_sel, 3);
      print_named_value(f, "DATA_LO", ac_ib_get(ib), 32);
      print_named_value(f, "DATA_HI", ac_ib_get(ib), 32);
      break;
   }
   case PKT3_RELEASE_MEM: {
      uint32_t event_dw = ac_ib_get(ib);
      if (ib->gfx_level >= GFX10) {
         ac_dump_reg(f, ib->gfx_level, ib->family, R_490_RELEASE_MEM_OP, event_dw, ~0u);
      } else {
         ac_dump_reg(f, ib->gfx_level, ib->family, R_028A90_VGT_EVENT_INITIATOR, event_dw,
                     S_028A90_EVENT_TYPE(~0));
         print_named_value(f, "EVENT_INDEX", (event_dw >> 8) & 0xf, 4);
         print_named_value(f, "TCL1_VOL_ACTION_ENA", (event_dw >> 12) & 0x1, 1);
         print_named_value(f, "TC_VOL_ACTION_ENA", (event_dw >> 13) & 0x1, 1);
         print_named_value(f, "TC_WB_ACTION_ENA", (event_dw >> 15) & 0x1, 1);
         print_named_value(f, "TCL1_ACTION_ENA", (event_dw >> 16) & 0x1, 1);
         print_named_value(f, "TC_ACTION_ENA", (event_dw >> 17) & 0x1, 1);
         print_named_value(f, "TC_NC_ACTION_ENA", (event_dw >> 19) & 0x1, 1);
         print_named_value(f, "TC_WC_ACTION_ENA", (event_dw >> 20) & 0x1, 1);
         print_named_value(f, "TC_MD_ACTION_ENA", (event_dw >> 21) & 0x1, 1);
      }
      uint32_t sel_dw = ac_ib_get(ib);
      print_named_value(f, "DST_SEL", (sel_dw >> 16) & 0x3, 2);
      print_named_value(f, "INT_SEL", (sel_dw >> 24) & 0x7, 3);
      print_named_value(f, "DATA_SEL", sel_dw >> 29, 3);
      print_named_value(f, "ADDRESS_LO", ac_ib_get(ib), 32);
      print_named_value(f, "ADDRESS_HI", ac_ib_get(ib), 32);
      print_named_value(f, "DATA_LO", ac_ib_get(ib), 32);
      print_named_value(f, "DATA_HI", ac_ib_get(ib), 32);
      print_named_value(f, "CTXID", ac_ib_get(ib), 32);
      break;
   }
   case PKT3_WAIT_REG_MEM:
      print_named_value(f, "OP", ac_ib_get(ib), 32);
      print_named_value(f, "ADDRESS_LO", ac_ib_get(ib), 32);
      print_named_value(f, "ADDRESS_HI", ac_ib_get(ib), 32);
      print_named_value(f, "REF", ac_ib_get(ib), 32);
      print_named_value(f, "MASK", ac_ib_get(ib), 32);
      print_named_value(f, "POLL_INTERVAL", ac_ib_get(ib), 16);
      break;
   case PKT3_DRAW_INDEX_AUTO:
      ac_dump_reg(f, ib->gfx_level, ib->family, R_030930_VGT_NUM_INDICES, ac_ib_get(ib), ~0);
      ac_dump_reg(f, ib->gfx_level, ib->family, R_0287F0_VGT_DRAW_INITIATOR, ac_ib_get(ib), ~0);
      break;
   case PKT3_DRAW_INDEX_2:
      ac_dump_reg(f, ib->gfx_level, ib->family, R_028A78_VGT_DMA_MAX_SIZE, ac_ib_get(ib), ~0);
      print_addr(ib, "INDEX_ADDR", ac_ib_get64(ib), 0);
      ac_dump_reg(f, ib->gfx_level, ib->family, R_030930_VGT_NUM_INDICES, ac_ib_get(ib), ~0);
      ac_dump_reg(f, ib->gfx_level, ib->family, R_0287F0_VGT_DRAW_INITIATOR, ac_ib_get(ib), ~0);
      break;
   case PKT3_DRAW_INDIRECT:
   case PKT3_DRAW_INDEX_INDIRECT:
      print_named_value(f, "OFFSET", ac_ib_get(ib), 32);
      print_named_value(f, "VERTEX_OFFSET_REG", ac_ib_get(ib), 32);
      print_named_value(f, "START_INSTANCE_REG", ac_ib_get(ib), 32);
      ac_dump_reg(f, ib->gfx_level, ib->family, R_0287F0_VGT_DRAW_INITIATOR, ac_ib_get(ib), ~0);
      break;
   case PKT3_DRAW_INDIRECT_MULTI:
   case PKT3_DRAW_INDEX_INDIRECT_MULTI:
      print_named_value(f, "OFFSET", ac_ib_get(ib), 32);
      print_named_value(f, "VERTEX_OFFSET_REG", ac_ib_get(ib), 32);
      print_named_value(f, "START_INSTANCE_REG", ac_ib_get(ib), 32);
      tmp = ac_ib_get(ib);
      print_named_value(f, "DRAW_ID_REG", tmp & 0xFFFF, 16);
      print_named_value(f, "DRAW_ID_ENABLE", tmp >> 31, 1);
      print_named_value(f, "COUNT_INDIRECT_ENABLE", (tmp >> 30) & 1, 1);
      print_named_value(f, "DRAW_COUNT", ac_ib_get(ib), 32);
      print_addr(ib, "COUNT_ADDR", ac_ib_get64(ib), 0);
      print_named_value(f, "STRIDE", ac_ib_get(ib), 32);
      ac_dump_reg(f, ib->gfx_level, ib->family, R_0287F0_VGT_DRAW_INITIATOR, ac_ib_get(ib), ~0);
      break;
   case PKT3_INDEX_BASE:
      print_addr(ib, "ADDR", ac_ib_get64(ib), 0);
      break;
   case PKT3_INDEX_TYPE:
      ac_dump_reg(f, ib->gfx_level, ib->family, R_028A7C_VGT_DMA_INDEX_TYPE, ac_ib_get(ib), ~0);
      break;
   case PKT3_NUM_INSTANCES:
      ac_dump_reg(f, ib->gfx_level, ib->family, R_030934_VGT_NUM_INSTANCES, ac_ib_get(ib), ~0);
      break;
   case PKT3_WRITE_DATA: {
      uint32_t control = ac_ib_get(ib);
      ac_dump_reg(f, ib->gfx_level, ib->family, R_370_CONTROL, control, ~0);
      uint32_t dst_sel = G_370_DST_SEL(control);
      uint64_t addr = ac_ib_get64(ib);
      uint32_t dword_count = first_dw + count + 1 - ib->cur_dw;
      bool writes_memory = dst_sel == V_370_MEM_GRBM || dst_sel == V_370_TC_L2 || dst_sel == V_370_MEM;
      print_addr(ib, "DST_ADDR", addr, writes_memory ? dword_count * 4 : AC_ADDR_SIZE_NOT_MEMORY);
      for (uint32_t i = 0; i < dword_count; i++)
          print_data_dword(f, ac_ib_get(ib), "data");
      break;
   }
   case PKT3_CP_DMA:
      ac_dump_reg(f, ib->gfx_level, ib->family, R_410_CP_DMA_WORD0, ac_ib_get(ib), ~0);
      ac_dump_reg(f, ib->gfx_level, ib->family, R_411_CP_DMA_WORD1, ac_ib_get(ib), ~0);
      ac_dump_reg(f, ib->gfx_level, ib->family, R_412_CP_DMA_WORD2, ac_ib_get(ib), ~0);
      ac_dump_reg(f, ib->gfx_level, ib->family, R_413_CP_DMA_WORD3, ac_ib_get(ib), ~0);
      ac_dump_reg(f, ib->gfx_level, ib->family, R_415_COMMAND, ac_ib_get(ib), ~0);
      break;
   case PKT3_DMA_DATA: {
      uint32_t header = ac_ib_get(ib);
      ac_dump_reg(f, ib->gfx_level, ib->family, R_501_DMA_DATA_WORD0, header, ~0);

      uint64_t src_addr = ac_ib_get64(ib);
      uint64_t dst_addr = ac_ib_get64(ib);

      uint32_t command = ac_ib_get(ib);
      uint32_t size = ib->gfx_level >= GFX9 ? G_415_BYTE_COUNT_GFX9(command)
                                            : G_415_BYTE_COUNT_GFX6(command);

      uint32_t src_sel = G_501_SRC_SEL(header);
      bool src_mem = (src_sel == V_501_SRC_ADDR && G_415_SAS(command) == V_415_MEMORY) ||
                      src_sel == V_411_SRC_ADDR_TC_L2;

      uint32_t dst_sel = G_501_DST_SEL(header);
      bool dst_mem = (dst_sel == V_501_DST_ADDR && G_415_DAS(command) == V_415_MEMORY) ||
                      dst_sel == V_411_DST_ADDR_TC_L2;

      print_addr(ib, "SRC_ADDR", src_addr, src_mem ? size : AC_ADDR_SIZE_NOT_MEMORY);
      print_addr(ib, "DST_ADDR", dst_addr, dst_mem ? size : AC_ADDR_SIZE_NOT_MEMORY);
      ac_dump_reg(f, ib->gfx_level, ib->family, R_415_COMMAND, command, ~0);
      break;
   }
   case PKT3_INDIRECT_BUFFER_SI:
   case PKT3_INDIRECT_BUFFER_CONST:
   case PKT3_INDIRECT_BUFFER: {
      uint32_t base_lo_dw = ac_ib_get(ib);
      ac_dump_reg(f, ib->gfx_level, ib->family, R_3F0_IB_BASE_LO, base_lo_dw, ~0);
      uint32_t base_hi_dw = ac_ib_get(ib);
      ac_dump_reg(f, ib->gfx_level, ib->family, R_3F1_IB_BASE_HI, base_hi_dw, ~0);
      uint32_t control_dw = ac_ib_get(ib);
      ac_dump_reg(f, ib->gfx_level, ib->family, R_3F2_IB_CONTROL, control_dw, ~0);

      if (!ib->addr_callback)
         break;

      uint64_t addr = ((uint64_t)base_hi_dw << 32) | base_lo_dw;
      struct ac_addr_info addr_info;
      ib->addr_callback(ib->addr_callback_data, addr, &addr_info);
      void *data = addr_info.cpu_addr;
      if (!data)
         break;

      if (G_3F2_CHAIN(control_dw)) {
         ib->ib = data;
         ib->num_dw = G_3F2_IB_SIZE(control_dw);
         ib->cur_dw = 0;
         return;
      }

      struct ac_ib_parser ib_recurse;
      memcpy(&ib_recurse, ib, sizeof(ib_recurse));
      ib_recurse.ib = data;
      ib_recurse.num_dw = G_3F2_IB_SIZE(control_dw);
      ib_recurse.cur_dw = 0;
      if (ib_recurse.trace_id_count) {
         if (*current_trace_id == *ib->trace_ids) {
            ++ib_recurse.trace_ids;
            --ib_recurse.trace_id_count;
         } else {
            ib_recurse.trace_id_count = 0;
         }
      }

      fprintf(f, "\n\035>------------------ nested begin ------------------\n");
      parse_gfx_compute_ib(f, &ib_recurse);
      fprintf(f, "\n\035<------------------- nested end -------------------\n");
      break;
   }
   case PKT3_CLEAR_STATE:
   case PKT3_INCREMENT_DE_COUNTER:
   case PKT3_PFP_SYNC_ME:
      print_data_dword(f, ac_ib_get(ib), "reserved");
      break;
   case PKT3_NOP:
      if (header == PKT3_NOP_PAD) {
         count = -1; /* One dword NOP. */
      } else if (count == 0 && ib->cur_dw < ib->num_dw && AC_IS_TRACE_POINT(ib->ib[ib->cur_dw])) {
         unsigned packet_id = AC_GET_TRACE_POINT_ID(ib->ib[ib->cur_dw]);

         print_spaces(f, INDENT_PKT);
         fprintf(f, "%sTrace point ID: %u%s\n", O_COLOR_RED, packet_id, O_COLOR_RESET);

         if (!ib->trace_id_count)
            break; /* tracing was disabled */

         *current_trace_id = packet_id;

         print_spaces(f, INDENT_PKT);
         if (packet_id < *ib->trace_ids) {
            fprintf(f, "%sThis trace point was reached by the CP.%s\n",
                    O_COLOR_RED, O_COLOR_RESET);
         } else if (packet_id == *ib->trace_ids) {
            fprintf(f, "%s!!!!! This is the last trace point that "
                                 "was reached by the CP !!!!!%s\n",
                    O_COLOR_RED, O_COLOR_RESET);
         } else if (packet_id + 1 == *ib->trace_ids) {
            fprintf(f, "%s!!!!! This is the first trace point that "
                                 "was NOT been reached by the CP !!!!!%s\n",
                    O_COLOR_RED, O_COLOR_RESET);
         } else {
            fprintf(f, "%s!!!!! This trace point was NOT reached "
                                 "by the CP !!!!!%s\n",
                    O_COLOR_RED, O_COLOR_RESET);
         }
      } else {
         while (ib->cur_dw <= first_dw + count)
             print_data_dword(f, ac_ib_get(ib), "unused");
      }
      break;
   case PKT3_DISPATCH_DIRECT:
   case PKT3_DISPATCH_DIRECT_INTERLEAVED:
      ac_dump_reg(f, ib->gfx_level, ib->family, R_00B804_COMPUTE_DIM_X, ac_ib_get(ib), ~0);
      ac_dump_reg(f, ib->gfx_level, ib->family, R_00B808_COMPUTE_DIM_Y, ac_ib_get(ib), ~0);
      ac_dump_reg(f, ib->gfx_level, ib->family, R_00B80C_COMPUTE_DIM_Z, ac_ib_get(ib), ~0);
      ac_dump_reg(f, ib->gfx_level, ib->family, R_00B800_COMPUTE_DISPATCH_INITIATOR,
                  ac_ib_get(ib), ~0);
      break;
   case PKT3_DISPATCH_INDIRECT:
   case PKT3_DISPATCH_INDIRECT_INTERLEAVED:
      if (count > 1)
         print_addr(ib, "ADDR", ac_ib_get64(ib), 12);
      else
         print_named_value(f, "DATA_OFFSET", ac_ib_get(ib), 32);

      ac_dump_reg(f, ib->gfx_level, ib->family, R_00B800_COMPUTE_DISPATCH_INITIATOR,
                  ac_ib_get(ib), ~0);
      break;
   case PKT3_SET_BASE:
      tmp = ac_ib_get(ib);
      print_string_value(f, "BASE_INDEX", tmp == 1 ? "INDIRECT_BASE" : COLOR_RED "UNKNOWN" COLOR_RESET);
      print_addr(ib, "ADDR", ac_ib_get64(ib), 0);
      break;
   case PKT3_PRIME_UTCL2:
      tmp = ac_ib_get(ib);
      print_named_value(f, "CACHE_PERM[rwx]", tmp & 0x7, 3);
      print_string_value(f, "PRIME_MODE", tmp & 0x8 ? "WAIT_FOR_XACK" : "DONT_WAIT_FOR_XACK");
      print_named_value(f, "ENGINE_SEL", tmp >> 30, 2);
      print_addr(ib, "ADDR", ac_ib_get64(ib), 0);
      print_named_value(f, "REQUESTED_PAGES", ac_ib_get(ib), 14);
      break;
   case PKT3_ATOMIC_MEM:
      tmp = ac_ib_get(ib);
      print_named_value(f, "ATOMIC", tmp & 0x7f, 7);
      print_named_value(f, "COMMAND", (tmp >> 8) & 0xf, 4);
      print_named_value(f, "CACHE_POLICY", (tmp >> 25) & 0x3, 2);
      print_named_value(f, "ENGINE_SEL", tmp >> 30, 2);
      print_addr(ib, "ADDR", ac_ib_get64(ib), 8);
      print_named_value(f, "SRC_DATA_LO", ac_ib_get(ib), 32);
      print_named_value(f, "SRC_DATA_HI", ac_ib_get(ib), 32);
      print_named_value(f, "CMP_DATA_LO", ac_ib_get(ib), 32);
      print_named_value(f, "CMP_DATA_HI", ac_ib_get(ib), 32);
      print_named_value(f, "LOOP_INTERVAL", ac_ib_get(ib) & 0x1fff, 13);
      break;
   case PKT3_INDEX_BUFFER_SIZE:
      print_named_value(f, "COUNT", ac_ib_get(ib), 32);
      break;
   case PKT3_COND_EXEC: {
      uint32_t size = ac_ib_get(ib) * 4;
      print_addr(ib, "ADDR", ac_ib_get64(ib), size);
      print_named_value(f, "SIZE", size, 32);
      break;
   }
   case PKT3_DISPATCH_TASKMESH_GFX:
      tmp = ac_ib_get(ib);
      print_named_value(f, "RING_ENTRY_REG", (tmp >> 16) & 0xffff, 16);
      print_named_value(f, "XYZ_DIM_REG", (tmp & 0xffff), 16);
      tmp = ac_ib_get(ib);
      print_named_value(f, "THREAD_TRACE_MARKER_ENABLE", (tmp >> 31) & 0x1, 1);
      if (ib->gfx_level >= GFX11) {
         print_named_value(f, "XYZ_DIM_ENABLE", (tmp >> 30) & 0x1, 1);
         print_named_value(f, "MODE1_ENABLE", (tmp >> 29) & 0x1, 1);
         print_named_value(f, "LINEAR_DISPATCH_ENABLED", (tmp >> 28) & 0x1, 1);
      }
      print_named_value(f, "DI_SRC_SEL_AUTO_INDEX", ac_ib_get(ib), ~0);
      break;
   case PKT3_DISPATCH_TASKMESH_DIRECT_ACE:
      print_named_value(f, "X_DIM", ac_ib_get(ib), ~0);
      print_named_value(f, "Y_DIM", ac_ib_get(ib), ~0);
      print_named_value(f, "Z_DIM", ac_ib_get(ib), ~0);
      ac_dump_reg(f, ib->gfx_level, ib->family, R_00B800_COMPUTE_DISPATCH_INITIATOR,
                  ac_ib_get(ib), ~0);
      print_named_value(f, "RING_ENTRY_REG", ac_ib_get(ib), 16);
      break;
   }

   /* print additional dwords */
   while (ib->cur_dw <= first_dw + count)
      ac_ib_get(ib);

   if (ib->cur_dw > first_dw + count + 1)
      fprintf(f, "%s !!!!! count in header too low !!!!!%s\n",
              O_COLOR_RED, O_COLOR_RESET);
}

/**
 * Parse and print an IB into a file.
 */
static void parse_gfx_compute_ib(FILE *f, struct ac_ib_parser *ib)
{
   int current_trace_id = -1;

   while (ib->cur_dw < ib->num_dw) {
      if (ib->annotations) {
         struct hash_entry *marker = _mesa_hash_table_search(ib->annotations, ib->ib + ib->cur_dw);
         if (marker)
            fprintf(f, "\n%s:", (char *)marker->data);
      }

      uint32_t header = ac_ib_get(ib);
      unsigned type = PKT_TYPE_G(header);

      switch (type) {
      case 3:
         ac_parse_packet3(f, header, ib, &current_trace_id);
         break;
      case 2:
         /* type-2 nop */
         if (header == 0x80000000) {
            fprintf(f, "%sNOP (type 2)%s\n",
                    O_COLOR_GREEN, O_COLOR_RESET);
            break;
         }
         FALLTHROUGH;
      default:
         fprintf(f, "Unknown packet type %i\n", type);
         break;
      }
   }
}

static void format_ib_output(FILE *f, char *out)
{
   unsigned depth = 0;

   for (;;) {
      char op = 0;

      if (out[0] == '\n' && out[1] == '\035')
         out++;
      if (out[0] == '\035') {
         op = out[1];
         out += 2;
      }

      if (op == '<')
         depth--;

      unsigned indent = 4 * depth;
      if (op != '#')
         indent += 9;

      if (indent)
         print_spaces(f, indent);

      char *end = strchrnul(out, '\n');
      fwrite(out, end - out, 1, f);
      fputc('\n', f); /* always end with a new line */
      if (!*end)
         break;

      out = end + 1;

      if (op == '>')
         depth++;
   }
}

static void parse_sdma_ib(FILE *f, struct ac_ib_parser *ib)
{
   while (ib->cur_dw < ib->num_dw) {
      const uint32_t header = ac_ib_get(ib);
      const uint32_t opcode = header & 0xff;
      const uint32_t sub_op = (header >> 8) & 0xff;

      switch (opcode) {
      case SDMA_OPCODE_NOP: {
         fprintf(f, "NOP\n");

         const uint32_t count = header >> 16;
         for (unsigned i = 0; i < count; ++i) {
            ac_ib_get(ib);
            fprintf(f, "\n");
         }
         break;
      }
      case SDMA_OPCODE_CONSTANT_FILL: {
         fprintf(f, "CONSTANT_FILL\n");
         ac_ib_get(ib);
         fprintf(f, "\n");
         ac_ib_get(ib);
         fprintf(f, "\n");
         uint32_t value = ac_ib_get(ib);
         fprintf(f, "    fill value = %u\n", value);
         uint32_t byte_count = ac_ib_get(ib) + 1;
         fprintf(f, "    fill byte count = %u\n", byte_count);

         unsigned dwords = byte_count / 4;
         for (unsigned i = 0; i < dwords; ++i) {
            ac_ib_get(ib);
            fprintf(f, "\n");
         }

         break;
      }
      case SDMA_OPCODE_WRITE: {
         fprintf(f, "WRITE\n");

         /* VA */
         ac_ib_get(ib);
         fprintf(f, "\n");
         ac_ib_get(ib);
         fprintf(f, "\n");

         uint32_t dwords = ac_ib_get(ib) + 1;
         fprintf(f, "    written dword count = %u\n", dwords);

         for (unsigned i = 0; i < dwords; ++i) {
            ac_ib_get(ib);
            fprintf(f, "\n");
         }

         break;
      }
      case SDMA_OPCODE_COPY: {
         switch (sub_op) {
         case SDMA_COPY_SUB_OPCODE_LINEAR: {
            fprintf(f, "COPY LINEAR\n");

            uint32_t copy_bytes = ac_ib_get(ib) + (ib->gfx_level >= GFX9 ? 1 : 0);
            fprintf(f, "    copy bytes: %u\n", copy_bytes);
            ac_ib_get(ib);
            fprintf(f, "\n");
            ac_ib_get(ib);
            fprintf(f, "    src VA low\n");
            ac_ib_get(ib);
            fprintf(f, "    src VA high\n");
            ac_ib_get(ib);
            fprintf(f, "    dst VA low\n");
            ac_ib_get(ib);
            fprintf(f, "    dst VA high\n");

            break;
         }
         case SDMA_COPY_SUB_OPCODE_LINEAR_SUB_WINDOW: {
            fprintf(f, "COPY LINEAR_SUB_WINDOW\n");

            for (unsigned i = 0; i < 12; ++i) {
               ac_ib_get(ib);
               fprintf(f, "\n");
            }
            break;
         }
         case SDMA_COPY_SUB_OPCODE_TILED_SUB_WINDOW: {
            fprintf(f, "COPY TILED_SUB_WINDOW %s\n", header >> 31 ? "t2l" : "l2t");
            uint32_t dcc = (header >> 19) & 1;

            /* Tiled VA */
            ac_ib_get(ib);
            fprintf(f, "    tiled VA low\n");
            ac_ib_get(ib);
            fprintf(f, "    tiled VA high\n");

            uint32_t dw3 = ac_ib_get(ib);
            fprintf(f, "    tiled offset x = %u, y=%u\n", dw3 & 0xffff, dw3 >> 16);
            uint32_t dw4 = ac_ib_get(ib);
            fprintf(f, "    tiled offset z = %u, tiled width = %u\n", dw4 & 0xffff, (dw4 >> 16) + 1);
            uint32_t dw5 = ac_ib_get(ib);
            fprintf(f, "    tiled height = %u, tiled depth = %u\n", (dw5 & 0xffff) + 1, (dw5 >> 16) + 1);

            /* Tiled image info */
            ac_ib_get(ib);
            fprintf(f, "    (tiled image info)\n");

            /* Linear VA */
            ac_ib_get(ib);
            fprintf(f, "    linear VA low\n");
            ac_ib_get(ib);
            fprintf(f, "    linear VA high\n");

            uint32_t dw9 = ac_ib_get(ib);
            fprintf(f, "    linear offset x = %u, y=%u\n", dw9 & 0xffff, dw9 >> 16);
            uint32_t dw10 = ac_ib_get(ib);
            fprintf(f, "    linear offset z = %u, linear pitch = %u\n", dw10 & 0xffff, (dw10 >> 16) + 1);
            uint32_t dw11 = ac_ib_get(ib);
            fprintf(f, "    linear slice pitch = %u\n", dw11 + 1);
            uint32_t dw12 = ac_ib_get(ib);
            fprintf(f, "    copy width = %u, copy height = %u\n", (dw12 & 0xffff) + 1, (dw12 >> 16) + 1);
            uint32_t dw13 = ac_ib_get(ib);
            fprintf(f, "    copy depth = %u\n", dw13 + 1);

            if (dcc) {
               ac_ib_get(ib);
               fprintf(f, "    metadata VA low\n");
               ac_ib_get(ib);
               fprintf(f, "    metadata VA high\n");
               ac_ib_get(ib);
               fprintf(f, "    (metadata config)\n");
            }
            break;
         }
         case SDMA_COPY_SUB_OPCODE_T2T_SUB_WINDOW: {
            fprintf(f, "COPY T2T_SUB_WINDOW\n");
            uint32_t dcc = (header >> 19) & 1;

            for (unsigned i = 0; i < 14; ++i) {
               ac_ib_get(ib);
               fprintf(f, "\n");
            }

            if (dcc) {
               ac_ib_get(ib);
               fprintf(f, "    metadata VA low\n");
               ac_ib_get(ib);
               fprintf(f, "    metadata VA high\n");
               ac_ib_get(ib);
               fprintf(f, "    (metadata config)\n");
            }
            break;
         }
         default:
            fprintf(f, "(unrecognized COPY sub op)\n");
            break;
         }
         break;
      }
      default:
         fprintf(f, " (unrecognized opcode)\n");
         break;
      }
   }
}

static void print_vcn_unrecognized_params(FILE *f, struct ac_ib_parser *ib, uint32_t start_dw, uint32_t size)
{
   for (uint32_t i = ib->cur_dw - start_dw; i < size / 4; i++) {
      ac_ib_get(ib);
      fprintf(f, "    %s(unrecognized)%s\n", O_COLOR_RED, O_COLOR_RESET);
   }
}

static const char *vcn_picture_type(uint32_t type)
{
   switch (type) {
   case RENCODE_PICTURE_TYPE_B:
      return "B";
   case RENCODE_PICTURE_TYPE_P:
      return "P";
   case RENCODE_PICTURE_TYPE_I:
      return "I";
   case RENCODE_PICTURE_TYPE_P_SKIP:
      return "P SKIP";
   default:
      return "???";
   }
}

static const char *vcn_picture_structure(uint32_t structure)
{
   switch (structure) {
   case RENCODE_H264_PICTURE_STRUCTURE_FRAME:
      return "FRAME";
   case RENCODE_H264_PICTURE_STRUCTURE_TOP_FIELD:
      return "TOP FIELD";
   case RENCODE_H264_PICTURE_STRUCTURE_BOTTOM_FIELD:
      return "BOTTOM FIELD";
   default:
      return "???";
   }
}

static const char *vcn_color_volume(uint32_t color_volume)
{
   switch (color_volume) {
   case RENCODE_COLOR_VOLUME_G22_BT709:
      return "G22 BT.709";
   default:
      return "???";
   }
}

static const char *vcn_color_range(uint32_t color_range)
{
   switch (color_range) {
   case RENCODE_COLOR_RANGE_FULL:
      return "FULL";
   case RENCODE_COLOR_RANGE_STUDIO:
      return "STUDIO";
   default:
      return "???";
   }
}

static const char *vcn_chroma_subsampling(uint32_t chroma_subsampling)
{
   switch (chroma_subsampling) {
   case RENCODE_CHROMA_SUBSAMPLING_4_2_0:
      return "4:2:0";
   case RENCODE_CHROMA_SUBSAMPLING_4_4_4:
      return "4:4:4";
   default:
      return "???";
   }
}

static const char *vcn_chroma_location(uint32_t chroma_location)
{
   switch (chroma_location) {
   case RENCODE_CHROMA_LOCATION_INTERSTITIAL:
      return "INTERSTITIAL";
   default:
      return "???";
   }
}

static const char *vcn_color_bit_depth(uint32_t bit_depth)
{
   switch (bit_depth) {
   case RENCODE_COLOR_BIT_DEPTH_8_BIT:
      return "8 BIT";
   case RENCODE_COLOR_BIT_DEPTH_10_BIT:
      return "10 BIT";
   default:
      return "???";
   }
}

static void print_vcn_addr(FILE *f, struct ac_ib_parser *ib, const char *prefix_format, ...)
{
   uint32_t high = ac_ib_get(ib);
   fprintf(f, "\n");
   uint32_t low = ac_ib_get(ib);

   va_list args;
   va_start(args, prefix_format);
   vfprintf(f, prefix_format, args);
   va_end(args);

   fprintf(f, " VA = 0x%"PRIx64"\n", ((uint64_t)high << 32) | low);
}

static void print_vcn_ref_pic_info(FILE *f, struct ac_ib_parser *ib, const char *prefix)
{
   uint32_t pic_type = ac_ib_get(ib);
   fprintf(f, "%s picture type = %s\n", prefix, vcn_picture_type(pic_type));
   uint32_t long_term = ac_ib_get(ib);
   fprintf(f, "%s is long term = %u\n", prefix, long_term);
   uint32_t pic_structure = ac_ib_get(ib);
   fprintf(f, "%s picture structure = %s\n", prefix, vcn_picture_structure(pic_structure));
   uint32_t pic_order_cnt = ac_ib_get(ib);
   fprintf(f, "%s pic order cnt = %u\n", prefix, pic_order_cnt);
}

static void print_vcn_reconstructed_picture(FILE *f, struct ac_ib_parser *ib, bool valid, const char *prefix_format, ...)
{
   char prefix[128];
   va_list args;
   va_start(args, prefix_format);
   vsnprintf(prefix, sizeof(prefix), prefix_format, args);
   va_end(args);

   if (ib->vcn_version >= VCN_5_0_0) {
      if (valid) {
         print_vcn_addr(f, ib, "%s luma", prefix);
         uint32_t luma_pitch = ac_ib_get(ib);
         fprintf(f, "%s luma pitch = %u\n", prefix, luma_pitch);
         print_vcn_addr(f, ib, "%s chroma", prefix);
         uint32_t chroma_pitch = ac_ib_get(ib);
         fprintf(f, "%s chroma pitch = %u\n", prefix, chroma_pitch);
         print_vcn_addr(f, ib, "%s chroma V", prefix);
         uint32_t chroma_v_pitch = ac_ib_get(ib);
         fprintf(f, "%s chroma V pitch = %u\n", prefix, chroma_v_pitch);
         uint32_t swizzle = ac_ib_get(ib);
         fprintf(f, "%s swizzle mode = %u\n", prefix, swizzle);
         print_vcn_addr(f, ib, "%s frame context buffer", prefix);
         uint32_t frame_context_offset = ac_ib_get(ib);
         fprintf(f, "%s AV1 cdf frame context offset / colloc buffer offset = %u\n", prefix, frame_context_offset);
         uint32_t cdef_offset = ac_ib_get(ib);
         fprintf(f, "%s AV1 cdef algorithm context offset = %u\n", prefix, cdef_offset);
         uint32_t metadata_offset = ac_ib_get(ib);
         fprintf(f, "%s encode metadata offset = %u\n", prefix, metadata_offset);
      } else {
         ib->cur_dw += 15;
      }
   } else {
      if (valid) {
         uint32_t luma_offset = ac_ib_get(ib);
         fprintf(f, "%s luma offset = %u\n", prefix, luma_offset);
         uint32_t chroma_offset = ac_ib_get(ib);
         fprintf(f, "%s chroma offset = %u\n", prefix, chroma_offset);
      } else {
         ib->cur_dw += 2;
      }
      if (ib->vcn_version >= VCN_4_0_0) {
         if (valid) {
            uint32_t frame_context_offset = ac_ib_get(ib);
            fprintf(f, "%s AV1 cdf frame context offset = %u\n", prefix, frame_context_offset);
            uint32_t cdef_offset = ac_ib_get(ib);
            fprintf(f, "%s AV1 cdef algorithm context offset = %u\n", prefix, cdef_offset);
         } else {
            ib->cur_dw += 2;
         }
      }
   }
}

static void print_vcn_preencode_input_picture(FILE *f, struct ac_ib_parser *ib, const char *prefix)
{
   uint32_t r_offset = ac_ib_get(ib);
   fprintf(f, "%s luma offset / red offset = %u\n", prefix, r_offset);
   uint32_t g_offset = ac_ib_get(ib);
   fprintf(f, "%s chroma offset / green offset = %u\n", prefix, g_offset);
   uint32_t b_offset = ac_ib_get(ib);
   fprintf(f, "%s blue offset = %u\n", prefix, b_offset);
}

static void parse_vcn_enc_ib(FILE *f, struct ac_ib_parser *ib)
{
   rvcn_enc_cmd_t cmd = {};
   ac_vcn_enc_init_cmds(&cmd, ib->vcn_version);

   while (ib->cur_dw < ib->num_dw) {
      const uint32_t start_dw = ib->cur_dw;
      const uint32_t size = ac_ib_get(ib);
      const uint32_t op = ac_ib_get(ib);

      if (op == RENCODE_IB_OP_INITIALIZE) {
         fprintf(f, "%sINITIALIZE:%s\n", O_COLOR_PURPLE, O_COLOR_RESET);
      } else if (op == RENCODE_IB_OP_CLOSE_SESSION) {
         fprintf(f, "%sCLOSE_SESSION%s\n", O_COLOR_PURPLE, O_COLOR_RESET);
      } else if (op == RENCODE_IB_OP_ENCODE) {
         fprintf(f, "%sENCODE%s\n", O_COLOR_PURPLE, O_COLOR_RESET);
      } else if (op == RENCODE_IB_OP_INIT_RC) {
         fprintf(f, "%sINIT_RC%s\n", O_COLOR_PURPLE, O_COLOR_RESET);
      } else if (op == RENCODE_IB_OP_INIT_RC_VBV_BUFFER_LEVEL) {
         fprintf(f, "%sINIT_RC_VBV_BUFFER_LEVEL%s\n", O_COLOR_PURPLE, O_COLOR_RESET);
      } else if (op == RENCODE_IB_OP_SET_SPEED_ENCODING_MODE) {
         fprintf(f, "%sSET_SPEED_ENCODING_MODE%s\n", O_COLOR_PURPLE, O_COLOR_RESET);
      } else if (op == RENCODE_IB_OP_SET_BALANCE_ENCODING_MODE) {
         fprintf(f, "%sSET_BALANCE_ENCODING_MODE%s\n", O_COLOR_PURPLE, O_COLOR_RESET);
      } else if (op == RENCODE_IB_OP_SET_QUALITY_ENCODING_MODE) {
         fprintf(f, "%sSET_QUALITY_ENCODING_MODE%s\n", O_COLOR_PURPLE, O_COLOR_RESET);
      } else if (op == RENCODE_IB_OP_SET_HIGH_QUALITY_ENCODING_MODE) {
         fprintf(f, "%sSET_HIGH_QUALITY_ENCODING_MODE%s\n", O_COLOR_PURPLE, O_COLOR_RESET);
      } else if (op == cmd.session_info) {
         fprintf(f, "%sSESSION_INFO%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t version = ac_ib_get(ib);
         fprintf(f, "    interface version = %u.%u\n",
                 (version & RENCODE_IF_MAJOR_VERSION_MASK) >> RENCODE_IF_MAJOR_VERSION_SHIFT,
                 (version & RENCODE_IF_MINOR_VERSION_MASK) >> RENCODE_IF_MINOR_VERSION_SHIFT);
         print_vcn_addr(f, ib, "    sw context");
         if (ib->vcn_version < VCN_3_0_0) {
            uint32_t engine = ac_ib_get(ib);
            fprintf(f, "    engine type = %s\n",
                    engine == RENCODE_ENGINE_TYPE_ENCODE ? "ENCODE" :
                    "???");
         }
      } else if (op == cmd.task_info) {
         fprintf(f, "%sTASK_INFO%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t total_size = ac_ib_get(ib);
         fprintf(f, "    size of all packages = %u\n", total_size);
         uint32_t task_id = ac_ib_get(ib);
         fprintf(f, "    task id = %u\n", task_id);
         uint32_t num_feedbacks = ac_ib_get(ib);
         fprintf(f, "    allowed max num feedbacks = %u\n", num_feedbacks);
      } else if (op == cmd.session_init) {
         fprintf(f, "%sSESSION_INIT%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t standard = ac_ib_get(ib);
         fprintf(f, "    encode standard = %s\n",
                 standard == RENCODE_ENCODE_STANDARD_H264 ? "H264" :
                 standard == RENCODE_ENCODE_STANDARD_HEVC ? "HEVC" :
                 standard == RENCODE_ENCODE_STANDARD_AV1 ? "AV1" :
                 "???");
         uint32_t pic_width = ac_ib_get(ib);
         fprintf(f, "    aligned picture width = %u\n", pic_width);
         uint32_t pic_height = ac_ib_get(ib);
         fprintf(f, "    aligned picture height = %u\n", pic_height);
         uint32_t padding_width = ac_ib_get(ib);
         fprintf(f, "    padding width = %u\n", padding_width);
         uint32_t padding_height = ac_ib_get(ib);
         fprintf(f, "    padding height = %u\n", padding_height);
         uint32_t preenc = ac_ib_get(ib);
         fprintf(f, "    preencode mode = %s\n",
                 preenc == RENCODE_PREENCODE_MODE_NONE ? "NONE" :
                 preenc == RENCODE_PREENCODE_MODE_1X ? "1X" :
                 preenc == RENCODE_PREENCODE_MODE_2X ? "2X" :
                 preenc == RENCODE_PREENCODE_MODE_4X ? "4X" :
                 "???");
         uint32_t preenc_chroma = ac_ib_get(ib);
         fprintf(f, "    preencode chroma enabled = %u\n", preenc_chroma);
         if (ib->vcn_version >= VCN_3_0_0) {
            uint32_t slice_output = ac_ib_get(ib);
            fprintf(f, "    slice output enabled = %u\n", slice_output);
         }
         uint32_t display_remote = ac_ib_get(ib);
         fprintf(f, "    display remote = %u\n", display_remote);
      } else if (op == cmd.layer_control) {
         fprintf(f, "%sLAYER_CONTROL%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t max_num_layers = ac_ib_get(ib);
         fprintf(f, "    max num temporal layers = %u\n", max_num_layers);
         uint32_t num_layers = ac_ib_get(ib);
         fprintf(f, "    num temporal layers = %u\n", num_layers);
      } else if (op == cmd.layer_select) {
         fprintf(f, "%sLAYER_SELECT%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t index = ac_ib_get(ib);
         fprintf(f, "    temporal layer index = %u\n", index);
      } else if (op == cmd.rc_session_init) {
         fprintf(f, "%sRATE_CONTROL_SESSION_INIT%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t method = ac_ib_get(ib);
         fprintf(f, "    rate control method = %s\n",
                 method == RENCODE_RATE_CONTROL_METHOD_NONE ? "NONE" :
                 method == RENCODE_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR ? "LATENCY CONSTRAINED VBR" :
                 method == RENCODE_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR ? "PEAK CONSTRAINED VBR" :
                 method == RENCODE_RATE_CONTROL_METHOD_CBR ? "CBR" :
                 method == RENCODE_RATE_CONTROL_METHOD_QUALITY_VBR ? "QUALITY VBR" :
                 "???");
         uint32_t buf_lvl = ac_ib_get(ib);
         fprintf(f, "    vbv buffer level = %u\n", buf_lvl);
      } else if (op == cmd.rc_layer_init) {
         fprintf(f, "%sRATE_CONTROL_LAYER_INIT%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t target_bitrate = ac_ib_get(ib);
         fprintf(f, "    target bitrate = %u\n", target_bitrate);
         uint32_t peak_bitrate = ac_ib_get(ib);
         fprintf(f, "    peak bitrate = %u\n", peak_bitrate);
         uint32_t frame_rate_num = ac_ib_get(ib);
         fprintf(f, "    frame rate numerator = %u\n", frame_rate_num);
         uint32_t frame_rate_den = ac_ib_get(ib);
         fprintf(f, "    frame rate denominator = %u\n", frame_rate_den);
         uint32_t vbv_size = ac_ib_get(ib);
         fprintf(f, "    vbv buffer size = %u\n", vbv_size);
         uint32_t avg_bits = ac_ib_get(ib);
         fprintf(f, "    average target bits per picture = %u\n", avg_bits);
         uint32_t peak_bits_integer = ac_ib_get(ib);
         fprintf(f, "    peak bits per picture (integer) = %u\n", peak_bits_integer);
         uint32_t peak_bits_fractional = ac_ib_get(ib);
         fprintf(f, "    peak bits per picture (fractional) = %u\n", peak_bits_fractional);
      } else if (op == cmd.quality_params) {
         fprintf(f, "%sQUALITY_PARAMS%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t vbaq_mode = ac_ib_get(ib);
         fprintf(f, "    VBAQ mode = %s\n",
                 vbaq_mode == RENCODE_VBAQ_NONE ? "NONE" :
                 vbaq_mode == RENCODE_VBAQ_AUTO ? "AUTO" :
                 "???");
         uint32_t scene_change_sens = ac_ib_get(ib);
         fprintf(f, "    scene change sensitivity = %u\n", scene_change_sens);
         uint32_t scene_change_interval = ac_ib_get(ib);
         fprintf(f, "    scene change min IDR interval = %u\n", scene_change_interval);
         uint32_t search_map_mode = ac_ib_get(ib);
         fprintf(f, "    2-pass search center map mode = %u\n", search_map_mode);
         if (ib->vcn_version >= VCN_2_0_0) {
            uint32_t vbaq_strength = ac_ib_get(ib);
            fprintf(f, "    VBAQ strength = %u\n", vbaq_strength);
         }
      } else if (op == cmd.slice_control_hevc) {
         fprintf(f, "%sHEVC_SLICE_CONTROL%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t mode = ac_ib_get(ib);
         fprintf(f, "    slice control mode = %s\n",
                 mode == RENCODE_HEVC_SLICE_CONTROL_MODE_FIXED_CTBS ? "FIXED CTBS" :
                 mode == RENCODE_HEVC_SLICE_CONTROL_MODE_FIXED_BITS ? "FIXED BITS" :
                 "???");
         uint32_t per_slice = ac_ib_get(ib);
         fprintf(f, "    num %s per slice = %u\n",
                 mode == RENCODE_HEVC_SLICE_CONTROL_MODE_FIXED_CTBS ? "ctbs" : "bits", per_slice);
         uint32_t per_slice_segment = ac_ib_get(ib);
         fprintf(f, "    num %s per slice segment = %u\n",
                 mode == RENCODE_HEVC_SLICE_CONTROL_MODE_FIXED_CTBS ? "ctbs" : "bits", per_slice_segment);
      } else if (op == cmd.spec_misc_hevc) {
         fprintf(f, "%sHEVC_SPEC_MISC%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t min_coding_block_size = ac_ib_get(ib);
         fprintf(f, "    log2_min_luma_coding_block_size_minus3 = %u\n", min_coding_block_size);
         uint32_t amp_disabled = ac_ib_get(ib);
         fprintf(f, "    amp disabled = %u\n", amp_disabled);
         uint32_t intra_smooth = ac_ib_get(ib);
         fprintf(f, "    strong_intra_smoothing_enabled_flag = %u\n", intra_smooth);
         uint32_t constrained_intra = ac_ib_get(ib);
         fprintf(f, "    constrained_intra_pred_flag = %u\n", constrained_intra);
         uint32_t cabac_init_flag = ac_ib_get(ib);
         fprintf(f, "    cabac_init_flag = %u\n", cabac_init_flag);
         uint32_t half_pel_enabled = ac_ib_get(ib);
         fprintf(f, "    half pel motion estimation = %u\n", half_pel_enabled);
         uint32_t quarter_pel_enabled = ac_ib_get(ib);
         fprintf(f, "    quarter pel motion estimation = %u\n", quarter_pel_enabled);
         if (ib->vcn_version >= VCN_3_0_0) {
            uint32_t transform_skip_disabled = ac_ib_get(ib);
            fprintf(f, "    transform skip disabled = %u\n", transform_skip_disabled);
            if (ib->vcn_version >= VCN_5_0_0) {
               uint32_t transquant_bypass = ac_ib_get(ib);
               fprintf(f, "    transquant bypass enabled = %u\n", transquant_bypass);
            }
         }
         if (ib->vcn_version >= VCN_2_0_0) {
            uint32_t cu_qp_delta = ac_ib_get(ib);
            fprintf(f, "    cu_qp_delta_enabled_flag = %u\n", cu_qp_delta);
         }
      } else if (op == cmd.deblocking_filter_hevc) {
         fprintf(f, "%sHEVC_DEBLOCKING_FILTER%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t across_slices = ac_ib_get(ib);
         fprintf(f, "    loop filter across slices enabled = %u\n", across_slices);
         uint32_t deblock_disabled = ac_ib_get(ib);
         fprintf(f, "    deblocking filter disabled = %u\n", deblock_disabled);
         uint32_t beta_offset = ac_ib_get(ib);
         fprintf(f, "    beta offset div2 = %u\n", beta_offset);
         uint32_t tc_offset = ac_ib_get(ib);
         fprintf(f, "    tc offset div2 = %u\n", tc_offset);
         uint32_t cb_qp_offset = ac_ib_get(ib);
         fprintf(f, "    cb_qp_offset = %u\n", cb_qp_offset);
         uint32_t cr_qp_offset = ac_ib_get(ib);
         fprintf(f, "    cr_qp_offset = %u\n", cr_qp_offset);
         if (ib->vcn_version >= VCN_2_0_0) {
            uint32_t disable_sao = ac_ib_get(ib);
            fprintf(f, "    force disable SAO = %u\n", disable_sao);
         }
      } else if (op == cmd.slice_control_h264) {
         fprintf(f, "%sH264_SLICE_CONTROL%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t mode = ac_ib_get(ib);
         fprintf(f, "    slice control mode = %s\n",
                 mode == RENCODE_H264_SLICE_CONTROL_MODE_FIXED_MBS ? "FIXED MBS" :
                 mode == RENCODE_H264_SLICE_CONTROL_MODE_FIXED_BITS ? "FIXED BITS" :
                 "???");
         uint32_t per_slice = ac_ib_get(ib);
         fprintf(f, "    num %s per slice = %u\n",
                 mode == RENCODE_H264_SLICE_CONTROL_MODE_FIXED_MBS ? "mbs" : "bits", per_slice);
      } else if (op == cmd.spec_misc_h264) {
         fprintf(f, "%sH264_SPEC_MISC%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t constrained_intra = ac_ib_get(ib);
         fprintf(f, "    constrained_intra_pred_flag = %u\n", constrained_intra);
         uint32_t cabac_enable = ac_ib_get(ib);
         fprintf(f, "    cabac enable = %u\n", cabac_enable);
         uint32_t cabac_init_idc = ac_ib_get(ib);
         fprintf(f, "    cabac_init_idc = %u\n", cabac_init_idc);
         if (ib->vcn_version >= VCN_5_0_0) {
            uint32_t transform8x8 = ac_ib_get(ib);
            fprintf(f, "    transform 8x8 enable = %u\n", transform8x8);
         }
         uint32_t half_pel_enabled = ac_ib_get(ib);
         fprintf(f, "    half pel motion estimation = %u\n", half_pel_enabled);
         uint32_t quarter_pel_enabled = ac_ib_get(ib);
         fprintf(f, "    quarter pel motion estimation = %u\n", quarter_pel_enabled);
         uint32_t profile_idc = ac_ib_get(ib);
         fprintf(f, "    profile_idc = %u\n", profile_idc);
         uint32_t level_idc = ac_ib_get(ib);
         fprintf(f, "    level_idc = %u\n", level_idc);
         if (ib->vcn_version >= VCN_3_0_0) {
            uint32_t b_pic = ac_ib_get(ib);
            fprintf(f, "    B picture enabled = %u\n", b_pic);
            uint32_t weighted_bipred = ac_ib_get(ib);
            fprintf(f, "    weighted_bipred_idc = %u\n", weighted_bipred);
         }
      } else if (op == cmd.enc_params_h264) {
         fprintf(f, "%sH264_ENCODE_PARAMS%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t input_structure = ac_ib_get(ib);
         fprintf(f, "    input picture structure = %s\n", vcn_picture_structure(input_structure));
         if (ib->vcn_version >= VCN_3_0_0) {
            uint32_t pic_order_cnt = ac_ib_get(ib);
            fprintf(f, "    input pic order cnt = %u\n", pic_order_cnt);
            if (ib->vcn_version >= VCN_5_0_0) {
               uint32_t ref = ac_ib_get(ib);
               fprintf(f, "    is reference = %u\n", ref);
               uint32_t long_term = ac_ib_get(ib);
               fprintf(f, "    is long term reference = %u\n", long_term);
            }
         }
         uint32_t interlaced_mode = ac_ib_get(ib);
         fprintf(f, "    interlaced mode = %s\n",
                 interlaced_mode == RENCODE_H264_INTERLACING_MODE_PROGRESSIVE ? "PROGRESSIVE" :
                 interlaced_mode == RENCODE_H264_INTERLACING_MODE_INTERLACED_STACKED ? "INTERLACED STACKED" :
                 interlaced_mode == RENCODE_H264_INTERLACING_MODE_INTERLACED_INTERLEAVED ? "INTERLACED INTERLEAVED" :
                 "???");
         if (ib->vcn_version < VCN_3_0_0) {
            uint32_t ref_pic_structure = ac_ib_get(ib);
            fprintf(f, "    reference picture structure = %s\n", vcn_picture_structure(ref_pic_structure));
            uint32_t ref_pic1_index = ac_ib_get(ib);
            fprintf(f, "    reference picture1 index = %u\n", ref_pic1_index);
         } else if (ib->vcn_version < VCN_5_0_0) {
            print_vcn_ref_pic_info(f, ib, "    l0[0] reference");
            uint32_t l0_pic1_idx = ac_ib_get(ib);
            fprintf(f, "    l0[1] reference picture index = %u\n", l0_pic1_idx);
            print_vcn_ref_pic_info(f, ib, "    l0[1] reference");
            uint32_t l1_pic0_idx = ac_ib_get(ib);
            fprintf(f, "    l1[0] reference picture index = %u\n", l1_pic0_idx);
            print_vcn_ref_pic_info(f, ib, "    l1[0] reference");
            uint32_t ref = ac_ib_get(ib);
            fprintf(f, "    is reference = %u\n", ref);
         } else if (ib->vcn_version >= VCN_5_0_0) {
            for (uint32_t i = 0; i < RENCODE_H264_MAX_REFERENCE_LIST_SIZE; i++) {
               uint32_t idx = ac_ib_get(ib);
               fprintf(f, "    ref_list0[%u] = %u\n", i, idx);
            }
            uint32_t num_refs0 = ac_ib_get(ib);
            fprintf(f, "    num active references l0 = %u\n", num_refs0);
            for (uint32_t i = 0; i < RENCODE_H264_MAX_REFERENCE_LIST_SIZE; i++) {
               uint32_t idx = ac_ib_get(ib);
               fprintf(f, "    ref_list1[%u] = %u\n", i, idx);
            }
            uint32_t num_refs1 = ac_ib_get(ib);
            fprintf(f, "    num active references l1 = %u\n", num_refs1);
            for (uint32_t i = 0; i < 2; i++) {
               uint32_t list = ac_ib_get(ib);
               fprintf(f, "    lsm_reference_pictures[%u].list = %u\n", i, list);
               uint32_t list_index = ac_ib_get(ib);
               fprintf(f, "    lsm_reference_pictures[%u].list_index = %u\n", i, list_index);
            }
         }
      } else if (op == cmd.deblocking_filter_h264) {
         fprintf(f, "%sH264_DEBLOCKING_FILTER%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t disable_filter = ac_ib_get(ib);
         fprintf(f, "    disable_deblocking_filter_idc = %u\n", disable_filter);
         uint32_t alpha_offset = ac_ib_get(ib);
         fprintf(f, "    alpha c0 offset div2 = %u\n", alpha_offset);
         uint32_t beta_offset = ac_ib_get(ib);
         fprintf(f, "    beta offset div2 = %u\n", beta_offset);
         uint32_t cb_qp_offset = ac_ib_get(ib);
         fprintf(f, "    cb_qp_offset = %u\n", cb_qp_offset);
         uint32_t cr_qp_offset = ac_ib_get(ib);
         fprintf(f, "    cr_qp_offset = %u\n", cr_qp_offset);
      } else if (ib->vcn_version < VCN_5_0_0 && op == cmd.rc_per_pic) {
         fprintf(f, "%sRATE_CONTROL_PER_PICTURE%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t qp = ac_ib_get(ib);
         fprintf(f, "    QP = %u\n", qp);
         uint32_t min_qp = ac_ib_get(ib);
         fprintf(f, "    min QP = %u\n", min_qp);
         uint32_t max_qp = ac_ib_get(ib);
         fprintf(f, "    max QP = %u\n", max_qp);
         uint32_t max_au_size = ac_ib_get(ib);
         fprintf(f, "    max access unit size = %u\n", max_au_size);
         uint32_t filler_data = ac_ib_get(ib);
         fprintf(f, "    filler data enabled = %u\n", filler_data);
         uint32_t skip_frame = ac_ib_get(ib);
         fprintf(f, "    skip frame enabled = %u\n", skip_frame);
         uint32_t enforce_hrd = ac_ib_get(ib);
         fprintf(f, "    enforce HRD = %u\n", enforce_hrd);
      } else if ((ib->vcn_version >= VCN_5_0_0 && op == cmd.rc_per_pic) ||
                 (ib->vcn_version < VCN_5_0_0 && op == cmd.rc_per_pic_ex)) {
         fprintf(f, "%sRATE_CONTROL_PER_PICTURE%s%s\n", O_COLOR_GREEN, ib->vcn_version < VCN_5_0_0 ? "_EX" : "", O_COLOR_RESET);
         uint32_t qp_i = ac_ib_get(ib);
         fprintf(f, "    QP I = %u\n", qp_i);
         uint32_t qp_p = ac_ib_get(ib);
         fprintf(f, "    QP P = %u\n", qp_p);
         uint32_t qp_b = ac_ib_get(ib);
         fprintf(f, "    QP B = %u\n", qp_b);
         uint32_t min_qp_i = ac_ib_get(ib);
         fprintf(f, "    min QP I = %u\n", min_qp_i);
         uint32_t max_qp_i = ac_ib_get(ib);
         fprintf(f, "    max QP I = %u\n", max_qp_i);
         uint32_t min_qp_p = ac_ib_get(ib);
         fprintf(f, "    min QP P = %u\n", min_qp_p);
         uint32_t max_qp_p = ac_ib_get(ib);
         fprintf(f, "    max QP P = %u\n", max_qp_p);
         uint32_t min_qp_b = ac_ib_get(ib);
         fprintf(f, "    min QP B = %u\n", min_qp_b);
         uint32_t max_qp_b = ac_ib_get(ib);
         fprintf(f, "    max QP B = %u\n", max_qp_b);
         uint32_t max_au_size_i = ac_ib_get(ib);
         fprintf(f, "    max access unit size I = %u\n", max_au_size_i);
         uint32_t max_au_size_p = ac_ib_get(ib);
         fprintf(f, "    max access unit size P = %u\n", max_au_size_p);
         uint32_t max_au_size_b = ac_ib_get(ib);
         fprintf(f, "    max access unit size B = %u\n", max_au_size_b);
         uint32_t filler_data = ac_ib_get(ib);
         fprintf(f, "    filler data enabled = %u\n", filler_data);
         uint32_t skip_frame = ac_ib_get(ib);
         fprintf(f, "    skip frame enabled = %u\n", skip_frame);
         uint32_t enforce_hrd = ac_ib_get(ib);
         fprintf(f, "    enforce HRD = %u\n", enforce_hrd);
         if (ib->vcn_version >= VCN_3_0_0) {
            uint32_t qvbr_level = ac_ib_get(ib);
            fprintf(f, "    QVBR quality level = %u\n", qvbr_level);
         }
      } else if (op == cmd.slice_header) {
         fprintf(f, "%sSLICE_HEADER%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         for (uint32_t i = 0; i < RENCODE_SLICE_HEADER_TEMPLATE_MAX_TEMPLATE_SIZE_IN_DWORDS; i++) {
            uint32_t value = ac_ib_get(ib);
            fprintf(f, "    %s\n", value ? "bitstream template" : "");
         }
         bool at_end = false;
         for (uint32_t i = 0; i < RENCODE_SLICE_HEADER_TEMPLATE_MAX_NUM_INSTRUCTIONS; i++) {
            if (at_end) {
               ib->cur_dw += 2;
               continue;
            }
            uint32_t instruction = ac_ib_get(ib);
            fprintf(f, "    instruction = %s\n",
                    instruction == RENCODE_HEADER_INSTRUCTION_END ? "END" :
                    instruction == RENCODE_HEADER_INSTRUCTION_COPY ? "COPY" :
                    instruction == RENCODE_HEVC_HEADER_INSTRUCTION_DEPENDENT_SLICE_END ? "DEPENDENT SLICE END" :
                    instruction == RENCODE_HEVC_HEADER_INSTRUCTION_FIRST_SLICE ? "FIRST SLICE" :
                    instruction == RENCODE_HEVC_HEADER_INSTRUCTION_SLICE_SEGMENT ? "SLICE SEGMENT" :
                    instruction == RENCODE_HEVC_HEADER_INSTRUCTION_SLICE_QP_DELTA ? "SLICE QP DELTA" :
                    instruction == RENCODE_HEVC_HEADER_INSTRUCTION_SAO_ENABLE ? "SAO ENABLE" :
                    instruction == RENCODE_HEVC_HEADER_INSTRUCTION_LOOP_FILTER_ACROSS_SLICES_ENABLE ? "LOOP FILTER ACROSS SLICES ENABLE" :
                    instruction == RENCODE_H264_HEADER_INSTRUCTION_FIRST_MB ? "FIRST MB" :
                    instruction == RENCODE_H264_HEADER_INSTRUCTION_SLICE_QP_DELTA ? "SLICE QP DELTA" :
                    "???");
            uint32_t bits = ac_ib_get(ib);
            if (instruction == RENCODE_HEADER_INSTRUCTION_COPY)
               fprintf(f, "    num bits = %u\n", bits);
            else
               fprintf(f, "\n");
            at_end = instruction == RENCODE_HEADER_INSTRUCTION_END;
         }
      } else if (op == cmd.enc_params) {
         fprintf(f, "%sENCODE_PARAMS%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t pic_type = ac_ib_get(ib);
         fprintf(f, "    picture type = %s\n", vcn_picture_type(pic_type));
         uint32_t bs_size = ac_ib_get(ib);
         fprintf(f, "    allowed max bitstream size = %u\n", bs_size);
         print_vcn_addr(f, ib, "    input picture luma");
         print_vcn_addr(f, ib, "    input picture chroma");
         uint32_t luma_pitch = ac_ib_get(ib);
         fprintf(f, "    input picture luma pitch = %u\n", luma_pitch);
         uint32_t chroma_pitch = ac_ib_get(ib);
         fprintf(f, "    input picture chroma pitch = %u\n", chroma_pitch);
         uint32_t swizzle = ac_ib_get(ib);
         fprintf(f, "    input picture swizzle mode = %s\n",
                 swizzle == RENCODE_INPUT_SWIZZLE_MODE_LINEAR ? "LINEAR" :
                 swizzle == RENCODE_INPUT_SWIZZLE_MODE_256B_S ? "256B S" :
                 swizzle == RENCODE_INPUT_SWIZZLE_MODE_4kB_S ? "4kB S" :
                 swizzle == RENCODE_INPUT_SWIZZLE_MODE_64kB_S ? "64kB S" :
                 "???");
         if (ib->vcn_version < VCN_5_0_0) {
            uint32_t ref_pic_idx = ac_ib_get(ib);
            fprintf(f, "    reference picture index = %u\n", ref_pic_idx);
         }
         uint32_t recon_pic_idx = ac_ib_get(ib);
         fprintf(f, "    reconstructed picture index = %u\n", recon_pic_idx);
      } else if (op == cmd.intra_refresh) {
         fprintf(f, "%sINTRA_REFRESH%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t mode = ac_ib_get(ib);
         fprintf(f, "    intra refresh mode = %s\n",
                 mode == RENCODE_INTRA_REFRESH_MODE_NONE ? "NONE" :
                 mode == RENCODE_INTRA_REFRESH_MODE_CTB_MB_ROWS ? "CTB MB ROWS" :
                 mode == RENCODE_INTRA_REFRESH_MODE_CTB_MB_COLUMNS ? "CTB MB COLUMNS" :
                 "???");
         uint32_t offset = ac_ib_get(ib);
         fprintf(f, "    offset = %u\n", offset);
         uint32_t region_size = ac_ib_get(ib);
         fprintf(f, "    region size = %u\n", region_size);
      } else if (op == cmd.ctx) {
         fprintf(f, "%sENCODE_CONTEXT_BUFFER%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         print_vcn_addr(f, ib, "    encode context buffer");
         if (ib->vcn_version < VCN_5_0_0) {
            uint32_t swizzle = ac_ib_get(ib);
            fprintf(f, "    swizzle mode = %s\n",
                    swizzle == RENCODE_REC_SWIZZLE_MODE_LINEAR ? "LINEAR" :
                    (ib->vcn_version < VCN_5_0_0 && swizzle == RENCODE_REC_SWIZZLE_MODE_256B_D) ||
                    (ib->vcn_version >= VCN_5_0_0 && swizzle == RENCODE_REC_SWIZZLE_MODE_256B_D_VCN5) ? "256B D" :
                    swizzle == RENCODE_REC_SWIZZLE_MODE_256B_S ? "256B S" :
                    swizzle == RENCODE_REC_SWIZZLE_MODE_8x8_1D_THIN_12_24BPP ? "8x8 1D THIN 12 24BPP" :
                    "???");
            uint32_t luma_pitch = ac_ib_get(ib);
            fprintf(f, "    reconstructed surface luma pitch = %u\n", luma_pitch);
            uint32_t chroma_pitch = ac_ib_get(ib);
            fprintf(f, "    reconstructed surface chroma pitch = %u\n", chroma_pitch);
         }
         uint32_t num_recons = ac_ib_get(ib);
         fprintf(f, "    num reconstructed pictures = %u\n", num_recons);
         for (uint32_t i = 0; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++)
            print_vcn_reconstructed_picture(f, ib, i < num_recons, "    recon[%u]", i);
         if (ib->vcn_version >= VCN_3_0_0 && ib->vcn_version < VCN_4_0_0) {
            uint32_t colloc_offset = ac_ib_get(ib);
            fprintf(f, "    collocated buffer offset = %u\n", colloc_offset);
         }
         if (ib->vcn_version < VCN_5_0_0) {
            uint32_t luma_pitch = ac_ib_get(ib);
            fprintf(f, "    preencode reconstructed surface luma pitch = %u\n", luma_pitch);
            uint32_t chroma_pitch = ac_ib_get(ib);
            fprintf(f, "    preencode reconstructed surface chroma pitch = %u\n", chroma_pitch);
         }
         for (uint32_t i = 0; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++)
            print_vcn_reconstructed_picture(f, ib, i < num_recons, "    preenc recon[%u]", i);
         if (ib->vcn_version >= VCN_5_0_0) {
            uint32_t luma_pitch = ac_ib_get(ib);
            fprintf(f, "    preencode input surface luma pitch = %u\n", luma_pitch);
            uint32_t chroma_pitch = ac_ib_get(ib);
            fprintf(f, "    preencode input surface chroma pitch = %u\n", chroma_pitch);
         }
         if (ib->vcn_version < VCN_2_0_0)
            print_vcn_reconstructed_picture(f, ib, true, "    preencode input");
         else if (ib->vcn_version < VCN_3_0_0)
            print_vcn_reconstructed_picture(f, ib, true, "    preencode input old");
         else
            print_vcn_preencode_input_picture(f, ib, "    preencode input");
         if (ib->vcn_version < VCN_5_0_0) {
            uint32_t search_offset = ac_ib_get(ib);
            fprintf(f, "    2-pass search center map offset = %u\n", search_offset);
         }
         if (ib->vcn_version >= VCN_2_0_0 && ib->vcn_version < VCN_3_0_0)
            print_vcn_preencode_input_picture(f, ib, "    preencode input");
         if (ib->vcn_version >= VCN_4_0_0 && ib->vcn_version < VCN_5_0_0) {
            uint32_t colloc_offset = ac_ib_get(ib);
            fprintf(f, "    colloc buffer offset / AV1 sdb intermediate context offset = %u\n", colloc_offset);
         } else if (ib->vcn_version >= VCN_5_0_0) {
            uint32_t sdb_offset = ac_ib_get(ib);
            fprintf(f, "    AV1 sdb intermediate context offset = %u\n", sdb_offset);
         }
      } else if (op == cmd.bitstream) {
         fprintf(f, "%sVIDEO_BITSTREAM_BUFFER%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t mode = ac_ib_get(ib);
         fprintf(f, "    mode = %s\n",
                 mode == RENCODE_VIDEO_BITSTREAM_BUFFER_MODE_LINEAR ? "LINEAR" :
                 mode == RENCODE_VIDEO_BITSTREAM_BUFFER_MODE_CIRCULAR ? "CIRCULAR" :
                 "???");
         print_vcn_addr(f, ib, "    video bitstream buffer");
         uint32_t size = ac_ib_get(ib);
         fprintf(f, "    video bitstream buffer size = %u\n", size);
         uint32_t offset = ac_ib_get(ib);
         fprintf(f, "    video bitstream buffer data offset = %u\n", offset);
      } else if (op == cmd.feedback) {
         fprintf(f, "%sFEEDBACK_BUFFER%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t mode = ac_ib_get(ib);
         fprintf(f, "    mode = %s\n",
                 mode == RENCODE_FEEDBACK_BUFFER_MODE_LINEAR ? "LINEAR" :
                 mode == RENCODE_FEEDBACK_BUFFER_MODE_CIRCULAR ? "CIRCULAR" :
                 "???");
         print_vcn_addr(f, ib, "    feedback buffer");
         uint32_t size = ac_ib_get(ib);
         fprintf(f, "    feedback buffer size = %u\n", size);
         uint32_t data_size = ac_ib_get(ib);
         fprintf(f, "    feedback buffer data size = %u\n", data_size);
      } else if (op == cmd.enc_qp_map) {
         fprintf(f, "%sQP_MAP%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t type = ac_ib_get(ib);
         fprintf(f, "    QP map type = %s\n",
                 type == RENCODE_QP_MAP_TYPE_NONE ? "NONE" :
                 type == RENCODE_QP_MAP_TYPE_DELTA ? "DELTA" :
                 type == RENCODE_QP_MAP_TYPE_MAP_PA ? "PA" :
                 "???");
         print_vcn_addr(f, ib, "    QP map buffer");
         uint32_t pitch = ac_ib_get(ib);
         fprintf(f, "    QP map buffer pitch = %u\n", pitch);
      } else if (op == cmd.enc_statistics) {
         fprintf(f, "%sENCODE_STATISTICS%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t type = ac_ib_get(ib);
         fprintf(f, "    encode statistics type = %u\n", type);
         print_vcn_addr(f, ib, "    encode statistics buffer");
      } else if (op == cmd.enc_latency) {
         fprintf(f, "%sENCODE_LATENCY%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t latency = ac_ib_get(ib);
         fprintf(f, "    encode latency = %u\n", latency);
      } else if (op == cmd.input_format) {
         fprintf(f, "%sINPUT_FORMAT%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t color_volume = ac_ib_get(ib);
         fprintf(f, "    color volume = %s\n", vcn_color_volume(color_volume));
         uint32_t color_space = ac_ib_get(ib);
         fprintf(f, "    color space = %s\n",
                 color_space == RENCODE_COLOR_SPACE_RGB ? "RGB" :
                 color_space == RENCODE_COLOR_SPACE_YUV ? "YUV" :
                 "???");
         uint32_t color_range = ac_ib_get(ib);
         fprintf(f, "    color range = %s\n", vcn_color_range(color_range));
         uint32_t chroma_subsampling = ac_ib_get(ib);
         fprintf(f, "    chroma subsampling = %s\n", vcn_chroma_subsampling(chroma_subsampling));
         uint32_t chroma_location = ac_ib_get(ib);
         fprintf(f, "    chroma location = %s\n", vcn_chroma_location(chroma_location));
         uint32_t bit_depth = ac_ib_get(ib);
         fprintf(f, "    color bit depth = %s\n", vcn_color_bit_depth(bit_depth));
         uint32_t packing_format = ac_ib_get(ib);
         fprintf(f, "    packing format = %s\n",
                 packing_format == RENCODE_COLOR_PACKING_FORMAT_NV12 ? "NV12" :
                 packing_format == RENCODE_COLOR_PACKING_FORMAT_P010 ? "P010" :
                 packing_format == RENCODE_COLOR_PACKING_FORMAT_A8R8G8B8 ? "A8R8G8B8" :
                 packing_format == RENCODE_COLOR_PACKING_FORMAT_A2R10G10B10 ? "A2R10G10B10" :
                 packing_format == RENCODE_COLOR_PACKING_FORMAT_A8B8G8R8 ? "A8B8G8R8" :
                 packing_format == RENCODE_COLOR_PACKING_FORMAT_A2B10G10R10 ? "A2B10G10R10" :
                 "???");
      } else if (op == cmd.output_format) {
         fprintf(f, "%sOUTPUT_FORMAT%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t color_volume = ac_ib_get(ib);
         fprintf(f, "    color volume = %s\n", vcn_color_volume(color_volume));
         uint32_t color_range = ac_ib_get(ib);
         fprintf(f, "    color range = %s\n", vcn_color_range(color_range));
         uint32_t chroma_location = ac_ib_get(ib);
         if (ib->vcn_version >= VCN_5_0_0) {
            uint32_t chroma_subsampling = ac_ib_get(ib);
            fprintf(f, "    chroma subsampling = %s\n", vcn_chroma_subsampling(chroma_subsampling));
         }
         fprintf(f, "    chroma location = %s\n", vcn_chroma_location(chroma_location));
         uint32_t bit_depth = ac_ib_get(ib);
         fprintf(f, "    color bit depth = %s\n", vcn_color_bit_depth(bit_depth));
      } else if (op == cmd.cdf_default_table_av1) {
         fprintf(f, "%sCDF_DEFAULT_TABLE_BUFFER%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t use_default = ac_ib_get(ib);
         fprintf(f, "    use cdf default = %u\n", use_default);
         ac_ib_get(ib);
         fprintf(f, "    cdf default buffer VA low\n");
         ac_ib_get(ib);
         fprintf(f, "    cdf default buffer VA high\n");
      } else if (op == cmd.spec_misc_av1) {
         fprintf(f, "%sAV1_SPEC_MISC%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t palette = ac_ib_get(ib);
         fprintf(f, "    palette mode enable = %u\n", palette);
         uint32_t mv_precision = ac_ib_get(ib);
         fprintf(f, "    motion vector precision = %s\n",
                 mv_precision == RENCODE_AV1_MV_PRECISION_ALLOW_HIGH_PRECISION ? "ALLOW HIGH PRECISION" :
                 mv_precision == RENCODE_AV1_MV_PRECISION_DISALLOW_HIGH_PRECISION ? "DISALLOW HIGH PRECISION" :
                 mv_precision == RENCODE_AV1_MV_PRECISION_FORCE_INTEGER_MV ? "FORCE INTEGER MV" :
                 "???");
         uint32_t cdef_mode = ac_ib_get(ib);
         fprintf(f, "    cdef mode = %s\n",
                 cdef_mode == RENCODE_AV1_CDEF_MODE_DISABLE ? "DISABLE" :
                 cdef_mode == RENCODE_AV1_CDEF_MODE_DEFAULT ? "DEFAULT" :
                 cdef_mode == RENCODE_AV1_CDEF_MODE_EXPLICIT ? "EXPLICIT" :
                 "???");
         if (ib->vcn_version >= VCN_5_0_0) {
            uint32_t cdef_bits = ac_ib_get(ib);
            fprintf(f, "    cdef_bits = %u\n", cdef_bits);
            uint32_t cdef_damping_minus_3 = ac_ib_get(ib);
            fprintf(f, "    cdef_damping_minus_3 = %u\n", cdef_damping_minus_3);
            for (uint32_t i = 0; i < RENCODE_AV1_CDEF_MAX_NUM; i++) {
               uint32_t cdef_y_pri_strength = ac_ib_get(ib);
               fprintf(f, "    cdef_y_pri_strength[%u] = %u\n", i, cdef_y_pri_strength);
            }
            for (uint32_t i = 0; i < RENCODE_AV1_CDEF_MAX_NUM; i++) {
               uint32_t cdef_y_sec_strength = ac_ib_get(ib);
               fprintf(f, "    cdef_y_sec_strength[%u] = %u\n", i, cdef_y_sec_strength);
            }
            for (uint32_t i = 0; i < RENCODE_AV1_CDEF_MAX_NUM; i++) {
               uint32_t cdef_uv_pri_strength = ac_ib_get(ib);
               fprintf(f, "    cdef_uv_pri_strength[%u] = %u\n", i, cdef_uv_pri_strength);
            }
            for (uint32_t i = 0; i < RENCODE_AV1_CDEF_MAX_NUM; i++) {
               uint32_t cdef_uv_sec_strength = ac_ib_get(ib);
               fprintf(f, "    cdef_uv_sec_strength[%u] = %u\n", i, cdef_uv_sec_strength);
            }
            uint32_t intrabc = ac_ib_get(ib);
            fprintf(f, "    allow intrabc = %u\n", intrabc);
         }
         uint32_t cdf_update = ac_ib_get(ib);
         fprintf(f, "    disable cdf update = %u\n", cdf_update);
         uint32_t frame_end_update = ac_ib_get(ib);
         fprintf(f, "    disable frame end update cdf = %u\n", frame_end_update);
         if (ib->vcn_version >= VCN_5_0_0) {
            uint32_t unk1 = ac_ib_get(ib);
            fprintf(f, "    ??? = %u\n", unk1);
            uint32_t delta_q_y_dc = ac_ib_get(ib);
            fprintf(f, "    delta QYDc = %u\n", delta_q_y_dc);
            uint32_t delta_q_u_dc = ac_ib_get(ib);
            fprintf(f, "    delta QUDc = %u\n", delta_q_u_dc);
            uint32_t delta_q_u_ac = ac_ib_get(ib);
            fprintf(f, "    delta QUAc = %u\n", delta_q_u_ac);
            uint32_t delta_q_v_dc = ac_ib_get(ib);
            fprintf(f, "    delta QVDc = %u\n", delta_q_v_dc);
            uint32_t delta_q_v_ac = ac_ib_get(ib);
            fprintf(f, "    delta QVAc = %u\n", delta_q_v_ac);
         } else {
            uint32_t num_tiles = ac_ib_get(ib);
            fprintf(f, "    num tiles per picture = %u\n", num_tiles);
         }
         uint32_t screen_content_detect = ac_ib_get(ib);
         fprintf(f, "    enable screen content auto detection = %u\n", screen_content_detect);
         uint32_t screen_content_threshold = ac_ib_get(ib);
         fprintf(f, "    screen content frame percentage threshold = %u\n", screen_content_threshold);
      } else if (op == cmd.bitstream_instruction_av1) {
         fprintf(f, "%sAV1_BITSTREAM_INSTRUCTION%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         while (true) {
            ac_ib_get(ib); /* size */
            fprintf(f, "\n");
            uint32_t type = ac_ib_get(ib);
            fprintf(f, "    type = %s\n",
                    type == RENCODE_AV1_BITSTREAM_INSTRUCTION_END ? "END" :
                    type == RENCODE_AV1_BITSTREAM_INSTRUCTION_COPY ? "COPY" :
                    type == RENCODE_AV1_BITSTREAM_INSTRUCTION_OBU_START ? "OBU START" :
                    type == RENCODE_AV1_BITSTREAM_INSTRUCTION_OBU_SIZE ? "OBU SIZE" :
                    type == RENCODE_AV1_BITSTREAM_INSTRUCTION_OBU_END ? "OBU END" :
                    type == RENCODE_AV1_BITSTREAM_INSTRUCTION_ALLOW_HIGH_PRECISION_MV ? "ALLOW HIGH PRECISION MV" :
                    type == RENCODE_AV1_BITSTREAM_INSTRUCTION_DELTA_LF_PARAMS ? "DELTA LF PARAMS" :
                    type == RENCODE_AV1_BITSTREAM_INSTRUCTION_READ_INTERPOLATION_FILTER ? "READ INTERPOLATION FILTER" :
                    type == RENCODE_AV1_BITSTREAM_INSTRUCTION_LOOP_FILTER_PARAMS ? "LOOP FILTER PARAMS" :
                    (ib->vcn_version >= VCN_5_0_0 &&
                     type == RENCODE_V5_AV1_BITSTREAM_INSTRUCTION_CONTEXT_UPDATE_TILE_ID) ? "CONTEXT UPDATE TILE ID" :
                    (ib->vcn_version >= VCN_5_0_0 &&
                     type == RENCODE_V5_AV1_BITSTREAM_INSTRUCTION_BASE_Q_IDX) ? "BASE Q IDX" :
                    type == RENCODE_V4_AV1_BITSTREAM_INSTRUCTION_TILE_INFO ? "TILE INFO" :
                    type == RENCODE_V4_AV1_BITSTREAM_INSTRUCTION_QUANTIZATION_PARAMS ? "QUANTIZATION PARAMS" :
                    type == RENCODE_AV1_BITSTREAM_INSTRUCTION_DELTA_Q_PARAMS ? "DELTA Q PARAMS" :
                    type == RENCODE_AV1_BITSTREAM_INSTRUCTION_CDEF_PARAMS ? "CDEF PARAMS" :
                    type == RENCODE_AV1_BITSTREAM_INSTRUCTION_READ_TX_MODE ? "READ TX MODE" :
                    type == RENCODE_AV1_BITSTREAM_INSTRUCTION_TILE_GROUP_OBU ? "TILE GROUP OBU" :
                    "???");
            if (type == RENCODE_AV1_BITSTREAM_INSTRUCTION_COPY) {
               uint32_t num_bits = ac_ib_get(ib);
               fprintf(f, "    num bits = %u\n", num_bits);
               for (uint32_t i = 0; i < num_bits; i += 32) {
                  ac_ib_get(ib);
                  fprintf(f, "    bitstream data\n");
               }
            } else if (type == RENCODE_AV1_BITSTREAM_INSTRUCTION_OBU_START) {
               uint32_t type = ac_ib_get(ib);
               fprintf(f, "    OBU type = %s\n",
                       type == RENCODE_OBU_START_TYPE_FRAME ? "FRAME" :
                       type == RENCODE_OBU_START_TYPE_FRAME_HEADER ? "FRAME HEADER" :
                       type == RENCODE_OBU_START_TYPE_TILE_GROUP ? "TILE GROUP" :
                       "???");
            } else if (type == RENCODE_AV1_BITSTREAM_INSTRUCTION_END) {
               break;
            }
         }
      } else if (op == cmd.metadata) {
         fprintf(f, "%sMETADATA_BUFFER%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         print_vcn_addr(f, ib, "    metadata buffer");
         uint32_t search_offset = ac_ib_get(ib);
         fprintf(f, "    2-pass search center map offset = %u\n", search_offset);
      } else if (op == cmd.enc_params_hevc) {
         fprintf(f, "%sHEVC_ENCODE_PARAMS%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         for (uint32_t i = 0; i < RENCODE_HEVC_MAX_REFERENCE_LIST_SIZE; i++) {
            uint32_t ref = ac_ib_get(ib);
            fprintf(f, "    ref list[%u] = %u\n", i, ref);
         }
         uint32_t num_active = ac_ib_get(ib);
         fprintf(f, "    num active references l0 = %u\n", num_active);
         uint32_t lsm_idx = ac_ib_get(ib);
         fprintf(f, "    lsm reference picture list index = %u\n", lsm_idx);
      } else if (op == cmd.tile_config_av1) {
         fprintf(f, "%sAV1_TILE_CONFIG%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         uint32_t num_cols = ac_ib_get(ib);
         fprintf(f, "    num tile columns = %u\n", num_cols);
         uint32_t num_rows = ac_ib_get(ib);
         fprintf(f, "    num tile rows = %u\n", num_rows);
         for (uint32_t i = 0; i < RENCODE_AV1_TILE_CONFIG_MAX_NUM_COLS; i++) {
            uint32_t w = ac_ib_get(ib);
            fprintf(f, "    tile width[%u] = %u\n", i, w);
         }
         for (uint32_t i = 0; i < RENCODE_AV1_TILE_CONFIG_MAX_NUM_ROWS; i++) {
            uint32_t h = ac_ib_get(ib);
            fprintf(f, "    tile height[%u] = %u\n", i, h);
         }
         uint32_t num_groups = ac_ib_get(ib);
         fprintf(f, "    num tile groups = %u\n", num_groups);
         for (uint32_t i = 0; i < RENCODE_AV1_TILE_CONFIG_MAX_NUM_COLS * RENCODE_AV1_TILE_CONFIG_MAX_NUM_ROWS; i++) {
            uint32_t start = ac_ib_get(ib);
            fprintf(f, "    tile group[%u] start = %u\n", i, start);
            uint32_t end = ac_ib_get(ib);
            fprintf(f, "    tile group[%u] end = %u\n", i, end);
         }
         uint32_t tile_id_mode = ac_ib_get(ib);
         fprintf(f, "    context update tile id mode = %s\n",
                 tile_id_mode == RENCODE_AV1_CONTEXT_UPDATE_TILE_ID_MODE_CUSTOMIZED ? "CUSTOMIZED" :
                 tile_id_mode == RENCODE_AV1_CONTEXT_UPDATE_TILE_ID_MODE_DEFAULT ? "DEFAULT" :
                 "???");
         uint32_t tile_id = ac_ib_get(ib);
         fprintf(f, "    context_update_tile_id = %u\n", tile_id);
         uint32_t tile_size_bytes = ac_ib_get(ib);
         fprintf(f, "    tile_size_bytes_minus_1 = %u\n", tile_size_bytes);
      } else if (op == cmd.enc_params_av1) {
         fprintf(f, "%sAV1_ENCODE_PARAMS%s\n", O_COLOR_GREEN, O_COLOR_RESET);
         for (uint32_t i = 0; i < RENCODE_AV1_REFS_PER_FRAME; i++) {
            uint32_t ref = ac_ib_get(ib);
            fprintf(f, "    ref frame[%u] = %u\n", i, ref);
         }
         for (uint32_t i = 0; i < 2; i++) {
            uint32_t idx = ac_ib_get(ib);
            fprintf(f, "    lsm reference frame index[%u] = %u\n", i, idx);
         }
      } else {
         fprintf(f, "%sUNRECOGNIZED%s\n", O_COLOR_RED, O_COLOR_RESET);
      }
      print_vcn_unrecognized_params(f, ib, start_dw, size);
   }
}

static void parse_vcn_ib(FILE *f, struct ac_ib_parser *ib)
{
   uint32_t engine = 0;

   if (ib->vcn_version >= VCN_4_0_0) {
      while (ib->cur_dw < ib->num_dw) {
         const uint32_t start_dw = ib->cur_dw;
         const uint32_t size = ac_ib_get(ib);
         const uint32_t op = ac_ib_get(ib);

         switch (op) {
         case RADEON_VCN_ENGINE_INFO: {
            fprintf(f, "%sENGINE_INFO%s\n", O_COLOR_CYAN, O_COLOR_RESET);
            engine = ac_ib_get(ib);
            fprintf(f, "    engine = %s\n",
                    engine == RADEON_VCN_ENGINE_TYPE_COMMON ? "COMMON" :
                    engine == RADEON_VCN_ENGINE_TYPE_ENCODE ? "ENCODE" :
                    engine == RADEON_VCN_ENGINE_TYPE_DECODE ? "DECODE" :
                    "???");
            uint32_t total_size = ac_ib_get(ib);
            fprintf(f, "    size of all packages = %u\n", total_size);
            break;
         }
         case RADEON_VCN_SIGNATURE: {
            fprintf(f, "%sSIGNATURE%s\n", O_COLOR_CYAN, O_COLOR_RESET);
            ac_ib_get(ib);
            fprintf(f, "    checksum\n");
            uint32_t num_dwords = ac_ib_get(ib);
            fprintf(f, "    num dwords = %u\n", num_dwords);
            break;
         }
         case RDECODE_IB_PARAM_DECODE_BUFFER: {
            fprintf(f, "%sDECODE_BUFFER%s\n", O_COLOR_GREEN, O_COLOR_RESET);
            uint32_t valid = ac_ib_get(ib);
            fprintf(f, "      valid =\n");
            for (uint32_t i = 0; i < 32; i++) {
               uint32_t buf = 1 << i;
               if ((valid & buf) != buf)
                  continue;
               fprintf(f, "              ");
               switch (buf) {
               case RDECODE_CMDBUF_FLAGS_MSG_BUFFER:
                  fprintf(f, "MSG BUFFER\n");
                  break;
               case RDECODE_CMDBUF_FLAGS_DPB_BUFFER:
                  fprintf(f, "DPB BUFFER\n");
                  break;
               case RDECODE_CMDBUF_FLAGS_BITSTREAM_BUFFER:
                  fprintf(f, "BITSTREAM BUFFER\n");
                  break;
               case RDECODE_CMDBUF_FLAGS_DECODING_TARGET_BUFFER:
                  fprintf(f, "DECODING TARGET BUFFER\n");
                  break;
               case RDECODE_CMDBUF_FLAGS_FEEDBACK_BUFFER:
                  fprintf(f, "FEEDBACK BUFFER\n");
                  break;
               case RDECODE_CMDBUF_FLAGS_PICTURE_PARAM_BUFFER:
                  fprintf(f, "PICTURE PARAM BUFFER\n");
                  break;
               case RDECODE_CMDBUF_FLAGS_MB_CONTROL_BUFFER:
                  fprintf(f, "MB CONTROL BUFFER\n");
                  break;
               case RDECODE_CMDBUF_FLAGS_IDCT_COEF_BUFFER:
                  fprintf(f, "IDCT COEFF BUFFER\n");
                  break;
               case RDECODE_CMDBUF_FLAGS_PREEMPT_BUFFER:
                  fprintf(f, "PREEMPT BUFFER\n");
                  break;
               case RDECODE_CMDBUF_FLAGS_IT_SCALING_BUFFER:
                  fprintf(f, "IT SCALING BUFFER\n");
                  break;
               case RDECODE_CMDBUF_FLAGS_SCALER_TARGET_BUFFER:
                  fprintf(f, "SCALER TARGET BUFFER\n");
                  break;
               case RDECODE_CMDBUF_FLAGS_CONTEXT_BUFFER:
                  fprintf(f, "CONTEXT BUFFER\n");
                  break;
               case RDECODE_CMDBUF_FLAGS_PROB_TBL_BUFFER:
                  fprintf(f, "PROB TBL BUFFER\n");
                  break;
               case RDECODE_CMDBUF_FLAGS_QUERY_BUFFER:
                  fprintf(f, "QUERY BUFFER\n");
                  break;
               case RDECODE_CMDBUF_FLAGS_PREDICATION_BUFFER:
                  fprintf(f, "PREDICATION BUFFER\n");
                  break;
               case RDECODE_CMDBUF_FLAGS_SCLR_COEF_BUFFER:
                  fprintf(f, "SCRL COEF BUFFER\n");
                  break;
               case RDECODE_CMDBUF_FLAGS_RECORD_TIMESTAMP:
                  fprintf(f, "RECORD TIMESTAMP\n");
                  break;
               case RDECODE_CMDBUF_FLAGS_REPORT_EVENT_STATUS:
                  fprintf(f, "REPORT EVENT STATUS\n");
                  break;
               case RDECODE_CMDBUF_FLAGS_RESERVED_SIZE_INFO_BUFFER:
                  fprintf(f, "RESERVED SIZE INFO BUFFER\n");
                  break;
               case RDECODE_CMDBUF_FLAGS_LUMA_HIST_BUFFER:
                  fprintf(f, "LUMA HIST BUFFER\n");
                  break;
               case RDECODE_CMDBUF_FLAGS_SESSION_CONTEXT_BUFFER:
                  fprintf(f, "SESSION CONTEXT BUFFER\n");
                  break;
               default:
                  fprintf(f, "%s(UNRECOGNIZED)%s\n", O_COLOR_RED, O_COLOR_RESET);
                  break;
               }
            }
            print_vcn_addr(f, ib, "    msg buffer");
            print_vcn_addr(f, ib, "    dpb buffer");
            print_vcn_addr(f, ib, "    target buffer");
            print_vcn_addr(f, ib, "    session context buffer");
            print_vcn_addr(f, ib, "    bitstream buffer");
            print_vcn_addr(f, ib, "    context buffer");
            print_vcn_addr(f, ib, "    feedback buffer");
            print_vcn_addr(f, ib, "    luma hist buffer");
            print_vcn_addr(f, ib, "    prob tbl buffer");
            print_vcn_addr(f, ib, "    sclr coeff buffer");
            print_vcn_addr(f, ib, "    it sclr table buffer");
            print_vcn_addr(f, ib, "    sclr target buffer");
            print_vcn_addr(f, ib, "    reserved size info buffer");
            print_vcn_addr(f, ib, "    mpeg2 pic param buffer");
            print_vcn_addr(f, ib, "    mpeg2 mb control buffer");
            print_vcn_addr(f, ib, "    mpeg2 idct coeff buffer");
            break;
         }
         default:
            fprintf(f, "%sUNRECOGNIZED%s\n", O_COLOR_RED, O_COLOR_RESET);
            break;
         }
         print_vcn_unrecognized_params(f, ib, start_dw, size);

         if (engine == RADEON_VCN_ENGINE_TYPE_ENCODE) {
            parse_vcn_enc_ib(f, ib);
            return;
         }
      }
   } else {
      if (ib->ip_type == AMD_IP_VCN_ENC) {
         parse_vcn_enc_ib(f, ib);
         return;
      }
   }
}

/**
 * Parse and print an IB into a file.
 *
 * \param f            file
 * \param ib_ptr       IB
 * \param num_dw       size of the IB
 * \param gfx_level   gfx level
 * \param vcn_version vcn version
 * \param family	chip family
 * \param ip_type IP type
 * \param trace_ids	the last trace IDs that are known to have been reached
 *			and executed by the CP, typically read from a buffer
 * \param trace_id_count The number of entries in the trace_ids array.
 * \param addr_callback Get a mapped pointer of the IB at a given address. Can
 *                      be NULL.
 * \param addr_callback_data user data for addr_callback
 */
void ac_parse_ib_chunk(struct ac_ib_parser *ib)
{
   struct ac_ib_parser tmp_ib = *ib;

   char *out;
   size_t outsize;
   struct u_memstream mem;
   u_memstream_open(&mem, &out, &outsize);
   FILE *const memf = u_memstream_get(&mem);
   tmp_ib.f = memf;

   if (ib->ip_type == AMD_IP_GFX || ib->ip_type == AMD_IP_COMPUTE)
      parse_gfx_compute_ib(memf, &tmp_ib);
   else if (ib->ip_type == AMD_IP_SDMA)
      parse_sdma_ib(memf, &tmp_ib);
   else if (ib->ip_type == AMD_IP_VCN_DEC || ib->ip_type == AMD_IP_VCN_ENC)
      parse_vcn_ib(memf, &tmp_ib);
   else
      unreachable("unsupported IP type");

   u_memstream_close(&mem);

   if (out) {
      format_ib_output(ib->f, out);
      free(out);
   }

   if (tmp_ib.cur_dw > tmp_ib.num_dw) {
      printf("\nPacket ends after the end of IB.\n");
      exit(1);
   }
}

/**
 * Parse and print an IB into a file.
 *
 * \param f		file
 * \param ib		IB
 * \param num_dw	size of the IB
 * \param gfx_level	gfx level
 * \param family	chip family
 * \param ip_type IP type
 * \param trace_ids	the last trace IDs that are known to have been reached
 *			and executed by the CP, typically read from a buffer
 * \param trace_id_count The number of entries in the trace_ids array.
 * \param addr_callback Get a mapped pointer of the IB at a given address. Can
 *                      be NULL.
 * \param addr_callback_data user data for addr_callback
 */
void ac_parse_ib(struct ac_ib_parser *ib, const char *name)
{
   fprintf(ib->f, "------------------ %s begin - %s ------------------\n", name,
           ac_get_ip_type_string(NULL, ib->ip_type));

   ac_parse_ib_chunk(ib);

   fprintf(ib->f, "------------------- %s end - %s -------------------\n\n", name,
           ac_get_ip_type_string(NULL, ib->ip_type));
}
