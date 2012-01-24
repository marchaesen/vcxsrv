#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#ifdef WIN32
#include <X11/Xwinsock.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#if NTDDI_VERSION < NTDDI_VISTA
const char *inet_ntop(int af, const void *src, char *dst, socklen_t cnt)
{
    if (af == AF_INET)
    {
        struct sockaddr_in in;
        memset(&in, 0, sizeof(in));
        in.sin_family = AF_INET;
        memcpy(&in.sin_addr, src, sizeof(struct in_addr));
        if (getnameinfo((struct sockaddr *)&in, sizeof(struct sockaddr_in), dst, cnt, NULL, 0, NI_NUMERICHOST) != 0)
        {
            errno = WSAGetLastError();
            return NULL;
        }
        else return dst;
    }
    else if (af == AF_INET6)
    {
        struct sockaddr_in6 in;
        memset(&in, 0, sizeof(in));
        in.sin6_family = AF_INET6;
        memcpy(&in.sin6_addr, src, sizeof(struct in_addr6));
        if (getnameinfo((struct sockaddr *)&in, sizeof(struct sockaddr_in6), dst, cnt, NULL, 0, NI_NUMERICHOST) != 0)
        {
            errno = WSAGetLastError();
            return NULL;
        }
        else return dst;
    }
    errno = WSAEAFNOSUPPORT;
    return NULL;
}

int inet_pton(int af, const char *src, void *dst)
{
    struct sockaddr_storage ss;
    int sslen = sizeof(ss);
    if (af == AF_INET)
    {
        struct in_addr out;
        char buffer[INET_ADDRSTRLEN + 1];
        strncpy (buffer, src, INET_ADDRSTRLEN);
        buffer [INET_ADDRSTRLEN] = '\0';
        if (WSAStringToAddressA(buffer, AF_INET, NULL, (struct sockaddr*)&ss, &sslen) == SOCKET_ERROR)
        {
            errno = WSAGetLastError();
            return 0;
        }
        else
        {
            out = ((struct sockaddr_in *)&ss)->sin_addr;
            memcpy (dst, &out, sizeof(struct in_addr));
            return 1;
        }
    }
    else if (af == AF_INET6)
    {
        struct in6_addr out6;
        char buffer6[INET6_ADDRSTRLEN + 1];
        strncpy (buffer6, src, INET6_ADDRSTRLEN);
        buffer6 [INET6_ADDRSTRLEN] = '\0';
        if (WSAStringToAddressA(buffer6, AF_INET6, NULL, (struct sockaddr*)&ss, &sslen) == SOCKET_ERROR)
        {
            errno = WSAGetLastError();
            return 0;
        }
        else
        {
            out6 = ((struct sockaddr_in6 *)&ss)->sin6_addr;
            memcpy (dst, &out6, sizeof(struct in6_addr));
            return 1;
        }
    }
    errno = WSAEAFNOSUPPORT;
    return -1;
}
#endif
