/*
 * Copyright © 2017 Timothy Arceri
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include "st_debug.h"
#include "st_program.h"
#include "st_shader_cache.h"
#include "compiler/glsl/program.h"
#include "pipe/p_shader_tokens.h"
#include "program/ir_to_mesa.h"
#include "util/u_memory.h"

void
st_get_program_binary_driver_sha1(struct gl_context *ctx, uint8_t *sha1)
{
   disk_cache_compute_key(ctx->Cache, NULL, 0, sha1);
}

static void
write_stream_out_to_cache(struct blob *blob,
                          struct pipe_shader_state *tgsi)
{
   blob_write_bytes(blob, &tgsi->stream_output,
                    sizeof(tgsi->stream_output));
}

static void
write_tgsi_to_cache(struct blob *blob, const struct tgsi_token *tokens,
                    struct gl_program *prog, unsigned num_tokens)
{
   blob_write_uint32(blob, num_tokens);
   blob_write_bytes(blob, tokens, num_tokens * sizeof(struct tgsi_token));

   prog->driver_cache_blob = ralloc_size(NULL, blob->size);
   memcpy(prog->driver_cache_blob, blob->data, blob->size);
   prog->driver_cache_blob_size = blob->size;
}

void
st_serialise_tgsi_program(struct gl_context *ctx, struct gl_program *prog)
{
   struct blob blob;
   blob_init(&blob);

   switch (prog->info.stage) {
   case MESA_SHADER_VERTEX: {
      struct st_vertex_program *stvp = (struct st_vertex_program *) prog;

      blob_write_uint32(&blob, stvp->num_inputs);
      blob_write_bytes(&blob, stvp->index_to_input,
                       sizeof(stvp->index_to_input));
      blob_write_bytes(&blob, stvp->result_to_output,
                       sizeof(stvp->result_to_output));

      write_stream_out_to_cache(&blob, &stvp->tgsi);
      write_tgsi_to_cache(&blob, stvp->tgsi.tokens, prog,
                          stvp->num_tgsi_tokens);
      break;
   }
   case MESA_SHADER_TESS_CTRL:
   case MESA_SHADER_TESS_EVAL:
   case MESA_SHADER_GEOMETRY: {
      struct st_common_program *stcp = (struct st_common_program *) prog;

      write_stream_out_to_cache(&blob, &stcp->tgsi);
      write_tgsi_to_cache(&blob, stcp->tgsi.tokens, prog,
                          stcp->num_tgsi_tokens);
      break;
   }
   case MESA_SHADER_FRAGMENT: {
      struct st_fragment_program *stfp = (struct st_fragment_program *) prog;

      write_tgsi_to_cache(&blob, stfp->tgsi.tokens, prog,
                          stfp->num_tgsi_tokens);
      break;
   }
   case MESA_SHADER_COMPUTE: {
      struct st_compute_program *stcp = (struct st_compute_program *) prog;

      write_tgsi_to_cache(&blob, stcp->tgsi.prog, prog,
                          stcp->num_tgsi_tokens);
      break;
   }
   default:
      unreachable("Unsupported stage");
   }

   blob_finish(&blob);
}

/**
 * Store tgsi and any other required state in on-disk shader cache.
 */
void
st_store_tgsi_in_disk_cache(struct st_context *st, struct gl_program *prog)
{
   if (!st->ctx->Cache)
      return;

   /* Exit early when we are dealing with a ff shader with no source file to
    * generate a source from.
    */
   static const char zero[sizeof(prog->sh.data->sha1)] = {0};
   if (memcmp(prog->sh.data->sha1, zero, sizeof(prog->sh.data->sha1)) == 0)
      return;

   st_serialise_tgsi_program(st->ctx, prog);

   if (st->ctx->_Shader->Flags & GLSL_CACHE_INFO) {
      fprintf(stderr, "putting %s tgsi_tokens in cache\n",
              _mesa_shader_stage_to_string(prog->info.stage));
   }
}

static void
read_stream_out_from_cache(struct blob_reader *blob_reader,
                           struct pipe_shader_state *tgsi)
{
   blob_copy_bytes(blob_reader, (uint8_t *) &tgsi->stream_output,
                    sizeof(tgsi->stream_output));
}

static void
read_tgsi_from_cache(struct blob_reader *blob_reader,
                     const struct tgsi_token **tokens,
                     unsigned *num_tokens)
{
   *num_tokens  = blob_read_uint32(blob_reader);
   unsigned tokens_size = *num_tokens * sizeof(struct tgsi_token);
   *tokens = (const struct tgsi_token*) MALLOC(tokens_size);
   blob_copy_bytes(blob_reader, (uint8_t *) *tokens, tokens_size);
}

void
st_deserialise_tgsi_program(struct gl_context *ctx,
                            struct gl_shader_program *shProg,
                            struct gl_program *prog)
{
   struct st_context *st = st_context(ctx);
   size_t size = prog->driver_cache_blob_size;
   uint8_t *buffer = (uint8_t *) prog->driver_cache_blob;

   struct blob_reader blob_reader;
   blob_reader_init(&blob_reader, buffer, size);

   switch (prog->info.stage) {
   case MESA_SHADER_VERTEX: {
      struct st_vertex_program *stvp = (struct st_vertex_program *) prog;

      st_release_vp_variants(st, stvp);

      stvp->num_inputs = blob_read_uint32(&blob_reader);
      blob_copy_bytes(&blob_reader, (uint8_t *) stvp->index_to_input,
                      sizeof(stvp->index_to_input));
      blob_copy_bytes(&blob_reader, (uint8_t *) stvp->result_to_output,
                      sizeof(stvp->result_to_output));

      read_stream_out_from_cache(&blob_reader, &stvp->tgsi);
      read_tgsi_from_cache(&blob_reader, &stvp->tgsi.tokens,
                           &stvp->num_tgsi_tokens);

      if (st->vp == stvp)
         st->dirty |= ST_NEW_VERTEX_PROGRAM(st, stvp);

      break;
   }
   case MESA_SHADER_TESS_CTRL: {
      struct st_common_program *sttcp = st_common_program(prog);

      st_release_basic_variants(st, sttcp->Base.Target,
                                &sttcp->variants, &sttcp->tgsi);

      read_stream_out_from_cache(&blob_reader, &sttcp->tgsi);
      read_tgsi_from_cache(&blob_reader, &sttcp->tgsi.tokens,
                           &sttcp->num_tgsi_tokens);

      if (st->tcp == sttcp)
         st->dirty |= sttcp->affected_states;

      break;
   }
   case MESA_SHADER_TESS_EVAL: {
      struct st_common_program *sttep = st_common_program(prog);

      st_release_basic_variants(st, sttep->Base.Target,
                                &sttep->variants, &sttep->tgsi);

      read_stream_out_from_cache(&blob_reader, &sttep->tgsi);
      read_tgsi_from_cache(&blob_reader, &sttep->tgsi.tokens,
                           &sttep->num_tgsi_tokens);

      if (st->tep == sttep)
         st->dirty |= sttep->affected_states;

      break;
   }
   case MESA_SHADER_GEOMETRY: {
      struct st_common_program *stgp = st_common_program(prog);

      st_release_basic_variants(st, stgp->Base.Target, &stgp->variants,
                                &stgp->tgsi);

      read_stream_out_from_cache(&blob_reader, &stgp->tgsi);
      read_tgsi_from_cache(&blob_reader, &stgp->tgsi.tokens,
                           &stgp->num_tgsi_tokens);

      if (st->gp == stgp)
         st->dirty |= stgp->affected_states;

      break;
   }
   case MESA_SHADER_FRAGMENT: {
      struct st_fragment_program *stfp = (struct st_fragment_program *) prog;

      st_release_fp_variants(st, stfp);

      read_tgsi_from_cache(&blob_reader, &stfp->tgsi.tokens,
                           &stfp->num_tgsi_tokens);

      if (st->fp == stfp)
         st->dirty |= stfp->affected_states;

      break;
   }
   case MESA_SHADER_COMPUTE: {
      struct st_compute_program *stcp = (struct st_compute_program *) prog;

      st_release_cp_variants(st, stcp);

      read_tgsi_from_cache(&blob_reader,
                           (const struct tgsi_token**) &stcp->tgsi.prog,
                           &stcp->num_tgsi_tokens);

      stcp->tgsi.req_local_mem = stcp->Base.info.cs.shared_size;
      stcp->tgsi.req_private_mem = 0;
      stcp->tgsi.req_input_mem = 0;

      if (st->cp == stcp)
         st->dirty |= stcp->affected_states;

      break;
   }
   default:
      unreachable("Unsupported stage");
   }

   /* Make sure we don't try to read more data than we wrote. This should
    * never happen in release builds but its useful to have this check to
    * catch development bugs.
    */
   if (blob_reader.current != blob_reader.end || blob_reader.overrun) {
      assert(!"Invalid TGSI shader disk cache item!");

      if (ctx->_Shader->Flags & GLSL_CACHE_INFO) {
         fprintf(stderr, "Error reading program from cache (invalid "
                 "TGSI cache item)\n");
      }
   }

   st_set_prog_affected_state_flags(prog);
   _mesa_associate_uniform_storage(ctx, shProg, prog, false);

   /* Create Gallium shaders now instead of on demand. */
   if (ST_DEBUG & DEBUG_PRECOMPILE ||
       st->shader_has_one_variant[prog->info.stage])
      st_precompile_shader_variant(st, prog);
}

bool
st_load_tgsi_from_disk_cache(struct gl_context *ctx,
                             struct gl_shader_program *prog)
{
   if (!ctx->Cache)
      return false;

   /* If we didn't load the GLSL metadata from cache then we could not have
    * loaded the tgsi either.
    */
   if (prog->data->LinkStatus != LINKING_SKIPPED)
      return false;

   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      if (prog->_LinkedShaders[i] == NULL)
         continue;

      struct gl_program *glprog = prog->_LinkedShaders[i]->Program;
      st_deserialise_tgsi_program(ctx, prog, glprog);

      /* We don't need the cached blob anymore so free it */
      ralloc_free(glprog->driver_cache_blob);
      glprog->driver_cache_blob = NULL;
      glprog->driver_cache_blob_size = 0;

      if (ctx->_Shader->Flags & GLSL_CACHE_INFO) {
         fprintf(stderr, "%s tgsi_tokens retrieved from cache\n",
                 _mesa_shader_stage_to_string(i));
      }
   }

   return true;
}
