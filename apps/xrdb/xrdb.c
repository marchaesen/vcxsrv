/*
 * xrdb - X resource manager database utility
 *
 */

/*
 *			  COPYRIGHT 1987, 1991
 *		   DIGITAL EQUIPMENT CORPORATION
 *		       MAYNARD, MASSACHUSETTS
 *		   MASSACHUSETTS INSTITUTE OF TECHNOLOGY
 *		       CAMBRIDGE, MASSACHUSETTS
 *			ALL RIGHTS RESERVED.
 *
 * THE INFORMATION IN THIS SOFTWARE IS SUBJECT TO CHANGE WITHOUT NOTICE AND
 * SHOULD NOT BE CONSTRUED AS A COMMITMENT BY DIGITAL EQUIPMENT CORPORATION.
 * DIGITAL MAKES NO REPRESENTATIONS ABOUT THE SUITABILITY OF THIS SOFTWARE FOR
 * ANY PURPOSE.  IT IS SUPPLIED "AS IS" WITHOUT EXPRESS OR IMPLIED WARRANTY.
 *
 * IF THE SOFTWARE IS MODIFIED IN A MANNER CREATING DERIVATIVE COPYRIGHT RIGHTS,
 * APPROPRIATE LEGENDS MAY BE PLACED ON THE DERIVATIVE WORK IN ADDITION TO THAT
 * SET FORTH ABOVE.
 *
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Digital Equipment Corporation not be
 * used in advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.
 */

/*
 * this program is used to load, or dump the resource manager database
 * in the server.
 *
 * Original Author: Jim Gettys, August 28, 1987
 * Extensively Modified: Phil Karlton, January 5, 1987
 * Modified a Bunch More: Bob Scheifler, February, 1991
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xos.h>
#include <X11/Xmu/SysUtil.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>

#ifdef NEED_SYS_PARAM_H
# include <sys/param.h>		/* defines MAXHOSTNAMELEN on BSD & Linux */
#endif

#ifdef NEED_NETDB_H
# include <netdb.h>		/* defines MAXHOSTNAMELEN on Solaris */
#endif

#define SCREEN_RESOURCES "SCREEN_RESOURCES"

#ifndef CPP
#ifdef __UNIXOS2__
/* expected to be in path */
#define CPP "cpp"
#else
#define CPP "/usr/lib/cpp"
#endif /* __UNIXOS2__ */
#endif /* CPP */

#define INIT_BUFFER_SIZE 10000
#define INIT_ENTRY_SIZE 500

#define RALL 0
#define RGLOBAL 1
#define RSCREEN 2
#define RSCREENS 3

#define OPSYMBOLS 0
#define OPQUERY 1
#define OPREMOVE 2
#define OPEDIT 3
#define OPLOAD 4
#define OPMERGE 5
#define OPOVERRIDE 6

#define RESOURCE_PROPERTY_NAME "RESOURCE_MANAGER"
#define BACKUP_SUFFIX ".bak"		/* for editting */

typedef struct _Entry {
    char *tag, *value;
    int lineno;
    Bool usable;
} Entry;
typedef struct _Buffer {
    char *buff;
    int  room, used;
} Buffer;
typedef struct _Entries {
    Entry *entry;
    int   room, used;
} Entries;

/* dynamically allocated strings */
#define CHUNK_SIZE 4096
typedef struct _String {
    char *val;
    int room, used;
} String;

static char *ProgramName;
static Bool quiet = False;
static char tmpname[32];
static char *filename = NULL;
#ifdef PATHETICCPP
static Bool need_real_defines = False;
static char tmpname2[32];
#endif
#ifdef WIN32
static char tmpname3[32];
#endif
static int oper = OPLOAD;
static char *editFile = NULL;
static const char *cpp_program = NULL;
static const char* const cpp_locations[] = { CPP };
static char *backup_suffix = BACKUP_SUFFIX;
static Bool dont_execute = False;
static String defines;
static int defines_base;
#define MAX_CMD_DEFINES 512
static char *cmd_defines[MAX_CMD_DEFINES];
static int num_cmd_defines = 0;
static String includes;
static Display *dpy;
static Buffer buffer;
static Entries newDB;

static void fatal(char *, ...);
static void addstring ( String *arg, const char *s );
static void addescapedstring ( String *arg, const char *s );
static void addtokstring ( String *arg, const char *s );
static void FormatEntries ( Buffer *buffer, Entries *entries );
static void StoreProperty ( Display *dpy, Window root, Atom res_prop );
static void Process ( int scrno, Bool doScreen, Bool execute );
static void ShuffleEntries ( Entries *db, Entries *dbs, int num );
static void ReProcess ( int scrno, Bool doScreen );

#ifndef HAVE_ASPRINTF
/* sprintf variant found in newer libc's which allocates string to print to */
static int _X_ATTRIBUTE_PRINTF(2,3)
asprintf(char ** ret, const char *format, ...)
{
    char buf[256];
    int len;
    va_list ap;

    va_start(ap, format);
    len = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    if (len < 0)
	return -1;

    if (len < sizeof(buf))
    {
	*ret = strdup(buf);
    }
    else
    {
	*ret = malloc(len + 1); /* snprintf doesn't count trailing '\0' */
	if (*ret != NULL)
	{
	    va_start(ap, format);
	    len = vsnprintf(*ret, len + 1, format, ap);
	    va_end(ap);
	    if (len < 0) {
		free(*ret);
		*ret = NULL;
	    }
	}
    }

    if (*ret == NULL)
	return -1;

    return len;
}
#endif /* HAVE_ASPRINTF */

static void 
InitBuffer(Buffer *b)
{
    b->room = INIT_BUFFER_SIZE;
    b->used = 0;
    b->buff = (char *)malloc(INIT_BUFFER_SIZE*sizeof(char));
}

#ifdef notyet
static void 
FreeBuffer(Buffer *b)
{
    free(b->buff);
}
#endif

static void 
AppendToBuffer(Buffer *b, char *str, int len)
{
    while (b->used + len > b->room) {
	b->buff = (char *)realloc(b->buff, 2*b->room*(sizeof(char)));
	b->room *= 2;
    }
    strncpy(b->buff + b->used, str, len);
    b->used += len;
}

static void 
InitEntries(Entries *e)
{
    e->room = INIT_ENTRY_SIZE;
    e->used = 0;
    e->entry = (Entry *)malloc(INIT_ENTRY_SIZE*sizeof(Entry));
}

static void 
FreeEntries(Entries *e)
{
    register int i;

    for (i = 0; i < e->used; i++) {
	if (e->entry[i].usable) {
	    free(e->entry[i].tag);
	    free(e->entry[i].value);
	}
    }
    free((char *)e->entry);
}

static void 
AddEntry(Entries *e, Entry *entry)
{
    register int n;

    for (n = 0; n < e->used; n++) {
	if (!strcmp(e->entry[n].tag, entry->tag)) {
	    /* overwrite old entry */
	    if (e->entry[n].lineno && !quiet) {
		fprintf (stderr, 
			 "%s:  \"%s\" on line %d overrides entry on line %d\n",
			 ProgramName, entry->tag, entry->lineno, 
			 e->entry[n].lineno);
	    }
	    free(e->entry[n].tag);
	    free(e->entry[n].value);
	    entry->usable = True;
	    e->entry[n] = *entry;
	    return;  /* ok to leave, now there's only one of each tag in db */
	}
    }

    if (e->used == e->room) {
	e->entry = (Entry *)realloc((char *)e->entry,
				    2*e->room*(sizeof(Entry)));
	e->room *= 2;
    }
    entry->usable = True;
    e->entry[e->used++] = *entry;
}


static int 
CompareEntries(const void *e1, const void *e2)
{
    return strcmp(((Entry *)e1)->tag, ((Entry *)e2)->tag);
}

static void 
AppendEntryToBuffer(Buffer *buffer, Entry *entry)
{
    AppendToBuffer(buffer, entry->tag, strlen(entry->tag));
    AppendToBuffer(buffer, ":\t", 2);
    AppendToBuffer(buffer, entry->value, strlen(entry->value));
    AppendToBuffer(buffer, "\n", 1);
}

/*
 * Return the position of the first unescaped occurrence of dest in string.
 * If lines is non-null, return the number of newlines skipped over.
 */
static char *
FindFirst(char *string, char dest, int *lines)
{
    if (lines)
	*lines = 0;
    for (;;) {
	if (*string == '\0')
	    return NULL;
	if (*string == '\\') {
	    if (*++string == '\0')
		return NULL;
	} else if (*string == dest)
	    return string;
	if (*string == '\n'  &&  lines)
	    (*lines)++;
	string++;
    }
}

static void 
GetEntries(Entries *entries, Buffer *buff, int bequiet)
{
    register char *line, *colon, *temp, *str;
    Entry entry;
    register int length;
    int lineno = 0;
    int lines_skipped;

    str = buff->buff;
    if (!str) return;
    for ( ; str < buff->buff + buff->used;
	  str = line + 1, lineno += lines_skipped) {
	line = FindFirst(str, '\n', &lines_skipped);
	lineno++;
	if (!line)
	    line = buff->buff + buff->used;
	if (*str == '!')
	    continue;
	if (*str == '\n')
	    continue;
	if (!bequiet && *str == '#') {
	    int dummy;
	    if (sscanf (str, "# %d", &dummy) == 1 ||
		sscanf (str, "# line %d", &dummy) == 1)
		lineno = dummy - 1;
	    continue;
	}
	for (temp = str; 
	     *temp && *temp != '\n' && isascii(*temp) && isspace(*temp); 
	     temp++) ;
	if (!*temp || *temp == '\n') continue;

	colon = FindFirst(str, ':', NULL);
	if (!colon || colon > line) {
	    if (!bequiet && !quiet)
		fprintf (stderr, 
			 "%s: colon missing on line %d, ignoring line\n",
			 ProgramName, lineno);
	    continue;
	}

	/* strip leading and trailing blanks from name and store result */
	while (*str == ' ' || *str == '\t')
	    str++;
	length = colon - str;
	while (length && (str[length-1] == ' ' || str[length-1] == '\t'))
	    length--;
	temp = (char *)malloc(length + 1);
	strncpy(temp, str, length);
	temp[length] = '\0';
	entry.tag = temp;

	/* strip leading and trailing blanks from value and store result */
	colon++;
	while (*colon == ' ' || *colon == '\t')
	    colon++;
	length = line - colon;
	temp = (char *)malloc(length + 1);
	strncpy(temp, colon, length);
	temp[length] = '\0';
	entry.value = temp;
	entry.lineno = bequiet ? 0 : lineno;

	AddEntry(entries, &entry);
    }
}

static void
GetEntriesString(Entries *entries, char *str)
{
    Buffer buff;

    if (str && *str) {
	buff.buff = str;
	buff.used = strlen(str);
	GetEntries(entries, &buff, 1);
    }
}

static void 
ReadFile(Buffer *buffer, FILE *input)
{
	     char	buf[BUFSIZ + 1];
    register int	bytes;

    buffer->used = 0;
    while (!feof(input) && (bytes = fread(buf, 1, BUFSIZ, input)) > 0) {
#ifdef WIN32
	char *p;
	buf[bytes] = '\0';
	for (p = buf; p = strchr(p, '\r'); ) {
	    if (p[-1] == '\\' && p[1] == '\n') {
		bytes -= 3;
		strcpy(p - 1, p + 2);
	    }
	}
#endif
	AppendToBuffer(buffer, buf, bytes);
    }
    AppendToBuffer(buffer, "", 1);
}

static void
AddDef(String *buff, char *title, char *value)
{
#ifdef PATHETICCPP
    if (need_real_defines) {
	addstring(buff, "\n#define ");
	addtokstring(buff, title);
	if (value && (value[0] != '\0')) {
	    addstring(buff, " ");
	    addstring(buff, value);
	}
	return;
    }
#endif
    if (buff->used) {
	if (oper == OPSYMBOLS)
	    addstring(buff, "\n-D");
	else
	    addstring(buff, " -D");
    } else
	addstring(buff, "-D");
    addtokstring(buff, title);
    if (value && (value[0] != '\0')) {
	addstring(buff, "=");
	addescapedstring(buff, value);
    }
}

static void
AddSimpleDef(String *buff, char *title)
{
    AddDef(buff, title, (char *)NULL);
}

static void
AddDefQ(String *buff, char *title, char *value)
{
#ifdef PATHETICCPP
    if (need_real_defines)
	AddDef(buff, title, value);
    else
#endif
    if (value && (value[0] != '\0')) {
	AddSimpleDef(buff, title);
	addstring(buff, "=\"");
	addescapedstring(buff, value);
	addstring(buff, "\"");
    } else
	AddDef(buff, title, NULL);
}

static void
AddNum(String *buff, char *title, int value)
{
    char num[20];
    snprintf(num, sizeof(num), "%d", value);
    AddDef(buff, title, num);
}

static void
AddDefTok(String *buff, char *prefix, char *title)
{
    char name[512];

    snprintf(name, sizeof(name), "%s%s", prefix, title);
    AddSimpleDef(buff, name);
}

static void
AddDefHostname(String *buff, char *title, char *value)
{
    char *s;
    char name[512];
    char c;

    strncpy (name, value, sizeof(name)-1);
    name[sizeof(name)-1] = '\0';
    for (s = name; (c = *s); s++) {
	if (!isalpha(c) && !isdigit(c) && c != '_' && c != '.' && c != ':' && c != '-')
	    *s = '_';
    }
    AddDef(buff, title, name);
}

static void
AddUndef(String *buff, char *title)
{
#ifdef PATHETICCPP
    if (need_real_defines) {
	addstring(buff, "\n#undef ");
	addstring(buff, title);
	return;
    }
#endif
    if (buff->used) {
	if (oper == OPSYMBOLS)
	    addstring(buff, "\n-U");
	else
	    addstring(buff, " -U");
    } else
	addstring(buff, "-U");
    addtokstring(buff, title);
}

static void 
DoCmdDefines(String *buff)
{
    int i;
    char *arg, *val;

    for (i = 0; i < num_cmd_defines; i++) {
	arg = cmd_defines[i];
	if (arg[1] == 'D') {
	    val = strchr(arg, '=');
	    if (val) {
		*val = '\0';
		AddDefQ(buff, arg + 2, val + 1);
		*val = '=';
	    } else
		AddSimpleDef(buff, arg + 2);
	} else
	    AddUndef(buff, arg + 2);
    }
}

static int 
Resolution(int pixels, int mm)
{
    if (mm == 0)
	return 0;
    else
	return ((pixels * 100000 / mm) + 50) / 100;
}


static void
DoDisplayDefines(Display *display, String *defs, char *host)
{
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 255
#endif
    char client[MAXHOSTNAMELEN], server[MAXHOSTNAMELEN], *colon;
    char **extnames;
    int n;
    
    XmuGetHostname(client, MAXHOSTNAMELEN);
    strncpy(server, XDisplayName(host), sizeof(server));
    server[sizeof(server) - 1] = '\0';
    /* search for final colon to skip over any embedded colons in IPv6
       numeric address forms */
    colon = strrchr(server, ':');
    n = 0;
    if (colon) {
	/* remove extra colon if there are exactly two, since it indicates 
	   DECnet.  Three colons is an IPv6 address ending in :: though. */
	if ((colon > server) && (*(colon-1) == ':') &&
	  ( ((colon - 1) == server) || (*(colon-2) != ':') ) ) {
	    *(colon-1) = ':';
	}
	*colon++ = '\0';
	sscanf(colon, "%d", &n);
    }
    if (!*server || !strcmp(server, "unix") || !strcmp(server, "localhost"))
	strcpy(server, client);
    AddDefHostname(defs, "HOST", server); /* R3 compatibility */
    AddDefHostname(defs, "SERVERHOST", server);
    AddDefTok(defs, "SRVR_", server);
    AddNum(defs, "DISPLAY_NUM", n);
    AddDefHostname(defs, "CLIENTHOST", client);
    AddDefTok(defs, "CLNT_", client);
    AddNum(defs, "VERSION", ProtocolVersion(display));
    AddNum(defs, "REVISION", ProtocolRevision(display));
    AddDefQ(defs, "VENDOR", ServerVendor(display));
    AddDefTok(defs, "VNDR_", ServerVendor(display));
    AddNum(defs, "RELEASE", VendorRelease(display));
    AddNum(defs, "NUM_SCREENS", ScreenCount(display));
    extnames = XListExtensions(display, &n);
    while (--n >= 0)
	AddDefTok(defs, "EXT_", extnames[n]);
    XFreeExtensionList(extnames);
}

static char *ClassNames[] = {
    "StaticGray",
    "GrayScale",
    "StaticColor",
    "PseudoColor",
    "TrueColor",
    "DirectColor"
};

static void
DoScreenDefines(Display *display, int scrno, String *defs)
{
    Screen *screen;
    Visual *visual;
    XVisualInfo vinfo, *vinfos;
    int nv, i, j;
    char name[50];
    
    screen = ScreenOfDisplay(display, scrno);
    visual = DefaultVisualOfScreen(screen);
    vinfo.screen = scrno;
    vinfos = XGetVisualInfo(display, VisualScreenMask, &vinfo, &nv);
    AddNum(defs, "SCREEN_NUM", scrno);
    AddNum(defs, "WIDTH", screen->width);
    AddNum(defs, "HEIGHT", screen->height);
    AddNum(defs, "X_RESOLUTION", Resolution(screen->width,screen->mwidth));
    AddNum(defs, "Y_RESOLUTION", Resolution(screen->height,screen->mheight));
    AddNum(defs, "PLANES", DisplayPlanes(display, scrno));
    AddNum(defs, "BITS_PER_RGB", visual->bits_per_rgb);
    AddDefQ(defs, "CLASS", ClassNames[visual->class]);
    snprintf(name, sizeof(name), "CLASS_%s", ClassNames[visual->class]);
    AddNum(defs, name, (int)visual->visualid);
    switch(visual->class) {
	case StaticColor:
	case PseudoColor:
	case TrueColor:
	case DirectColor:
	    AddSimpleDef(defs, "COLOR");
	    break;
    }
    for (i = 0; i < nv; i++) {
	for (j = i; --j >= 0; ) {
	    if (vinfos[j].class == vinfos[i].class &&
		vinfos[j].depth == vinfos[i].depth)
		break;
	}
	if (j < 0) {
	    snprintf(name, sizeof(name), "CLASS_%s_%d",
		    ClassNames[vinfos[i].class], vinfos[i].depth);
	    AddNum(defs, name, (int)vinfos[i].visualid);
	}
    }
    XFree((char *)vinfos);
}

static Entry *
FindEntry(Entries *db, Buffer  *b)
{
    int i;
    register Entry *e;
    Entries phoney;
    Entry entry;

    entry.usable = False;
    entry.tag = NULL;
    entry.value = NULL;
    phoney.used = 0;
    phoney.room = 1;
    phoney.entry = &entry;
    GetEntries(&phoney, b, 1);
    if (phoney.used < 1)
	return NULL;
    for (i = 0; i < db->used; i++) {
	e = &db->entry[i];
	if (!e->usable)
	    continue;
	if (strcmp(e->tag, entry.tag))
	    continue;
	e->usable = False;
	if (strcmp(e->value, entry.value))
	    return e;
	return NULL;
    }
    return NULL;
}

static void 
EditFile(Entries *new, FILE *in, FILE *out)
{
    Buffer b;
    char buff[BUFSIZ];
    register Entry *e;
    register char *c;
    int i;

    InitBuffer(&b);
    while (in) {
	b.used = 0;
	while (1) {
	    buff[0] ='\0';
	    if (!fgets(buff, BUFSIZ, in))
		goto cleanup;
	    AppendToBuffer(&b, buff, strlen(buff));
	    c = &b.buff[b.used - 1];
	    if ((*(c--) == '\n') && (b.used == 1 || *c != '\\'))
		break;
	}
	if ((e = FindEntry(new, &b)))
	    fprintf(out, "%s:\t%s\n", e->tag, e->value);
	else
	    fwrite(b.buff, 1, b.used, out);
    }
cleanup:
    for (i = 0; i < new->used; i++) {
	e = &new->entry[i];
	if (e->usable)
	    fprintf(out, "%s:\t%s\n", e->tag, e->value);
    }
}

static void 
Syntax (void)
{
    fprintf (stderr, 
	     "usage:  %s [-options ...] [filename]\n\n"
	     "where options include:\n"
	     " -display host:dpy   display to use\n"
	     " -all                do all resources [default]\n"
	     " -global             do screen-independent resources\n"
	     " -screen             do screen-specific resources for one screen\n"
	     " -screens            do screen-specific resources for all screens\n"
	     " -n                  show but don't do changes\n"
	     " -cpp filename       preprocessor to use [%s]\n"
	     " -nocpp              do not use a preprocessor\n"
	     " -query              query resources\n"
	     " -load               load resources from file [default]\n"
	     " -override           add in resources from file\n"
	     " -merge              merge resources from file & sort\n"
	     " -edit filename      edit resources into file\n"
	     " -backup string      backup suffix for -edit [%s]\n"
	     " -symbols            show preprocessor symbols\n"
	     " -remove             remove resources\n"
	     " -retain             avoid server reset (avoid using this)\n"
	     " -quiet              don't warn about duplicates\n"
	     " -Dname[=value], -Uname, -Idirectory    passed to preprocessor\n"
	     "\n"
	     "A - or no input filename represents stdin.\n",
	     ProgramName, cpp_program ? cpp_program : "", BACKUP_SUFFIX);
    exit (1);
}

/*
 * The following is a hack until XrmParseCommand is ready.  It determines
 * whether or not the given string is an abbreviation of the arg.
 */

static Bool 
isabbreviation(char *arg, char *s, int minslen)
{
    int arglen;
    int slen;

    /* exact match */
    if (!strcmp (arg, s)) return (True);

    arglen = strlen (arg);
    slen = strlen (s);

    /* too long or too short */
    if (slen >= arglen || slen < minslen) return (False);

    /* abbreviation */
    if (strncmp (arg, s, slen) == 0) return (True);

    /* bad */
    return (False);
}

static void
addstring(String *arg, const char *s)
{
    if(arg->used + strlen(s) + 1 >= arg->room) {
	if(arg->val)
	    arg->val = (char *)realloc(arg->val, arg->room + CHUNK_SIZE);
	else
	    arg->val = (char *)malloc(arg->room + CHUNK_SIZE);	    
	if(arg->val == NULL)
	    fatal("%s: Not enough memory\n", ProgramName);
	arg->room += CHUNK_SIZE;
    }
    if(arg->used)
	strcat(arg->val, s);
    else
	strcpy(arg->val, s);
    arg->used += strlen(s);
}   

static void
addescapedstring(String *arg, const char *s)
{
    char copy[512], *c;

    for (c = copy; *s && c < &copy[sizeof(copy)-1]; s++) {
	switch (*s) {
	case '"':       case '\'':      case '`':
	case '$':       case '\\':
	    *c++ = '_';
	    break;
	default:
	    *c++ = *s;
	}
    }
    *c = 0;
    addstring (arg, copy);
}

static void
addtokstring(String *arg, const char *s)
{
    char copy[512], *c;

    for (c = copy; *s && c < &copy[sizeof(copy)-1]; s++) {
	if (!isalpha(*s) && !isdigit(*s) && *s != '_')
	    *c++ = '_';
	else
	    *c++ = *s;
    }
    *c = 0;
    addstring (arg, copy);
}


int
main(int argc, char *argv[])
{
    int i;
    char *displayname = NULL;
    int whichResources = RALL;
    int retainProp = 0;
    FILE *fp = NULL;
    Bool need_newline;

    ProgramName = argv[0];

    defines.room = defines.used = includes.room = includes.used = 0;

    /* initialize the includes String struct */
    addstring(&includes, "");

    /* Pick the default cpp to use.  This needs to be done before
     * we parse the command line in order to honor -nocpp which sets
     * it back to NULL.
     */
    if (cpp_program == NULL) {
	int number_of_elements
	    = (sizeof cpp_locations) / (sizeof cpp_locations[0]);
	int j;

	for (j = 0; j < number_of_elements; j++) {
	    char *end, *dup;
	    /* cut off arguments */
	    dup = strdup(cpp_locations[j]);
	    end = strchr(dup,' ');
	    if (end)
		*end = '\0';
	    if (access(dup, X_OK) == 0) {
		cpp_program = cpp_locations[j];
		free(dup);
		break;
	    }
	    free(dup);
	}
    }

    /* needs to be replaced with XrmParseCommand */

    for (i = 1; i < argc; i++) {
	char *arg = argv[i];

	if (arg[0] == '-') {
	    if (arg[1] == '\0') {
		filename = NULL;
		continue;
	    } else if (isabbreviation ("-help", arg, 2)) {
		Syntax ();
		/* doesn't return */
	    } else if (isabbreviation ("-display", arg, 2)) {
		if (++i >= argc) Syntax ();
		displayname = argv[i];
		continue;
	    } else if (isabbreviation ("-geometry", arg, 3)) {
		if (++i >= argc) Syntax ();
		/* ignore geometry */
		continue;
	    } else if (isabbreviation ("-cpp", arg, 2)) {
		if (++i >= argc) Syntax ();
		cpp_program = argv[i];
		continue;
	    } else if (!strcmp ("-n", arg)) {
		dont_execute = True;
		continue;
	    } else if (isabbreviation ("-nocpp", arg, 3)) {
		cpp_program = NULL;
		continue;
	    } else if (isabbreviation ("-query", arg, 2)) {
		oper = OPQUERY;
		continue;
	    } else if (isabbreviation ("-load", arg, 2)) {
		oper = OPLOAD;
		continue;
	    } else if (isabbreviation ("-merge", arg, 2)) {
		oper = OPMERGE;
		continue;
	    } else if (isabbreviation ("-override", arg, 2)) {
		oper = OPOVERRIDE;
		continue;
	    } else if (isabbreviation ("-symbols", arg, 3)) {
		oper = OPSYMBOLS;
		continue;
	    } else if (isabbreviation ("-remove", arg, 4)) {
		oper = OPREMOVE;
		continue;
	    } else if (isabbreviation ("-edit", arg, 2)) {
		if (++i >= argc) Syntax ();
		oper = OPEDIT;
		editFile = argv[i];
		continue;
	    } else if (isabbreviation ("-backup", arg, 2)) {
		if (++i >= argc) Syntax ();
		backup_suffix = argv[i];
		continue;
	    } else if (isabbreviation ("-all", arg, 2)) {
		whichResources = RALL;
		continue;
	    } else if (isabbreviation ("-global", arg, 3)) {
		whichResources = RGLOBAL;
		continue;
	    } else if (isabbreviation ("-screen", arg, 3)) {
		whichResources = RSCREEN;
		continue;
	    } else if (!strcmp ("-screens", arg)) {
		whichResources = RSCREENS;
		continue;
	    } else if (isabbreviation ("-retain", arg, 4)) {
		retainProp = 1;
		continue;
	    } else if (isabbreviation ("-quiet", arg, 2)) {
		quiet = True;
		continue;
	    } else if (arg[1] == 'I') {
		addstring(&includes, " ");
		addescapedstring(&includes, arg);
		continue;
	    } else if (arg[1] == 'U' || arg[1] == 'D') {
		if (num_cmd_defines < MAX_CMD_DEFINES) {
		    cmd_defines[num_cmd_defines++] = arg;
		} else {
		    fatal("%s: Too many -U/-D arguments\n", ProgramName);
		}
		continue;
	    }
	    Syntax ();
	} else if (arg[0] == '=') 
	    continue;
	else
	    filename = arg;
    }							/* end for */

#ifndef WIN32
    while ((i = open("/dev/null", O_RDONLY)) < 3)
	; /* make sure later freopen won't clobber things */
    (void) close(i);
#endif
    /* Open display  */
    if (!(dpy = XOpenDisplay (displayname)))
	fatal("%s: Can't open display '%s'\n", ProgramName,
		 XDisplayName (displayname));

    if (whichResources == RALL && ScreenCount(dpy) == 1)
	whichResources = RGLOBAL;

#ifdef PATHETICCPP
    if (cpp_program &&
	(oper == OPLOAD || oper == OPMERGE || oper == OPOVERRIDE)) {
	need_real_defines = True;
#ifdef WIN32
	strcpy(tmpname2, "xrdbD_XXXXXX");
	strcpy(tmpname3, "\\temp\\xrdbD_XXXXXX");
#else
#ifdef __UNIXOS2__
	{ char *tmpdir=getenv("TMP");
	  if (!tmpdir) tmpdir="/";
	  sprintf(tmpname2, "%s/xrdbD_XXXXXX",tmpdir);
	}
#else
	strcpy(tmpname2, "/tmp/xrdbD_XXXXXX");
#endif
#endif
	(void) mktemp(tmpname2);
    }
#endif

    if (!filename &&
#ifdef PATHETICCPP
	need_real_defines
#else
	(oper == OPLOAD || oper == OPMERGE || oper == OPOVERRIDE) &&
	(whichResources == RALL || whichResources == RSCREENS)
#endif
	) {
	char inputbuf[1024];
#ifdef WIN32
	strcpy(tmpname, "\\temp\\xrdb_XXXXXX");
#else
#ifdef __UNIXOS2__
	{ char *tmpdir=getenv("TMP");
	  if (!tmpdir) tmpdir="/";
	  sprintf(tmpname, "%s/xrdb_XXXXXX",tmpdir);
	}
#else
	strcpy(tmpname, "/tmp/xrdb_XXXXXX");
#endif
#endif
#ifndef HAVE_MKSTEMP
	(void) mktemp(tmpname);
	filename = tmpname;
	fp = fopen(filename, "w");
#else
	{
	int fd = mkstemp(tmpname);
	filename = tmpname;
	fp = fdopen(fd, "w");
	}
#endif /* MKSTEMP */
	if (!fp)
	    fatal("%s: Failed to open temp file: %s\n", ProgramName,
		  filename);
	while (fgets(inputbuf, sizeof(inputbuf), stdin) != NULL) 
	    fputs(inputbuf, fp);
	fclose(fp);
    }
	
    DoDisplayDefines(dpy, &defines, displayname);
    defines_base = defines.used;
    need_newline = (oper == OPQUERY || oper == OPSYMBOLS ||
		    (dont_execute && oper != OPREMOVE));
    InitBuffer(&buffer);
    if (whichResources == RGLOBAL)
	Process(DefaultScreen(dpy), False, True);
    else if (whichResources == RSCREEN)
	Process(DefaultScreen(dpy), True, True);
    else if (whichResources == RSCREENS ||
	     (oper != OPLOAD && oper != OPMERGE && oper != OPOVERRIDE)) {
	if (whichResources == RALL && oper != OPSYMBOLS) {
	    if (need_newline)
		printf("! screen-independent resources\n");
	    Process(0, False, True);
	    if (need_newline)
		printf("\n");
	}
	for (i = 0; i < ScreenCount(dpy); i++) {
	    if (need_newline) {
		if (oper == OPSYMBOLS)
		    printf("# screen %d symbols\n", i);
		else {
		    printf("! screen %d resources\n", i);
		    printf("#if SCREEN_NUM == %d\n", i);
		}
	    }
	    Process(i, True, True);
	    if (need_newline) {
		if (oper != OPSYMBOLS)
		    printf("#endif\n");
		if (i+1 != ScreenCount(dpy))
		    printf("\n");
	    }
	}
    }
    else {
	Entries *dbs;

	dbs = (Entries *)malloc(ScreenCount(dpy) * sizeof(Entries));
	for (i = 0; i < ScreenCount(dpy); i++) {
	    Process(i, True, False);
	    dbs[i] = newDB;
	}
	InitEntries(&newDB);
	if (oper == OPMERGE || oper == OPOVERRIDE)
	    GetEntriesString(&newDB, XResourceManagerString(dpy));
	ShuffleEntries(&newDB, dbs, ScreenCount(dpy));
	if (need_newline)
	    printf("! screen-independent resources\n");
	ReProcess(0, False);
	if (need_newline)
	    printf("\n");
	for (i = 0; i < ScreenCount(dpy); i++) {
	    newDB = dbs[i];
	    if (need_newline) {
		printf("! screen %d resources\n", i);
		printf("#if SCREEN_NUM == %d\n", i);
	    }
	    ReProcess(i, True);
	    if (need_newline) {
		printf("#endif\n");
		if (i+1 != ScreenCount(dpy))
		    printf("\n");
	    }
	}
    }

    if (fp)
	unlink(filename);
    if (retainProp)
	XSetCloseDownMode(dpy, RetainPermanent);
    XCloseDisplay(dpy);
    exit (0);
}


static void 
FormatEntries(Buffer *buffer, Entries *entries)
{
    register int i;

    buffer->used = 0;
    if (!entries->used)
	return;
    if (oper == OPMERGE)
	qsort(entries->entry, entries->used, sizeof(Entry),
	      CompareEntries);
    for (i = 0; i < entries->used; i++) {
	if (entries->entry[i].usable)
	    AppendEntryToBuffer(buffer, &entries->entry[i]);
    }
}

static void
StoreProperty(Display *dpy, Window root, Atom res_prop)
{
    int len = buffer.used;
    int mode = PropModeReplace;
    unsigned char *buf = (unsigned char *)buffer.buff;
    int max = (XMaxRequestSize(dpy) << 2) - 28;

    if (len > max) {
	XGrabServer(dpy);
	do {
	    XChangeProperty(dpy, root, res_prop, XA_STRING, 8, mode, buf, max);
	    buf += max;
	    len -= max;
	    mode = PropModeAppend;
	} while (len > max);
    }
    XChangeProperty(dpy, root, res_prop, XA_STRING, 8, mode, buf, len);
    if (mode != PropModeReplace)
	XUngrabServer(dpy);
}

static void
Process(int scrno, Bool doScreen, Bool execute)
{
    char *xdefs;
    Window root;
    Atom res_prop;
    FILE *input, *output;
    char *cmd;

    defines.val[defines_base] = '\0';
    defines.used = defines_base;
    buffer.used = 0;
    InitEntries(&newDB);
    DoScreenDefines(dpy, scrno, &defines);
    DoCmdDefines(&defines);
    if (doScreen) {
	xdefs = XScreenResourceString (ScreenOfDisplay(dpy, scrno));
	root = RootWindow(dpy, scrno);
	res_prop = XInternAtom(dpy, SCREEN_RESOURCES, False);
    } else {
	xdefs = XResourceManagerString (dpy);
	root = RootWindow(dpy, 0);
	res_prop = XA_RESOURCE_MANAGER;
    }
    if (oper == OPSYMBOLS) {
	printf ("%s\n", defines.val);
    } else if (oper == OPQUERY) {
	if (xdefs)
	    printf ("%s", xdefs);	/* fputs broken in SunOS 4.0 */
    } else if (oper == OPREMOVE) {
	if (xdefs)
	    XDeleteProperty(dpy, root, res_prop);
    } else if (oper == OPEDIT) {
	char template[100], old[100];

	input = fopen(editFile, "r");
	snprintf(template, sizeof(template), "%sXXXXXX", editFile);
#ifndef HAVE_MKSTEMP
	(void) mktemp(template);
	output = fopen(template, "w");
#else
	{ 
	int fd = mkstemp(template);
	output = fdopen(fd, "w");
	}
#endif
	if (!output)
	    fatal("%s: can't open temporary file '%s'\n", ProgramName, template);
	GetEntriesString(&newDB, xdefs);
	EditFile(&newDB, input, output);
	if (input)
	    fclose(input);
	fclose(output);
	snprintf(old, sizeof(old), "%s%s", editFile, backup_suffix);
	if (dont_execute) {		/* then write to standard out */
	    char buf[BUFSIZ];
	    int n;

	    output = fopen (template, "r");
	    if (output) {
		while ((n = fread (buf, 1, sizeof buf, output)) > 0) {
		    fwrite (buf, 1, n, stdout);
		}
		fclose (output);
	    }
	    unlink (template);
	} else {
	    rename (editFile, old);
	    if (rename (template, editFile))
		fatal("%s: can't rename file '%s' to '%s'\n", ProgramName,
		      template, editFile);
	}
    } else {
	if (oper == OPMERGE || oper == OPOVERRIDE)
	    GetEntriesString(&newDB, xdefs);
#ifdef PATHETICCPP
	if (need_real_defines) {
#ifdef WIN32
	    if (!(input = fopen(tmpname2, "w")))
		fatal("%s: can't open file '%s'\n", ProgramName, tmpname2);
	    fputs(defines.val, input);
	    fprintf(input, "\n#include \"%s\"\n", filename);
	    fclose(input);
	    (void) mktemp(tmpname3);
	    if (asprintf(&cmd, "%s -P%s %s > %s", cpp_program, includes.val,
			 tmpname2, tmpname3) == -1)
		fatal("%s: Out of memory\n", ProgramName);
	    if (system(cmd) < 0)
		fatal("%s: cannot run '%s'\n", ProgramName, cmd);
	    free(cmd);
	    if (!(input = fopen(tmpname3, "r")))
		fatal("%s: can't open file '%s'\n", ProgramName, tmpname3);
#else
	    if (!freopen(tmpname2, "w+", stdin))
		fatal("%s: can't open file '%s'\n", ProgramName, tmpname2);
	    fputs(defines.val, stdin);
	    fprintf(stdin, "\n#include \"%s\"\n", filename);
	    fflush(stdin);
	    fseek(stdin, 0, 0);
	    if (asprintf(&cmd, "%s -P%s", cpp_program, includes.val) == -1)
		fatal("%s: Out of memory\n", ProgramName);
	    if (!(input = popen(cmd, "r")))
		fatal("%s: cannot run '%s'\n", ProgramName, cmd);
	    free(cmd);
#endif
	} else {
#endif
	if (filename) {
	    if (!freopen (filename, "r", stdin))
		fatal("%s: can't open file '%s'\n", ProgramName, filename);
	}
	if (cpp_program) {
#ifdef WIN32
	    (void) mktemp(tmpname3);
	    if (asprintf(&cmd, "%s -P%s %s %s > %s", cpp_program,
			 includes.val, defines.val,
			 filename ? filename : "", tmpname3) == -1)
		fatal("%s: Out of memory\n", ProgramName);
	    if (system(cmd) < 0)
		fatal("%s: cannot run '%s'\n", ProgramName, cmd);
	    free(cmd);
	    if (!(input = fopen(tmpname3, "r")))
		fatal("%s: can't open file '%s'\n", ProgramName, tmpname3);
#else
	    if (asprintf(&cmd, "%s -P%s %s %s", cpp_program,
			 includes.val, defines.val,
			 filename ? filename : "") == -1)
		fatal("%s: Out of memory\n", ProgramName);
	    if (!(input = popen(cmd, "r")))
		fatal("%s: cannot run '%s'\n", ProgramName, cmd);
	    free(cmd);
#endif
	} else {
	    input = stdin;
	}
#ifdef PATHETICCPP
	}
#endif
	ReadFile(&buffer, input);
	if (cpp_program) {
#ifdef WIN32
	    fclose(input);
#else
	    pclose(input);
#endif
	}
#ifdef PATHETICCPP
	if (need_real_defines) {
	    unlink(tmpname2);
#ifdef WIN32
	    if (tmpname3[strlen(tmpname3) - 1] != 'X')
		unlink(tmpname3);
#endif
	}
#endif
	GetEntries(&newDB, &buffer, 0);
	if (execute) {
	    FormatEntries(&buffer, &newDB);
	    if (dont_execute) {
		if (buffer.used > 0) {
		    fwrite (buffer.buff, 1, buffer.used, stdout);
		    if (buffer.buff[buffer.used - 1] != '\n') putchar ('\n');
		}
	    } else if (buffer.used > 1 || !doScreen)
		StoreProperty (dpy, root, res_prop);
	    else
		XDeleteProperty (dpy, root, res_prop);
	}
    }
    if (execute)
	FreeEntries(&newDB);
    if (doScreen && xdefs)
	XFree(xdefs);
}

static void
ShuffleEntries(Entries *db, Entries *dbs, int num)
{
    int *hits;
    register int i, j, k;
    Entries cur, cmp;
    char *curtag, *curvalue;

    hits = (int *)malloc(num * sizeof(int));
    cur = dbs[0];
    for (i = 0; i < cur.used; i++) {
	curtag = cur.entry[i].tag;
	curvalue = cur.entry[i].value;
	for (j = 1; j < num; j++) {
	    cmp = dbs[j];
	    for (k = 0; k < cmp.used; k++) {
		if (cmp.entry[k].usable &&
		    !strcmp(curtag, cmp.entry[k].tag) &&
		    !strcmp(curvalue, cmp.entry[k].value))
		{
		    hits[j] = k;
		    break;
		}
	    }
	    if (k == cmp.used)
		break;
	}
	if (j == num) {
	    AddEntry(db, &cur.entry[i]);
	    hits[0] = i;
	    for (j = 0; j < num; j++)
		dbs[j].entry[hits[j]].usable = False;
	}
    }
    free((char *)hits);
}

static void
ReProcess(int scrno, Bool doScreen)
{
    Window root;
    Atom res_prop;

    FormatEntries(&buffer, &newDB);
    if (doScreen) {
	root = RootWindow(dpy, scrno);
	res_prop = XInternAtom(dpy, SCREEN_RESOURCES, False);
    } else {
	root = RootWindow(dpy, 0);
	res_prop = XA_RESOURCE_MANAGER;
    }
    if (dont_execute) {
	if (buffer.used > 0) {
	    fwrite (buffer.buff, 1, buffer.used, stdout);
	    if (buffer.buff[buffer.used - 1] != '\n') putchar ('\n');
	}
    } else {
	if (buffer.used > 1 || !doScreen)
	    StoreProperty (dpy, root, res_prop);
	else
	    XDeleteProperty (dpy, root, res_prop);
    }
    FreeEntries(&newDB);
}

static void
fatal(char *msg, ...)
{
    va_list args;

    if (errno != 0)
	perror(ProgramName);
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
    exit(1);
}
