#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "stubs.h"

#ifdef __SUNPRO_C
#pragma weak DeleteFontClientID
#endif

weak void
DeleteFontClientID(Font id)
{
    OVERRIDE_SYMBOL(DeleteFontClientID, id);
}
