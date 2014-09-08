#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "stubs.h"

#ifdef __SUNPRO_C
#pragma weak find_old_font
#endif

weak FontPtr
find_old_font(FSID id)
{
    OVERRIDE_SYMBOL(find_old_font, id);
    return (FontPtr)NULL;
}
