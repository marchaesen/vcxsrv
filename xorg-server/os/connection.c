/***********************************************************

Copyright 1987, 1989, 1998  The Open Group

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

Copyright 1987, 1989 by Digital Equipment Corporation, Maynard, Massachusetts.

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
/*****************************************************************
 *  Stuff to create connections --- OS dependent
 *
 *      EstablishNewConnections, CreateWellKnownSockets, ResetWellKnownSockets,
 *      CloseDownConnection, CheckConnections, AddEnabledDevice,
 *	RemoveEnabledDevice, OnlyListToOneClient,
 *      ListenToAllClients,
 *
 *      (WaitForSomething is in its own file)
 *
 *      In this implementation, a client socket table is not kept.
 *      Instead, what would be the index into the table is just the
 *      file descriptor of the socket.  This won't work for if the
 *      socket ids aren't small nums (0 - 2^8)
 *
 *****************************************************************/

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#ifdef WIN32
#include <X11/Xwinsock.h>
#endif
#include <X11/X.h>
#include <X11/Xproto.h>
#define XSERV_t
#define TRANS_SERVER
#define TRANS_REOPEN
#include <X11/Xtrans/Xtrans.h>
#include <X11/Xtrans/Xtransint.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef WIN32
#include <sys/socket.h>

#if defined(TCPCONN) || defined(STREAMSCONN)
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef apollo
#ifndef NO_TCP_H
#include <netinet/tcp.h>
#endif
#else
#ifdef CSRG_BASED
#include <sys/param.h>
#endif
#include <netinet/tcp.h>
#endif
#include <arpa/inet.h>
#endif

#include <sys/uio.h>

#endif                          /* WIN32 */
#include "misc.h"               /* for typedef of pointer */
#include "osdep.h"
#include <X11/Xpoll.h>
#include "opaque.h"
#include "dixstruct.h"
#include "xace.h"

#define Pid_t pid_t

#ifdef HAVE_GETPEERUCRED
#include <ucred.h>
#include <zone.h>
#endif

#ifdef XSERVER_DTRACE
#include <sys/types.h>
typedef const char *string;

#ifndef HAVE_GETPEERUCRED
#define zoneid_t int
#endif
#include "../dix/Xserver-dtrace.h"
#endif

static int lastfdesc;           /* maximum file descriptor */

fd_set WellKnownConnections;    /* Listener mask */
fd_set EnabledDevices;          /* mask for input devices that are on */
fd_set AllSockets;              /* select on this */
fd_set AllClients;              /* available clients */
fd_set LastSelectMask;          /* mask returned from last select call */
fd_set ClientsWithInput;        /* clients with FULL requests in buffer */
fd_set ClientsWriteBlocked;     /* clients who cannot receive output */
fd_set OutputPending;           /* clients with reply/event data ready to go */
int MaxClients = 0;
Bool NewOutputPending;          /* not yet attempted to write some new output */
Bool AnyClientsWriteBlocked;    /* true if some client blocked on write */
Bool NoListenAll;               /* Don't establish any listening sockets */

static Bool RunFromSmartParent; /* send SIGUSR1 to parent process */
Bool RunFromSigStopParent;      /* send SIGSTOP to our own process; Upstart (or
                                   equivalent) will send SIGCONT back. */
static char dynamic_display[7]; /* display name */
Bool PartialNetwork;            /* continue even if unable to bind all addrs */
static Pid_t ParentProcess;

static Bool debug_conns = FALSE;

fd_set IgnoredClientsWithInput;
static fd_set GrabImperviousClients;
static fd_set SavedAllClients;
static fd_set SavedAllSockets;
static fd_set SavedClientsWithInput;
int GrabInProgress = 0;

#if !defined(WIN32)
int *ConnectionTranslation = NULL;
#else
/*
 * On NT fds are not between 0 and MAXSOCKS, they are unrelated, and there is
 * not even a known maximum value, so use something quite arbitrary for now.
 * Do storage is a hash table of size 256. Collisions are handled in a linked
 * list.
 */

#undef MAXSOCKS
#define MAXSOCKS 500
#undef MAXSELECT
#define MAXSELECT 500

struct _ct_node {
    struct _ct_node *next;
    int key;
    int value;
};

struct _ct_node *ct_head[256];

void
InitConnectionTranslation(void)
{
    memset(ct_head, 0, sizeof(ct_head));
}

int
GetConnectionTranslation(int conn)
{
    struct _ct_node *node = ct_head[conn & 0xff];

    while (node != NULL) {
        if (node->key == conn)
            return node->value;
        node = node->next;
    }
    return 0;
}

void
SetConnectionTranslation(int conn, int client)
{
    struct _ct_node **node = ct_head + (conn & 0xff);

    if (client == 0) {          /* remove entry */
        while (*node != NULL) {
            if ((*node)->key == conn) {
                struct _ct_node *temp = *node;

                *node = (*node)->next;
                free(temp);
                return;
            }
            node = &((*node)->next);
        }
        return;
    }
    else {
        while (*node != NULL) {
            if ((*node)->key == conn) {
                (*node)->value = client;
                return;
            }
            node = &((*node)->next);
        }
        *node = malloc(sizeof(struct _ct_node));
        (*node)->next = NULL;
        (*node)->key = conn;
        (*node)->value = client;
        return;
    }
}

void
ClearConnectionTranslation(void)
{
    unsigned i;

    for (i = 0; i < 256; i++) {
        struct _ct_node *node = ct_head[i];

        while (node != NULL) {
            struct _ct_node *temp = node;

            node = node->next;
            free(temp);
        }
    }
}
#endif

static XtransConnInfo *ListenTransConns = NULL;
static int *ListenTransFds = NULL;
static int ListenTransCount;

static void ErrorConnMax(XtransConnInfo /* trans_conn */ );

static XtransConnInfo
lookup_trans_conn(int fd)
{
    if (ListenTransFds) {
        int i;

        for (i = 0; i < ListenTransCount; i++)
            if (ListenTransFds[i] == fd)
                return ListenTransConns[i];
    }

    return NULL;
}

/* Set MaxClients and lastfdesc, and allocate ConnectionTranslation */

void
InitConnectionLimits(void)
{
    lastfdesc = -1;

#ifndef __CYGWIN__

#if !defined(XNO_SYSCONF) && defined(_SC_OPEN_MAX)
    lastfdesc = sysconf(_SC_OPEN_MAX) - 1;
#endif

#ifdef HAVE_GETDTABLESIZE
    if (lastfdesc < 0)
        lastfdesc = getdtablesize() - 1;
#endif

#ifdef _NFILE
    if (lastfdesc < 0)
        lastfdesc = _NFILE - 1;
#endif

#endif                          /* __CYGWIN__ */

    /* This is the fallback */
    if (lastfdesc < 0)
        lastfdesc = MAXSOCKS;

    if (lastfdesc > MAXSELECT)
        lastfdesc = MAXSELECT;

    if (lastfdesc > MAXCLIENTS) {
        lastfdesc = MAXCLIENTS;
        if (debug_conns)
            ErrorF("REACHED MAXIMUM CLIENTS LIMIT %d\n", MAXCLIENTS);
    }
    MaxClients = lastfdesc;

#ifdef DEBUG
    ErrorF("InitConnectionLimits: MaxClients = %d\n", MaxClients);
#endif

#if !defined(WIN32)
    if (!ConnectionTranslation)
        ConnectionTranslation = (int *) xnfalloc(sizeof(int) * (lastfdesc + 1));
#else
    InitConnectionTranslation();
#endif
}

/*
 * If SIGUSR1 was set to SIG_IGN when the server started, assume that either
 *
 *  a- The parent process is ignoring SIGUSR1
 *
 * or
 *
 *  b- The parent process is expecting a SIGUSR1
 *     when the server is ready to accept connections
 *
 * In the first case, the signal will be harmless, in the second case,
 * the signal will be quite useful.
 */
static void
InitParentProcess(void)
{
#if !defined(WIN32)
    OsSigHandlerPtr handler;

    handler = OsSignal(SIGUSR1, SIG_IGN);
    if (handler == SIG_IGN)
        RunFromSmartParent = TRUE;
    OsSignal(SIGUSR1, handler);
    ParentProcess = getppid();
#endif
}

void
NotifyParentProcess(void)
{
#if !defined(WIN32)
    if (displayfd >= 0) {
        if (write(displayfd, display, strlen(display)) != strlen(display))
            FatalError("Cannot write display number to fd %d\n", displayfd);
        if (write(displayfd, "\n", 1) != 1)
            FatalError("Cannot write display number to fd %d\n", displayfd);
        close(displayfd);
        displayfd = -1;
    }
    if (RunFromSmartParent) {
        if (ParentProcess > 1) {
            kill(ParentProcess, SIGUSR1);
        }
    }
    if (RunFromSigStopParent)
        raise(SIGSTOP);
#endif
}

static Bool
TryCreateSocket(int num, int *partial)
{
    char port[20];

    snprintf(port, sizeof(port), "%d", num);

    return (_XSERVTransMakeAllCOTSServerListeners(port, partial,
                                                  &ListenTransCount,
                                                  &ListenTransConns) >= 0);
}

/*****************
 * CreateWellKnownSockets
 *    At initialization, create the sockets to listen on for new clients.
 *****************/

void
CreateWellKnownSockets(void)
{
    int i;
    int partial;

    FD_ZERO(&AllSockets);
    FD_ZERO(&AllClients);
    FD_ZERO(&LastSelectMask);
    FD_ZERO(&ClientsWithInput);

#if !defined(WIN32)
    for (i = 0; i < MaxClients; i++)
        ConnectionTranslation[i] = 0;
#else
    ClearConnectionTranslation();
#endif

    FD_ZERO(&WellKnownConnections);

    /* display is initialized to "0" by main(). It is then set to the display
     * number if specified on the command line. */

    if (NoListenAll) {
        ListenTransCount = 0;
    }
    else if ((displayfd < 0) || explicit_display) {
        if (TryCreateSocket(atoi(display), &partial) &&
            ListenTransCount >= 1)
            if (!PartialNetwork && partial)
                FatalError ("Failed to establish all listening sockets");
    }
    else { /* -displayfd and no explicit display number */
        Bool found = 0;
        for (i = 0; i < 65535 - X_TCP_PORT; i++) {
            if (TryCreateSocket(i, &partial) && !partial) {
                found = 1;
                break;
            }
            else
                CloseWellKnownConnections();
        }
        if (!found)
            FatalError("Failed to find a socket to listen on");
        snprintf(dynamic_display, sizeof(dynamic_display), "%d", i);
        display = dynamic_display;
    }

    ListenTransFds = malloc(ListenTransCount * sizeof (int));

    for (i = 0; i < ListenTransCount; i++) {
        int fd = _XSERVTransGetConnectionNumber(ListenTransConns[i]);

        ListenTransFds[i] = fd;
        FD_SET(fd, &WellKnownConnections);

        if (!_XSERVTransIsLocal(ListenTransConns[i]))
            DefineSelf (fd);
    }

    if (!XFD_ANYSET(&WellKnownConnections) && !NoListenAll)
        FatalError
            ("Cannot establish any listening sockets - Make sure an X server isn't already running");

#if !defined(WIN32)
    OsSignal(SIGPIPE, SIG_IGN);
    OsSignal(SIGHUP, AutoResetServer);
#endif
    OsSignal(SIGINT, GiveUp);
    OsSignal(SIGTERM, GiveUp);
    XFD_COPYSET(&WellKnownConnections, &AllSockets);
    ResetHosts(display);

    InitParentProcess();

#ifdef XDMCP
    XdmcpInit();
#endif
}

void
ResetWellKnownSockets(void)
{
    int i;

    ResetOsBuffers();

    for (i = 0; i < ListenTransCount; i++) {
        int status = _XSERVTransResetListener(ListenTransConns[i]);

        if (status != TRANS_RESET_NOOP) {
            if (status == TRANS_RESET_FAILURE) {
                /*
                 * ListenTransConns[i] freed by xtrans.
                 * Remove it from out list.
                 */

                FD_CLR(ListenTransFds[i], &WellKnownConnections);
                ListenTransFds[i] = ListenTransFds[ListenTransCount - 1];
                ListenTransConns[i] = ListenTransConns[ListenTransCount - 1];
                ListenTransCount -= 1;
                i -= 1;
            }
            else if (status == TRANS_RESET_NEW_FD) {
                /*
                 * A new file descriptor was allocated (the old one was closed)
                 */

                int newfd = _XSERVTransGetConnectionNumber(ListenTransConns[i]);

                FD_CLR(ListenTransFds[i], &WellKnownConnections);
                ListenTransFds[i] = newfd;
                FD_SET(newfd, &WellKnownConnections);
            }
        }
    }

    ResetAuthorization();
    ResetHosts(display);
    /*
     * restart XDMCP
     */
#ifdef XDMCP
    XdmcpReset();
#endif
}

void
CloseWellKnownConnections(void)
{
    int i;

    for (i = 0; i < ListenTransCount; i++)
        _XSERVTransClose(ListenTransConns[i]);
}

static void
AuthAudit(ClientPtr client, Bool letin,
          struct sockaddr *saddr, int len,
          unsigned int proto_n, char *auth_proto, int auth_id)
{
    char addr[128];
    char client_uid_string[64];
    LocalClientCredRec *lcc;

#ifdef XSERVER_DTRACE
    pid_t client_pid = -1;
    zoneid_t client_zid = -1;
#endif

    if (!len)
        strlcpy(addr, "local host", sizeof(addr));
    else
        switch (saddr->sa_family) {
        case AF_UNSPEC:
#if defined(UNIXCONN) || defined(LOCALCONN)
        case AF_UNIX:
#endif
            strlcpy(addr, "local host", sizeof(addr));
            break;
#if defined(TCPCONN) || defined(STREAMSCONN)
        case AF_INET:
            snprintf(addr, sizeof(addr), "IP %s",
                     inet_ntoa(((struct sockaddr_in *) saddr)->sin_addr));
            break;
#if defined(IPv6) && defined(AF_INET6)
        case AF_INET6:{
            char ipaddr[INET6_ADDRSTRLEN];

            inet_ntop(AF_INET6, &((struct sockaddr_in6 *) saddr)->sin6_addr,
                      ipaddr, sizeof(ipaddr));
            snprintf(addr, sizeof(addr), "IP %s", ipaddr);
        }
            break;
#endif
#endif
        default:
            strlcpy(addr, "unknown address", sizeof(addr));
        }

    if (GetLocalClientCreds(client, &lcc) != -1) {
        int slen;               /* length written to client_uid_string */

        strcpy(client_uid_string, " ( ");
        slen = 3;

        if (lcc->fieldsSet & LCC_UID_SET) {
            snprintf(client_uid_string + slen,
                     sizeof(client_uid_string) - slen,
                     "uid=%ld ", (long) lcc->euid);
            slen = strlen(client_uid_string);
        }

        if (lcc->fieldsSet & LCC_GID_SET) {
            snprintf(client_uid_string + slen,
                     sizeof(client_uid_string) - slen,
                     "gid=%ld ", (long) lcc->egid);
            slen = strlen(client_uid_string);
        }

        if (lcc->fieldsSet & LCC_PID_SET) {
#ifdef XSERVER_DTRACE
            client_pid = lcc->pid;
#endif
            snprintf(client_uid_string + slen,
                     sizeof(client_uid_string) - slen,
                     "pid=%ld ", (long) lcc->pid);
            slen = strlen(client_uid_string);
        }

        if (lcc->fieldsSet & LCC_ZID_SET) {
#ifdef XSERVER_DTRACE
            client_zid = lcc->zoneid;
#endif
            snprintf(client_uid_string + slen,
                     sizeof(client_uid_string) - slen,
                     "zoneid=%ld ", (long) lcc->zoneid);
            slen = strlen(client_uid_string);
        }

        snprintf(client_uid_string + slen, sizeof(client_uid_string) - slen,
                 ")");
        FreeLocalClientCreds(lcc);
    }
    else {
        client_uid_string[0] = '\0';
    }

#ifdef XSERVER_DTRACE
    XSERVER_CLIENT_AUTH(client->index, addr, client_pid, client_zid);
#endif
    if (auditTrailLevel > 1) {
        if (proto_n)
            AuditF("client %d %s from %s%s\n  Auth name: %.*s ID: %d\n",
                   client->index, letin ? "connected" : "rejected", addr,
                   client_uid_string, (int) proto_n, auth_proto, auth_id);
        else
            AuditF("client %d %s from %s%s\n",
                   client->index, letin ? "connected" : "rejected", addr,
                   client_uid_string);

    }
}

XID
AuthorizationIDOfClient(ClientPtr client)
{
    if (client->osPrivate)
        return ((OsCommPtr) client->osPrivate)->auth_id;
    else
        return None;
}

/*****************************************************************
 * ClientAuthorized
 *
 *    Sent by the client at connection setup:
 *                typedef struct _xConnClientPrefix {
 *                   CARD8	byteOrder;
 *                   BYTE	pad;
 *                   CARD16	majorVersion, minorVersion;
 *                   CARD16	nbytesAuthProto;    
 *                   CARD16	nbytesAuthString;   
 *                 } xConnClientPrefix;
 *
 *     	It is hoped that eventually one protocol will be agreed upon.  In the
 *        mean time, a server that implements a different protocol than the
 *        client expects, or a server that only implements the host-based
 *        mechanism, will simply ignore this information.
 *
 *****************************************************************/

const char *
ClientAuthorized(ClientPtr client,
                 unsigned int proto_n, char *auth_proto,
                 unsigned int string_n, char *auth_string)
{
    OsCommPtr priv;
    Xtransaddr *from = NULL;
    int family;
    int fromlen;
    XID auth_id;
    const char *reason = NULL;
    XtransConnInfo trans_conn;

    priv = (OsCommPtr) client->osPrivate;
    trans_conn = priv->trans_conn;

    /* Allow any client to connect without authorization on a launchd socket,
       because it is securely created -- this prevents a race condition on launch */
    if (trans_conn->flags & TRANS_NOXAUTH) {
        auth_id = (XID) 0L;
    }
    else {
        auth_id =
            CheckAuthorization(proto_n, auth_proto, string_n, auth_string,
                               client, &reason);
    }

    if (auth_id == (XID) ~0L) {
        if (_XSERVTransGetPeerAddr(trans_conn, &family, &fromlen, &from) != -1) {
            if (InvalidHost((struct sockaddr *) from, fromlen, client))
                AuthAudit(client, FALSE, (struct sockaddr *) from,
                          fromlen, proto_n, auth_proto, auth_id);
            else {
                auth_id = (XID) 0;
#ifdef XSERVER_DTRACE
                if ((auditTrailLevel > 1) || XSERVER_CLIENT_AUTH_ENABLED())
#else
                if (auditTrailLevel > 1)
#endif
                    AuthAudit(client, TRUE,
                              (struct sockaddr *) from, fromlen,
                              proto_n, auth_proto, auth_id);
            }

            free(from);
        }

        if (auth_id == (XID) ~0L) {
            if (reason)
                return reason;
            else
                return "Client is not authorized to connect to Server";
        }
    }
#ifdef XSERVER_DTRACE
    else if ((auditTrailLevel > 1) || XSERVER_CLIENT_AUTH_ENABLED())
#else
    else if (auditTrailLevel > 1)
#endif
    {
        if (_XSERVTransGetPeerAddr(trans_conn, &family, &fromlen, &from) != -1) {
            AuthAudit(client, TRUE, (struct sockaddr *) from, fromlen,
                      proto_n, auth_proto, auth_id);

            free(from);
        }
    }
    priv->auth_id = auth_id;
    priv->conn_time = 0;

#ifdef XDMCP
    /* indicate to Xdmcp protocol that we've opened new client */
    XdmcpOpenDisplay(priv->fd);
#endif                          /* XDMCP */

    XaceHook(XACE_AUTH_AVAIL, client, auth_id);

    /* At this point, if the client is authorized to change the access control
     * list, we should getpeername() information, and add the client to
     * the selfhosts list.  It's not really the host machine, but the
     * true purpose of the selfhosts list is to see who may change the
     * access control list.
     */
    return ((char *) NULL);
}

static ClientPtr
AllocNewConnection(XtransConnInfo trans_conn, int fd, CARD32 conn_time)
{
    OsCommPtr oc;
    ClientPtr client;

    if (
#ifndef WIN32
           fd >= lastfdesc
#else
           XFD_SETCOUNT(&AllClients) >= MaxClients
#endif
        )
        return NullClient;
    oc = malloc(sizeof(OsCommRec));
    if (!oc)
        return NullClient;
    oc->trans_conn = trans_conn;
    oc->fd = fd;
    oc->input = (ConnectionInputPtr) NULL;
    oc->output = (ConnectionOutputPtr) NULL;
    oc->auth_id = None;
    oc->conn_time = conn_time;
    if (!(client = NextAvailableClient((void *) oc))) {
        free(oc);
        return NullClient;
    }
    client->local = ComputeLocalClient(client);
#if !defined(WIN32)
    ConnectionTranslation[fd] = client->index;
#else
    SetConnectionTranslation(fd, client->index);
#endif
    if (GrabInProgress) {
        FD_SET(fd, &SavedAllClients);
        FD_SET(fd, &SavedAllSockets);
    }
    else {
        FD_SET(fd, &AllClients);
        FD_SET(fd, &AllSockets);
    }

#ifdef DEBUG
    ErrorF("AllocNewConnection: client index = %d, socket fd = %d\n",
           client->index, fd);
#endif
#ifdef XSERVER_DTRACE
    XSERVER_CLIENT_CONNECT(client->index, fd);
#endif

    return client;
}

/*****************
 * EstablishNewConnections
 *    If anyone is waiting on listened sockets, accept them.
 *    Returns a mask with indices of new clients.  Updates AllClients
 *    and AllSockets.
 *****************/

 /*ARGSUSED*/ Bool
EstablishNewConnections(ClientPtr clientUnused, void *closure)
{
    fd_set readyconnections;    /* set of listeners that are ready */
    int curconn;                /* fd of listener that's ready */
    register int newconn;       /* fd of new client */
    CARD32 connect_time;
    register int i;
    register ClientPtr client;
    register OsCommPtr oc;
    fd_set tmask;

    XFD_ANDSET(&tmask, (fd_set *) closure, &WellKnownConnections);
    XFD_COPYSET(&tmask, &readyconnections);
    if (!XFD_ANYSET(&readyconnections))
        return TRUE;
    connect_time = GetTimeInMillis();
    /* kill off stragglers */
    for (i = 1; i < currentMaxClients; i++) {
        if ((client = clients[i])) {
            oc = (OsCommPtr) (client->osPrivate);
            if ((oc && (oc->conn_time != 0) &&
                 (connect_time - oc->conn_time) >= TimeOutValue) ||
                (client->noClientException != Success && !client->clientGone))
                CloseDownClient(client);
        }
    }
#ifndef WIN32
    for (i = 0; i < howmany(XFD_SETSIZE, NFDBITS); i++) {
        while (readyconnections.fds_bits[i])
#else
    for (i = 0; i < XFD_SETCOUNT(&readyconnections); i++)
#endif
    {
        XtransConnInfo trans_conn, new_trans_conn;
        int status;

#ifndef WIN32
        curconn = mffs(readyconnections.fds_bits[i]) - 1;
        readyconnections.fds_bits[i] &= ~((fd_mask) 1 << curconn);
        curconn += (i * (sizeof(fd_mask) * 8));
#else
        curconn = XFD_FD(&readyconnections, i);
#endif

        if ((trans_conn = lookup_trans_conn(curconn)) == NULL)
            continue;

        if ((new_trans_conn = _XSERVTransAccept(trans_conn, &status)) == NULL)
            continue;

        newconn = _XSERVTransGetConnectionNumber(new_trans_conn);

        if (newconn < lastfdesc) {
            int clientid;

#if !defined(WIN32)
            clientid = ConnectionTranslation[newconn];
#else
            clientid = GetConnectionTranslation(newconn);
#endif
            if (clientid && (client = clients[clientid]))
                CloseDownClient(client);
        }

        _XSERVTransSetOption(new_trans_conn, TRANS_NONBLOCKING, 1);

        if (trans_conn->flags & TRANS_NOXAUTH)
            new_trans_conn->flags = new_trans_conn->flags | TRANS_NOXAUTH;

        if (!AllocNewConnection(new_trans_conn, newconn, connect_time)) {
            ErrorConnMax(new_trans_conn);
            _XSERVTransClose(new_trans_conn);
        }
    }
#ifndef WIN32
}
#endif
return TRUE;
}

#define NOROOM "Maximum number of clients reached"

/************
 *   ErrorConnMax
 *     Fail a connection due to lack of client or file descriptor space
 ************/

#define BOTIMEOUT 200           /* in milliseconds */

static void
ErrorConnMax(XtransConnInfo trans_conn)
{
    int fd = _XSERVTransGetConnectionNumber(trans_conn);
    xConnSetupPrefix csp;
    char pad[3] = { 0, 0, 0 };
    struct iovec iov[3];
    char order = 0;
    int whichbyte = 1;
    struct timeval waittime;
    fd_set mask;

    /* if these seems like a lot of trouble to go to, it probably is */
    waittime.tv_sec = BOTIMEOUT / MILLI_PER_SECOND;
    waittime.tv_usec = (BOTIMEOUT % MILLI_PER_SECOND) *
        (1000000 / MILLI_PER_SECOND);
    FD_ZERO(&mask);
    FD_SET(fd, &mask);
    (void) Select(fd + 1, &mask, NULL, NULL, &waittime);
    /* try to read the byte-order of the connection */
    (void) _XSERVTransRead(trans_conn, &order, 1);
    if (order == 'l' || order == 'B' || order == 'r' || order == 'R') {
        csp.success = xFalse;
        csp.lengthReason = sizeof(NOROOM) - 1;
        csp.length = (sizeof(NOROOM) + 2) >> 2;
        csp.majorVersion = X_PROTOCOL;
        csp.minorVersion = X_PROTOCOL_REVISION;
	if (((*(char *) &whichbyte) && (order == 'B' || order == 'R')) ||
	    (!(*(char *) &whichbyte) && (order == 'l' || order == 'r'))) {
            swaps(&csp.majorVersion);
            swaps(&csp.minorVersion);
            swaps(&csp.length);
        }
        iov[0].iov_len = sz_xConnSetupPrefix;
        iov[0].iov_base = (char *) &csp;
        iov[1].iov_len = csp.lengthReason;
        iov[1].iov_base = (void *) NOROOM;
        iov[2].iov_len = (4 - (csp.lengthReason & 3)) & 3;
        iov[2].iov_base = pad;
        (void) _XSERVTransWritev(trans_conn, iov, 3);
    }
}

/************
 *   CloseDownFileDescriptor:
 *     Remove this file descriptor and it's I/O buffers, etc.
 ************/

static void
CloseDownFileDescriptor(OsCommPtr oc)
{
    int connection = oc->fd;

    if (oc->trans_conn) {
        _XSERVTransDisconnect(oc->trans_conn);
        _XSERVTransClose(oc->trans_conn);
    }
#ifndef WIN32
    ConnectionTranslation[connection] = 0;
#else
    SetConnectionTranslation(connection, 0);
#endif
    FD_CLR(connection, &AllSockets);
    FD_CLR(connection, &AllClients);
    FD_CLR(connection, &ClientsWithInput);
    FD_CLR(connection, &GrabImperviousClients);
    if (GrabInProgress) {
        FD_CLR(connection, &SavedAllSockets);
        FD_CLR(connection, &SavedAllClients);
        FD_CLR(connection, &SavedClientsWithInput);
    }
    FD_CLR(connection, &ClientsWriteBlocked);
    if (!XFD_ANYSET(&ClientsWriteBlocked))
        AnyClientsWriteBlocked = FALSE;
    FD_CLR(connection, &OutputPending);
}

/*****************
 * CheckConnections
 *    Some connection has died, go find which one and shut it down 
 *    The file descriptor has been closed, but is still in AllClients.
 *    If would truly be wonderful if select() would put the bogus
 *    file descriptors in the exception mask, but nooooo.  So we have
 *    to check each and every socket individually.
 *****************/

void
CheckConnections(void)
{
#ifndef WIN32
    fd_mask mask;
#endif
    fd_set tmask;
    int curclient, curoff;
    int i;
    struct timeval notime;
    int r;

#ifdef WIN32
    fd_set savedAllClients;
#endif

    notime.tv_sec = 0;
    notime.tv_usec = 0;

#ifndef WIN32
    for (i = 0; i < howmany(XFD_SETSIZE, NFDBITS); i++) {
        mask = AllClients.fds_bits[i];
        while (mask) {
            curoff = mffs(mask) - 1;
            curclient = curoff + (i * (sizeof(fd_mask) * 8));
            FD_ZERO(&tmask);
            FD_SET(curclient, &tmask);
            do {
                r = Select(curclient + 1, &tmask, NULL, NULL, &notime);
            } while (r < 0 && (errno == EINTR || errno == EAGAIN));
            if (r < 0)
                if (ConnectionTranslation[curclient] > 0)
                    CloseDownClient(clients[ConnectionTranslation[curclient]]);
            mask &= ~((fd_mask) 1 << curoff);
        }
    }
#else
    XFD_COPYSET(&AllClients, &savedAllClients);
    for (i = 0; i < XFD_SETCOUNT(&savedAllClients); i++) {
        curclient = XFD_FD(&savedAllClients, i);
        FD_ZERO(&tmask);
        FD_SET(curclient, &tmask);
        do {
            r = Select(curclient + 1, &tmask, NULL, NULL, &notime);
        } while (r < 0 && (errno == EINTR || errno == EAGAIN));
        if (r < 0)
            if (GetConnectionTranslation(curclient) > 0)
                CloseDownClient(clients[GetConnectionTranslation(curclient)]);
    }
#endif
}

/*****************
 * CloseDownConnection
 *    Delete client from AllClients and free resources 
 *****************/

void
CloseDownConnection(ClientPtr client)
{
    OsCommPtr oc = (OsCommPtr) client->osPrivate;

    if (FlushCallback)
        CallCallbacks(&FlushCallback, NULL);

    if (oc->output)
	FlushClient(client, oc, (char *) NULL, 0);
#ifdef XDMCP
    XdmcpCloseDisplay(oc->fd);
#endif
    CloseDownFileDescriptor(oc);
    FreeOsBuffers(oc);
    free(client->osPrivate);
    client->osPrivate = (void *) NULL;
    if (auditTrailLevel > 1)
        AuditF("client %d disconnected\n", client->index);
}

void
AddGeneralSocket(int fd)
{
    FD_SET(fd, &AllSockets);
    if (GrabInProgress)
        FD_SET(fd, &SavedAllSockets);
}

void
AddEnabledDevice(int fd)
{
    FD_SET(fd, &EnabledDevices);
    AddGeneralSocket(fd);
}

void
RemoveGeneralSocket(int fd)
{
    FD_CLR(fd, &AllSockets);
    if (GrabInProgress)
        FD_CLR(fd, &SavedAllSockets);
}

void
RemoveEnabledDevice(int fd)
{
    FD_CLR(fd, &EnabledDevices);
    RemoveGeneralSocket(fd);
}

/*****************
 * OnlyListenToOneClient:
 *    Only accept requests from  one client.  Continue to handle new
 *    connections, but don't take any protocol requests from the new
 *    ones.  Note that if GrabInProgress is set, EstablishNewConnections
 *    needs to put new clients into SavedAllSockets and SavedAllClients.
 *    Note also that there is no timeout for this in the protocol.
 *    This routine is "undone" by ListenToAllClients()
 *****************/

int
OnlyListenToOneClient(ClientPtr client)
{
    OsCommPtr oc = (OsCommPtr) client->osPrivate;
    int rc, connection = oc->fd;

    rc = XaceHook(XACE_SERVER_ACCESS, client, DixGrabAccess);
    if (rc != Success)
        return rc;

    if (!GrabInProgress) {
        XFD_COPYSET(&ClientsWithInput, &SavedClientsWithInput);
        XFD_ANDSET(&ClientsWithInput,
                   &ClientsWithInput, &GrabImperviousClients);
        if (FD_ISSET(connection, &SavedClientsWithInput)) {
            FD_CLR(connection, &SavedClientsWithInput);
            FD_SET(connection, &ClientsWithInput);
        }
        XFD_UNSET(&SavedClientsWithInput, &GrabImperviousClients);
        XFD_COPYSET(&AllSockets, &SavedAllSockets);
        XFD_COPYSET(&AllClients, &SavedAllClients);
        XFD_UNSET(&AllSockets, &AllClients);
        XFD_ANDSET(&AllClients, &AllClients, &GrabImperviousClients);
        FD_SET(connection, &AllClients);
        XFD_ORSET(&AllSockets, &AllSockets, &AllClients);
        GrabInProgress = client->index;
    }
    return rc;
}

/****************
 * ListenToAllClients:
 *    Undoes OnlyListentToOneClient()
 ****************/

void
ListenToAllClients(void)
{
    if (GrabInProgress) {
        XFD_ORSET(&AllSockets, &AllSockets, &SavedAllSockets);
        XFD_ORSET(&AllClients, &AllClients, &SavedAllClients);
        XFD_ORSET(&ClientsWithInput, &ClientsWithInput, &SavedClientsWithInput);
        GrabInProgress = 0;
    }
}

/****************
 * IgnoreClient
 *    Removes one client from input masks.
 *    Must have cooresponding call to AttendClient.
 ****************/

void
IgnoreClient(ClientPtr client)
{
    OsCommPtr oc = (OsCommPtr) client->osPrivate;
    int connection = oc->fd;

    client->ignoreCount++;
    if (client->ignoreCount > 1)
        return;

    isItTimeToYield = TRUE;
    if (!GrabInProgress || FD_ISSET(connection, &AllClients)) {
        if (FD_ISSET(connection, &ClientsWithInput))
            FD_SET(connection, &IgnoredClientsWithInput);
        else
            FD_CLR(connection, &IgnoredClientsWithInput);
        FD_CLR(connection, &ClientsWithInput);
        FD_CLR(connection, &AllSockets);
        FD_CLR(connection, &AllClients);
        FD_CLR(connection, &LastSelectMask);
    }
    else {
        if (FD_ISSET(connection, &SavedClientsWithInput))
            FD_SET(connection, &IgnoredClientsWithInput);
        else
            FD_CLR(connection, &IgnoredClientsWithInput);
        FD_CLR(connection, &SavedClientsWithInput);
        FD_CLR(connection, &SavedAllSockets);
        FD_CLR(connection, &SavedAllClients);
    }
}

/****************
 * AttendClient
 *    Adds one client back into the input masks.
 ****************/

void
AttendClient(ClientPtr client)
{
    OsCommPtr oc = (OsCommPtr) client->osPrivate;
    int connection = oc->fd;

    client->ignoreCount--;
    if (client->ignoreCount)
        return;

    if (!GrabInProgress || GrabInProgress == client->index ||
        FD_ISSET(connection, &GrabImperviousClients)) {
        FD_SET(connection, &AllClients);
        FD_SET(connection, &AllSockets);
        FD_SET(connection, &LastSelectMask);
        if (FD_ISSET(connection, &IgnoredClientsWithInput))
            FD_SET(connection, &ClientsWithInput);
    }
    else {
        FD_SET(connection, &SavedAllClients);
        FD_SET(connection, &SavedAllSockets);
        if (FD_ISSET(connection, &IgnoredClientsWithInput))
            FD_SET(connection, &SavedClientsWithInput);
    }
}

/* make client impervious to grabs; assume only executing client calls this */

void
MakeClientGrabImpervious(ClientPtr client)
{
    OsCommPtr oc = (OsCommPtr) client->osPrivate;
    int connection = oc->fd;

    FD_SET(connection, &GrabImperviousClients);

    if (ServerGrabCallback) {
        ServerGrabInfoRec grabinfo;

        grabinfo.client = client;
        grabinfo.grabstate = CLIENT_IMPERVIOUS;
        CallCallbacks(&ServerGrabCallback, &grabinfo);
    }
}

/* make client pervious to grabs; assume only executing client calls this */

void
MakeClientGrabPervious(ClientPtr client)
{
    OsCommPtr oc = (OsCommPtr) client->osPrivate;
    int connection = oc->fd;

    FD_CLR(connection, &GrabImperviousClients);
    if (GrabInProgress && (GrabInProgress != client->index)) {
        if (FD_ISSET(connection, &ClientsWithInput)) {
            FD_SET(connection, &SavedClientsWithInput);
            FD_CLR(connection, &ClientsWithInput);
        }
        FD_CLR(connection, &AllSockets);
        FD_CLR(connection, &AllClients);
        isItTimeToYield = TRUE;
    }

    if (ServerGrabCallback) {
        ServerGrabInfoRec grabinfo;

        grabinfo.client = client;
        grabinfo.grabstate = CLIENT_PERVIOUS;
        CallCallbacks(&ServerGrabCallback, &grabinfo);
    }
}

/* Add a fd (from launchd or similar) to our listeners */
void
ListenOnOpenFD(int fd, int noxauth)
{
    char port[256];
    XtransConnInfo ciptr;
    const char *display_env = getenv("DISPLAY");

    if (display_env && (strncmp(display_env, "/tmp/launch", 11) == 0)) {
        /* Make the path the launchd socket if our DISPLAY is set right */
        strcpy(port, display_env);
    }
    else {
        /* Just some default so things don't break and die. */
        snprintf(port, sizeof(port), ":%d", atoi(display));
    }

    /* Make our XtransConnInfo
     * TRANS_SOCKET_LOCAL_INDEX = 5 from Xtrans.c
     */
    ciptr = _XSERVTransReopenCOTSServer(5, fd, port);
    if (ciptr == NULL) {
        ErrorF("Got NULL while trying to Reopen listen port.\n");
        return;
    }

    if (noxauth)
        ciptr->flags = ciptr->flags | TRANS_NOXAUTH;

    /* Allocate space to store it */
    ListenTransFds =
        (int *) realloc(ListenTransFds, (ListenTransCount + 1) * sizeof(int));
    ListenTransConns =
        (XtransConnInfo *) realloc(ListenTransConns,
                                   (ListenTransCount +
                                    1) * sizeof(XtransConnInfo));

    /* Store it */
    ListenTransConns[ListenTransCount] = ciptr;
    ListenTransFds[ListenTransCount] = fd;

    FD_SET(fd, &WellKnownConnections);
    FD_SET(fd, &AllSockets);

    /* Increment the count */
    ListenTransCount++;

    /* This *might* not be needed... /shrug */
    ResetAuthorization();
    ResetHosts(display);
#ifdef XDMCP
    XdmcpReset();
#endif
}

/* based on TRANS(SocketUNIXAccept) (XtransConnInfo ciptr, int *status) */
Bool
AddClientOnOpenFD(int fd)
{
    XtransConnInfo ciptr;
    CARD32 connect_time;
    char port[20];

    snprintf(port, sizeof(port), ":%d", atoi(display));
    ciptr = _XSERVTransReopenCOTSServer(5, fd, port);
    if (ciptr == NULL)
        return FALSE;

    _XSERVTransSetOption(ciptr, TRANS_NONBLOCKING, 1);
    ciptr->flags |= TRANS_NOXAUTH;

    connect_time = GetTimeInMillis();

    if (!AllocNewConnection(ciptr, fd, connect_time)) {
        ErrorConnMax(ciptr);
        _XSERVTransClose(ciptr);
        return FALSE;
    }

    return TRUE;
}
