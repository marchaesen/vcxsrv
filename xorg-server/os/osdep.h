/***********************************************************

Copyright 1987, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.

Copyright 1987 by Digital Equipment Corporation, Maynard, Massachusetts.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the name of Digital not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

******************************************************************/

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#ifndef _OSDEP_H_
#define _OSDEP_H_ 1

#if defined(XDMCP) || defined(HASXDMAUTH)
#include <X11/Xdmcp.h>
#endif

#ifdef _POSIX_SOURCE
#include <limits.h>
#else
#define _POSIX_SOURCE
#include <limits.h>
#undef _POSIX_SOURCE
#endif

#ifndef OPEN_MAX
#ifdef SVR4
#define OPEN_MAX 256
#else
#include <sys/param.h>
#ifndef OPEN_MAX
#if defined(NOFILE) && !defined(NOFILES_MAX)
#define OPEN_MAX NOFILE
#else
#if !defined(WIN32)
#define OPEN_MAX NOFILES_MAX
#else
#define OPEN_MAX 256
#endif
#endif
#endif
#endif
#endif

#include <X11/Xpoll.h>

/*
 * MAXSOCKS is used only for initialising MaxClients when no other method
 * like sysconf(_SC_OPEN_MAX) is not supported.
 */

#if OPEN_MAX <= 256
#define MAXSOCKS (OPEN_MAX - 1)
#else
#define MAXSOCKS 256
#endif

/* MAXSELECT is the number of fds that select() can handle */
#define MAXSELECT (sizeof(fd_set) * NBBY)

#include <stddef.h>

#if defined(XDMCP) || defined(HASXDMAUTH)
typedef Bool (*ValidatorFunc) (ARRAY8Ptr Auth, ARRAY8Ptr Data, int packet_type);
typedef Bool (*GeneratorFunc) (ARRAY8Ptr Auth, ARRAY8Ptr Data, int packet_type);
typedef Bool (*AddAuthorFunc) (unsigned name_length, const char *name,
                               unsigned data_length, char *data);
#endif

typedef struct _connectionInput *ConnectionInputPtr;
typedef struct _connectionOutput *ConnectionOutputPtr;

struct _osComm;

#define AuthInitArgs void
typedef void (*AuthInitFunc) (AuthInitArgs);

#define AuthAddCArgs unsigned short data_length, const char *data, XID id
typedef int (*AuthAddCFunc) (AuthAddCArgs);

#define AuthCheckArgs unsigned short data_length, const char *data, ClientPtr client, const char **reason
typedef XID (*AuthCheckFunc) (AuthCheckArgs);

#define AuthFromIDArgs XID id, unsigned short *data_lenp, char **datap
typedef int (*AuthFromIDFunc) (AuthFromIDArgs);

#define AuthGenCArgs unsigned data_length, const char *data, XID id, unsigned *data_length_return, char **data_return
typedef XID (*AuthGenCFunc) (AuthGenCArgs);

#define AuthRemCArgs unsigned short data_length, const char *data
typedef int (*AuthRemCFunc) (AuthRemCArgs);

#define AuthRstCArgs void
typedef int (*AuthRstCFunc) (AuthRstCArgs);

#define AuthToIDArgs unsigned short data_length, char *data
typedef XID (*AuthToIDFunc) (AuthToIDArgs);

typedef void (*OsCloseFunc) (ClientPtr);

typedef int (*OsFlushFunc) (ClientPtr who, struct _osComm * oc, char *extraBuf,
                            int extraCount);

typedef struct _osComm {
    int fd;
    ConnectionInputPtr input;
    ConnectionOutputPtr output;
    XID auth_id;                /* authorization id */
    CARD32 conn_time;           /* timestamp if not established, else 0  */
    struct _XtransConnInfo *trans_conn; /* transport connection object */
} OsCommRec, *OsCommPtr;

extern int FlushClient(ClientPtr /*who */ ,
                       OsCommPtr /*oc */ ,
                       const void * /*extraBuf */ ,
                       int      /*extraCount */
    );

extern void FreeOsBuffers(OsCommPtr     /*oc */
    );

#include "dix.h"

extern fd_set AllSockets;
extern fd_set AllClients;
extern fd_set LastSelectMask;
extern fd_set WellKnownConnections;
extern fd_set EnabledDevices;
extern fd_set ClientsWithInput;
extern fd_set ClientsWriteBlocked;
extern fd_set OutputPending;
extern fd_set IgnoredClientsWithInput;

#ifndef WIN32
extern int *ConnectionTranslation;
#else
extern int GetConnectionTranslation(int conn);
extern void SetConnectionTranslation(int conn, int client);
extern void ClearConnectionTranslation(void);
#endif

extern Bool NewOutputPending;
extern Bool AnyClientsWriteBlocked;

extern WorkQueuePtr workQueue;

/* in WaitFor.c */
#ifdef WIN32
typedef long int fd_mask;
#endif
#define ffs mffs
extern int mffs(fd_mask);

/* in access.c */
extern Bool ComputeLocalClient(ClientPtr client);

/* in auth.c */
extern void GenerateRandomData(int len, char *buf);

/* in mitauth.c */
extern XID MitCheckCookie(AuthCheckArgs);
extern XID MitGenerateCookie(AuthGenCArgs);
extern XID MitToID(AuthToIDArgs);
extern int MitAddCookie(AuthAddCArgs);
extern int MitFromID(AuthFromIDArgs);
extern int MitRemoveCookie(AuthRemCArgs);
extern int MitResetCookie(AuthRstCArgs);

/* in xdmauth.c */
#ifdef HASXDMAUTH
extern XID XdmCheckCookie(AuthCheckArgs);
extern XID XdmToID(AuthToIDArgs);
extern int XdmAddCookie(AuthAddCArgs);
extern int XdmFromID(AuthFromIDArgs);
extern int XdmRemoveCookie(AuthRemCArgs);
extern int XdmResetCookie(AuthRstCArgs);
#endif

/* in rpcauth.c */
#ifdef SECURE_RPC
extern void SecureRPCInit(AuthInitArgs);
extern XID SecureRPCCheck(AuthCheckArgs);
extern XID SecureRPCToID(AuthToIDArgs);
extern int SecureRPCAdd(AuthAddCArgs);
extern int SecureRPCFromID(AuthFromIDArgs);
extern int SecureRPCRemove(AuthRemCArgs);
extern int SecureRPCReset(AuthRstCArgs);
#endif

#ifdef XDMCP
/* in xdmcp.c */
extern void XdmcpUseMsg(void);
extern int XdmcpOptions(int argc, char **argv, int i);
extern void XdmcpRegisterConnection(int type, const char *address, int addrlen);
extern void XdmcpRegisterAuthorizations(void);
extern void XdmcpRegisterAuthorization(const char *name, int namelen);
extern void XdmcpInit(void);
extern void XdmcpReset(void);
extern void XdmcpOpenDisplay(int sock);
extern void XdmcpCloseDisplay(int sock);
extern void XdmcpRegisterAuthentication(const char *name,
                                        int namelen,
                                        const char *data,
                                        int datalen,
                                        ValidatorFunc Validator,
                                        GeneratorFunc Generator,
                                        AddAuthorFunc AddAuth);

struct sockaddr_in;
extern void XdmcpRegisterBroadcastAddress(const struct sockaddr_in *addr);
#endif

#ifdef HASXDMAUTH
extern void XdmAuthenticationInit(const char *cookie, int cookie_length);
#endif

#endif                          /* _OSDEP_H_ */
