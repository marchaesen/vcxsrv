/*
 * Copyright (c) 2018 Lima Project
 *
 * Copyright (c) 2013 Codethink (http://www.codethink.co.uk)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include "gpir.h"
#include "codegen.h"

typedef enum {
   unit_acc_0,
   unit_acc_1,
   unit_mul_0,
   unit_mul_1,
   unit_pass,
   unit_complex,
   num_units
} gp_unit;

static const gpir_codegen_store_src gp_unit_to_store_src[num_units] = {
   [unit_acc_0] = gpir_codegen_store_src_acc_0,
   [unit_acc_1] = gpir_codegen_store_src_acc_1,
   [unit_mul_0] = gpir_codegen_store_src_mul_0,
   [unit_mul_1] = gpir_codegen_store_src_mul_1,
   [unit_pass] = gpir_codegen_store_src_pass,
   [unit_complex] = gpir_codegen_store_src_complex,
};

static void
print_dest(gpir_codegen_instr *instr, gp_unit unit, unsigned cur_dest_index)
{
   printf("^%u", cur_dest_index + unit);

   gpir_codegen_store_src src = gp_unit_to_store_src[unit];

   if (instr->store0_src_x == src ||
       instr->store0_src_y == src) {
      if (instr->store0_temporary) {
         /* Temporary stores ignore the address, and always use whatever's
          * stored in address register 0.
          */
         printf("/t[addr0]");
      } else {
         if (instr->store0_varying)
            printf("/v");
         else
            printf("/$");
         printf("%u", instr->store0_addr);
      }

      printf(".");
      if (instr->store0_src_x == src)
         printf("x");
      if (instr->store0_src_y == src)
         printf("y");
   }

   if (instr->store1_src_z == src ||
       instr->store1_src_w == src) {
      if (instr->store1_temporary) {
         printf("/t[addr0]");
      } else {
         if (instr->store1_varying)
            printf("/v");
         else
            printf("/$");
         printf("%u", instr->store1_addr);
      }

      printf(".");
      if (instr->store1_src_z == src)
         printf("z");
      if (instr->store1_src_w == src)
         printf("w");
   }

   if (unit == unit_complex) {
      switch (instr->complex_op) {
      case gpir_codegen_complex_op_temp_store_addr:
         printf("/addr0");
         break;
      case gpir_codegen_complex_op_temp_load_addr_0:
         printf("/addr1");
         break;
      case gpir_codegen_complex_op_temp_load_addr_1:
         printf("/addr2");
         break;
      case gpir_codegen_complex_op_temp_load_addr_2:
         printf("/addr3");
         break;
      default:
         break;
      }
   }
}

static void
print_src(gpir_codegen_src src, gp_unit unit, unsigned unit_src_num,
          gpir_codegen_instr *instr, gpir_codegen_instr *prev_instr,
          unsigned cur_dest_index)
{
   switch (src) {
   case gpir_codegen_src_attrib_x:
   case gpir_codegen_src_attrib_y:
   case gpir_codegen_src_attrib_z:
   case gpir_codegen_src_attrib_w:
      printf("%c%d.%c", instr->register0_attribute ? 'a' : '$',
             instr->register0_addr, "xyzw"[src - gpir_codegen_src_attrib_x]);
      break;

   case gpir_codegen_src_register_x:
   case gpir_codegen_src_register_y:
   case gpir_codegen_src_register_z:
   case gpir_codegen_src_register_w:
      printf("$%d.%c", instr->register1_addr,
             "xyzw"[src - gpir_codegen_src_register_x]);
      break;

   case gpir_codegen_src_unknown_0:
   case gpir_codegen_src_unknown_1:
   case gpir_codegen_src_unknown_2:
   case gpir_codegen_src_unknown_3:
      printf("unknown%d", src - gpir_codegen_src_unknown_0);
      break;

   case gpir_codegen_src_load_x:
   case gpir_codegen_src_load_y:
   case gpir_codegen_src_load_z:
   case gpir_codegen_src_load_w:
      printf("t[%d", instr->load_addr);
      switch (instr->load_offset) {
      case gpir_codegen_load_off_ld_addr_0:
         printf("+addr1");
         break;
      case gpir_codegen_load_off_ld_addr_1:
         printf("+addr2");
         break;
      case gpir_codegen_load_off_ld_addr_2:
         printf("+addr3");
         break;
      case gpir_codegen_load_off_none:
         break;
      default:
         printf("+unk%d", instr->load_offset);
      }
      printf("].%c", "xyzw"[src - gpir_codegen_src_load_x]);
      break;

   case gpir_codegen_src_p1_acc_0:
      printf("^%d", cur_dest_index - 1 * num_units + unit_acc_0);
      break;

   case gpir_codegen_src_p1_acc_1:
      printf("^%d", cur_dest_index - 1 * num_units + unit_acc_1);
      break;

   case gpir_codegen_src_p1_mul_0:
      printf("^%d", cur_dest_index - 1 * num_units + unit_mul_0);
      break;

   case gpir_codegen_src_p1_mul_1:
      printf("^%d", cur_dest_index - 1 * num_units + unit_mul_1);
      break;

   case gpir_codegen_src_p1_pass:
      printf("^%d", cur_dest_index - 1 * num_units + unit_pass);
      break;

   case gpir_codegen_src_unused:
      printf("unused");
      break;

   case gpir_codegen_src_p1_complex: /* Also ident */
      switch (unit) {
      case unit_acc_0:
      case unit_acc_1:
         if (unit_src_num == 1) {
            printf("0");
            return;
         }
         break;
      case unit_mul_0:
      case unit_mul_1:
         if (unit_src_num == 1) {
            printf("1");
            return;
         }
         break;
      default:
         break;
      }
      printf("^%d", cur_dest_index - 1 * num_units + unit_complex);
      break;

   case gpir_codegen_src_p2_pass:
      printf("^%d", cur_dest_index - 2 * num_units + unit_pass);
      break;

   case gpir_codegen_src_p2_acc_0:
      printf("^%d", cur_dest_index - 2 * num_units + unit_acc_0);
      break;

   case gpir_codegen_src_p2_acc_1:
      printf("^%d", cur_dest_index - 2 * num_units + unit_acc_1);
      break;

   case gpir_codegen_src_p2_mul_0:
      printf("^%d", cur_dest_index - 2 * num_units + unit_mul_0);
      break;

   case gpir_codegen_src_p2_mul_1:
      printf("^%d", cur_dest_index - 2 * num_units + unit_mul_1);
      break;

   case gpir_codegen_src_p1_attrib_x:
   case gpir_codegen_src_p1_attrib_y:
   case gpir_codegen_src_p1_attrib_z:
   case gpir_codegen_src_p1_attrib_w:
      printf("%c%d.%c", prev_instr->register0_attribute ? 'a' : '$',
             prev_instr->register0_addr,
             "xyzw"[src - gpir_codegen_src_p1_attrib_x]);
      break;
   }
}

static bool
print_mul(gpir_codegen_instr *instr, gpir_codegen_instr *prev_instr,
          unsigned cur_dest_index)
{
   bool printed = false;

   switch (instr->mul_op) {
   case gpir_codegen_mul_op_mul:
   case gpir_codegen_mul_op_complex2:
      if (instr->mul0_src0 != gpir_codegen_src_unused &&
          instr->mul0_src1 != gpir_codegen_src_unused) {
         printed = true;
         printf("\t");
         if (instr->mul0_src1 == gpir_codegen_src_ident &&
             !instr->mul0_neg) {
            printf("mov.m0 ");
            print_dest(instr, unit_mul_0, cur_dest_index);
            printf(" ");
            print_src(instr->mul0_src0, unit_mul_0, 0, instr, prev_instr,
                      cur_dest_index);
         } else {
            if (instr->mul_op == gpir_codegen_mul_op_complex2)
               printf("complex2.m0 ");
            else
               printf("mul.m0 ");

            print_dest(instr, unit_mul_0, cur_dest_index);
            printf(" ");
            print_src(instr->mul0_src0, unit_mul_0, 0, instr, prev_instr,
                      cur_dest_index);
            printf(" ");
            if (instr->mul0_neg)
               printf("-");
            print_src(instr->mul0_src1, unit_mul_0, 1, instr, prev_instr,
                      cur_dest_index);
         }

         printf("\n");
      }

      if (instr->mul1_src0 != gpir_codegen_src_unused &&
          instr->mul1_src1 != gpir_codegen_src_unused) {
         printed = true;
         printf("\t");
         if (instr->mul1_src1 == gpir_codegen_src_ident &&
             !instr->mul1_neg) {
            printf("mov.m1 ");
            print_dest(instr, unit_mul_1, cur_dest_index);
            printf(" ");
            print_src(instr->mul1_src0, unit_mul_1, 0, instr, prev_instr,
                      cur_dest_index);
         } else {
            printf("mul.m1 ");
            print_dest(instr, unit_mul_1, cur_dest_index);
            printf(" ");
            print_src(instr->mul1_src0, unit_mul_1, 0, instr, prev_instr,
                      cur_dest_index);
            printf(" ");
            if (instr->mul1_neg)
               printf("-");
            print_src(instr->mul1_src1, unit_mul_0, 1, instr, prev_instr,
                      cur_dest_index);
         }
         printf("\n");
      }

      break;
   case gpir_codegen_mul_op_complex1:
      printed = true;
      printf("\tcomplex1.m01 ");
      print_dest(instr, unit_mul_0, cur_dest_index);
      printf(" ");
      print_src(instr->mul0_src0, unit_mul_0, 0, instr, prev_instr,
                cur_dest_index);
      printf(" ");
      print_src(instr->mul0_src1, unit_mul_0, 1, instr, prev_instr,
                cur_dest_index);
      printf(" ");
      print_src(instr->mul1_src0, unit_mul_1, 0, instr, prev_instr,
                cur_dest_index);
      printf(" ");
      print_src(instr->mul1_src1, unit_mul_1, 1, instr, prev_instr,
                cur_dest_index);
      printf("\n");
      break;

   case gpir_codegen_mul_op_select:
      printed = true;
      printf("\tsel.m01 ");
      print_dest(instr, unit_mul_0, cur_dest_index);
      printf(" ");
      print_src(instr->mul0_src1, unit_mul_0, 1, instr, prev_instr,
                cur_dest_index);
      printf(" ");
      print_src(instr->mul0_src0, unit_mul_0, 0, instr, prev_instr,
                cur_dest_index);
      printf(" ");
      print_src(instr->mul1_src0, unit_mul_1, 0, instr, prev_instr,
                cur_dest_index);
      printf("\n");
      break;

   default:
      printed = true;
      printf("\tunknown%u.m01 ", instr->mul_op);
      print_dest(instr, unit_mul_0, cur_dest_index);
      printf(" ");
      print_src(instr->mul0_src0, unit_mul_0, 0, instr, prev_instr,
                cur_dest_index);
      printf(" ");
      print_src(instr->mul0_src1, unit_mul_0, 1, instr, prev_instr,
                cur_dest_index);
      printf(" ");
      print_src(instr->mul1_src0, unit_mul_1, 0, instr, prev_instr,
                cur_dest_index);
      printf(" ");
      print_src(instr->mul1_src1, unit_mul_1, 1, instr, prev_instr,
                cur_dest_index);
      printf("\n");
      break;
   }

   return printed;
}

typedef struct {
   const char *name;
   unsigned srcs;
} acc_op_info;

#define CASE(_name, _srcs) \
   [gpir_codegen_acc_op_##_name] = { \
      .name = #_name, \
      .srcs = _srcs \
   }

static const acc_op_info acc_op_infos[8] = {
   CASE(add, 2),
   CASE(floor, 1),
   CASE(sign, 1),
   CASE(ge, 2),
   CASE(lt, 2),
   CASE(min, 2),
   CASE(max, 2),
};

#undef CASE

static bool
print_acc(gpir_codegen_instr *instr, gpir_codegen_instr *prev_instr,
          unsigned cur_dest_index)
{
   bool printed = false;
   const acc_op_info op = acc_op_infos[instr->acc_op];

   if (instr->acc0_src0 != gpir_codegen_src_unused) {
      printed = true;
      printf("\t");
      acc_op_info acc0_op = op;
      if (instr->acc0_src1 == gpir_codegen_src_ident &&
          instr->acc0_src1_neg) {
         /* add x, -0 -> mov x */
         acc0_op.name = "mov";
         acc0_op.srcs = 1;
      }

      if (acc0_op.name)
         printf("%s.a0 ", acc0_op.name);
      else
         printf("op%u.a0 ", instr->acc_op);

      print_dest(instr, unit_acc_0, cur_dest_index);
      printf(" ");
      if (instr->acc0_src0_neg)
         printf("-");
      print_src(instr->acc0_src0, unit_acc_0, 0, instr, prev_instr,
                cur_dest_index);
      if (acc0_op.srcs > 1) {
         printf(" ");
         if (instr->acc0_src1_neg)
            printf("-");
         print_src(instr->acc0_src1, unit_acc_0, 1, instr, prev_instr,
                   cur_dest_index);
      }

      printf("\n");
   }

   if (instr->acc1_src0 != gpir_codegen_src_unused) {
      printed = true;
      printf("\t");
      acc_op_info acc1_op = op;
      if (instr->acc1_src1 == gpir_codegen_src_ident &&
          instr->acc1_src1_neg) {
         /* add x, -0 -> mov x */
         acc1_op.name = "mov";
         acc1_op.srcs = 1;
      }

      if (acc1_op.name)
         printf("%s.a1 ", acc1_op.name);
      else
         printf("op%u.a1 ", instr->acc_op);

      print_dest(instr, unit_acc_1, cur_dest_index);
      printf(" ");
      if (instr->acc1_src0_neg)
         printf("-");
      print_src(instr->acc1_src0, unit_acc_1, 0, instr, prev_instr,
                cur_dest_index);
      if (acc1_op.srcs > 1) {
         printf(" ");
         if (instr->acc1_src1_neg)
            printf("-");
         print_src(instr->acc1_src1, unit_acc_1, 1, instr, prev_instr,
                   cur_dest_index);
      }

      printf("\n");
   }

   return printed;
}

static bool
print_pass(gpir_codegen_instr *instr, gpir_codegen_instr *prev_instr,
           unsigned cur_dest_index)
{
   if (instr->pass_src == gpir_codegen_src_unused)
      return false;

   printf("\t");

   switch (instr->pass_op) {
   case gpir_codegen_pass_op_pass:
      printf("mov.p ");
      break;
   case gpir_codegen_pass_op_preexp2:
      printf("preexp2.p ");
      break;
   case gpir_codegen_pass_op_postlog2:
      printf("postlog2.p ");
      break;
   case gpir_codegen_pass_op_clamp:
      printf("clamp.p ");
      break;
   default:
      printf("unk%u.p ", instr->pass_op);
   }

   print_dest(instr, unit_pass, cur_dest_index);
   printf(" ");
   print_src(instr->pass_src, unit_pass, 0, instr, prev_instr,
             cur_dest_index);

   if (instr->pass_op == gpir_codegen_pass_op_clamp) {
      printf(" ");
      print_src(gpir_codegen_src_load_x, unit_pass, 1, instr, prev_instr,
                cur_dest_index);
      printf(" ");
      print_src(gpir_codegen_src_load_y, unit_pass, 2, instr, prev_instr,
                cur_dest_index);
   }

   printf("\n");

   return true;
}

static bool
print_complex(gpir_codegen_instr *instr, gpir_codegen_instr *prev_instr,
              unsigned cur_dest_index)
{
   if (instr->complex_src == gpir_codegen_src_unused)
      return false;

   printf("\t");

   switch (instr->complex_op) {
   case gpir_codegen_complex_op_nop:
      return false;

   case gpir_codegen_complex_op_exp2:
      printf("exp2.c ");
      break;
   case gpir_codegen_complex_op_log2:
      printf("log2.c ");
      break;
   case gpir_codegen_complex_op_rsqrt:
      printf("rsqrt.c ");
      break;
   case gpir_codegen_complex_op_rcp:
      printf("rcp.c ");
      break;
   case gpir_codegen_complex_op_pass:
   case gpir_codegen_complex_op_temp_store_addr:
   case gpir_codegen_complex_op_temp_load_addr_0:
   case gpir_codegen_complex_op_temp_load_addr_1:
   case gpir_codegen_complex_op_temp_load_addr_2:
      printf("mov.c ");
      break;
   default:
      printf("unk%u.c ", instr->complex_op);
   }

   print_dest(instr, unit_complex, cur_dest_index);
   printf(" ");
   print_src(instr->complex_src, unit_complex, 0, instr, prev_instr,
             cur_dest_index);
   printf("\n");

   return true;
}

static void
print_instr(gpir_codegen_instr *instr, gpir_codegen_instr *prev_instr,
            unsigned instr_number, unsigned cur_dest_index)
{
   bool printed = false;
   printf("%03d:", instr_number);
   printed |= print_acc(instr, prev_instr, cur_dest_index);
   printed |= print_mul(instr, prev_instr, cur_dest_index);
   printed |= print_complex(instr, prev_instr, cur_dest_index);
   printed |= print_pass(instr, prev_instr, cur_dest_index);

   if (instr->branch) {
      printed = true;
      /* The branch condition is taken from the current pass unit result */
      printf("\tbranch ^%d %03d\n", cur_dest_index + unit_pass,
             instr->branch_target + (instr->branch_target_lo ? 0 : 0x100));
   }

   if (instr->unknown_1 != 0) {
      printed = true;
      printf("\tunknown_1 %u\n", instr->unknown_1);
   }

   if (!printed)
      printf("\tnop\n");
}

void
gpir_disassemble_program(gpir_codegen_instr *code, unsigned num_instr)
{
   printf("=======disassembly:=======\n");

   unsigned cur_dest_index = 0;
   unsigned cur_instr = 0;
   for (gpir_codegen_instr *instr = code; cur_instr < num_instr;
        instr++, cur_instr++, cur_dest_index += num_units) {
      print_instr(instr, instr - 1, cur_instr, cur_dest_index);
   }
}

