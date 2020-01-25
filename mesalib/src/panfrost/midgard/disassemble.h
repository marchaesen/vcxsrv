#include <stddef.h>
#include "compiler/shader_enums.h"

struct midgard_disasm_stats {
        /* Counts gleaned from disassembly, or negative if the field cannot be
         * inferred, for instance due to indirect access. If negative, the abs
         * is the upper limit for the count. */

        signed texture_count;
        signed sampler_count;
        signed attribute_count;
        signed varying_count;
        signed uniform_count;
        signed uniform_buffer_count;
        signed work_count;

        /* These are pseudometrics for shader-db */
        unsigned instruction_count;
        unsigned bundle_count;
        unsigned quadword_count;

        /* Should we enable helper invocations? */
        bool helper_invocations;
};

struct midgard_disasm_stats
disassemble_midgard(FILE *fp, uint8_t *code, size_t size, unsigned gpu_id, gl_shader_stage stage);
