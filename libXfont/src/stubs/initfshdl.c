#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "stubs.h"

#ifdef __SUNPRO_C
#pragma weak init_fs_handlers
#endif

weak int
init_fs_handlers(FontPathElementPtr fpe,
                 BlockHandlerProcPtr block_handler)
{
  OVERRIDE_SYMBOL(init_fs_handlers, fpe, block_handler);
  return Successful;
}
