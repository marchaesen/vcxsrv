#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "stubs.h"

#ifdef __SUNPRO_C
#pragma weak GetTimeInMillis
#endif

weak unsigned long
GetTimeInMillis (void)
{
    OVERRIDE_SYMBOL(GetTimeInMillis);
    return 0;
}
