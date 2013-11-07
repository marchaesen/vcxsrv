#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <X11/Xfuncproto.h>

/* ErrorF is used by xtrans */
/*#ifndef HAVE_DIX_CONFIG_H
extern _X_EXPORT void
ErrorF(const char *f, ...)
_X_ATTRIBUTE_PRINTF(1, 2);
#endif*/

#define TRANS_REOPEN
#define TRANS_SERVER
#define XSERV_t
#ifndef TCPCONN
#define TCPCONN
#endif
#ifdef WIN32
#undef SO_REUSEADDR
#define SO_BINDRETRYCOUNT 0  // do not try to bind again when it fails, this will speed up searching for a free listening port
#endif

#include <X11/Xtrans/transport.c>
