/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_pipe.h"
#include "tgsi/tgsi_text.h"
#include "tgsi/tgsi_ureg.h"

/* Create the compute shader that is used to collect the results.
 *
 * One compute grid with a single thread is launched for every query result
 * buffer. The thread (optionally) reads a previous summary buffer, then
 * accumulates data from the query result buffer, and writes the result either
 * to a summary buffer to be consumed by the next grid invocation or to the
 * user-supplied buffer.
 *
 * Data layout:
 *
 * CONST
 *  0.x = end_offset
 *  0.y = result_stride
 *  0.z = result_count
 *  0.w = bit field:
 *          1: read previously accumulated values
 *          2: write accumulated values for chaining
 *          4: write result available
 *          8: convert result to boolean (0/1)
 *         16: only read one dword and use that as result
 *         32: apply timestamp conversion
 *         64: store full 64 bits result
 *        128: store signed 32 bits result
 *        256: SO_OVERFLOW mode: take the difference of two successive half-pairs
 *  1.x = fence_offset
 *  1.y = pair_stride
 *  1.z = pair_count
 *
 * BUFFER[0] = query result buffer
 * BUFFER[1] = previous summary buffer
 * BUFFER[2] = next summary buffer or user-supplied buffer
 */
void *si_create_query_result_cs(struct si_context *sctx)
{
   /* TEMP[0].xy = accumulated result so far
    * TEMP[0].z = result not available
    *
    * TEMP[1].x = current result index
    * TEMP[1].y = current pair index
    */
   static const char text_tmpl[] =
      "COMP\n"
      "PROPERTY CS_FIXED_BLOCK_WIDTH 1\n"
      "PROPERTY CS_FIXED_BLOCK_HEIGHT 1\n"
      "PROPERTY CS_FIXED_BLOCK_DEPTH 1\n"
      "DCL BUFFER[0]\n"
      "DCL BUFFER[1]\n"
      "DCL BUFFER[2]\n"
      "DCL CONST[0][0..1]\n"
      "DCL TEMP[0..5]\n"
      "IMM[0] UINT32 {0, 31, 2147483647, 4294967295}\n"
      "IMM[1] UINT32 {1, 2, 4, 8}\n"
      "IMM[2] UINT32 {16, 32, 64, 128}\n"
      "IMM[3] UINT32 {1000000, 0, %u, 0}\n" /* for timestamp conversion */
      "IMM[4] UINT32 {256, 0, 0, 0}\n"

      "AND TEMP[5], CONST[0][0].wwww, IMM[2].xxxx\n"
      "UIF TEMP[5]\n"
      /* Check result availability. */
      "LOAD TEMP[1].x, BUFFER[0], CONST[0][1].xxxx\n"
      "ISHR TEMP[0].z, TEMP[1].xxxx, IMM[0].yyyy\n"
      "MOV TEMP[1], TEMP[0].zzzz\n"
      "NOT TEMP[0].z, TEMP[0].zzzz\n"

      /* Load result if available. */
      "UIF TEMP[1]\n"
      "LOAD TEMP[0].xy, BUFFER[0], IMM[0].xxxx\n"
      "ENDIF\n"
      "ELSE\n"
      /* Load previously accumulated result if requested. */
      "MOV TEMP[0], IMM[0].xxxx\n"
      "AND TEMP[4], CONST[0][0].wwww, IMM[1].xxxx\n"
      "UIF TEMP[4]\n"
      "LOAD TEMP[0].xyz, BUFFER[1], IMM[0].xxxx\n"
      "ENDIF\n"

      "MOV TEMP[1].x, IMM[0].xxxx\n"
      "BGNLOOP\n"
      /* Break if accumulated result so far is not available. */
      "UIF TEMP[0].zzzz\n"
      "BRK\n"
      "ENDIF\n"

      /* Break if result_index >= result_count. */
      "USGE TEMP[5], TEMP[1].xxxx, CONST[0][0].zzzz\n"
      "UIF TEMP[5]\n"
      "BRK\n"
      "ENDIF\n"

      /* Load fence and check result availability */
      "UMAD TEMP[5].x, TEMP[1].xxxx, CONST[0][0].yyyy, CONST[0][1].xxxx\n"
      "LOAD TEMP[5].x, BUFFER[0], TEMP[5].xxxx\n"
      "ISHR TEMP[0].z, TEMP[5].xxxx, IMM[0].yyyy\n"
      "NOT TEMP[0].z, TEMP[0].zzzz\n"
      "UIF TEMP[0].zzzz\n"
      "BRK\n"
      "ENDIF\n"

      "MOV TEMP[1].y, IMM[0].xxxx\n"
      "BGNLOOP\n"
      /* Load start and end. */
      "UMUL TEMP[5].x, TEMP[1].xxxx, CONST[0][0].yyyy\n"
      "UMAD TEMP[5].x, TEMP[1].yyyy, CONST[0][1].yyyy, TEMP[5].xxxx\n"
      "LOAD TEMP[2].xy, BUFFER[0], TEMP[5].xxxx\n"

      "UADD TEMP[5].y, TEMP[5].xxxx, CONST[0][0].xxxx\n"
      "LOAD TEMP[3].xy, BUFFER[0], TEMP[5].yyyy\n"

      "U64ADD TEMP[4].xy, TEMP[3], -TEMP[2]\n"

      "AND TEMP[5].z, CONST[0][0].wwww, IMM[4].xxxx\n"
      "UIF TEMP[5].zzzz\n"
      /* Load second start/end half-pair and
       * take the difference
       */
      "UADD TEMP[5].xy, TEMP[5], IMM[1].wwww\n"
      "LOAD TEMP[2].xy, BUFFER[0], TEMP[5].xxxx\n"
      "LOAD TEMP[3].xy, BUFFER[0], TEMP[5].yyyy\n"

      "U64ADD TEMP[3].xy, TEMP[3], -TEMP[2]\n"
      "U64ADD TEMP[4].xy, TEMP[4], -TEMP[3]\n"
      "ENDIF\n"

      "U64ADD TEMP[0].xy, TEMP[0], TEMP[4]\n"

      /* Increment pair index */
      "UADD TEMP[1].y, TEMP[1].yyyy, IMM[1].xxxx\n"
      "USGE TEMP[5], TEMP[1].yyyy, CONST[0][1].zzzz\n"
      "UIF TEMP[5]\n"
      "BRK\n"
      "ENDIF\n"
      "ENDLOOP\n"

      /* Increment result index */
      "UADD TEMP[1].x, TEMP[1].xxxx, IMM[1].xxxx\n"
      "ENDLOOP\n"
      "ENDIF\n"

      "AND TEMP[4], CONST[0][0].wwww, IMM[1].yyyy\n"
      "UIF TEMP[4]\n"
      /* Store accumulated data for chaining. */
      "STORE BUFFER[2].xyz, IMM[0].xxxx, TEMP[0]\n"
      "ELSE\n"
      "AND TEMP[4], CONST[0][0].wwww, IMM[1].zzzz\n"
      "UIF TEMP[4]\n"
      /* Store result availability. */
      "NOT TEMP[0].z, TEMP[0]\n"
      "AND TEMP[0].z, TEMP[0].zzzz, IMM[1].xxxx\n"
      "STORE BUFFER[2].x, IMM[0].xxxx, TEMP[0].zzzz\n"

      "AND TEMP[4], CONST[0][0].wwww, IMM[2].zzzz\n"
      "UIF TEMP[4]\n"
      "STORE BUFFER[2].y, IMM[0].xxxx, IMM[0].xxxx\n"
      "ENDIF\n"
      "ELSE\n"
      /* Store result if it is available. */
      "NOT TEMP[4], TEMP[0].zzzz\n"
      "UIF TEMP[4]\n"
      /* Apply timestamp conversion */
      "AND TEMP[4], CONST[0][0].wwww, IMM[2].yyyy\n"
      "UIF TEMP[4]\n"
      "U64MUL TEMP[0].xy, TEMP[0], IMM[3].xyxy\n"
      "U64DIV TEMP[0].xy, TEMP[0], IMM[3].zwzw\n"
      "ENDIF\n"

      /* Convert to boolean */
      "AND TEMP[4], CONST[0][0].wwww, IMM[1].wwww\n"
      "UIF TEMP[4]\n"
      "U64SNE TEMP[0].x, TEMP[0].xyxy, IMM[4].zwzw\n"
      "AND TEMP[0].x, TEMP[0].xxxx, IMM[1].xxxx\n"
      "MOV TEMP[0].y, IMM[0].xxxx\n"
      "ENDIF\n"

      "AND TEMP[4], CONST[0][0].wwww, IMM[2].zzzz\n"
      "UIF TEMP[4]\n"
      "STORE BUFFER[2].xy, IMM[0].xxxx, TEMP[0].xyxy\n"
      "ELSE\n"
      /* Clamping */
      "UIF TEMP[0].yyyy\n"
      "MOV TEMP[0].x, IMM[0].wwww\n"
      "ENDIF\n"

      "AND TEMP[4], CONST[0][0].wwww, IMM[2].wwww\n"
      "UIF TEMP[4]\n"
      "UMIN TEMP[0].x, TEMP[0].xxxx, IMM[0].zzzz\n"
      "ENDIF\n"

      "STORE BUFFER[2].x, IMM[0].xxxx, TEMP[0].xxxx\n"
      "ENDIF\n"
      "ENDIF\n"
      "ENDIF\n"
      "ENDIF\n"

      "END\n";

   char text[sizeof(text_tmpl) + 32];
   struct tgsi_token tokens[1024];
   struct pipe_compute_state state = {};

   /* Hard code the frequency into the shader so that the backend can
    * use the full range of optimizations for divide-by-constant.
    */
   snprintf(text, sizeof(text), text_tmpl, sctx->screen->info.clock_crystal_freq);

   if (!tgsi_text_translate(text, tokens, ARRAY_SIZE(tokens))) {
      assert(false);
      return NULL;
   }

   state.ir_type = PIPE_SHADER_IR_TGSI;
   state.prog = tokens;

   return sctx->b.create_compute_state(&sctx->b, &state);
}

/* Create the compute shader that is used to collect the results of gfx10+
 * shader queries.
 *
 * One compute grid with a single thread is launched for every query result
 * buffer. The thread (optionally) reads a previous summary buffer, then
 * accumulates data from the query result buffer, and writes the result either
 * to a summary buffer to be consumed by the next grid invocation or to the
 * user-supplied buffer.
 *
 * Data layout:
 *
 * BUFFER[0] = query result buffer (layout is defined by gfx10_sh_query_buffer_mem)
 * BUFFER[1] = previous summary buffer
 * BUFFER[2] = next summary buffer or user-supplied buffer
 *
 * CONST
 *  0.x = config; the low 3 bits indicate the mode:
 *          0: sum up counts
 *          1: determine result availability and write it as a boolean
 *          2: SO_OVERFLOW
 *          3: SO_ANY_OVERFLOW
 *        the remaining bits form a bitfield:
 *          8: write result as a 64-bit value
 *  0.y = offset in bytes to counts or stream for SO_OVERFLOW mode
 *  0.z = chain bit field:
 *          1: have previous summary buffer
 *          2: write next summary buffer
 *  0.w = result_count
 */
void *gfx11_create_sh_query_result_cs(struct si_context *sctx)
{
   /* TEMP[0].x = accumulated result so far
    * TEMP[0].y = result missing
    * TEMP[0].z = whether we're in overflow mode
    */
   static const char text_tmpl[] =
         "COMP\n"
         "PROPERTY CS_FIXED_BLOCK_WIDTH 1\n"
         "PROPERTY CS_FIXED_BLOCK_HEIGHT 1\n"
         "PROPERTY CS_FIXED_BLOCK_DEPTH 1\n"
         "DCL BUFFER[0]\n"
         "DCL BUFFER[1]\n"
         "DCL BUFFER[2]\n"
         "DCL CONST[0][0..0]\n"
         "DCL TEMP[0..5]\n"
         "IMM[0] UINT32 {0, 7, 256, 4294967295}\n"
         "IMM[1] UINT32 {1, 2, 4, 8}\n"
         "IMM[2] UINT32 {16, 32, 64, 128}\n"

         /* acc_result = 0;
          * acc_missing = 0;
          */
         "MOV TEMP[0].xy, IMM[0].xxxx\n"

         /* if (chain & 1) {
          *    acc_result = buffer[1][0];
          *    acc_missing = buffer[1][1];
          * }
          */
         "AND TEMP[5], CONST[0][0].zzzz, IMM[1].xxxx\n"
         "UIF TEMP[5]\n"
         "LOAD TEMP[0].xy, BUFFER[1], IMM[0].xxxx\n"
         "ENDIF\n"

         /* is_overflow (TEMP[0].z) = (config & 7) >= 2; */
         "AND TEMP[5].x, CONST[0][0].xxxx, IMM[0].yyyy\n"
         "USGE TEMP[0].z, TEMP[5].xxxx, IMM[1].yyyy\n"

         /* result_remaining (TEMP[1].x) = (is_overflow && acc_result) ? 0 : result_count; */
         "AND TEMP[5].x, TEMP[0].zzzz, TEMP[0].xxxx\n"
         "UCMP TEMP[1].x, TEMP[5].xxxx, IMM[0].xxxx, CONST[0][0].wwww\n"

         /* base_offset (TEMP[1].y) = 0; */
         "MOV TEMP[1].y, IMM[0].xxxx\n"

         /* for (;;) {
          *    if (!result_remaining) {
          *       break;
          *    }
          *    result_remaining--;
          */
         "BGNLOOP\n"
         "  USEQ TEMP[5], TEMP[1].xxxx, IMM[0].xxxx\n"
         "  UIF TEMP[5]\n"
         "     BRK\n"
         "  ENDIF\n"
         "  UADD TEMP[1].x, TEMP[1].xxxx, IMM[0].wwww\n"

         /*    fence = buffer[0]@(base_offset + sizeof(gfx10_sh_query_buffer_mem.stream)); */
         "  UADD TEMP[5].x, TEMP[1].yyyy, IMM[2].wwww\n"
         "  LOAD TEMP[5].x, BUFFER[0], TEMP[5].xxxx\n"

         /*    if (!fence) {
          *       acc_missing = ~0u;
          *       break;
          *    }
          */
         "  USEQ TEMP[5], TEMP[5].xxxx, IMM[0].xxxx\n"
         "  UIF TEMP[5]\n"
         "     MOV TEMP[0].y, TEMP[5].xxxx\n"
         "     BRK\n"
         "  ENDIF\n"

         /*    stream_offset (TEMP[2].x) = base_offset + offset; */
         "  UADD TEMP[2].x, TEMP[1].yyyy, CONST[0][0].yyyy\n"

         /*    if (!(config & 7)) {
          *       acc_result += buffer[0]@stream_offset;
          *    }
          */
         "  AND TEMP[5].x, CONST[0][0].xxxx, IMM[0].yyyy\n"
         "  USEQ TEMP[5], TEMP[5].xxxx, IMM[0].xxxx\n"
         "  UIF TEMP[5]\n"
         "     LOAD TEMP[5].x, BUFFER[0], TEMP[2].xxxx\n"
         "     UADD TEMP[0].x, TEMP[0].xxxx, TEMP[5].xxxx\n"
         "  ENDIF\n"

         /*    if ((config & 7) >= 2) {
          *       count (TEMP[2].y) = (config & 1) ? 4 : 1;
          */
         "  AND TEMP[5].x, CONST[0][0].xxxx, IMM[0].yyyy\n"
         "  USGE TEMP[5], TEMP[5].xxxx, IMM[1].yyyy\n"
         "  UIF TEMP[5]\n"
         "     AND TEMP[5].x, CONST[0][0].xxxx, IMM[1].xxxx\n"
         "     UCMP TEMP[2].y, TEMP[5].xxxx, IMM[1].zzzz, IMM[1].xxxx\n"

         /*       do {
          *          generated = buffer[0]@(stream_offset + 2 * sizeof(uint64_t));
          *          emitted = buffer[0]@(stream_offset + 3 * sizeof(uint64_t));
          *          if (generated != emitted) {
          *             acc_result = 1;
          *             result_remaining = 0;
          *             break;
          *          }
          *
          *          stream_offset += sizeof(gfx10_sh_query_buffer_mem.stream[0]);
          *       } while (--count);
          *    }
          */
         "     BGNLOOP\n"
         "        UADD TEMP[5].x, TEMP[2].xxxx, IMM[2].xxxx\n"
         "        LOAD TEMP[4].xyzw, BUFFER[0], TEMP[5].xxxx\n"
         "        USNE TEMP[5], TEMP[4].xyxy, TEMP[4].zwzw\n"
         "        UIF TEMP[5]\n"
         "           MOV TEMP[0].x, IMM[1].xxxx\n"
         "           MOV TEMP[1].y, IMM[0].xxxx\n"
         "           BRK\n"
         "        ENDIF\n"

         "        UADD TEMP[2].y, TEMP[2].yyyy, IMM[0].wwww\n"
         "        USEQ TEMP[5], TEMP[2].yyyy, IMM[0].xxxx\n"
         "        UIF TEMP[5]\n"
         "           BRK\n"
         "        ENDIF\n"
         "        UADD TEMP[2].x, TEMP[2].xxxx, IMM[2].yyyy\n"
         "     ENDLOOP\n"
         "  ENDIF\n"

         /*    base_offset += sizeof(gfx10_sh_query_buffer_mem);
          * } // end outer loop
          */
         "  UADD TEMP[1].y, TEMP[1].yyyy, IMM[0].zzzz\n"
         "ENDLOOP\n"

         /* if (chain & 2) {
          *    buffer[2][0] = acc_result;
          *    buffer[2][1] = acc_missing;
          * } else {
          */
         "AND TEMP[5], CONST[0][0].zzzz, IMM[1].yyyy\n"
         "UIF TEMP[5]\n"
         "  STORE BUFFER[2].xy, IMM[0].xxxx, TEMP[0]\n"
         "ELSE\n"

         /*    if ((config & 7) == 1) {
          *       acc_result = acc_missing ? 0 : 1;
          *       acc_missing = 0;
          *    }
          */
         "  AND TEMP[5], CONST[0][0].xxxx, IMM[0].yyyy\n"
         "  USEQ TEMP[5], TEMP[5].xxxx, IMM[1].xxxx\n"
         "  UIF TEMP[5]\n"
         "     UCMP TEMP[0].x, TEMP[0].yyyy, IMM[0].xxxx, IMM[1].xxxx\n"
         "     MOV TEMP[0].y, IMM[0].xxxx\n"
         "  ENDIF\n"

         /*    if (!acc_missing) {
          *       buffer[2][0] = acc_result;
          *       if (config & 8) {
          *          buffer[2][1] = 0;
          *       }
          *    }
          * }
          */
         "  USEQ TEMP[5], TEMP[0].yyyy, IMM[0].xxxx\n"
         "  UIF TEMP[5]\n"
         "     STORE BUFFER[2].x, IMM[0].xxxx, TEMP[0].xxxx\n"
         "     AND TEMP[5], CONST[0][0].xxxx, IMM[1].wwww\n"
         "     UIF TEMP[5]\n"
         "        STORE BUFFER[2].x, IMM[1].zzzz, TEMP[0].yyyy\n"
         "     ENDIF\n"
         "  ENDIF\n"
         "ENDIF\n"
         "END\n";

   struct tgsi_token tokens[1024];
   struct pipe_compute_state state = {};

   if (!tgsi_text_translate(text_tmpl, tokens, ARRAY_SIZE(tokens))) {
      assert(false);
      return NULL;
   }

   state.ir_type = PIPE_SHADER_IR_TGSI;
   state.prog = tokens;

   return sctx->b.create_compute_state(&sctx->b, &state);
}
