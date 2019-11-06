
#include "pipe/p_compiler.h"
#include "util/u_network.h"
#include "util/u_debug.h"
#include "util/u_string.h"

#include <stdio.h>
#if defined(PIPE_OS_WINDOWS)
#  include <winsock2.h>
#  include <windows.h>
#  include <ws2tcpip.h>
#elif defined(PIPE_OS_UNIX)
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <netdb.h>
#else
#  warning "No socket implementation"
#endif

boolean
u_socket_init(void)
{
#if defined(PIPE_OS_WINDOWS)
   WORD wVersionRequested;
   WSADATA wsaData;
   int err;

   /* Use the MAKEWORD(lowbyte, highbyte) macro declared in Windef.h */
   wVersionRequested = MAKEWORD(1, 1);

   err = WSAStartup(wVersionRequested, &wsaData);
   if (err != 0) {
      debug_printf("WSAStartup failed with error: %d\n", err);
      return FALSE;
   }
   return TRUE;
#elif defined(PIPE_HAVE_SOCKETS)
   return TRUE;
#else
   return FALSE;
#endif
}

void
u_socket_stop(void)
{
#if defined(PIPE_OS_WINDOWS)
   WSACleanup();
#endif
}

void
u_socket_close(int s)
{
   if (s < 0)
      return;

#if defined(PIPE_OS_UNIX)
   shutdown(s, SHUT_RDWR);
   close(s);
#elif defined(PIPE_OS_WINDOWS)
   shutdown(s, SD_BOTH);
   closesocket(s);
#else
   assert(0);
#endif
}

int u_socket_accept(int s)
{
#if defined(PIPE_HAVE_SOCKETS)
   return accept(s, NULL, NULL);
#else
   return -1;
#endif
}

int
u_socket_send(int s, void *data, size_t size)
{
#if defined(PIPE_HAVE_SOCKETS)
   return send(s, data, size, 0);
#else
   return -1;
#endif
}

int
u_socket_peek(int s, void *data, size_t size)
{
#if defined(PIPE_HAVE_SOCKETS)
   return recv(s, data, size, MSG_PEEK);
#else
   return -1;
#endif
}

int
u_socket_recv(int s, void *data, size_t size)
{
#if defined(PIPE_HAVE_SOCKETS)
   return recv(s, data, size, 0);
#else
   return -1;
#endif
}

int
u_socket_connect(const char *hostname, uint16_t port)
{
#if defined(PIPE_HAVE_SOCKETS)
   int s, r;
   struct addrinfo hints, *addr;
   char portString[20];

   memset(&hints, 0, sizeof hints);
   hints.ai_family = AF_UNSPEC; // AF_INET or AF_INET6 to force version
   hints.ai_socktype = SOCK_STREAM;

   snprintf(portString, sizeof(portString), "%d", port);

   r = getaddrinfo(hostname, portString, NULL, &addr);
   if (r != 0) {
      return -1;
   }

   s = socket(addr->ai_family, SOCK_STREAM, IPPROTO_TCP);
   if (s < 0) {
      freeaddrinfo(addr);
      return -1;
   }

   if (connect(s, addr->ai_addr, (int) addr->ai_addrlen)) {
      u_socket_close(s);
      freeaddrinfo(addr);
      return -1;
   }

   freeaddrinfo(addr);

   return s;
#else
   assert(0);
   return -1;
#endif
}

int
u_socket_listen_on_port(uint16_t portnum)
{
#if defined(PIPE_HAVE_SOCKETS)
   int s;
   struct sockaddr_in sa;
   memset(&sa, 0, sizeof(struct sockaddr_in));

   sa.sin_family = AF_INET;
   sa.sin_port = htons(portnum);

   s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
   if (s < 0)
      return -1;

   if (bind(s, (struct sockaddr *)&sa, sizeof(struct sockaddr_in)) == -1) {
      u_socket_close(s);
      return -1;
   }

   listen(s, 1);

   return s;
#else
   assert(0);
   return -1;
#endif
}

void
u_socket_block(int s, boolean block)
{
#if defined(PIPE_OS_UNIX)
   int old = fcntl(s, F_GETFL, 0);
   if (old == -1)
      return;

   /* TODO obey block */
   if (block)
      fcntl(s, F_SETFL, old & ~O_NONBLOCK);
   else
      fcntl(s, F_SETFL, old | O_NONBLOCK);
#elif defined(PIPE_OS_WINDOWS)
   u_long iMode = block ? 0 : 1;
   ioctlsocket(s, FIONBIO, &iMode);
#else
   assert(0);
#endif
}
