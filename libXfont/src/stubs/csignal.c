#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "stubs.h"

#ifdef __SUNPRO_C
#pragma weak ClientSignal
#endif

weak Bool
ClientSignal(ClientPtr client)
{
    OVERRIDE_SYMBOL(ClientSignal,client);
    return True;
}
