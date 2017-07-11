/*
 *
Copyright 1989, 1998  The Open Group

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
 * *
 * Author:  Jim Fulton, MIT X Consortium
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef WIN32
#include <sys/socket.h>
#ifndef EPROTOTYPE
#define EPROTOTYPE WSAEPROTOTYPE
#endif
#endif
#include <X11/X.h>
#include <signal.h>
#include <setjmp.h>
#include <ctype.h>
#ifndef __TYPES__
#include <sys/types.h>
#define __TYPES__
#endif
#ifndef WIN32
#ifndef Lynx
#include <sys/socket.h>
#else
#include <socket.h>
#endif
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_NET_ERRNO_H
#include <net/errno.h>
#endif /* HAVE_NET_ERRNO_H */
#endif /* !WIN32 */
#include <errno.h>
#include "xauth.h"

#include <sys/stat.h>
#include <limits.h>

#ifndef WIN32
#include <arpa/inet.h>
#endif

const char *
get_hostname (Xauth *auth)
{
#ifdef TCPCONN
    static struct hostent *hp;
    int af;

    hp = NULL;
#endif

    if (auth->address_length == 0)
	return "Illegal Address";
#ifdef TCPCONN
    if (auth->family == FamilyInternet
#if defined(IPv6) && defined(AF_INET6)
      || auth->family == FamilyInternet6
#endif
	)
    {
#if defined(IPv6) && defined(AF_INET6)
	if (auth->family == FamilyInternet6)
	    af = AF_INET6;
	else
#endif
	    af = AF_INET;
	if (no_name_lookups == False) {
	    hp = gethostbyaddr (auth->address, auth->address_length, af);
	}
	if (hp)
	  return (hp->h_name);
#if defined(IPv6) && defined(AF_INET6)
	else if (af == AF_INET6) {
	  static char addr[INET6_ADDRSTRLEN+2];
	  /* Add [] for clarity to distinguish between address & display,
	     like RFC 2732 for URL's.  Not required, since X display syntax
	     always ends in :<display>, but makes it easier for people to read
	     and less confusing to those who expect the RFC 2732 style. */
	  addr[0] = '[';
	  if (inet_ntop(af, auth->address, addr + 1, INET6_ADDRSTRLEN) == NULL)
	    return NULL;
	  strcat(addr, "]");
          return addr;
	}
#endif
	else {
	  return (inet_ntoa(*((struct in_addr *)(auth->address))));
	}
    }
#endif

    return (NULL);
}

#if defined(TCPCONN) && (!defined(IPv6) || !defined(AF_INET6))
/*
 * cribbed from lib/X/XConnDis.c
 */
static Bool
get_inet_address(char *name, unsigned int *resultp)
{
    unsigned int hostinetaddr = inet_addr (name);
    struct hostent *host_ptr;
    struct sockaddr_in inaddr;		/* dummy variable for size calcs */

#ifndef INADDR_NONE
#define INADDR_NONE -1
#endif

    if (hostinetaddr == INADDR_NONE) {
	if ((host_ptr = gethostbyname (name)) == NULL) {
	    /* No such host! */
	    errno = EINVAL;
	    return False;
	}
	/* Check the address type for an internet host. */
	if (host_ptr->h_addrtype != AF_INET) {
	    /* Not an Internet host! */
	    errno = EPROTOTYPE;
	    return False;
	}

	memmove( (char *)&hostinetaddr, (char *)host_ptr->h_addr,
	      sizeof(inaddr.sin_addr));
    }
    *resultp = hostinetaddr;
    return True;
}
#endif


struct addrlist *get_address_info (
    int family,
    const char *fulldpyname,
    int prefix,
    char *host)
{
    struct addrlist *retval = NULL;
    int len = 0;
    const void *src = NULL;
#ifdef TCPCONN
#if defined(IPv6) && defined(AF_INET6)
    struct addrlist *lastrv = NULL;
    struct addrinfo *firstai = NULL;
    struct addrinfo *ai = NULL;
    struct addrinfo hints;
#else
    unsigned int hostinetaddr;
#endif
#endif
    char buf[255];

    /*
     * based on the family, set the pointer src to the start of the address
     * information to be copied and set len to the number of bytes.
     */
    switch (family) {
      case FamilyLocal:			/* hostname/unix:0 */
					/* handle unix:0 and :0 specially */
	if (prefix == 0 && (strncmp (fulldpyname, "unix:", 5) == 0 ||
			    fulldpyname[0] == ':')) {

	    if (!get_local_hostname (buf, sizeof buf)) {
		len = 0;
	    } else {
		src = buf;
		len = strlen (buf);
	    }
	} else {
	    char path[PATH_MAX];
	    struct stat sbuf;
	    int is_path_to_socket = 0;

#ifdef HAVE_STRLCPY
	    strlcpy(path, fulldpyname, sizeof(path));
#else
	    strncpy(path, fulldpyname, sizeof(path));
	    path[sizeof(path) - 1] = '\0';
#endif
	    if (0 == stat(path, &sbuf)) {
		is_path_to_socket = 1;
	    } else {
		char *dot = strrchr(path, '.');
		if (dot) {
		    *dot = '\0';
		    /* screen = atoi(dot + 1); */
		    if (0 == stat(path, &sbuf)) {
		        is_path_to_socket = 1;
		    }
		}
	    }

	    if (is_path_to_socket) {
		/* Use the bundle id (part preceding : in the basename) as our src id */
		char *c;
#ifdef HAVE_STRLCPY
		strlcpy(buf, strrchr(fulldpyname, '/') + 1, sizeof(buf));
#else
		strncpy(buf, strrchr(fulldpyname, '/') + 1, sizeof(buf));
		buf[sizeof(buf) - 1] = '\0';
#endif

		c = strchr(buf, ':');

		/* In the legacy case with no bundle id, use the full path */
		if(c == buf) {
			src = fulldpyname;
		} else {
			*c = '\0';
			src = buf;
		}

		len = strlen(src);
	    } else {
		src = fulldpyname;
		len = prefix;
            }
	}
	break;
      case FamilyInternet:		/* host:0 */
#ifdef TCPCONN
#if defined(IPv6) && defined(AF_INET6)
      case FamilyInternet6:
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC; /* IPv4 or IPv6 */
	hints.ai_socktype = SOCK_STREAM; /* only interested in TCP */
	hints.ai_protocol = 0;
        if (getaddrinfo(host,NULL,&hints,&firstai) !=0) return NULL;
	for (ai = firstai; ai != NULL; ai = ai->ai_next) {
	    struct addrlist *duplicate;

            len = 0;
	    if (ai->ai_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)ai->ai_addr;
		src = &(sin->sin_addr);
                if (*(const in_addr_t *) src == htonl(INADDR_LOOPBACK)) {
                    family = FamilyLocal;
                    if (get_local_hostname (buf, sizeof buf)) {
                        src = buf;
                        len = strlen (buf);
                    } else
                        src = NULL;
                } else {
                    len = sizeof(sin->sin_addr);
                    family = FamilyInternet;
                }
	    } else if (ai->ai_family == AF_INET6) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ai->ai_addr;
		src = &(sin6->sin6_addr);
                if (!IN6_IS_ADDR_V4MAPPED((const struct in6_addr *)src)) {
                    if (IN6_IS_ADDR_LOOPBACK((const struct in6_addr *)src)) {
                        family = FamilyLocal;
                        if (get_local_hostname (buf, sizeof buf)) {
                            src = buf;
                            len = strlen (buf);
                        } else
                            src = NULL;
                    } else {
                        len = sizeof(sin6->sin6_addr);
                        family = FamilyInternet6;
                    }
                } else {
                    src = &(sin6->sin6_addr.s6_addr[12]);
                    len = sizeof(((struct sockaddr_in *)
                                  ai->ai_addr)->sin_addr);
                    family = FamilyInternet;
                }
	    }

	    for(duplicate = retval; duplicate != NULL; duplicate = duplicate->next) {
		if(duplicate->family == family && duplicate->len == len &&
                   memcmp(duplicate->address, src, len) == 0) {
		    break;
                }
	    }

	    if (len > 0 && src != NULL && duplicate == NULL) {
		struct addrlist *newrv = malloc (sizeof(struct addrlist));
		if (newrv) {
		    newrv->address = malloc (len);
		    if (newrv->address) {
			memcpy(newrv->address, src, len);
			newrv->next = NULL;
			newrv->family = family;
			newrv->len = len;
			if (retval == NULL) {
			    lastrv = retval = newrv;
			} else {
			    lastrv->next = newrv;
			    lastrv = newrv;
			}
		    } else {
			free(newrv);
		    }
		}
	    }
	    /* reset to avoid copying into list twice */
	    len = 0;
	    src = NULL;
	}
	freeaddrinfo(firstai);
	break;
#else
	if (!get_inet_address (host, &hostinetaddr)) return NULL;
	src = (char *) &hostinetaddr;
        if (*(const in_addr_t *) src == htonl(INADDR_LOOPBACK)) {
            family = FamilyLocal;
            if (get_local_hostname (buf, sizeof buf)) {
                src = buf;
                len = strlen (buf);
            } else {
                len = 0;
                src = NULL;
            }
        } else
            len = 4; /* sizeof inaddr.sin_addr, would fail on Cray */
	break;
#endif /* IPv6 */
#else
	return NULL;
#endif
      case FamilyDECnet:		/* host::0 */
	/* fall through since we don't have code for it */
      default:
	src = NULL;
	len = 0;
    }

    /*
     * if source was provided, allocate space and copy it
     */
    if (len > 0 && src != NULL) {
	retval = malloc (sizeof(struct addrlist));
	if (retval) {
	    retval->address = malloc (len);
	    if (retval->address) {
		memcpy(retval->address, src, len);
		retval->next = NULL;
		retval->family = family;
		retval->len = len;
	    } else {
		free(retval);
		retval = NULL;
	    }
	}
    }
    return retval;
}
