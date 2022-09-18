#define nir_shader_get_entrypoint __nir_shader_get_entrypoint_wraped
#define pipe_resource_reference __pipe_resource_reference_wraped
#define util_format_pack_rgba __util_format_pack_rgba
#include "nir.h"
#include "util/u_inlines.h"
#include "util/format/u_format.h"
#undef nir_shader_get_entrypoint
#undef pipe_resource_reference
#undef util_format_pack_rgba

const char* mesa_version_string(void);
nir_function_impl *nir_shader_get_entrypoint(const nir_shader *shader);
void pipe_resource_reference(struct pipe_resource **dst, struct pipe_resource *src);
void util_format_pack_rgba(enum pipe_format format, void *dst, const void *src, unsigned w);
