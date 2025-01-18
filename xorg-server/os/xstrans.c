#include <dix-config.h>

#include <X11/Xfuncproto.h>

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
