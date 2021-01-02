/**************************************************************************
 * 
 * Copyright 2008 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/

/**
 * TGSI program transformation utility.
 *
 * Authors:  Brian Paul
 */

#include "util/u_debug.h"

#include "tgsi_transform.h"



static void
emit_instruction(struct tgsi_transform_context *ctx,
                 const struct tgsi_full_instruction *inst)
{
   uint ti = ctx->ti;

   ti += tgsi_build_full_instruction(inst,
                                     ctx->tokens_out + ti,
                                     ctx->header,
                                     ctx->max_tokens_out - ti);
   ctx->ti = ti;
}


static void
emit_declaration(struct tgsi_transform_context *ctx,
                 const struct tgsi_full_declaration *decl)
{
   uint ti = ctx->ti;

   ti += tgsi_build_full_declaration(decl,
                                     ctx->tokens_out + ti,
                                     ctx->header,
                                     ctx->max_tokens_out - ti);
   ctx->ti = ti;
}


static void
emit_immediate(struct tgsi_transform_context *ctx,
               const struct tgsi_full_immediate *imm)
{
   uint ti = ctx->ti;

   ti += tgsi_build_full_immediate(imm,
                                   ctx->tokens_out + ti,
                                   ctx->header,
                                   ctx->max_tokens_out - ti);
   ctx->ti = ti;
}


static void
emit_property(struct tgsi_transform_context *ctx,
              const struct tgsi_full_property *prop)
{
   uint ti = ctx->ti;

   ti += tgsi_build_full_property(prop,
                                  ctx->tokens_out + ti,
                                  ctx->header,
                                  ctx->max_tokens_out - ti);
   ctx->ti = ti;
}


/**
 * Apply user-defined transformations to the input shader to produce
 * the output shader.
 * For example, a register search-and-replace operation could be applied
 * by defining a transform_instruction() callback that examined and changed
 * the instruction src/dest regs.
 *
 * \return number of tokens emitted
 */
int
tgsi_transform_shader(const struct tgsi_token *tokens_in,
                      struct tgsi_token *tokens_out,
                      uint max_tokens_out,
                      struct tgsi_transform_context *ctx)
{
   uint procType;
   boolean first_instruction = TRUE;
   boolean epilog_emitted = FALSE;
   int cond_stack = 0;
   int call_stack = 0;

   /* input shader */
   struct tgsi_parse_context parse;

   /* output shader */
   struct tgsi_processor *processor;


   /**
    ** callback context init
    **/
   ctx->emit_instruction = emit_instruction;
   ctx->emit_declaration = emit_declaration;
   ctx->emit_immediate = emit_immediate;
   ctx->emit_property = emit_property;
   ctx->tokens_out = tokens_out;
   ctx->max_tokens_out = max_tokens_out;


   /**
    ** Setup to begin parsing input shader
    **/
   if (tgsi_parse_init( &parse, tokens_in ) != TGSI_PARSE_OK) {
      debug_printf("tgsi_parse_init() failed in tgsi_transform_shader()!\n");
      return -1;
   }
   procType = parse.FullHeader.Processor.Processor;

   /**
    **  Setup output shader
    **/
   ctx->header = (struct tgsi_header *)tokens_out;
   *ctx->header = tgsi_build_header();

   processor = (struct tgsi_processor *) (tokens_out + 1);
   *processor = tgsi_build_processor( procType, ctx->header );

   ctx->ti = 2;


   /**
    ** Loop over incoming program tokens/instructions
    */
   while( !tgsi_parse_end_of_tokens( &parse ) ) {

      tgsi_parse_token( &parse );

      switch( parse.FullToken.Token.Type ) {
      case TGSI_TOKEN_TYPE_INSTRUCTION:
         {
            struct tgsi_full_instruction *fullinst
               = &parse.FullToken.FullInstruction;
            enum tgsi_opcode opcode = fullinst->Instruction.Opcode;

            if (first_instruction && ctx->prolog) {
               ctx->prolog(ctx);
            }

            /*
             * XXX Note: we handle the case of ret in main.
             * However, the output redirections done by transform
             * have their limits with control flow and will generally
             * not work correctly. e.g.
             * if (cond) {
             *    oColor = x;
             *    ret;
             * }
             * oColor = y;
             * end;
             * If the color output is redirected to a temp and modified
             * by a transform, this will not work (the oColor assignment
             * in the conditional will never make it to the actual output).
             */
            if ((opcode == TGSI_OPCODE_END || opcode == TGSI_OPCODE_RET) &&
                 call_stack == 0 && ctx->epilog && !epilog_emitted) {
               if (opcode == TGSI_OPCODE_RET && cond_stack != 0) {
                  assert(!"transform ignoring RET in main");
               } else {
                  assert(cond_stack == 0);
                  /* Emit caller's epilog */
                  ctx->epilog(ctx);
                  epilog_emitted = TRUE;
               }
               /* Emit END (or RET) */
               ctx->emit_instruction(ctx, fullinst);
            }
            else {
               switch (opcode) {
               case TGSI_OPCODE_IF:
               case TGSI_OPCODE_UIF:
               case TGSI_OPCODE_SWITCH:
               case TGSI_OPCODE_BGNLOOP:
                  cond_stack++;
                  break;
               case TGSI_OPCODE_CAL:
                  call_stack++;
                  break;
               case TGSI_OPCODE_ENDIF:
               case TGSI_OPCODE_ENDSWITCH:
               case TGSI_OPCODE_ENDLOOP:
                  assert(cond_stack > 0);
                  cond_stack--;
                  break;
               case TGSI_OPCODE_ENDSUB:
                  assert(call_stack > 0);
                  call_stack--;
                  break;
               case TGSI_OPCODE_BGNSUB:
               case TGSI_OPCODE_RET:
               default:
                  break;
               }
               if (ctx->transform_instruction)
                  ctx->transform_instruction(ctx, fullinst);
               else
                  ctx->emit_instruction(ctx, fullinst);
            }

            first_instruction = FALSE;
         }
         break;

      case TGSI_TOKEN_TYPE_DECLARATION:
         {
            struct tgsi_full_declaration *fulldecl
               = &parse.FullToken.FullDeclaration;

            if (ctx->transform_declaration)
               ctx->transform_declaration(ctx, fulldecl);
            else
               ctx->emit_declaration(ctx, fulldecl);
         }
         break;

      case TGSI_TOKEN_TYPE_IMMEDIATE:
         {
            struct tgsi_full_immediate *fullimm
               = &parse.FullToken.FullImmediate;

            if (ctx->transform_immediate)
               ctx->transform_immediate(ctx, fullimm);
            else
               ctx->emit_immediate(ctx, fullimm);
         }
         break;
      case TGSI_TOKEN_TYPE_PROPERTY:
         {
            struct tgsi_full_property *fullprop
               = &parse.FullToken.FullProperty;

            if (ctx->transform_property)
               ctx->transform_property(ctx, fullprop);
            else
               ctx->emit_property(ctx, fullprop);
         }
         break;

      default:
         assert( 0 );
      }
   }
   assert(call_stack == 0);

   tgsi_parse_free (&parse);

   return ctx->ti;
}


#include "tgsi_text.h"

extern int tgsi_transform_foo( struct tgsi_token *tokens_out,
                               uint max_tokens_out );

/* This function exists only so that tgsi_text_translate() doesn't get
 * magic-ed out of the libtgsi.a archive by the build system.  Don't
 * remove unless you know this has been fixed - check on mingw/scons
 * builds as well.
 */
int
tgsi_transform_foo( struct tgsi_token *tokens_out,
                    uint max_tokens_out )
{
   const char *text = 
      "FRAG\n"
      "DCL IN[0], COLOR, CONSTANT\n"
      "DCL OUT[0], COLOR\n"
      "  0: MOV OUT[0], IN[0]\n"
      "  1: END";
        
   return tgsi_text_translate( text,
                               tokens_out,
                               max_tokens_out );
}
