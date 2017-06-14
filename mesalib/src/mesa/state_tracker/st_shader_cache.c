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

static void
write_stream_out_to_cache(struct blob *blob,
                          struct pipe_shader_state *tgsi)
{
   blob_write_bytes(blob, &tgsi->stream_output,
                    sizeof(tgsi->stream_output));
}

static void
write_tgsi_to_cache(struct blob *blob, struct pipe_shader_state *tgsi,
                    struct st_context *st, unsigned char *sha1,
                    unsigned num_tokens)
{
   blob_write_uint32(blob, num_tokens);
   blob_write_bytes(blob, tgsi->tokens,
                    num_tokens * sizeof(struct tgsi_token));

   disk_cache_put(st->ctx->Cache, sha1, blob->data, blob->size);
}

/**
 * Store tgsi and any other required state in on-disk shader cache.
 */
void
st_store_tgsi_in_disk_cache(struct st_context *st, struct gl_program *prog,
                            struct pipe_shader_state *out_state,
                            unsigned num_tokens)
{
   if (!st->ctx->Cache)
      return;

   /* Exit early when we are dealing with a ff shader with no source file to
    * generate a source from.
    */
   static const char zero[sizeof(prog->sh.data->sha1)] = {0};
   if (memcmp(prog->sh.data->sha1, zero, sizeof(prog->sh.data->sha1)) == 0)
      return;

   unsigned char *sha1;
   struct blob *blob = blob_create();

   switch (prog->info.stage) {
   case MESA_SHADER_VERTEX: {
      struct st_vertex_program *stvp = (struct st_vertex_program *) prog;
      sha1 = stvp->sha1;

      blob_write_uint32(blob, stvp->num_inputs);
      blob_write_bytes(blob, stvp->index_to_input,
                       sizeof(stvp->index_to_input));
      blob_write_bytes(blob, stvp->result_to_output,
                       sizeof(stvp->result_to_output));

      write_stream_out_to_cache(blob, &stvp->tgsi);
      write_tgsi_to_cache(blob, &stvp->tgsi, st, sha1, num_tokens);
      break;
   }
   case MESA_SHADER_TESS_CTRL:
   case MESA_SHADER_TESS_EVAL:
   case MESA_SHADER_GEOMETRY: {
      struct st_common_program *p = st_common_program(prog);
      sha1 = p->sha1;

      write_stream_out_to_cache(blob, out_state);
      write_tgsi_to_cache(blob, out_state, st, sha1, num_tokens);
      break;
   }
   case MESA_SHADER_FRAGMENT: {
      struct st_fragment_program *stfp = (struct st_fragment_program *) prog;
      sha1 = stfp->sha1;

      write_tgsi_to_cache(blob, &stfp->tgsi, st, sha1, num_tokens);
      break;
   }
   case MESA_SHADER_COMPUTE: {
      struct st_compute_program *stcp = (struct st_compute_program *) prog;
      sha1 = stcp->sha1;

      write_tgsi_to_cache(blob, out_state, st, sha1, num_tokens);
      break;
   }
   default:
      unreachable("Unsupported stage");
   }

   if (st->ctx->_Shader->Flags & GLSL_CACHE_INFO) {
      char sha1_buf[41];
      _mesa_sha1_format(sha1_buf, sha1);
      fprintf(stderr, "putting %s tgsi_tokens in cache: %s\n",
              _mesa_shader_stage_to_string(prog->info.stage), sha1_buf);
   }

   blob_destroy(blob);
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
                     const struct tgsi_token **tokens)
{
   uint32_t num_tokens  = blob_read_uint32(blob_reader);
   unsigned tokens_size = num_tokens * sizeof(struct tgsi_token);
   *tokens = (const struct tgsi_token*) MALLOC(tokens_size);
   blob_copy_bytes(blob_reader, (uint8_t *) *tokens, tokens_size);
}

bool
st_load_tgsi_from_disk_cache(struct gl_context *ctx,
                             struct gl_shader_program *prog)
{
   if (!ctx->Cache)
      return false;

   unsigned char *stage_sha1[MESA_SHADER_STAGES];
   char sha1_buf[41];

   /* Compute and store sha1 for each stage. These will be reused by the
    * cache store pass if we fail to find the cached tgsi.
    */
   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      if (prog->_LinkedShaders[i] == NULL)
         continue;

      char *buf = ralloc_strdup(NULL, "tgsi_tokens ");
      _mesa_sha1_format(sha1_buf,
                        prog->_LinkedShaders[i]->Program->sh.data->sha1);
      ralloc_strcat(&buf, sha1_buf);

      struct gl_program *glprog = prog->_LinkedShaders[i]->Program;
      switch (glprog->info.stage) {
      case MESA_SHADER_VERTEX: {
         struct st_vertex_program *stvp = (struct st_vertex_program *) glprog;
         stage_sha1[i] = stvp->sha1;
         ralloc_strcat(&buf, " vs");
         disk_cache_compute_key(ctx->Cache, buf, strlen(buf), stage_sha1[i]);
         break;
      }
      case MESA_SHADER_TESS_CTRL: {
         struct st_common_program *stcp = st_common_program(glprog);
         stage_sha1[i] = stcp->sha1;
         ralloc_strcat(&buf, " tcs");
         disk_cache_compute_key(ctx->Cache, buf, strlen(buf), stage_sha1[i]);
         break;
      }
      case MESA_SHADER_TESS_EVAL: {
         struct st_common_program *step = st_common_program(glprog);
         stage_sha1[i] = step->sha1;
         ralloc_strcat(&buf, " tes");
         disk_cache_compute_key(ctx->Cache, buf, strlen(buf), stage_sha1[i]);
         break;
      }
      case MESA_SHADER_GEOMETRY: {
         struct st_common_program *stgp = st_common_program(glprog);
         stage_sha1[i] = stgp->sha1;
         ralloc_strcat(&buf, " gs");
         disk_cache_compute_key(ctx->Cache, buf, strlen(buf), stage_sha1[i]);
         break;
      }
      case MESA_SHADER_FRAGMENT: {
         struct st_fragment_program *stfp =
            (struct st_fragment_program *) glprog;
         stage_sha1[i] = stfp->sha1;
         ralloc_strcat(&buf, " fs");
         disk_cache_compute_key(ctx->Cache, buf, strlen(buf), stage_sha1[i]);
         break;
      }
      case MESA_SHADER_COMPUTE: {
         struct st_compute_program *stcp =
            (struct st_compute_program *) glprog;
         stage_sha1[i] = stcp->sha1;
         ralloc_strcat(&buf, " cs");
         disk_cache_compute_key(ctx->Cache, buf, strlen(buf), stage_sha1[i]);
         break;
      }
      default:
         unreachable("Unsupported stage");
      }

      ralloc_free(buf);
   }

   /* Now that we have created the sha1 keys that will be used for writting to
    * the tgsi cache fallback to the regular glsl to tgsi path if we didn't
    * load the GLSL IR from cache. We do this as glsl to tgsi can alter things
    * such as gl_program_parameter_list which holds things like uniforms.
    */
   if (prog->data->LinkStatus != linking_skipped)
      return false;

   uint8_t *buffer = NULL;
   if (ctx->_Shader->Flags & GLSL_CACHE_FALLBACK) {
      goto fallback_recompile;
   }

   struct st_context *st = st_context(ctx);
   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      if (prog->_LinkedShaders[i] == NULL)
         continue;

      unsigned char *sha1 = stage_sha1[i];
      size_t size;
      buffer = (uint8_t *) disk_cache_get(ctx->Cache, sha1, &size);
      if (buffer) {
         struct blob_reader blob_reader;
         blob_reader_init(&blob_reader, buffer, size);

         struct gl_program *glprog = prog->_LinkedShaders[i]->Program;
         switch (glprog->info.stage) {
         case MESA_SHADER_VERTEX: {
            struct st_vertex_program *stvp =
               (struct st_vertex_program *) glprog;

            st_release_vp_variants(st, stvp);

            stvp->num_inputs = blob_read_uint32(&blob_reader);
            blob_copy_bytes(&blob_reader, (uint8_t *) stvp->index_to_input,
                            sizeof(stvp->index_to_input));
            blob_copy_bytes(&blob_reader, (uint8_t *) stvp->result_to_output,
                            sizeof(stvp->result_to_output));

            read_stream_out_from_cache(&blob_reader, &stvp->tgsi);
            read_tgsi_from_cache(&blob_reader, &stvp->tgsi.tokens);

            if (st->vp == stvp)
               st->dirty |= ST_NEW_VERTEX_PROGRAM(st, stvp);

            break;
         }
         case MESA_SHADER_TESS_CTRL: {
            struct st_common_program *sttcp = st_common_program(glprog);

            st_release_basic_variants(st, sttcp->Base.Target,
                                      &sttcp->variants, &sttcp->tgsi);

            read_stream_out_from_cache(&blob_reader, &sttcp->tgsi);
            read_tgsi_from_cache(&blob_reader, &sttcp->tgsi.tokens);

            if (st->tcp == sttcp)
               st->dirty |= sttcp->affected_states;

            break;
         }
         case MESA_SHADER_TESS_EVAL: {
            struct st_common_program *sttep = st_common_program(glprog);

            st_release_basic_variants(st, sttep->Base.Target,
                                      &sttep->variants, &sttep->tgsi);

            read_stream_out_from_cache(&blob_reader, &sttep->tgsi);
            read_tgsi_from_cache(&blob_reader, &sttep->tgsi.tokens);

            if (st->tep == sttep)
               st->dirty |= sttep->affected_states;

            break;
         }
         case MESA_SHADER_GEOMETRY: {
            struct st_common_program *stgp = st_common_program(glprog);

            st_release_basic_variants(st, stgp->Base.Target, &stgp->variants,
                                      &stgp->tgsi);

            read_stream_out_from_cache(&blob_reader, &stgp->tgsi);
            read_tgsi_from_cache(&blob_reader, &stgp->tgsi.tokens);

            if (st->gp == stgp)
               st->dirty |= stgp->affected_states;

            break;
         }
         case MESA_SHADER_FRAGMENT: {
            struct st_fragment_program *stfp =
               (struct st_fragment_program *) glprog;

            st_release_fp_variants(st, stfp);

            read_tgsi_from_cache(&blob_reader, &stfp->tgsi.tokens);

            if (st->fp == stfp)
               st->dirty |= stfp->affected_states;

            break;
         }
         case MESA_SHADER_COMPUTE: {
            struct st_compute_program *stcp =
               (struct st_compute_program *) glprog;

            st_release_cp_variants(st, stcp);

            read_tgsi_from_cache(&blob_reader,
                                 (const struct tgsi_token**) &stcp->tgsi.prog);

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

         if (blob_reader.current != blob_reader.end || blob_reader.overrun) {
            /* Something very bad has gone wrong discard the item from the
             * cache and rebuild/link from source.
             */
            assert(!"Invalid TGSI shader disk cache item!");

            if (ctx->_Shader->Flags & GLSL_CACHE_INFO) {
               fprintf(stderr, "Error reading program from cache (invalid "
                       "TGSI cache item)\n");
            }

            disk_cache_remove(ctx->Cache, sha1);

            goto fallback_recompile;
         }

         if (ctx->_Shader->Flags & GLSL_CACHE_INFO) {
            _mesa_sha1_format(sha1_buf, sha1);
            fprintf(stderr, "%s tgsi_tokens retrieved from cache: %s\n",
                    _mesa_shader_stage_to_string(i), sha1_buf);
         }

         st_set_prog_affected_state_flags(glprog);
         _mesa_associate_uniform_storage(ctx, prog, glprog, false);

         /* Create Gallium shaders now instead of on demand. */
         if (ST_DEBUG & DEBUG_PRECOMPILE ||
             st->shader_has_one_variant[glprog->info.stage])
            st_precompile_shader_variant(st, glprog);

         free(buffer);
      } else {
         /* Failed to find a matching cached shader so fallback to recompile.
          */
         if (ctx->_Shader->Flags & GLSL_CACHE_INFO) {
            fprintf(stderr, "TGSI cache item not found.\n");
         }

         goto fallback_recompile;
      }
   }

   return true;

fallback_recompile:
   free(buffer);

   if (ctx->_Shader->Flags & GLSL_CACHE_INFO)
      fprintf(stderr, "TGSI cache falling back to recompile.\n");

   for (unsigned i = 0; i < prog->NumShaders; i++) {
      _mesa_glsl_compile_shader(ctx, prog->Shaders[i], false, false, true);
   }

   prog->data->skip_cache = true;
   _mesa_glsl_link_shader(ctx, prog);

   return true;
}
