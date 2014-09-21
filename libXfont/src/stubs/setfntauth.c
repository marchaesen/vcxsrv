#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "stubs.h"

#ifdef __SUNPRO_C
#pragma weak set_font_authorizations
#endif

weak int
set_font_authorizations(char **authorizations, int *authlen, ClientPtr client)
{
    OVERRIDE_SYMBOL(set_font_authorizations, authorizations, authlen, client);
    return 0;
}
