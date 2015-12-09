/*

Copyright (c) 1991, 1998 The Open Group

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

/* Constructs string definitions */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct _TableEnt {
    struct _TableEnt* next;
    char* left;
    char* right;
    int offset;
} TableEnt;

typedef struct _Table {
    struct _Table* next;
    TableEnt* tableent;
    TableEnt* tableentcurrent;
    TableEnt** tableenttail;
    char* name;
    int offset;
} Table;

typedef struct _File {
    struct _File* next;
    FILE* tmpl;
    char* name;
    Table* table;
    Table* tablecurrent;
    Table** tabletail;
} File;

static File* file = NULL;
static File* filecurrent = NULL;
static File** filetail = &file;
static char* conststr;
static char* prefixstr = NULL;
static char* featurestr = NULL;
static char* ctmplstr = NULL;
static char* fileprotstr;
static char* externrefstr;
static char* externdefstr;

#ifndef FALSE
# define FALSE 0
# define TRUE  !(FALSE)
#endif

static int   solaris_abi_names = FALSE;

#define X_DEFAULT_ABI	0
#define X_ARRAYPER_ABI	1
#define X_INTEL_ABI	2
#define X_INTEL_ABI_BC	3
#define X_SPARC_ABI	4
#define X_FUNCTION_ABI	5

#define X_MAGIC_STRING "<<<STRING_TABLE_GOES_HERE>>>"

/* Wrapper for fopen()
 * Prepend filename with an includedir which can be specified on the
 * commandline. Needed to separate source and build directories.
 */
static char* includedir = NULL;
static FILE *ifopen(const char *file, const char *mode)
{
#ifndef HAVE_ASPRINTF
    size_t len;
#endif
    char *buffer;
    FILE *ret;

    if (includedir == NULL)
        return fopen(file, mode);

#ifdef HAVE_ASPRINTF
    if (asprintf(&buffer, "%s/%s", includedir, file) == -1)
        return NULL;
#else
    len = strlen(file) + strlen(includedir) + 1;
    buffer = (char*)malloc(len + 1);
    if (buffer == NULL)
        return NULL;

    snprintf(buffer, len + 1, "%s/%s", includedir, file);
#endif

    ret = fopen(buffer, mode);

    free(buffer);
    return ret;
}

static void WriteHeaderProlog (FILE *f, File *phile)
{
    Table* t;
    TableEnt* te;

    (void) fprintf (f, "#ifdef %s\n", featurestr);
    for (t = phile->table; t; t = t->next)
	for (te = t->tableent; te; te = te->next) {
	    if (strcmp (te->left, "RAtom") == 0) {
		(void) fprintf (f,
			"#ifndef %s%s\n#define %s%s \"%s\"\n#endif\n",
			prefixstr, te->left, prefixstr, te->left, te->right);
	    } else {
		(void) fprintf (f,
			"#define %s%s \"%s\"\n",
			prefixstr, te->left, te->right);
	    }
	}
    (void) fprintf (f, "%s", "#else\n");
}

static void IntelABIWriteHeader (FILE *f, File *phile)
{
    Table* t;
    TableEnt* te;

    WriteHeaderProlog (f, phile);

    for (t = phile->table; t; t = t->next) {
      (void) fprintf (f, "%s %sConst char %s[];\n",
		      externrefstr, conststr ? conststr : fileprotstr, t->name);
	for (te = t->tableent; te; te = te->next)
	    (void) fprintf (f,
		"#ifndef %s%s\n#define %s%s ((char*)&%s[%d])\n#endif\n",
		prefixstr, te->left, prefixstr, te->left, t->name, te->offset);
    }

    (void) fprintf (f, "#endif /* %s */\n", featurestr);
}

static void SPARCABIWriteHeader (FILE *f, File *phile)
{
    Table* t;
    TableEnt* te;

    for (t = phile->table; t; t = t->next)
	for (te = t->tableent; te; te = te->next)
	    (void) fprintf (f, "#define %s%s \"%s\"\n",
			    prefixstr, te->left, te->right);
}

static void FunctionWriteHeader (FILE *f, File *phile)
{
    Table* t;
    TableEnt* te;

    WriteHeaderProlog (f, phile);

    (void) fprintf (f, "%s %sConst char* %s();\n",
		    externrefstr, conststr ? conststr : fileprotstr,
		    phile->table->name);

    for (t = phile->table; t; t = t->next)
	for (te = t->tableent; te; te = te->next)
	    (void) fprintf (f,
		"#ifndef %s%s\n#define %s%s (%s(%d))\n#endif\n",
		prefixstr, te->left, prefixstr, te->left, phile->table->name,
		te->offset);

    (void) fprintf (f, "#endif /* %s */\n", featurestr);
}

static void ArrayperWriteHeader (FILE *f, File *phile)
{
    Table* t;
    TableEnt* te;

    WriteHeaderProlog (f, phile);

    for (t = phile->table; t; t = t->next)
        for (te = t->tableent; te; te = te->next)
	    (void) fprintf (f,
			    "#ifndef %s%s\n%s %sConst char %s%s[];\n#endif\n",
			    prefixstr, te->left,
			    externrefstr, conststr ? conststr : fileprotstr,
			    prefixstr, te->left);

    (void) fprintf (f, "#endif /* %s */\n", featurestr);
}

static void DefaultWriteHeader (FILE *f, File *phile)
{
    Table* t;
    TableEnt* te;

    WriteHeaderProlog (f, phile);

    (void) fprintf (f, "%s %sConst char %s[];\n",
		    externrefstr, conststr ? conststr : fileprotstr,
		    phile->table->name);

    for (t = phile->table; t; t = t->next)
	for (te = t->tableent; te; te = te->next)
	    (void) fprintf (f,
		"#ifndef %s%s\n#define %s%s ((char*)&%s[%d])\n#endif\n",
		prefixstr, te->left, prefixstr, te->left, phile->table->name,
		te->offset);

    (void) fprintf (f, "#endif /* %s */\n", featurestr);
}

static void CopyTmplProlog (FILE *tmpl, FILE *f)
{
    char buf[1024];
    static const char* magic_string = X_MAGIC_STRING;
    int magic_string_len = strlen (magic_string);

    while (fgets (buf, sizeof buf, tmpl)) {
	if (strncmp (buf, magic_string, magic_string_len) == 0) {
	    return;
	}
	(void) fputs (buf, f);
    }
}

static void CopyTmplEpilog (FILE *tmpl, FILE *f)
{
    char buf[1024];

    while (fgets (buf, sizeof buf, tmpl))
	(void) fputs (buf, f);
}

static const char* abistring[] = {
    "Default", "Array per string", "Intel", "Intel BC", "SPARC", "Function" };

static void WriteHeader (char *tagline, File *phile, int abi)
{
    FILE* f;
    char* tmp;
    static void (*headerproc[])(FILE *f, File *phile) = {
	DefaultWriteHeader, ArrayperWriteHeader,
	IntelABIWriteHeader, IntelABIWriteHeader,
	SPARCABIWriteHeader, FunctionWriteHeader };

    if ((f = fopen (phile->name, "w+")) == NULL) exit (1);

    if (phile->tmpl) CopyTmplProlog (phile->tmpl, f);

    (void) fprintf (f,
	"%s\n%s\n/* %s ABI version -- Do not edit */\n",
	"/* $Xorg: makestrs.c,v 1.6 2001/02/09 02:03:17 xorgcvs Exp $ */",
	"/* This file is automatically generated. */",
	abistring[abi]);

    if (tagline) (void) fprintf (f, "/* %s */\n\n", tagline);

    /* do the right thing for Motif, i.e. avoid _XmXmStrDefs_h_ */
    if (strcmp (prefixstr, "Xm") == 0) {
#ifdef HAVE_ASPRINTF
	if (asprintf (&fileprotstr, "_%s_", phile->name) == -1)
	    exit (1);
#else
	if ((fileprotstr = malloc (strlen (phile->name) + 3)) == NULL)
	   exit (1);
	(void) sprintf (fileprotstr, "_%s_", phile->name);
#endif
    } else {
#ifdef HAVE_ASPRINTF
	if (asprintf (&fileprotstr, "_%s%s_", prefixstr, phile->name) == -1)
	    exit (1);
#else
	if ((fileprotstr = malloc (strlen (phile->name) + strlen (prefixstr) +  3)) == NULL)
	   exit (1);
	(void) sprintf (fileprotstr, "_%s%s_", prefixstr, phile->name);
#endif
    }

    for (tmp = fileprotstr; *tmp; tmp++) if (*tmp == '.') *tmp = '_';

    (*headerproc[abi])(f, phile);

    if (phile->tmpl) CopyTmplEpilog (phile->tmpl, f);

    (void) free (fileprotstr);
    (void) fclose (phile->tmpl);
    (void) fclose (f);
}

static void WriteSourceLine (TableEnt *te, int abi, int fudge)
{
    char* c;

    for (c = te->right; *c; c++) (void) printf ("'%c',", *c);
    (void) printf ("%c", '0');
    if (te->next || fudge) (void) printf ("%c", ',');
    (void) printf ("%s", "\n");
}

static const char* const_string = "%s %sConst char %s[] = {\n";

static void IntelABIWriteSource (int abi)
{
    File* phile;

    for (phile = file; phile; phile = phile->next) {
	Table* t;
	TableEnt* te;

	for (t = phile->table; t; t = t->next) {
	    (void) printf (const_string, externdefstr,
			   conststr ? conststr : "", t->name);
	    for (te = t->tableent; te; te = te->next)
		WriteSourceLine (te, abi, 0);
	    (void) printf ("%s\n\n", "};");
	}
    }
}

static void IntelABIBCWriteSource (int abi)
{
    File* phile;

    for (phile = file; phile; phile = phile->next) {
	Table* t;
	TableEnt* te;

	(void) printf (const_string, externdefstr,
		       conststr ? conststr : "", phile->table->name);

	for (t = phile->table; t; t = t->next)
	    for (te = t->tableent; te; te = te->next)
		WriteSourceLine (te, abi, t->next ? 1 : 0);
	(void) printf ("%s\n\n", "};");

	if (phile->table->next) {
	    (void) printf (const_string, externdefstr,
			   conststr ? conststr : "", phile->table->next->name);
	    for (t = phile->table->next; t; t = t->next)
		for (te = t->tableent; te; te = te->next)
		    WriteSourceLine (te, abi, 0);
	    (void) printf ("%s\n\n", "};");
	}
    }
}

static void FunctionWriteSource (int abi)
{
    File* phile;

    for (phile = file; phile; phile = phile->next) {
	Table* t;
	TableEnt* te;

	(void) printf ("static %sConst char _%s[] = {\n",
		       conststr ? conststr : "", phile->table->name);

	for (t = phile->table; t; t = t->next)
	    for (te = t->tableent; te; te = te->next)
		WriteSourceLine (te, abi, t->next ? 1 : 0);
	(void) printf ("%s\n\n", "};");

	(void) printf ("%sConst char* %s(index)\n    int index;\n{\n    return &_%s[index];\n}\n\n",
		       conststr ? conststr : "",
		       phile->table->name, phile->table->name);
    }
}

static void ArrayperWriteSource (int abi)
{
    File* phile;
    static int done_atom;

    for (phile = file; phile; phile = phile->next) {
	Table* t;
	TableEnt* te;

	for (t = phile->table; t; t = t->next)
	    for (te = t->tableent; te; te = te->next) {
		if (strcmp (te->left, "RAtom") == 0) {
		    if (done_atom) return;
		    done_atom = 1;
		}
		(void) printf ("%s %sConst char %s%s[] = \"%s\";\n",
			       externdefstr, conststr ? conststr : "",
			       prefixstr,
			       te->left, te->right);
	    }
    }
}

static void DefaultWriteSource (int abi)
{
    File* phile;

    for (phile = file; phile; phile = phile->next) {
	Table* t;
	TableEnt* te;

	(void) printf (const_string, externdefstr, conststr ? conststr : "",
		       phile->table->name);

	for (t = phile->table; t; t = t->next)
	    for (te = t->tableent; te; te = te->next)
		WriteSourceLine (te, abi, t->next ? 1 : 0);
	(void) printf ("%s\n\n", "};");
    }
}

static void WriteSource(char *tagline, int abi)
{
    static void (*sourceproc[])(int) = {
	DefaultWriteSource, ArrayperWriteSource,
	IntelABIWriteSource, IntelABIBCWriteSource,
	DefaultWriteSource, FunctionWriteSource };

    FILE* tmpl;

    if (ctmplstr) {
	tmpl = ifopen (ctmplstr, "r");

	if (tmpl) CopyTmplProlog (tmpl, stdout);
	else {
	    (void) fprintf (stderr, "Expected template %s, not found\n",
			    ctmplstr);
	    exit (1);
	}
    } else
	tmpl = NULL;


    (void) printf ("%s\n%s\n/* %s ABI version -- Do not edit */\n",
		   "/* $Xorg: makestrs.c,v 1.6 2001/02/09 02:03:17 xorgcvs Exp $ */",
		   "/* This file is automatically generated. */",
		   abistring[abi]);

    if (tagline) (void) printf ("/* %s */\n\n", tagline);

    (*sourceproc[abi])(abi);

    if (tmpl) CopyTmplEpilog (tmpl, stdout);
}

static void DoLine(char *buf)
{
#define X_NO_TOKEN 0
#define X_FILE_TOKEN 1
#define X_TABLE_TOKEN 2
#define X_PREFIX_TOKEN 3
#define X_FEATURE_TOKEN 4
#define X_EXTERNREF_TOKEN 5
#define X_EXTERNDEF_TOKEN 6
#define X_CTMPL_TOKEN 7
#define X_HTMPL_TOKEN 8
#define X_CONST_TOKEN 9

    int token;
    char lbuf[1024];
    static const char* file_str = "#file";
    static const char* table_str = "#table";
    static const char* prefix_str = "#prefix";
    static const char* feature_str = "#feature";
    static const char* externref_str = "#externref";
    static const char* externdef_str = "#externdef";
    static const char* ctmpl_str = "#ctmpl";
    static const char* htmpl_str = "#htmpl";
    static const char* const_str = "#const";

    if (strncmp (buf, file_str, strlen (file_str)) == 0)
	token = X_FILE_TOKEN;
    else if (strncmp (buf, table_str, strlen (table_str)) == 0)
	token = X_TABLE_TOKEN;
    else if (strncmp (buf, prefix_str, strlen (prefix_str)) == 0)
	token = X_PREFIX_TOKEN;
    else if (strncmp (buf, feature_str, strlen (feature_str)) == 0)
	token = X_FEATURE_TOKEN;
    else if (strncmp (buf, externref_str, strlen (externref_str)) == 0)
	token = X_EXTERNREF_TOKEN;
    else if (strncmp (buf, externdef_str, strlen (externdef_str)) == 0)
	token = X_EXTERNDEF_TOKEN;
    else if (strncmp (buf, ctmpl_str, strlen (ctmpl_str)) == 0)
	token = X_CTMPL_TOKEN;
    else if (strncmp (buf, htmpl_str, strlen (htmpl_str)) == 0)
	token = X_HTMPL_TOKEN;
    else if (strncmp (buf, const_str, strlen (const_str)) == 0)
	token = X_CONST_TOKEN;
    else
        token = X_NO_TOKEN;

    switch (token) {
    case X_FILE_TOKEN:
	{
	    File* phile;

	    if ((phile = (File*) malloc (sizeof(File))) == NULL)
		exit(1);
	    if ((phile->name = strdup (buf + strlen (file_str) + 1)) == NULL)
		exit(1);
	    phile->table = NULL;
	    phile->tablecurrent = NULL;
	    phile->tabletail = &phile->table;
	    phile->next = NULL;
	    phile->tmpl = NULL;

	    *filetail = phile;
	    filetail = &phile->next;
	    filecurrent = phile;
	}
	break;
    case X_TABLE_TOKEN:
	{
	    Table* table;
	    if ((table = (Table*) malloc (sizeof(Table))) == NULL)
		exit(1);
	    if ((table->name = strdup (buf + strlen (table_str) + 1)) == NULL)
		exit(1);
	    if (solaris_abi_names) {
		if (strcmp(table->name, "XtStringsR6") == 0) {
		    strcpy(table->name, "XtR6Strings");
		} else if (strcmp(table->name, "XtShellStringsR6") == 0) {
		    strcpy(table->name, "XtR6ShellStrings");
		}
	    }
	    table->tableent = NULL;
	    table->tableentcurrent = NULL;
	    table->tableenttail = &table->tableent;
	    table->next = NULL;
	    table->offset = 0;

	    *filecurrent->tabletail = table;
	    filecurrent->tabletail = &table->next;
	    filecurrent->tablecurrent = table;
	}
	break;
    case X_PREFIX_TOKEN:
	if ((prefixstr = strdup (buf + strlen (prefix_str) + 1)) == NULL)
	    exit(1);
	break;
    case X_FEATURE_TOKEN:
	if ((featurestr = strdup (buf + strlen (feature_str) + 1)) == NULL)
	    exit(1);
	break;
    case X_EXTERNREF_TOKEN:
	if ((externrefstr = strdup (buf + strlen (externref_str) + 1)) == NULL)
	    exit(1);
	break;
    case X_EXTERNDEF_TOKEN:
	if ((externdefstr = strdup (buf + strlen (externdef_str) + 1)) == NULL)
	    exit(1);
	break;
    case X_CTMPL_TOKEN:
	if ((ctmplstr = strdup (buf + strlen (ctmpl_str) + 1)) == NULL)
	    exit(1);
	break;
    case X_HTMPL_TOKEN:
	if ((filecurrent->tmpl = ifopen (buf + strlen (htmpl_str) + 1, "r")) == NULL) {
	    (void) fprintf (stderr,
			    "Expected template %s, not found\n", htmpl_str);
	    exit (1);
	}
	break;
    case X_CONST_TOKEN:
	if ((conststr = strdup (buf + strlen (const_str) + 1)) == NULL)
	    exit(1);
	break;
    default:
	{
	    char* right;
	    TableEnt* tableent;
	    int llen;
	    int rlen;
	    int len;

	    if ((right = strchr(buf, ' ')))
		*right++ = 0;
	    else
		right = buf + 1;
	    if (buf[0] == 'H') {
		snprintf (lbuf, sizeof(lbuf), "%s%s", prefixstr, right);
		right = lbuf;
	    }

	    llen = len = strlen(buf) + 1;
	    rlen = strlen(right) + 1;
	    if (right != buf + 1) len += rlen;
	    if ((tableent = (TableEnt*)malloc(sizeof(TableEnt) + len)) == NULL)
		exit(1);
	    tableent->left = (char *)(tableent + 1);
	    strcpy(tableent->left, buf);
	    if (llen != len) {
		tableent->right = tableent->left + llen;
		strcpy(tableent->right, right);
	    } else {
		tableent->right = tableent->left + 1;
	    }
	    tableent->next = NULL;

	    *filecurrent->tablecurrent->tableenttail = tableent;
	    filecurrent->tablecurrent->tableenttail = &tableent->next;
	    filecurrent->tablecurrent->tableentcurrent = tableent;
	}
	break;
    }
}

static void IntelABIIndexEntries (File *file)
{
    Table* t;
    TableEnt* te;

    for (t = file->table; t; t = t->next)
	for (te = t->tableent; te; te = te->next) {
	    te->offset = t->offset;
	    t->offset += strlen (te->right);
	    t->offset++;
    }
}

static void DefaultIndexEntries (File *file)
{
    Table* t;
    TableEnt* te;
    int offset = 0;

    for (t = file->table; t; t = t->next)
	for (te = t->tableent; te; te = te->next) {
	    te->offset = offset;
	    offset += strlen (te->right);
	    offset++;
    }
}

static void IndexEntries (File *file, int abi)
{
    switch (abi) {
    case X_SPARC_ABI:
	break;
    case X_INTEL_ABI:
    case X_INTEL_ABI_BC:
	IntelABIIndexEntries (file);
	break;
    default:
	DefaultIndexEntries (file);
	break;
    }
}

static char* DoComment (char *line)
{
    char* tag;
    char* eol;
    char* ret;
    int len;

    /* assume that the first line with two '$' in it is the RCS tag line */
    if ((tag = strchr (line, '$')) == NULL) return NULL;
    if ((eol = strchr (tag + 1, '$')) == NULL) return NULL;
    len = eol - tag;
    if ((ret = malloc (len)) == NULL)
	exit (1);
    (void) strncpy (ret, tag + 1, len - 1);
    ret[len - 2] = 0;
    return ret;
}

int main(int argc, char *argv[])
{
    int len, i;
    char* tagline = NULL;
    File* phile;
    FILE *f;
    char buf[1024];
    int abi =
#ifndef ARRAYPERSTR
	X_DEFAULT_ABI;
#else
	X_ARRAYPER_ABI;
#endif

    f = stdin;
    if (argc > 1) {
	for (i = 1; i < argc; i++) {
	    if (strcmp (argv[i], "-f") == 0) {
		if (++i < argc)
		    f = fopen (argv[i], "r");
		else
		    return 1;
	    }
	    if (strcmp (argv[i], "-i") == 0) {
		if (++i < argc)
		    includedir = argv[i];
		else
		    return 1;
	    }
	    if (strcmp (argv[i], "-sparcabi") == 0)
		abi = X_SPARC_ABI;
	    if (strcmp (argv[i], "-intelabi") == 0)
		abi = X_INTEL_ABI;
	    if (strcmp (argv[i], "-functionabi") == 0)
		abi = X_FUNCTION_ABI;
	    if (strcmp (argv[i], "-earlyR6bc") == 0 && abi == X_INTEL_ABI)
		abi = X_INTEL_ABI_BC;
	    if (strcmp (argv[i], "-arrayperabi") == 0)
		abi = X_ARRAYPER_ABI;
#ifdef ARRAYPERSTR
	    if (strcmp (argv[i], "-defaultabi") == 0)
		abi = X_DEFAULT_ABI;
#endif
	    if (strcmp (argv[i], "-solarisabinames") == 0)
		solaris_abi_names = TRUE;
	}
    }

    if (f == NULL) return 1;
    while (fgets(buf, sizeof buf, f)) {
	if (!buf[0] || buf[0] == '\n')
	    continue;
	if (buf[0] == '!') {
	    if (tagline) continue;
	    tagline = DoComment (buf);
	    continue;
	}
	if (buf[(len = strlen (buf) - 1)] == '\n') buf[len] = '\0';
	DoLine(buf);
    }
    for (phile = file; phile; phile = phile->next) {
	if (abi != X_ARRAYPER_ABI) IndexEntries (phile, abi);
	WriteHeader (tagline, phile, abi);
    }
    WriteSource(tagline, abi);
    return 0;
}

