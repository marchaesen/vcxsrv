/*

Copyright 1989, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall
not be used in advertising or otherwise to promote the sale, use or
other dealings in this Software without prior written authorization
from The Open Group.

*/

/*
 * Author:  Jim Fulton, MIT X Consortium
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xauth.h"
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#ifndef WIN32
#include <sys/socket.h>
#else
#include <X11/Xwinsock.h>
#endif

#include <signal.h>
#include <X11/X.h>			/* for Family constants */

#include <X11/Xlib.h>
#include <X11/extensions/security.h>

#ifndef DEFAULT_PROTOCOL_ABBREV		/* to make add command easier */
#define DEFAULT_PROTOCOL_ABBREV "."
#endif
#ifndef DEFAULT_PROTOCOL		/* for protocol abbreviation */
#define DEFAULT_PROTOCOL "MIT-MAGIC-COOKIE-1"
#endif

#define SECURERPC "SUN-DES-1"
#define K5AUTH "MIT-KERBEROS-5"

#define XAUTH_DEFAULT_RETRIES 10	/* number of competitors we expect */
#define XAUTH_DEFAULT_TIMEOUT 2		/* in seconds, be quick */
#define XAUTH_DEFAULT_DEADTIME 600L	/* 10 minutes in seconds */

typedef struct _AuthList {		/* linked list of entries */
    struct _AuthList *next;
    Xauth *auth;
} AuthList;

typedef int (*ProcessFunc)(const char *, int, int, const char**);

#define add_to_list(h,t,e) {if (t) (t)->next = (e); else (h) = (e); (t) = (e);}

typedef struct _CommandTable {		/* commands that are understood */
    const char *name;			/* full name */
    int minlen;				/* unique prefix */
    int maxlen;				/* strlen(name) */
    ProcessFunc processfunc;		/* handler */
    const char *helptext;		/* what to print for help */
} CommandTable;

struct _extract_data {			/* for iterating */
    FILE *fp;				/* input source */
    const char *filename;		/* name of input */
    Bool used_stdout;			/* whether or not need to close */
    Bool numeric;			/* format in which to write */
    int nwritten;			/* number of entries written */
    const char *cmd;			/* for error messages */
};

struct _list_data {			/* for iterating */
    FILE *fp;				/* output file */
    Bool numeric;			/* format in which to write */
};


/*
 * private data
 */
static const char *stdin_filename = "(stdin)";		/* for messages */
static const char *stdout_filename = "(stdout)";	/* for messages */
static const char *Yes = "yes";		/* for messages */
static const char *No = "no";		/* for messages */

static int do_help ( const char *inputfilename, int lineno, int argc, const char **argv );
static int do_questionmark ( const char *inputfilename, int lineno, int argc, const char **argv );
static int do_list ( const char *inputfilename, int lineno, int argc, const char **argv );
static int do_merge ( const char *inputfilename, int lineno, int argc, const char **argv );
static int do_extract ( const char *inputfilename, int lineno, int argc, const char **argv );
static int do_add ( const char *inputfilename, int lineno, int argc, const char **argv );
static int do_remove ( const char *inputfilename, int lineno, int argc, const char **argv );
static int do_info ( const char *inputfilename, int lineno, int argc, const char **argv );
static int do_exit ( const char *inputfilename, int lineno, int argc, const char **argv );
static int do_quit ( const char *inputfilename, int lineno, int argc, const char **argv );
static int do_source ( const char *inputfilename, int lineno, int argc, const char **argv );
static int do_generate ( const char *inputfilename, int lineno, int argc, const char **argv );
static int do_version ( const char *inputfilename, int lineno, int argc, const char **argv );

static CommandTable command_table[] = {	/* table of known commands */
    { "add",      2, 3, do_add,
	"add dpyname protoname hexkey   add entry" },
    { "exit",     3, 4, do_exit,
	"exit                           save changes and exit program" },
    { "extract",  3, 7, do_extract,
	"extract filename dpyname...    extract entries into file" },
    { "help",     1, 4, do_help,
	"help [topic]                   print help" },
    { "info",     1, 4, do_info,
	"info                           print information about entries" },
    { "list",     1, 4, do_list,
	"list [dpyname...]              list entries" },
    { "merge",    1, 5, do_merge,
	"merge filename...              merge entries from files" },
    { "nextract", 2, 8, do_extract,
	"nextract filename dpyname...   numerically extract entries" },
    { "nlist",    2, 5, do_list,
	"nlist [dpyname...]             numerically list entries" },
    { "nmerge",   2, 6, do_merge,
	"nmerge filename...             numerically merge entries" },
    { "quit",     1, 4, do_quit,
	"quit                           abort changes and exit program" },
    { "remove",   1, 6, do_remove,
	"remove dpyname...              remove entries" },
    { "source",   1, 6, do_source,
	"source filename                read commands from file" },
    { "version",  1, 7, do_version,
	"version                        show version number of xauth" },
    { "?",        1, 1, do_questionmark,
	"?                              list available commands" },
    { "generate", 1, 8, do_generate,
	"generate dpyname protoname [options]  use server to generate entry\n"
        "    options are:\n"
        "      timeout n    authorization expiration time in seconds\n"
        "      trusted      clients using this entry are trusted\n"
        "      untrusted    clients using this entry are untrusted\n"
        "      group n      clients using this entry belong to application group n\n"
        "      data hexkey  auth protocol specific data needed to generate the entry\n"
    },
    { NULL,       0, 0, NULL, NULL },
};

#define COMMAND_NAMES_PADDED_WIDTH 10	/* wider than anything above */


static Bool okay_to_use_stdin = True;	/* set to false after using */

static const char *hex_table[] = {	/* for printing hex digits */
    "00", "01", "02", "03", "04", "05", "06", "07",
    "08", "09", "0a", "0b", "0c", "0d", "0e", "0f",
    "10", "11", "12", "13", "14", "15", "16", "17",
    "18", "19", "1a", "1b", "1c", "1d", "1e", "1f",
    "20", "21", "22", "23", "24", "25", "26", "27",
    "28", "29", "2a", "2b", "2c", "2d", "2e", "2f",
    "30", "31", "32", "33", "34", "35", "36", "37",
    "38", "39", "3a", "3b", "3c", "3d", "3e", "3f",
    "40", "41", "42", "43", "44", "45", "46", "47",
    "48", "49", "4a", "4b", "4c", "4d", "4e", "4f",
    "50", "51", "52", "53", "54", "55", "56", "57",
    "58", "59", "5a", "5b", "5c", "5d", "5e", "5f",
    "60", "61", "62", "63", "64", "65", "66", "67",
    "68", "69", "6a", "6b", "6c", "6d", "6e", "6f",
    "70", "71", "72", "73", "74", "75", "76", "77",
    "78", "79", "7a", "7b", "7c", "7d", "7e", "7f",
    "80", "81", "82", "83", "84", "85", "86", "87",
    "88", "89", "8a", "8b", "8c", "8d", "8e", "8f",
    "90", "91", "92", "93", "94", "95", "96", "97",
    "98", "99", "9a", "9b", "9c", "9d", "9e", "9f",
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
    "a8", "a9", "aa", "ab", "ac", "ad", "ae", "af",
    "b0", "b1", "b2", "b3", "b4", "b5", "b6", "b7",
    "b8", "b9", "ba", "bb", "bc", "bd", "be", "bf",
    "c0", "c1", "c2", "c3", "c4", "c5", "c6", "c7",
    "c8", "c9", "ca", "cb", "cc", "cd", "ce", "cf",
    "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
    "d8", "d9", "da", "db", "dc", "dd", "de", "df",
    "e0", "e1", "e2", "e3", "e4", "e5", "e6", "e7",
    "e8", "e9", "ea", "eb", "ec", "ed", "ee", "ef",
    "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
    "f8", "f9", "fa", "fb", "fc", "fd", "fe", "ff",
};

static unsigned int hexvalues[256];	/* for parsing hex input */

static int original_umask = 0;		/* for restoring */


/*
 * private utility procedures
 */

static void
prefix(const char *fn, int n)
{
    fprintf (stderr, "%s: %s:%d:  ", ProgramName, fn, n);
}

static void
baddisplayname(const char *dpy, const char *cmd)
{
    fprintf (stderr, "bad display name \"%s\" in \"%s\" command\n",
	     dpy, cmd);
}

static void
badcommandline(const char *cmd)
{
    fprintf (stderr, "bad \"%s\" command line\n", cmd);
}

static char *
skip_space(register char *s)
{
    if (!s) return NULL;

    for ( ; *s && isascii(*s) && isspace(*s); s++)
	;
    return s;
}


static char *
skip_nonspace(register char *s)
{
    if (!s) return NULL;

    /* put quoting into loop if need be */
    for ( ; *s && isascii(*s) && !isspace(*s); s++)
	;
    return s;
}

static const char **
split_into_words(char *src, int *argcp)  /* argvify string */
{
    char *jword;
    char savec;
    const char **argv;
    int cur, total;

    *argcp = 0;
#define WORDSTOALLOC 4			/* most lines are short */
    argv = malloc (WORDSTOALLOC * sizeof (char *));
    if (!argv) return NULL;
    cur = 0;
    total = WORDSTOALLOC;

    /*
     * split the line up into separate, nul-terminated tokens; the last
     * "token" will point to the empty string so that it can be bashed into
     * a null pointer.
     */

    do {
	jword = skip_space (src);
	src = skip_nonspace (jword);
	savec = *src;
	*src = '\0';
	if (cur == total) {
	    total += WORDSTOALLOC;
	    argv = realloc (argv, total * sizeof (char *));
	    if (!argv) return NULL;
	}
	argv[cur++] = jword;
	if (savec) src++;		/* if not last on line advance */
    } while (jword != src);

    argv[--cur] = NULL;			/* smash empty token to end list */
    *argcp = cur;
    return argv;
}


static FILE *
open_file(const char **filenamep,
	  const char *mode,
	  Bool *usedstdp,
	  const char *srcfn,
	  int srcln,
	  const char *cmd)
{
    FILE *fp;

    if (strcmp (*filenamep, "-") == 0) {
	*usedstdp = True;
					/* select std descriptor to use */
	if (mode[0] == 'r') {
	    if (okay_to_use_stdin) {
		okay_to_use_stdin = False;
		*filenamep = stdin_filename;
		return stdin;
	    } else {
		prefix (srcfn, srcln);
		fprintf (stderr, "%s:  stdin already in use\n", cmd);
		return NULL;
	    }
	} else {
	    *filenamep = stdout_filename;
	    return stdout;		/* always okay to use stdout */
	}
    }

    fp = fopen (*filenamep, mode);
    if (!fp) {
	prefix (srcfn, srcln);
	fprintf (stderr, "%s:  unable to open file %s\n", cmd, *filenamep);
    }
    return fp;
}

static int
getinput(FILE *fp)
{
    register int c;

    while ((c = getc (fp)) != EOF && isascii(c) && c != '\n' && isspace(c)) ;
    return c;
}

static int
get_short(FILE *fp, unsigned short *sp)	/* for reading numeric input */
{
    int c;
    int i;
    unsigned short us = 0;

    /*
     * read family:  written with %04x
     */
    for (i = 0; i < 4; i++) {
	switch (c = getinput (fp)) {
	  case EOF:
	  case '\n':
	    return 0;
	}
	if (c < 0 || c > 255) return 0;
	us = (us * 16) + hexvalues[c];	/* since msb */
    }
    *sp = us;
    return 1;
}

static int
get_bytes(FILE *fp, unsigned int n, char **ptr)	/* for reading numeric input */
{
    char *s;
    register char *cp;
    int c1, c2;

    cp = s = malloc (n);
    if (!cp) return 0;

    while (n > 0) {
	if ((c1 = getinput (fp)) == EOF || c1 == '\n' ||
	    (c2 = getinput (fp)) == EOF || c2 == '\n') {
	    free (s);
	    return 0;
	}
	*cp = (char) ((hexvalues[c1] * 16) + hexvalues[c2]);
	cp++;
	n--;
    }

    *ptr = s;
    return 1;
}


static Xauth *
read_numeric(FILE *fp)
{
    Xauth *auth;

    auth = (Xauth *) malloc (sizeof (Xauth));
    if (!auth) goto bad;
    auth->family = 0;
    auth->address = NULL;
    auth->address_length = 0;
    auth->number = NULL;
    auth->number_length = 0;
    auth->name = NULL;
    auth->name_length = 0;
    auth->data = NULL;
    auth->data_length = 0;

    if (!get_short (fp, (unsigned short *) &auth->family))
      goto bad;
    if (!get_short (fp, (unsigned short *) &auth->address_length))
      goto bad;
    if (!get_bytes (fp, (unsigned int) auth->address_length, &auth->address))
      goto bad;
    if (!get_short (fp, (unsigned short *) &auth->number_length))
      goto bad;
    if (!get_bytes (fp, (unsigned int) auth->number_length, &auth->number))
      goto bad;
    if (!get_short (fp, (unsigned short *) &auth->name_length))
      goto bad;
    if (!get_bytes (fp, (unsigned int) auth->name_length, &auth->name))
      goto bad;
    if (!get_short (fp, (unsigned short *) &auth->data_length))
      goto bad;
    if (!get_bytes (fp, (unsigned int) auth->data_length, &auth->data))
      goto bad;

    switch (getinput (fp)) {		/* get end of line */
      case EOF:
      case '\n':
	return auth;
    }

  bad:
    if (auth) XauDisposeAuth (auth);	/* won't free null pointers */
    return NULL;
}

typedef Xauth *(*ReadFunc)(FILE *);

static int
read_auth_entries(FILE *fp, Bool numeric, AuthList **headp, AuthList **tailp)
{
    ReadFunc readfunc = (numeric ? read_numeric : XauReadAuth);
    Xauth *auth;
    AuthList *head, *tail;
    int n;

    head = tail = NULL;
    n = 0;
					/* put all records into linked list */
    while ((auth = ((*readfunc) (fp))) != NULL) {
	AuthList *l = (AuthList *) malloc (sizeof (AuthList));
	if (!l) {
	    fprintf (stderr,
		     "%s:  unable to alloc entry reading auth file\n",
		     ProgramName);
	    exit (1);
	}
	l->next = NULL;
	l->auth = auth;
	if (tail) 			/* if not first time through append */
	  tail->next = l;
	else
	  head = l;			/* first time through, so assign */
	tail = l;
	n++;
    }
    *headp = head;
    *tailp = tail;
    return n;
}

static Bool
get_displayname_auth(const char *displayname, AuthList **authl)
{
    int family;
    char *host = NULL, *rest = NULL;
    int dpynum, scrnum;
    char *cp;
    int prelen = 0;
    struct addrlist *addrlist_head, *addrlist_cur;
    AuthList *authl_cur = NULL;

    *authl = NULL;
    /*
     * check to see if the display name is of the form "host/unix:"
     * which is how the list routine prints out local connections
     */
    cp = strchr(displayname, '/');
    if (cp && strncmp (cp, "/unix:", 6) == 0)
      prelen = (cp - displayname);

    if (!parse_displayname (displayname + ((prelen > 0) ? prelen + 1 : 0),
			    &family, &host, &dpynum, &scrnum, &rest)) {
	return False;
    }

    addrlist_head = get_address_info(family, displayname, prelen, host);
    if (addrlist_head) {
	char buf[40];			/* want to hold largest display num */
	unsigned short dpylen;

	buf[0] = '\0';
	sprintf (buf, "%d", dpynum);
	dpylen = strlen (buf);
	if (dpylen > 0) {
	    for (addrlist_cur = addrlist_head; addrlist_cur != NULL;
		 addrlist_cur = addrlist_cur->next) {
		AuthList *newal = malloc(sizeof(AuthList));
		Xauth *auth = malloc(sizeof(Xauth));

		if ((newal == NULL) || (auth == NULL)) {
		    if (newal != NULL) free(newal);
		    if (auth != NULL) free(auth);
		    break;
		}

		if (authl_cur == NULL) {
		    *authl = authl_cur = newal;
		} else {
		    authl_cur->next = newal;
		    authl_cur = newal;
		}

		newal->next = NULL;
		newal->auth = auth;

		auth->family = addrlist_cur->family;
		auth->address = addrlist_cur->address;
		auth->address_length = addrlist_cur->len;
		auth->number = copystring(buf, dpylen);
		auth->number_length = dpylen;
		auth->name = NULL;
		auth->name_length = 0;
		auth->data = NULL;
		auth->data_length = 0;
	    }
	}
    }

    if (host) free (host);
    if (rest) free (rest);

    if (*authl != NULL) {
	return True;
    } else {
	return False;
    }
}

static int
cvthexkey(const char *hexstr, char **ptrp) /* turn hex key string into octets */
{
    int i;
    int len = 0;
    char *retval;
    const char *s;
    unsigned char *us;
    char c;
    char savec = '\0';

    /* count */
    for (s = hexstr; *s; s++) {
	if (!isascii(*s)) return -1;
	if (isspace(*s)) continue;
	if (!isxdigit(*s)) return -1;
	len++;
    }

    /* if 0 or odd, then there was an error */
    if (len == 0 || (len & 1) == 1) return -1;


    /* now we know that the input is good */
    len >>= 1;
    retval = malloc (len);
    if (!retval) {
	fprintf (stderr, "%s:  unable to allocate %d bytes for hexkey\n",
		 ProgramName, len);
	return -1;
    }

    for (us = (unsigned char *) retval, i = len; i > 0; hexstr++) {
	c = *hexstr;
	if (isspace(c)) continue;	 /* already know it is ascii */
	if (isupper(c))
	    c = tolower(c);
	if (savec) {
#define atoh(c) ((c) - (((c) >= '0' && (c) <= '9') ? '0' : ('a'-10)))
	    *us = (unsigned char)((atoh(savec) << 4) + atoh(c));
#undef atoh
	    savec = 0;		/* ready for next character */
	    us++;
	    i--;
	} else {
	    savec = c;
	}
    }
    *ptrp = retval;
    return len;
}

static int
dispatch_command(const char *inputfilename,
		 int lineno,
		 int argc,
		 const char **argv,
		 CommandTable *tab,
		 int *statusp)
{
    CommandTable *ct;
    const char *cmd;
    int n;
					/* scan table for command */
    cmd = argv[0];
    n = strlen (cmd);
    for (ct = tab; ct->name; ct++) {
					/* look for unique prefix */
	if (n >= ct->minlen && n <= ct->maxlen &&
	    strncmp (cmd, ct->name, n) == 0) {
	    *statusp = (*(ct->processfunc))(inputfilename, lineno, argc, argv);
	    return 1;
	}
    }

    *statusp = 1;
    return 0;
}


static AuthList *xauth_head = NULL;	/* list of auth entries */
static Bool xauth_existed = False;	/* if was present at initialize */
static Bool xauth_modified = False;	/* if added, removed, or merged */
static Bool xauth_allowed = True;	/* if allowed to write auth file */
static Bool xauth_locked = False;     /* if has been locked */
static const char *xauth_filename = NULL;
static volatile Bool dieing = False;


/* poor man's puts(), for under signal handlers, 
   extended to ignore warn_unused_result */
#define WRITES(fd, S) {if(write((fd), (S), strlen((S))));}

/* ARGSUSED */
_X_NORETURN
static void
die(int sig)
{
    dieing = True;
    _exit (auth_finalize ());
    /* NOTREACHED */
}

_X_NORETURN
static void
catchsig(int sig)
{
#ifdef SYSV
    if (sig > 0) signal (sig, die);	/* re-establish signal handler */
#endif
    /*
     * fileno() might not be reentrant, avoid it if possible, and use
     * stderr instead of stdout
     */
#ifdef STDERR_FILENO
    if (verbose && xauth_modified) WRITES(STDERR_FILENO, "\r\n");
#else
    if (verbose && xauth_modified) WRITES(fileno(stderr), "\r\n");
#endif
    die (sig);
    /* NOTREACHED */
}

static void
register_signals(void)
{
    signal (SIGINT, catchsig);
    signal (SIGTERM, catchsig);
#ifdef SIGHUP
    signal (SIGHUP, catchsig);
#endif
#ifdef SIGPIPE
    signal (SIGPIPE, catchsig);
#endif
    return;
}


/*
 * public procedures for parsing lines of input
 */

int
auth_initialize(const char *authfilename)
{
    int n;
    AuthList *head, *tail;
    FILE *authfp;
    Bool exists;

    xauth_filename = authfilename;    /* used in cleanup, prevent race with
                                         signals */
    register_signals ();

    bzero ((char *) hexvalues, sizeof hexvalues);
    hexvalues['0'] = 0;
    hexvalues['1'] = 1;
    hexvalues['2'] = 2;
    hexvalues['3'] = 3;
    hexvalues['4'] = 4;
    hexvalues['5'] = 5;
    hexvalues['6'] = 6;
    hexvalues['7'] = 7;
    hexvalues['8'] = 8;
    hexvalues['9'] = 9;
    hexvalues['a'] = hexvalues['A'] = 0xa;
    hexvalues['b'] = hexvalues['B'] = 0xb;
    hexvalues['c'] = hexvalues['C'] = 0xc;
    hexvalues['d'] = hexvalues['D'] = 0xd;
    hexvalues['e'] = hexvalues['E'] = 0xe;
    hexvalues['f'] = hexvalues['F'] = 0xf;

    if (break_locks && verbose) {
	printf ("Attempting to break locks on authority file %s\n",
		authfilename);
    }

    if (ignore_locks) {
	if (break_locks) XauUnlockAuth (authfilename);
    } else {
	n = XauLockAuth (authfilename, XAUTH_DEFAULT_RETRIES,
			 XAUTH_DEFAULT_TIMEOUT,
			 (break_locks ? 0L : XAUTH_DEFAULT_DEADTIME));
	if (n != LOCK_SUCCESS) {
	    const char *reason = "unknown error";
	    switch (n) {
	      case LOCK_ERROR:
		reason = "error";
		break;
	      case LOCK_TIMEOUT:
		reason = "timeout";
		break;
	    }
	    fprintf (stderr, "%s:  %s in locking authority file %s\n",
		     ProgramName, reason, authfilename);
	    return -1;
	} else
	    xauth_locked = True;
    }

    /* these checks can only be done reliably after the file is locked */
    exists = (access (authfilename, F_OK) == 0);
    if (exists && access (authfilename, W_OK) != 0) {
	fprintf (stderr,
	 "%s:  %s not writable, changes will be ignored\n",
		 ProgramName, authfilename);
	xauth_allowed = False;
    }

    original_umask = umask (0077);	/* disallow non-owner access */

    authfp = fopen (authfilename, "rb");
    if (!authfp) {
	int olderrno = errno;

					/* if file there then error */
	if (access (authfilename, F_OK) == 0) {	 /* then file does exist! */
	    errno = olderrno;
	    return -1;
	}				/* else ignore it */
	fprintf (stderr,
		 "%s:  file %s does not exist\n",
		 ProgramName, authfilename);
    } else {
	xauth_existed = True;
	n = read_auth_entries (authfp, False, &head, &tail);
	(void) fclose (authfp);
	if (n < 0) {
	    fprintf (stderr,
		     "%s:  unable to read auth entries from file \"%s\"\n",
		     ProgramName, authfilename);
	    return -1;
	}
	xauth_head = head;
    }

    xauth_filename = strdup(authfilename);
    if (!xauth_filename) {
	fprintf(stderr,"cannot allocate memory\n");
	return -1;
    }

    xauth_modified = False;

    if (verbose) {
	printf ("%s authority file %s\n",
		ignore_locks ? "Ignoring locks on" : "Using", authfilename);
    }
    return 0;
}

static int
write_auth_file(char *tmp_nam)
{
    FILE *fp = NULL;
    int fd;
    AuthList *list;

    /*
     * xdm and auth spec assumes auth file is 12 or fewer characters
     */
    strcpy (tmp_nam, xauth_filename);
    strcat (tmp_nam, "-n");		/* for new */
    (void) unlink (tmp_nam);
    /* CPhipps 2000/02/12 - fix file unlink/fopen race */
    fd = open(tmp_nam, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd != -1) fp = _fdopen (fd, "wb");
    if (!fp) {
        if (fd != -1) close(fd);
	fprintf (stderr, "%s:  unable to open tmp file \"%s\"\n",
		 ProgramName, tmp_nam);
	return -1;
    }

    /*
     * Write MIT-MAGIC-COOKIE-1 first, because R4 Xlib knows
     * only that and uses the first authorization it finds.
     */
    for (list = xauth_head; list; list = list->next) {
	if (list->auth->name_length == 18
	    && strncmp(list->auth->name, "MIT-MAGIC-COOKIE-1", 18) == 0) {
	    if (!XauWriteAuth(fp, list->auth)) {
		(void) fclose(fp);
		return -1;
	    }
	}
    }
    for (list = xauth_head; list; list = list->next) {
	if (list->auth->name_length != 18
	    || strncmp(list->auth->name, "MIT-MAGIC-COOKIE-1", 18) != 0) {
	    if (!XauWriteAuth(fp, list->auth)) {
		(void) fclose(fp);
		return -1;
	    }
	}
    }

    if (fclose(fp)) {
	return -1;
    }

    return 0;
}

int
auth_finalize(void)
{
    char temp_name[1024];	/* large filename size */

    if (xauth_modified) {
	if (dieing) {
	    if (verbose) {
		/*
		 * called from a signal handler -- printf is *not* reentrant; also
		 * fileno() might not be reentrant, avoid it if possible, and use
		 * stderr instead of stdout
		 */
#ifdef STDERR_FILENO
		WRITES(STDERR_FILENO, "\nAborting changes to authority file ");
		WRITES(STDERR_FILENO, xauth_filename);
		WRITES(STDERR_FILENO, "\n");
#else
		WRITES(fileno(stderr), "\nAborting changes to authority file ");
		WRITES(fileno(stderr), xauth_filename);
		WRITES(fileno(stderr), "\n");
#endif
	    }
	} else if (!xauth_allowed) {
	    fprintf (stderr,
		     "%s:  %s not writable, changes ignored\n",
		     ProgramName, xauth_filename);
	} else {
	    if (verbose) {
		printf ("%s authority file %s\n",
			ignore_locks ? "Ignoring locks and writing" :
			"Writing", xauth_filename);
	    }
	    temp_name[0] = '\0';
	    if (write_auth_file (temp_name) == -1) {
		fprintf (stderr,
			 "%s:  unable to write authority file %s\n",
			 ProgramName, temp_name);
	    } else {
			unlink(xauth_filename);
		if (rename(temp_name, xauth_filename) == -1) {
		    fprintf (stderr,
		     "%s:  unable to rename authority file %s, use %s\n",
			     ProgramName, xauth_filename, temp_name);
                    unlink(temp_name);
		}
	    }
	}
    }

    if (xauth_locked) {
	XauUnlockAuth (xauth_filename);
    }
    (void) umask (original_umask);
    return 0;
}

int
process_command(const char *inputfilename, int lineno, int argc, const char **argv)
{
    int status;

    if (argc < 1 || !argv || !argv[0]) return 1;

    if (dispatch_command (inputfilename, lineno, argc, argv,
			  command_table, &status))
      return status;

    prefix (inputfilename, lineno);
    fprintf (stderr, "unknown command \"%s\"\n", argv[0]);
    return 1;
}


/*
 * utility routines
 */

static char *
bintohex(unsigned int len, const char *bindata)
{
    char *hexdata, *starthex;

    /* two chars per byte, plus null termination */
    starthex = hexdata = (char *)malloc(2*len + 1);
    if (!hexdata)
	return NULL;

    for (; len > 0; len--, bindata++) {
	register const char *s = hex_table[(unsigned char)*bindata];
	*hexdata++ = s[0];
	*hexdata++ = s[1];
    }
    *hexdata = '\0';
    return starthex;
}

static void
fprintfhex(register FILE *fp, int len, char *cp)
{
    char *hex;

    hex = bintohex(len, cp);
    fprintf(fp, "%s", hex);
    free(hex);
}

static int
dump_numeric(register FILE *fp, register Xauth *auth)
{
    fprintf (fp, "%04x", auth->family);  /* unsigned short */
    fprintf (fp, " %04x ", auth->address_length);  /* short */
    fprintfhex (fp, auth->address_length, auth->address);
    fprintf (fp, " %04x ", auth->number_length);  /* short */
    fprintfhex (fp, auth->number_length, auth->number);
    fprintf (fp, " %04x ", auth->name_length);  /* short */
    fprintfhex (fp, auth->name_length, auth->name);
    fprintf (fp, " %04x ", auth->data_length);  /* short */
    fprintfhex (fp, auth->data_length, auth->data);
    putc ('\n', fp);
    return 1;
}

/* ARGSUSED */
static int
dump_entry(const char *inputfilename, int lineno, Xauth *auth, char *data)
{
    struct _list_data *ld = (struct _list_data *) data;
    FILE *fp = ld->fp;

    if (ld->numeric) {
	dump_numeric (fp, auth);
    } else {
	const char *dpyname = NULL;

	switch (auth->family) {
	  case FamilyLocal:
	    fwrite (auth->address, sizeof (char), auth->address_length, fp);
	    fprintf (fp, "/unix");
	    break;
	  case FamilyInternet:
#if defined(IPv6) && defined(AF_INET6)
	  case FamilyInternet6:
#endif
	  case FamilyDECnet:
	    dpyname = get_hostname (auth);
	    if (dpyname) {
		fprintf (fp, "%s", dpyname);
		break;
	    }
	    /* else fall through */
	  default:
	    fprintf (fp, "#%04x#", auth->family);
	    fprintfhex (fp, auth->address_length, auth->address);
	    putc ('#', fp);
	}
	putc (':', fp);
	fwrite (auth->number, sizeof (char), auth->number_length, fp);
	putc (' ', fp);
	putc (' ', fp);
	fwrite (auth->name, sizeof (char), auth->name_length, fp);
	putc (' ', fp);
	putc (' ', fp);
	if (!strncmp(auth->name, SECURERPC, auth->name_length) ||
	    !strncmp(auth->name, K5AUTH, auth->name_length))
            fwrite (auth->data, sizeof (char), auth->data_length, fp);
	else
	    fprintfhex (fp, auth->data_length, auth->data);
	putc ('\n', fp);
    }
    return 0;
}

static int
extract_entry(const char *inputfilename, int lineno, Xauth *auth, char *data)
{
    struct _extract_data *ed = (struct _extract_data *) data;

    if (!ed->fp) {
	ed->fp = open_file (&ed->filename,
			    ed->numeric ? "w" : "wb",
			    &ed->used_stdout,
			    inputfilename, lineno, ed->cmd);
	if (!ed->fp) {
	    prefix (inputfilename, lineno);
	    fprintf (stderr,
		     "unable to open extraction file \"%s\"\n",
		     ed->filename);
	    return -1;
	}
    }
    (*(ed->numeric ? dump_numeric : XauWriteAuth)) (ed->fp, auth);
    ed->nwritten++;

    return 0;
}


static int
eq_auth_dpy_and_name(Xauth *a, Xauth *b)
{
    return((a->family == b->family &&
	    a->address_length == b->address_length &&
	    a->number_length == b->number_length &&
	    a->name_length == b->name_length &&
	    memcmp(a->address, b->address, a->address_length) == 0 &&
	    memcmp(a->number, b->number, a->number_length) == 0 &&
	    memcmp(a->name, b->name, a->name_length) == 0) ? 1 : 0);
}

static int
eq_auth(Xauth *a, Xauth *b)
{
    return((eq_auth_dpy_and_name(a, b) &&
	    a->data_length == b->data_length &&
	    memcmp(a->data, b->data, a->data_length) == 0) ? 1 : 0);
}

static int
match_auth_dpy(register Xauth *a, register Xauth *b)
{
    if (a->family != FamilyWild && b->family != FamilyWild) {
        /* Both "a" and "b" are not FamilyWild, they are "normal" families. */
	
	/* Make sure, that both families match: */
	if (a->family != b->family)
            return 0;
	
	/* By looking at 'man Xsecurity' and the code in
	 * GetAuthByAddr() and XauGetBestAuthByAddr() in libXau, we
	 * decided, that the address is only relevant for "normal"
	 * families and therefore should be ignored for
	 * "FamilyWild". */
	if (a->address_length != b->address_length ||
            memcmp(a->address, b->address, a->address_length) != 0)
            return 0;
    }
    
    if (a->number_length != 0 && b->number_length != 0) {
	/* Both "a" and "b" have a number, make sure they match: */
	if (a->number_length != b->number_length ||
	    memcmp(a->number, b->number, a->number_length) != 0)
            return 0;
    }
    
    return 1;
}

static int
merge_entries(AuthList **firstp, AuthList *second, int *nnewp, int *nreplp)
{
    AuthList *a, *b, *first, *tail;
    int n = 0, nnew = 0, nrepl = 0;

    if (!second) return 0;

    if (!*firstp) {			/* if nothing to merge into */
	*firstp = second;
	for (tail = *firstp, n = 1; tail->next; n++, tail = tail->next) ;
	*nnewp = n;
	*nreplp = 0;
	return n;
    }

    first = *firstp;
    /*
     * find end of first list and stick second list on it
     */
    for (tail = first; tail->next; tail = tail->next) ;
    tail->next = second;

    /*
     * run down list freeing duplicate entries; if an entry is okay, then
     * bump the tail up to include it, otherwise, cut the entry out of
     * the chain.
     */
    for (b = second; b; ) {
	AuthList *next = b->next;	/* in case we free it */

	a = first;
	for (;;) {
	    if (eq_auth_dpy_and_name (a->auth, b->auth)) {  /* found a duplicate */
		AuthList tmp;		/* swap it in for old one */
		tmp = *a;
		*a = *b;
		*b = tmp;
		a->next = b->next;
		XauDisposeAuth (b->auth);
		free ((char *) b);
		b = NULL;
		tail->next = next;
		nrepl++;
		nnew--;
		break;
	    }
	    if (a == tail) break;	/* if have looked at left side */
	    a = a->next;
	}
	if (b) {			/* if we didn't remove it */
	    tail = b;			/* bump end of first list */
	}
	b = next;
	n++;
	nnew++;
    }

    *nnewp = nnew;
    *nreplp = nrepl;
    return n;

}

static void
sort_entries(AuthList **firstp)
{
    /* Insert sort, in each pass it removes auth records of certain */
    /* cathegory from the given list and inserts them into the sorted list. */

    AuthList *sorted = NULL, *sorted_tail = NULL;
    AuthList *prev, *iter, *next;

    #define SORT_OUT(EXPRESSION) { \
	prev = NULL; \
	for (iter = *firstp; iter; iter = next) { \
	    next = iter->next; \
	    if (EXPRESSION) { \
		if (prev) \
		    prev->next = next; \
		else \
		    *firstp = next; \
		if (sorted_tail == NULL) { \
		    sorted = sorted_tail = iter; \
		} else { \
		    sorted_tail->next = iter; \
		    sorted_tail = iter; \
		} \
		iter->next = NULL; \
	    } else { \
		prev = iter; \
	    } \
	} \
    }

    SORT_OUT(iter->auth->family != FamilyWild && iter->auth->number_length != 0);
    SORT_OUT(iter->auth->family != FamilyWild && iter->auth->number_length == 0);
    SORT_OUT(iter->auth->family == FamilyWild && iter->auth->number_length != 0);
    SORT_OUT(iter->auth->family == FamilyWild && iter->auth->number_length == 0);

    *firstp = sorted;
}

static Xauth *
copyAuth(Xauth *auth)
{
    Xauth *a;

    a = (Xauth *)malloc(sizeof(Xauth));
    if (a == NULL) {
	return NULL;
    }
    memset(a, 0, sizeof(Xauth));
    a->family = auth->family;
    if (auth->address_length != 0) {
	a->address = malloc(auth->address_length);
	if (a->address == NULL) {
	    free(a);
	    return NULL;
	}
	memcpy(a->address, auth->address, auth->address_length);
	a->address_length = auth->address_length;
    }
    if (auth->number_length != 0) {
	a->number = malloc(auth->number_length);
	if (a->number == NULL) {
	    free(a->address);
	    free(a);
	    return NULL;
	}
	memcpy(a->number, auth->number, auth->number_length);
	a->number_length = auth->number_length;
    }
    if (auth->name_length != 0) {
	a->name = malloc(auth->name_length);
	if (a->name == NULL) {
	    free(a->address);
	    free(a->number);
	    free(a);
	    return NULL;
	}
	memcpy(a->name, auth->name, auth->name_length);
	a->name_length = auth->name_length;
    }
    if (auth->data_length != 0) {
	a->data = malloc(auth->data_length);
	if (a->data == NULL) {
	    free(a->address);
	    free(a->number);
	    free(a->name);
	    free(a);
	    return NULL;
	}
	memcpy(a->data, auth->data, auth->data_length);
	a->data_length = auth->data_length;
    }
    return a;
}

typedef int (*YesNoFunc)(const char *, int, Xauth *, char *);

static int
iterdpy (const char *inputfilename, int lineno, int start,
	 int argc, const char *argv[],
	 YesNoFunc yfunc, YesNoFunc nfunc, char *data)
{
    int i;
    int status;
    int errors = 0;
    Xauth *tmp_auth;
    AuthList *proto_head, *proto;
    AuthList *l, *next;

    /*
     * iterate
     */
    for (i = start; i < argc; i++) {
	const char *displayname = argv[i];
	if (!get_displayname_auth (displayname, &proto_head)) {
	    prefix (inputfilename, lineno);
	    baddisplayname (displayname, argv[0]);
	    errors++;
	    continue;
	}
	status = 0;
	for (l = xauth_head; l; l = next) {
	    Bool matched = False;

	    /* l may be freed by remove_entry below. so save its contents */
	    next = l->next;
	    tmp_auth = copyAuth(l->auth);
	    for (proto = proto_head; proto; proto = proto->next) {
		if (match_auth_dpy (proto->auth, tmp_auth)) {
		    matched = True;
		    if (yfunc) {
			status = (*yfunc) (inputfilename, lineno,
					   tmp_auth, data);
			if (status < 0) break;
		    }
		}
	    }
	    XauDisposeAuth(tmp_auth);
	    if (matched == False) {
		if (nfunc) {
		    status = (*nfunc) (inputfilename, lineno,
				       l->auth, data);
		}
	    }
	    if (status < 0) break;
	}
	for (proto = proto_head; proto ; proto = next) {
	    next = proto->next;
	    if (proto->auth->address) free (proto->auth->address);
	    if (proto->auth->number) free (proto->auth->number);
	    free (proto->auth);
	    free (proto);
	}
	if (status < 0) {
	    errors -= status;		/* since status is negative */
	    break;
	}
    }

    return errors;
}

/* ARGSUSED */
static int
remove_entry(const char *inputfilename, int lineno, Xauth *auth, char *data)
{
    int *nremovedp = (int *) data;
    AuthList **listp = &xauth_head;
    AuthList *list;

    /*
     * unlink the auth we were asked to
     */
    while (!eq_auth((list = *listp)->auth, auth)) {
        listp = &list->next;
        if (!*listp)
            return 0;
    }
    *listp = list->next;
    XauDisposeAuth (list->auth);                    /* free the auth */
    free (list);				    /* free the link */
    xauth_modified = True;
    (*nremovedp)++;
    return 1;
}

/*
 * action routines
 */

/*
 * help
 */
int
print_help(FILE *fp, const char *cmd, const char *line_prefix)
{
    CommandTable *ct;
    int n = 0;

    if (!line_prefix) line_prefix = "";

    if (!cmd) {				/* if no cmd, print all help */
	for (ct = command_table; ct->name; ct++) {
	    fprintf (fp, "%s%s\n", line_prefix, ct->helptext);
	    n++;
	}
    } else {
	int len = strlen (cmd);
	for (ct = command_table; ct->name; ct++) {
	    if (strncmp (cmd, ct->name, len) == 0) {
		fprintf (fp, "%s%s\n", line_prefix, ct->helptext);
		n++;
	    }
	}
    }

    return n;
}

static int
do_help(const char *inputfilename, int lineno, int argc, const char **argv)
{
    const char *cmd = (argc > 1 ? argv[1] : NULL);
    int n;

    n = print_help (stdout, cmd, "    ");  /* a nice amount */

    if (n < 0 || (n == 0 && !cmd)) {
	prefix (inputfilename, lineno);
	fprintf (stderr, "internal error with help");
	if (cmd) {
	    fprintf (stderr, " on command \"%s\"", cmd);
	}
	fprintf (stderr, "\n");
	return 1;
    }

    if (n == 0) {
	prefix (inputfilename, lineno);
	/* already know that cmd is set in this case */
	fprintf (stderr, "no help for noexistent command \"%s\"\n", cmd);
    }

    return 0;
}

/*
 * questionmark
 */
/* ARGSUSED */
static int
do_questionmark(const char *inputfilename, int lineno, int argc, const char **argv)
{
    CommandTable *ct;
    int i;
#define WIDEST_COLUMN 72
    int col = WIDEST_COLUMN;

    printf ("Commands:\n");
    for (ct = command_table; ct->name; ct++) {
	if ((col + ct->maxlen) > WIDEST_COLUMN) {
	    if (ct != command_table) {
		putc ('\n', stdout);
	    }
	    fputs ("        ", stdout);
	    col = 8;			/* length of string above */
	}
	fputs (ct->name, stdout);
	col += ct->maxlen;
	for (i = ct->maxlen; i < COMMAND_NAMES_PADDED_WIDTH; i++) {
	    putc (' ', stdout);
	    col++;
	}
    }
    if (col != 0) {
	putc ('\n', stdout);
    }

    /* allow bad lines since this is help */
    return 0;
}

/*
 * version
 */
/* ARGSUSED */
static int
do_version(const char *inputfilename, int lineno, int argc, const char **argv)
{
    puts (PACKAGE_VERSION);
    return 0;
}

/*
 * list [displayname ...]
 */
static int
do_list (const char *inputfilename, int lineno, int argc, const char **argv)
{
    struct _list_data ld;

    ld.fp = stdout;
    ld.numeric = (argv[0][0] == 'n');

    if (argc == 1) {
	register AuthList *l;

	if (xauth_head) {
	    for (l = xauth_head; l; l = l->next) {
		dump_entry (inputfilename, lineno, l->auth, (char *) &ld);
	    }
	}
	return 0;
    }

    return iterdpy (inputfilename, lineno, 1, argc, argv,
		    dump_entry, NULL, (char *) &ld);
}

/*
 * merge filename [filename ...]
 */
static int
do_merge(const char *inputfilename, int lineno, int argc, const char **argv)
{
    int i;
    int errors = 0;
    AuthList *head, *tail, *listhead, *listtail;
    int nentries, nnew, nrepl;
    Bool numeric = False;

    if (argc < 2) {
	prefix (inputfilename, lineno);
	badcommandline (argv[0]);
	return 1;
    }

    if (argv[0][0] == 'n') numeric = True;
    listhead = listtail = NULL;

    for (i = 1; i < argc; i++) {
	const char *filename = argv[i];
	FILE *fp;
	Bool used_stdin = False;

	fp = open_file (&filename,
			numeric ? "r" : "rb",
			&used_stdin, inputfilename, lineno,
			argv[0]);
	if (!fp) {
	    errors++;
	    continue;
	}

	head = tail = NULL;
	nentries = read_auth_entries (fp, numeric, &head, &tail);
	if (nentries == 0) {
	    prefix (inputfilename, lineno);
	    fprintf (stderr, "unable to read any entries from file \"%s\"\n",
		     filename);
	    errors++;
	} else {			/* link it in */
	    add_to_list (listhead, listtail, head);
 	}

	if (!used_stdin) (void) fclose (fp);
    }

    /*
     * if we have new entries, merge them in (freeing any duplicates)
     */
    if (listhead) {
	nentries = merge_entries (&xauth_head, listhead, &nnew, &nrepl);
	if (verbose)
	  printf ("%d entries read in:  %d new, %d replacement%s\n",
	  	  nentries, nnew, nrepl, nrepl != 1 ? "s" : "");
	if (nentries > 0) xauth_modified = True;
	sort_entries(&xauth_head);
    }

    return 0;
}

/*
 * extract filename displayname [displayname ...]
 */
static int
do_extract(const char *inputfilename, int lineno, int argc, const char **argv)
{
    int errors;
    struct _extract_data ed;

    if (argc < 3) {
	prefix (inputfilename, lineno);
	badcommandline (argv[0]);
	return 1;
    }

    ed.fp = NULL;
    ed.filename = argv[1];
    ed.used_stdout = False;
    ed.numeric = (argv[0][0] == 'n');
    ed.nwritten = 0;
    ed.cmd = argv[0];

    errors = iterdpy (inputfilename, lineno, 2, argc, argv,
		      extract_entry, NULL, (char *) &ed);

    if (!ed.fp) {
	fprintf (stderr,
		 "No matches found, authority file \"%s\" not written\n",
		 ed.filename);
    } else {
	if (verbose) {
	    printf ("%d entries written to \"%s\"\n",
		    ed.nwritten, ed.filename);
	}
	if (!ed.used_stdout) {
	    (void) fclose (ed.fp);
	}
    }

    return errors;
}


/*
 * add displayname protocolname hexkey
 */

static int
do_add(const char *inputfilename, int lineno, int argc, const char **argv)
{
    int n, nnew, nrepl;
    int len;
    const char *dpyname;
    const char *protoname;
    const char *hexkey;
    char *key;
    AuthList *list, *list_cur, *list_next;

    if (argc != 4 || !argv[1] || !argv[2] || !argv[3]) {
	prefix (inputfilename, lineno);
	badcommandline (argv[0]);
	return 1;
    }

    dpyname = argv[1];
    protoname = argv[2];
    hexkey = argv[3];

    len = strlen(hexkey);
    if (hexkey[0] == '"' && hexkey[len-1] == '"') {
	key = malloc(len-1);
	strncpy(key, hexkey+1, len-2);
	len -= 2;
    } else if (!strcmp(protoname, SECURERPC) ||
	       !strcmp(protoname, K5AUTH)) {
	key = malloc(len+1);
	strcpy(key, hexkey);
    } else {
	len = cvthexkey (hexkey, &key);
	if (len < 0) {
	    prefix (inputfilename, lineno);
	    fprintf (stderr,
		     "key contains odd number of or non-hex characters\n");
	    return 1;
	}
    }

    if (!get_displayname_auth (dpyname, &list)) {
	prefix (inputfilename, lineno);
	baddisplayname (dpyname, argv[0]);
	free (key);
	return 1;
    }

    /*
     * allow an abbreviation for common protocol names
     */
    if (strcmp (protoname, DEFAULT_PROTOCOL_ABBREV) == 0) {
	protoname = DEFAULT_PROTOCOL;
    }

    for (list_cur = list;  list_cur != NULL; list_cur = list_cur->next) {
	Xauth *auth = list_cur->auth;

	auth->name_length = strlen (protoname);
	auth->name = copystring (protoname, auth->name_length);
	if (!auth->name) {
	    prefix (inputfilename, lineno);
	    fprintf (stderr, "unable to allocate %d character protocol name\n",
		     auth->name_length);
	    for (list_cur = list; list_cur != NULL; list_cur = list_next) {
		list_next = list_cur->next;
		XauDisposeAuth(list_cur->auth);
		free(list_cur);
	    }
	    free (key);
	    return 1;
	}
	auth->data_length = len;
	auth->data = malloc(len);
	if (!auth->data) {
		prefix(inputfilename, lineno);
		fprintf(stderr, "unable to allocate %d bytes for key\n", len);
		for (list_cur = list; list_cur != NULL; list_cur = list_next) {
			list_next = list_cur->next;
			XauDisposeAuth(list_cur->auth);
			free(list_cur);
		}
		free(key);
		return 1;
	}
	memcpy(auth->data, key, len);
    }
    free(key);
    /*
     * merge it in; note that merge will deal with allocation
     */
    n = merge_entries (&xauth_head, list, &nnew, &nrepl);
    if (n <= 0) {
	prefix (inputfilename, lineno);
	fprintf (stderr, "unable to merge in added record\n");
	return 1;
    }
    sort_entries(&xauth_head);

    xauth_modified = True;
    return 0;
}

/*
 * remove displayname
 */
static int
do_remove(const char *inputfilename, int lineno, int argc, const char **argv)
{
    int nremoved = 0;
    int errors;

    if (argc < 2) {
	prefix (inputfilename, lineno);
	badcommandline (argv[0]);
	return 1;
    }

    errors = iterdpy (inputfilename, lineno, 1, argc, argv,
		      remove_entry, NULL, (char *) &nremoved);
    if (verbose) printf ("%d entries removed\n", nremoved);
    return errors;
}

/*
 * info
 */
static int
do_info(const char *inputfilename, int lineno, int argc, const char **argv)
{
    int n;
    AuthList *l;

    if (argc != 1) {
	prefix (inputfilename, lineno);
	badcommandline (argv[0]);
	return 1;
    }

    for (l = xauth_head, n = 0; l; l = l->next, n++) ;

    printf ("Authority file:       %s\n",
	    xauth_filename ? xauth_filename : "(none)");
    printf ("File new:             %s\n", xauth_existed ? No : Yes);
    printf ("File locked:          %s\n", xauth_locked ? No : Yes);
    printf ("Number of entries:    %d\n", n);
    printf ("Changes honored:      %s\n", xauth_allowed ? Yes : No);
    printf ("Changes made:         %s\n", xauth_modified ? Yes : No);
    printf ("Current input:        %s:%d\n", inputfilename, lineno);
    return 0;
}


/*
 * exit
 */
static Bool alldone = False;

/* ARGSUSED */
static int
do_exit(const char *inputfilename, int lineno, int argc, const char **argv)
{
    /* allow bogus stuff */
    alldone = True;
    return 0;
}

/*
 * quit
 */
/* ARGSUSED */
static int
do_quit(const char *inputfilename, int lineno, int argc, const char **argv)
{
    /* allow bogus stuff */
    die (0);
    /* NOTREACHED */
    return -1;				/* for picky compilers */
}


/*
 * source filename
 */
static int
do_source(const char *inputfilename, int lineno, int argc, const char **argv)
{
    const char *script;
    char buf[BUFSIZ];
    FILE *fp;
    Bool used_stdin = False;
    int len;
    int errors = 0, status;
    int sublineno = 0;
    const char **subargv;
    int subargc;
    Bool prompt = False;		/* only true if reading from tty */

    if (argc != 2 || !argv[1]) {
	prefix (inputfilename, lineno);
	badcommandline (argv[0]);
	return 1;
    }

    script = argv[1];

    fp = open_file (&script, "r", &used_stdin, inputfilename, lineno, argv[0]);
    if (!fp) {
	return 1;
    }

    if (verbose && used_stdin && isatty (fileno (fp))) prompt = True;

    while (!alldone) {
	buf[0] = '\0';
	if (prompt) {
	    printf ("xauth> ");
	    fflush (stdout);
	}
	if (fgets (buf, sizeof buf, fp) == NULL) break;
	sublineno++;
	len = strlen (buf);
	if (len == 0 || buf[0] == '#') continue;
	if (buf[len-1] != '\n') {
	    prefix (script, sublineno);
	    fprintf (stderr, "line too long\n");
	    errors++;
	    break;
	}
	buf[--len] = '\0';		/* remove new line */
	subargv = (const char **) split_into_words (buf, &subargc);
	if (subargv) {
	    status = process_command (script, sublineno, subargc, subargv);
	    free (subargv);
	    errors += status;
	} else {
	    prefix (script, sublineno);
	    fprintf (stderr, "unable to break line into words\n");
	    errors++;
	}
    }

    if (!used_stdin) {
	(void) fclose (fp);
    }
    return errors;
}

static int x_protocol_error;
static int
catch_x_protocol_error(Display *dpy, XErrorEvent *errevent)
{
    char buf[80];
    XGetErrorText(dpy, errevent->error_code, buf, sizeof (buf));
    fprintf(stderr, "%s\n", buf);
    x_protocol_error = errevent->error_code;
    return 1;
}

/*
 * generate
 */
static int
do_generate(const char *inputfilename, int lineno, int argc, const char **argv)
{
    const char *displayname;
    int major_version, minor_version;
    XSecurityAuthorization id_return;
    Xauth *auth_in, *auth_return;
    XSecurityAuthorizationAttributes attributes;
    unsigned long attrmask = 0;
    Display *dpy;
    int status;
    const char *args[4];
    const char *protoname = ".";
    int i;
    int authdatalen = 0;
    const char *hexdata;
    char *authdata = NULL;
    char *hex;

    if (argc < 2 || !argv[1]) {
	prefix (inputfilename, lineno);
	badcommandline (argv[0]);
	return 1;
    }

    displayname = argv[1];

    if (argc > 2) {
	protoname = argv[2];
    }

    for (i = 3; i < argc; i++) {
	if (0 == strcmp(argv[i], "timeout")) {
	    if (++i == argc) {
		prefix (inputfilename, lineno);
		badcommandline (argv[i-1]);
		return 1;
	    }
	    attributes.timeout = atoi(argv[i]);
	    attrmask |= XSecurityTimeout;

	} else if (0 == strcmp(argv[i], "trusted")) {
	    attributes.trust_level = XSecurityClientTrusted;
	    attrmask |= XSecurityTrustLevel;

	} else if (0 == strcmp(argv[i], "untrusted")) {
	    attributes.trust_level = XSecurityClientUntrusted;
	    attrmask |= XSecurityTrustLevel;

	} else if (0 == strcmp(argv[i], "group")) {
	    if (++i == argc) {
		prefix (inputfilename, lineno);
		badcommandline (argv[i-1]);
		return 1;
	    }
	    attributes.group = atoi(argv[i]);
	    attrmask |= XSecurityGroup;

	} else if (0 == strcmp(argv[i], "data")) {
	    if (++i == argc) {
		prefix (inputfilename, lineno);
		badcommandline (argv[i-1]);
		return 1;
	    }
	    hexdata = argv[i];
	    authdatalen = strlen(hexdata);
	    if (hexdata[0] == '"' && hexdata[authdatalen-1] == '"') {
		authdata = malloc(authdatalen-1);
		strncpy(authdata, hexdata+1, authdatalen-2);
		authdatalen -= 2;
	    } else {
		authdatalen = cvthexkey (hexdata, &authdata);
		if (authdatalen < 0) {
		    prefix (inputfilename, lineno);
		    fprintf (stderr,
			     "data contains odd number of or non-hex characters\n");
		    return 1;
		}
	    }
	} else {
	    prefix (inputfilename, lineno);
	    badcommandline (argv[i]);
	    return 1;
	}
    }

    /* generate authorization using the Security extension */

    dpy = XOpenDisplay (displayname);
    if (!dpy) {
	prefix (inputfilename, lineno);
	fprintf (stderr, "unable to open display \"%s\".\n", displayname);
	return 1;
    }

    status = XSecurityQueryExtension(dpy, &major_version, &minor_version);
    if (!status)
    {
	prefix (inputfilename, lineno);
	fprintf (stderr, "couldn't query Security extension on display \"%s\"\n",
		 displayname);
        return 1;
    }

    /* fill in input Xauth struct */

    auth_in = XSecurityAllocXauth();
    if (strcmp (protoname, DEFAULT_PROTOCOL_ABBREV) == 0) {
	 auth_in->name = copystring(DEFAULT_PROTOCOL, strlen(DEFAULT_PROTOCOL));
    }
    else
	auth_in->name = copystring (protoname, strlen(protoname));
    auth_in->name_length = strlen(auth_in->name);
    auth_in->data = authdata;
    auth_in->data_length = authdatalen;

    x_protocol_error = 0;
    XSetErrorHandler(catch_x_protocol_error);
    auth_return = XSecurityGenerateAuthorization(dpy, auth_in, attrmask,
						 &attributes, &id_return);
    XSync(dpy, False);

    if (!auth_return || x_protocol_error)
    {
	prefix (inputfilename, lineno);
	fprintf (stderr, "couldn't generate authorization\n");
	return 1;
    }

    if (verbose)
	printf("authorization id is %ld\n", id_return);

    /* create a fake input line to give to do_add */
    hex = bintohex(auth_return->data_length, auth_return->data);
    args[0] = "add";
    args[1] = displayname;
    args[2] = auth_in->name;
    args[3] = hex;

    status = do_add(inputfilename, lineno, 4, args);

    if (authdata) free(authdata);
    XSecurityFreeXauth(auth_in);
    XSecurityFreeXauth(auth_return);
    free(hex);
    XCloseDisplay(dpy);
    return status;
}
