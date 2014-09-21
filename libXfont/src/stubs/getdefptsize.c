#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "stubs.h"

#ifdef __SUNPRO_C
#pragma weak GetDefaultPointSize
#endif

weak int
GetDefaultPointSize(void)
{
    OVERRIDE_SYMBOL(GetDefaultPointSize);
    return 0;
}
