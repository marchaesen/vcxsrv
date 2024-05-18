#include "rusticl_system_bindings.h"

#include "git_sha1.h"

FILE *
stdout_ptr(void)
{
    return stdout;
}

FILE *
stderr_ptr(void)
{
    return stderr;
}

const char*
mesa_version_string(void)
{
    return PACKAGE_VERSION MESA_GIT_SHA1;
}
