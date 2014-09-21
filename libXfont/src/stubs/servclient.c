#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "stubs.h"

#ifdef __SUNPRO_C
#pragma weak serverClient
#endif

weak void *serverClient = 0;

void *__GetServerClient(void);

void *
__GetServerClient(void)
{
   OVERRIDE_DATA(serverClient);
   return serverClient;
}
