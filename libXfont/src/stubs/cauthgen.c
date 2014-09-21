#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "stubs.h"

#ifdef __SUNPRO_C
#pragma weak client_auth_generation
#endif

weak int
client_auth_generation(ClientPtr client)
{
    OVERRIDE_SYMBOL(client_auth_generation, client);
    return 0;
}
