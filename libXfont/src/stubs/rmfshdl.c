#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "stubs.h"

#ifdef __SUNPRO_C
#pragma weak remove_fs_handlers
#endif

weak void
remove_fs_handlers(FontPathElementPtr fpe,
                   BlockHandlerProcPtr blockHandler,
                   Bool all)
{
    OVERRIDE_SYMBOL(remove_fs_handlers, fpe, blockHandler, all);
}
