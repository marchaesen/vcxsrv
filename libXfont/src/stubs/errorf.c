#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "stubs.h"

#ifdef __SUNPRO_C
#pragma weak ErrorF
#endif

weak void
ErrorF(const char *f, ...)
{
    OVERRIDE_VA_SYMBOL(VErrorF, f);
}
