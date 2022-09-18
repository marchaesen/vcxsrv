#include "rusticl_mesa_inline_bindings_wrapper.h"
#include "git_sha1.h"

nir_function_impl *
nir_shader_get_entrypoint(const nir_shader *shader)
{
   return __nir_shader_get_entrypoint_wraped(shader);
}

void
pipe_resource_reference(struct pipe_resource **dst, struct pipe_resource *src)
{
   __pipe_resource_reference_wraped(dst, src);
}

void
util_format_pack_rgba(enum pipe_format format, void *dst, const void *src, unsigned w)
{
    return __util_format_pack_rgba(format, dst, src, w);
}

const char*
mesa_version_string(void)
{
    return PACKAGE_VERSION MESA_GIT_SHA1;
}
