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

#include <stdio.h>
#include <X11/Xos.h>
#include <X11/Xauth.h>
#include <X11/Xfuncs.h>

#ifndef True
typedef int Bool;
#define False 0
#define True 1
#endif

extern char *ProgramName;

#include <stdlib.h>

struct addrlist {
    struct addrlist *next;
    char	    *address;
    int		    len;
    int		    family;
};

extern char *get_hostname ( Xauth *auth );
extern struct addrlist *get_address_info ( int family, char *fulldpyname, int prefix, char *host);
extern char *copystring ( char *src, int len );
extern char *get_local_hostname ( char *buf, int maxlen );
extern Bool parse_displayname ( char *displayname, int *familyp, char **hostp, int *dpynump, int *scrnump, char **restp );
extern int auth_initialize ( char *authfilename );
extern int auth_finalize ( void );
extern int process_command ( char *inputfilename, int lineno, int argc, char **argv );
extern int print_help ( FILE *fp, char *cmd, char *prefix );
extern int verbose;
extern Bool ignore_locks;
extern Bool break_locks;
extern Bool no_name_lookups;
