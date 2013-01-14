/*

Copyright 1988, 1998  The Open Group

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

*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/Xauth.h>
#include <X11/Xos.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#define Time_t time_t
#ifndef X_NOT_POSIX
#include <unistd.h>
#else
#ifndef WIN32
extern unsigned	sleep ();
#else
#include <X11/Xwindows.h>
#define link rename
#endif
#endif

int
XauLockAuth (
_Xconst char *file_name,
int	retries,
int	timeout,
long	dead)
{
    char	creat_name[1025], link_name[1025];
    struct stat	statb;
    Time_t	now;
    int		creat_fd = -1;

    if (strlen (file_name) > 1022)
	return LOCK_ERROR;
    (void) strcpy (creat_name, file_name);
    (void) strcat (creat_name, "-c");
    (void) strcpy (link_name, file_name);
    (void) strcat (link_name, "-l");
    if (stat (creat_name, &statb) != -1) {
	now = time ((Time_t *) 0);
	/*
	 * NFS may cause ctime to be before now, special
	 * case a 0 deadtime to force lock removal
	 */
	if (dead == 0 || now - statb.st_ctime > dead) {
	    (void) unlink (creat_name);
	    (void) unlink (link_name);
	}
    }

    while (retries > 0) {
	if (creat_fd == -1) {
	    creat_fd = open (creat_name, O_WRONLY | O_CREAT | O_EXCL, 0600);
	    if (creat_fd == -1) {
		if (errno != EACCES && errno != EEXIST)
		    return LOCK_ERROR;
	    } else
		(void) close (creat_fd);
	}
	if (creat_fd != -1) {
#ifndef X_NOT_POSIX
	    /* The file system may not support hard links, and pathconf should tell us that. */
	    if (1 == pathconf(creat_name, _PC_LINK_MAX)) {
		if (-1 == rename(creat_name, link_name)) {
		    /* Is this good enough?  Perhaps we should retry.  TEST */
		    return LOCK_ERROR;
		} else {
		    return LOCK_SUCCESS;
		}
	    } else {
#endif
	    	if (link (creat_name, link_name) != -1)
		    return LOCK_SUCCESS;
		if (errno == ENOENT) {
		    creat_fd = -1;	/* force re-creat next time around */
		    continue;
	    	}
	    	if (errno != EEXIST)
		    return LOCK_ERROR;
#ifndef X_NOT_POSIX
	   }
#endif
	}
	(void) sleep ((unsigned) timeout);
	--retries;
    }
    return LOCK_TIMEOUT;
}
