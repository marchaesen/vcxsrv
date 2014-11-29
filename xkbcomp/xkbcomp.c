/************************************************************
 Copyright (c) 1994 by Silicon Graphics Computer Systems, Inc.

 Permission to use, copy, modify, and distribute this
 software and its documentation for any purpose and without
 fee is hereby granted, provided that the above copyright
 notice appear in all copies and that both that copyright
 notice and this permission notice appear in supporting
 documentation, and that the name of Silicon Graphics not be 
 used in advertising or publicity pertaining to distribution 
 of the software without specific prior written permission.
 Silicon Graphics makes no representation about the suitability 
 of this software for any purpose. It is provided "as is"
 without any express or implied warranty.
 
 SILICON GRAPHICS DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS 
 SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY 
 AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL SILICON
 GRAPHICS BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL 
 DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, 
 DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE 
 OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION  WITH
 THE USE OR PERFORMANCE OF THIS SOFTWARE.

 ********************************************************/

#include <stdio.h>
#include <ctype.h>
#include <X11/keysym.h>

/* for symlink attack security fix -- Branden Robinson */
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
/* end BR */

#if defined(sgi)
#include <malloc.h>
#endif

#define	DEBUG_VAR debugFlags
#include "xkbcomp.h"
#include <stdlib.h>
#include "xkbpath.h"
#include "parseutils.h"
#include "misc.h"
#include "tokens.h"
#include <X11/extensions/XKBgeom.h>


#ifdef WIN32
#define S_IRGRP 0
#define S_IWGRP 0
#define S_IROTH 0
#define S_IWOTH 0
#endif

#define	lowbit(x)	((x) & (-(x)))

/***====================================================================***/

#define	WANT_DEFAULT	0
#define	WANT_XKM_FILE	1
#define	WANT_C_HDR	2
#define	WANT_XKB_FILE	3
#define	WANT_X_SERVER	4
#define	WANT_LISTING	5

#define	INPUT_UNKNOWN	0
#define	INPUT_XKB	1
#define	INPUT_XKM	2

unsigned int debugFlags;

static const char *fileTypeExt[] = {
    "XXX",
    "xkm",
    "h",
    "xkb",
    "dir"
};

static unsigned inputFormat, outputFormat;
char *rootDir;
static char *inputFile;
static char *inputMap;
static char *outputFile;
static char *inDpyName;
static char *outDpyName;
static Display *inDpy;
static Display *outDpy;
static Bool showImplicit = False;
static Bool synch = False;
static Bool computeDflts = False;
static Bool xkblist = False;
unsigned warningLevel = 5;
unsigned verboseLevel = 0;
unsigned dirsToStrip = 0;
unsigned optionalParts = 0;
static char *preErrorMsg = NULL;
static char *postErrorMsg = NULL;
static char *errorPrefix = NULL;
static unsigned int device_id = XkbUseCoreKbd;

/***====================================================================***/

#define	M(m)	fprintf(stderr,(m))
#define	M1(m,a)	fprintf(stderr,(m),(a))

static void
Usage(int argc, char *argv[])
{
    if (!xkblist)
        M1("Usage: %s [options] input-file [ output-file ]\n", argv[0]);
    else
        M1("Usage: %s [options] file[(map)] ...\n", argv[0]);
    M("Legal options:\n");
    M("-?,-help             Print this message\n");
    M("-version             Print the version number\n");
    if (!xkblist)
    {
        M("-a                   Show all actions\n");
        M("-C                   Create a C header file\n");
    }
#ifdef DEBUG
    M("-d [flags]           Report debugging information\n");
#endif
    M("-em1 <msg>           Print <msg> before printing first error message\n");
    M("-emp <msg>           Print <msg> at the start of each message line\n");
    M("-eml <msg>           If there were any errors, print <msg> before exiting\n");
    if (!xkblist)
    {
        M("-dflts               Compute defaults for missing parts\n");
        M("-I[<dir>]            Specifies a top level directory for include\n");
        M("                     directives. Multiple directories are legal.\n");
        M("-l [flags]           List matching maps in the specified files\n");
        M("                     f: list fully specified names\n");
        M("                     h: also list hidden maps\n");
        M("                     l: long listing (show flags)\n");
        M("                     p: also list partial maps\n");
        M("                     R: recursively list subdirectories\n");
        M("                     default is all options off\n");
    }
    M("-i <deviceid>        Specifies device ID (not name) to compile for\n");
    M("-m[ap] <map>         Specifies map to compile\n");
    M("-o <file>            Specifies output file name\n");
    if (!xkblist)
    {
        M("-opt[ional] <parts>  Specifies optional components of keymap\n");
        M("                     Errors in optional parts are not fatal\n");
        M("                     <parts> can be any combination of:\n");
        M("                     c: compat map         g: geometry\n");
        M("                     k: keycodes           s: symbols\n");
        M("                     t: types\n");
    }
    if (xkblist)
    {
        M("-p <count>           Specifies the number of slashes to be stripped\n");
        M("                     from the front of the map name on output\n");
    }
    M("-R[<DIR>]            Specifies the root directory for\n");
    M("                     relative path names\n");
    M("-synch               Force synchronization\n");
    if (xkblist)
    {
        M("-v [<flags>]         Set level of detail for listing.\n");
        M("                     flags are as for the -l option\n");
    }
    M("-w [<lvl>]           Set warning level (0=none, 10=all)\n");
    if (!xkblist)
    {
        M("-xkb                 Create an XKB source (.xkb) file\n");
        M("-xkm                 Create a compiled key map (.xkm) file\n");
    }
    return;
}

/***====================================================================***/

static void
setVerboseFlags(char *str)
{
    for (; *str; str++)
    {
        switch (*str)
        {
        case 'f':
            verboseLevel |= WantFullNames;
            break;
        case 'h':
            verboseLevel |= WantHiddenMaps;
            break;
        case 'l':
            verboseLevel |= WantLongListing;
            break;
        case 'p':
            verboseLevel |= WantPartialMaps;
            break;
        case 'R':
            verboseLevel |= ListRecursive;
            break;
        default:
            if (warningLevel > 4)
            {
                WARN1("Unknown verbose option \"%c\"\n", (unsigned int) *str);
                ACTION("Ignored\n");
            }
            break;
        }
    }
    return;
}

static Bool
parseArgs(int argc, char *argv[])
{
    register int i, tmp;

    i = strlen(argv[0]);
    tmp = strlen("xkblist");
    if ((i >= tmp) && (strcmp(&argv[0][i - tmp], "xkblist") == 0))
    {
        xkblist = True;
    }
    for (i = 1; i < argc; i++)
    {
        int itmp;
        if ((argv[i][0] != '-') || (uStringEqual(argv[i], "-")))
        {
            if (!xkblist)
            {
                if (inputFile == NULL)
                    inputFile = argv[i];
                else if (outputFile == NULL)
                    outputFile = argv[i];
                else if (warningLevel > 0)
                {
                    WARN("Too many file names on command line\n");
                    ACTION3
                        ("Compiling %s, writing to %s, ignoring %s\n",
                         inputFile, outputFile, argv[i]);
                }
            }
            else if (!AddMatchingFiles(argv[i]))
                return False;
        }
        else if ((strcmp(argv[i], "-?") == 0)
                 || (strcmp(argv[i], "-help") == 0))
        {
            Usage(argc, argv);
            exit(0);
        }
        else if (strcmp(argv[i], "-version") == 0)
        {
            printf("xkbcomp %s\n", PACKAGE_VERSION);
            exit(0);
        } else if ((strcmp(argv[i], "-a") == 0) && (!xkblist))
        {
            showImplicit = True;
        }
        else if ((strcmp(argv[i], "-C") == 0) && (!xkblist))
        {
            if ((outputFormat != WANT_DEFAULT)
                && (outputFormat != WANT_C_HDR))
            {
                if (warningLevel > 0)
                {
                    WARN("Multiple output file formats specified\n");
                    ACTION1("\"%s\" flag ignored\n", argv[i]);
                }
            }
            else
                outputFormat = WANT_C_HDR;
        }
#ifdef DEBUG
        else if (strcmp(argv[i], "-d") == 0)
        {
            if ((i >= (argc - 1)) || (!isdigit(argv[i + 1][0])))
            {
                debugFlags = 1;
            }
            else
            {
                if (sscanf(argv[++i], "%i", &itmp) == 1)
                    debugFlags = itmp;
            }
            INFO1("Setting debug flags to %d\n", debugFlags);
        }
#endif
        else if ((strcmp(argv[i], "-dflts") == 0) && (!xkblist))
        {
            computeDflts = True;
        }
        else if (strcmp(argv[i], "-em1") == 0)
        {
            if (++i >= argc)
            {
                if (warningLevel > 0)
                {
                    WARN("No pre-error message specified\n");
                    ACTION("Trailing \"-em1\" option ignored\n");
                }
            }
            else if (preErrorMsg != NULL)
            {
                if (warningLevel > 0)
                {
                    WARN("Multiple pre-error messsages specified\n");
                    ACTION2("Compiling %s, ignoring %s\n",
                            preErrorMsg, argv[i]);
                }
            }
            else
                preErrorMsg = argv[i];
        }
        else if (strcmp(argv[i], "-emp") == 0)
        {
            if (++i >= argc)
            {
                if (warningLevel > 0)
                {
                    WARN("No error prefix specified\n");
                    ACTION("Trailing \"-emp\" option ignored\n");
                }
            }
            else if (errorPrefix != NULL)
            {
                if (warningLevel > 0)
                {
                    WARN("Multiple error prefixes specified\n");
                    ACTION2("Compiling %s, ignoring %s\n",
                            errorPrefix, argv[i]);
                }
            }
            else
                errorPrefix = argv[i];
        }
        else if (strcmp(argv[i], "-eml") == 0)
        {
            if (++i >= argc)
            {
                if (warningLevel > 0)
                {
                    WARN("No post-error message specified\n");
                    ACTION("Trailing \"-eml\" option ignored\n");
                }
            }
            else if (postErrorMsg != NULL)
            {
                if (warningLevel > 0)
                {
                    WARN("Multiple post-error messages specified\n");
                    ACTION2("Compiling %s, ignoring %s\n",
                            postErrorMsg, argv[i]);
                }
            }
            else
                postErrorMsg = argv[i];
        }
        else if ((strncmp(argv[i], "-I", 2) == 0) && (!xkblist))
        {
            if (!XkbAddDirectoryToPath(&argv[i][2]))
            {
                ACTION("Exiting\n");
                exit(1);
            }
        }
        else if ((strncmp(argv[i], "-i", 2) == 0) && (!xkblist))
        {
            if (++i >= argc)
            {
                if (warningLevel > 0)
                    WARN("No device ID specified\n");
            }
            device_id = atoi(argv[i]);
        }
        else if ((strncmp(argv[i], "-l", 2) == 0) && (!xkblist))
        {
            if (outputFormat != WANT_DEFAULT)
            {
                if (warningLevel > 0)
                {
                    WARN("Multiple output file formats specified\n");
                    ACTION1("\"%s\" flag ignored\n", argv[i]);
                }
            }
            else
            {
                if (argv[i][2] != '\0')
                    setVerboseFlags(&argv[i][2]);
                xkblist = True;
                if ((inputFile) && (!AddMatchingFiles(inputFile)))
                    return False;
                else
                    inputFile = NULL;
                if ((outputFile) && (!AddMatchingFiles(outputFile)))
                    return False;
                else
                    outputFile = NULL;
            }
        }
        else if ((strcmp(argv[i], "-m") == 0)
                 || (strcmp(argv[i], "-map") == 0))
        {
            if (++i >= argc)
            {
                if (warningLevel > 0)
                {
                    WARN("No map name specified\n");
                    ACTION1("Trailing \"%s\" option ignored\n", argv[i - 1]);
                }
            }
            else if (xkblist)
            {
                if (!AddMapOnly(argv[i]))
                    return False;
            }
            else if (inputMap != NULL)
            {
                if (warningLevel > 0)
                {
                    WARN("Multiple map names specified\n");
                    ACTION2("Compiling %s, ignoring %s\n", inputMap, argv[i]);
                }
            }
            else
                inputMap = argv[i];
        }
        else if ((strcmp(argv[i], "-merge") == 0) && (!xkblist))
        {
            /* Ignored */
        }
        else if (strcmp(argv[i], "-o") == 0)
        {
            if (++i >= argc)
            {
                if (warningLevel > 0)
                {
                    WARN("No output file specified\n");
                    ACTION("Trailing \"-o\" option ignored\n");
                }
            }
            else if (outputFile != NULL)
            {
                if (warningLevel > 0)
                {
                    WARN("Multiple output files specified\n");
                    ACTION2("Compiling %s, ignoring %s\n", outputFile,
                            argv[i]);
                }
            }
            else
                outputFile = argv[i];
        }
        else if (((strcmp(argv[i], "-opt") == 0)
                  || (strcmp(argv[i], "optional") == 0)) && (!xkblist))
        {
            if (++i >= argc)
            {
                if (warningLevel > 0)
                {
                    WARN("No optional components specified\n");
                    ACTION1("Trailing \"%s\" option ignored\n", argv[i - 1]);
                }
            }
            else
            {
                char *tmp2;
                for (tmp2 = argv[i]; (*tmp2 != '\0'); tmp2++)
                {
                    switch (*tmp2)
                    {
                    case 'c':
                    case 'C':
                        optionalParts |= XkmCompatMapMask;
                        break;
                    case 'g':
                    case 'G':
                        optionalParts |= XkmGeometryMask;
                        break;
                    case 'k':
                    case 'K':
                        optionalParts |= XkmKeyNamesMask;
                        break;
                    case 's':
                    case 'S':
                        optionalParts |= XkmSymbolsMask;
                        break;
                    case 't':
                    case 'T':
                        optionalParts |= XkmTypesMask;
                        break;
                    default:
                        if (warningLevel > 0)
                        {
                            WARN1
                                ("Illegal component for %s option\n",
                                 argv[i - 1]);
                            ACTION1
                                ("Ignoring unknown specifier \"%c\"\n",
                                 (unsigned int) *tmp2);
                        }
                        break;
                    }
                }
            }
        }
        else if (strncmp(argv[i], "-p", 2) == 0)
        {
            if (isdigit(argv[i][2]))
            {
                if (sscanf(&argv[i][2], "%i", &itmp) == 1)
                    dirsToStrip = itmp;
            }
            else if ((i < (argc - 1)) && (isdigit(argv[i + 1][0])))
            {
                if (sscanf(argv[++i], "%i", &itmp) == 1)
                    dirsToStrip = itmp;
            }
            else
            {
                dirsToStrip = 0;
            }
            if (warningLevel > 5)
                INFO1("Setting path count to %d\n", dirsToStrip);
        }
        else if (strncmp(argv[i], "-R", 2) == 0)
        {
            if (argv[i][2] == '\0')
            {
                if (warningLevel > 0)
                {
                    WARN("No root directory specified\n");
                    ACTION("Ignoring -R option\n");
                }
            }
            else if (rootDir != NULL)
            {
                if (warningLevel > 0)
                {
                    WARN("Multiple root directories specified\n");
                    ACTION2("Using %s, ignoring %s\n", rootDir, argv[i]);
                }
            }
            else
            {
                rootDir = &argv[i][2];
                if (warningLevel > 8)
                {
                    WARN1("Changing root directory to \"%s\"\n", rootDir);
                }
                if ((chdir(rootDir) < 0) && (warningLevel > 0))
                {
                    WARN1("Couldn't change directory to \"%s\"\n", rootDir);
                    ACTION("Root directory (-R) option ignored\n");
                    rootDir = NULL;
                }
            }
        }
        else if ((strcmp(argv[i], "-synch") == 0)
                 || (strcmp(argv[i], "-s") == 0))
        {
            synch = True;
        }
        else if (strncmp(argv[i], "-v", 2) == 0)
        {
            char *str;
            if (argv[i][2] != '\0')
                str = &argv[i][2];
            else if ((i < (argc - 1)) && (argv[i + 1][0] != '-'))
                str = argv[++i];
            else
                str = NULL;
            if (str)
                setVerboseFlags(str);
        }
        else if (strncmp(argv[i], "-w", 2) == 0)
        {
            unsigned long utmp;
            char *tmp2;
            /* If text is just after "-w" in the same word, then it must
             * be a number and it is the warning level. Otherwise, if the
             * next argument is a number, then it is the warning level,
             * else the warning level is assumed to be 0.
             */
            if (argv[i][2] == '\0')
            {
                warningLevel = 0;
                if (i < argc - 1)
                {
                    utmp = strtoul(argv[i+1], &tmp2, 10);
                    if (argv[i+1][0] != '\0' && *tmp2 == '\0')
                    {
                        warningLevel = utmp > 10 ? 10 : utmp;
                        i++;
                    }
                }
            }
            else
            {
                utmp = strtoul(&argv[i][2], &tmp2, 10);
                if (*tmp2 == '\0')
                    warningLevel = utmp > 10 ? 10 : utmp;
                else
                {
                    ERROR1("Unknown flag \"%s\" on command line\n", argv[i]);
                    Usage(argc, argv);
                    return False;
                }
            }
        }
        else if ((strcmp(argv[i], "-xkb") == 0) && (!xkblist))
        {
            if ((outputFormat != WANT_DEFAULT)
                && (outputFormat != WANT_XKB_FILE))
            {
                if (warningLevel > 0)
                {
                    WARN("Multiple output file formats specified\n");
                    ACTION1("\"%s\" flag ignored\n", argv[i]);
                }
            }
            else
                outputFormat = WANT_XKB_FILE;
        }
        else if ((strcmp(argv[i], "-xkm") == 0) && (!xkblist))
        {
            if ((outputFormat != WANT_DEFAULT)
                && (outputFormat != WANT_XKM_FILE))
            {
                if (warningLevel > 0)
                {
                    WARN("Multiple output file formats specified\n");
                    ACTION1("\"%s\" flag ignored\n", argv[i]);
                }
            }
            else
                outputFormat = WANT_XKM_FILE;
        }
        else
        {
            ERROR1("Unknown flag \"%s\" on command line\n", argv[i]);
            Usage(argc, argv);
            return False;
        }
    }
    if (xkblist)
        inputFormat = INPUT_XKB;
    else if (inputFile == NULL)
    {
        ERROR("No input file specified\n");
        return False;
    }
    else if (uStringEqual(inputFile, "-"))
    {
        inputFormat = INPUT_XKB;
    }
#ifndef WIN32
    else if (strchr(inputFile, ':') == NULL)
    {
#else
    else if ((strchr(inputFile, ':') == NULL) || (strlen(inputFile) > 2 &&
                                               isalpha(inputFile[0]) &&
                                               inputFile[1] == ':'
                                               && strchr(inputFile + 2,
                                                         ':') == NULL))
    {
#endif
        int len;
        len = strlen(inputFile);
        if (inputFile[len - 1] == ')')
        {
            char *tmpstr;
            if ((tmpstr = strchr(inputFile, '(')) != NULL)
            {
                *tmpstr = '\0';
                inputFile[len - 1] = '\0';
                tmpstr++;
                if (*tmpstr == '\0')
                {
                    WARN("Empty map in filename\n");
                    ACTION("Ignored\n");
                }
                else if (inputMap == NULL)
                {
                    inputMap = uStringDup(tmpstr);
                }
                else
                {
                    WARN("Map specified in filename and with -m flag\n");
                    ACTION1("map from name (\"%s\") ignored\n", tmpstr);
                }
            }
            else
            {
                ERROR1("Illegal name \"%s\" for input file\n", inputFile);
                return False;
            }
        }
        if ((len > 4) && (strcmp(&inputFile[len - 4], ".xkm") == 0))
        {
            inputFormat = INPUT_XKM;
        }
        else
        {
            FILE *file;
            file = fopen(inputFile, "r");
            if (file)
            {
                if (XkmProbe(file))
                    inputFormat = INPUT_XKM;
                else
                    inputFormat = INPUT_XKB;
                fclose(file);
            }
            else
            {
                fprintf(stderr, "Cannot open \"%s\" for reading\n",
                        inputFile);
                return False;
            }
        }
    }
    else
    {
        inDpyName = inputFile;
        inputFile = NULL;
        inputFormat = INPUT_XKM;
    }

    if (outputFormat == WANT_DEFAULT)
    {
        if (xkblist)
            outputFormat = WANT_LISTING;
        else if (inputFormat == INPUT_XKB)
            outputFormat = WANT_XKM_FILE;
        else
            outputFormat = WANT_XKB_FILE;
    }
    if ((outputFormat == WANT_LISTING) && (inputFormat != INPUT_XKB))
    {
        if (inputFile)
            ERROR("Cannot generate a listing from a .xkm file (yet)\n");
        else
            ERROR("Cannot generate a listing from an X connection (yet)\n");
        return False;
    }
    if (xkblist)
    {
        if (outputFile == NULL)
            outputFile = uStringDup("-");
        else if (strchr(outputFile, ':') != NULL)
        {
            ERROR("Cannot write a listing to an X connection\n");
            return False;
        }
    }
    else if ((!outputFile) && (inputFile) && uStringEqual(inputFile, "-"))
    {
        int len = strlen("stdin") + strlen(fileTypeExt[outputFormat]) + 2;
        outputFile = uTypedCalloc(len, char);
        if (outputFile == NULL)
        {
            WSGO("Cannot allocate space for output file name\n");
            ACTION("Exiting\n");
            exit(1);
        }
        snprintf(outputFile, len, "stdin.%s", fileTypeExt[outputFormat]);
    }
    else if ((outputFile == NULL) && (inputFile != NULL))
    {
        int len;
        char *base, *ext;

        if (inputMap == NULL)
        {
            base = strrchr(inputFile, '/');
            if (base == NULL)
                base = inputFile;
            else
                base++;
        }
        else
            base = inputMap;

        len = strlen(base) + strlen(fileTypeExt[outputFormat]) + 2;
        outputFile = uTypedCalloc(len, char);
        if (outputFile == NULL)
        {
            WSGO("Cannot allocate space for output file name\n");
            ACTION("Exiting\n");
            exit(1);
        }
        ext = strrchr(base, '.');
        if (ext == NULL)
            snprintf(outputFile, len, "%s.%s", base, fileTypeExt[outputFormat]);
        else
        {
            strcpy(outputFile, base);
            strcpy(&outputFile[ext - base + 1], fileTypeExt[outputFormat]);
        }
    }
    else if (outputFile == NULL)
    {
        int len;
        char *ch, *name, buf[128];
        if (inDpyName[0] == ':')
            snprintf(name = buf, sizeof(buf), "server%s", inDpyName);
        else
            name = inDpyName;

        len = strlen(name) + strlen(fileTypeExt[outputFormat]) + 2;
        outputFile = uTypedCalloc(len, char);
        if (outputFile == NULL)
        {
            WSGO("Cannot allocate space for output file name\n");
            ACTION("Exiting\n");
            exit(1);
        }
        strcpy(outputFile, name);
        for (ch = outputFile; (*ch) != '\0'; ch++)
        {
            if (*ch == ':')
                *ch = '-';
            else if (*ch == '.')
                *ch = '_';
        }
        *ch++ = '.';
        strcpy(ch, fileTypeExt[outputFormat]);
    }
#ifdef WIN32
    else if (strlen(outputFile) > 2 &&
             isalpha(outputFile[0]) &&
             outputFile[1] == ':' && strchr(outputFile + 2, ':') == NULL)
    {
    }
#endif
    else if (strchr(outputFile, ':') != NULL)
    {
        outDpyName = outputFile;
        outputFile = NULL;
        outputFormat = WANT_X_SERVER;
    }
    return True;
}

static Display *
GetDisplay(char *program, char *dpyName)
{
    int mjr, mnr, error;
    Display *dpy;

    mjr = XkbMajorVersion;
    mnr = XkbMinorVersion;
    dpy = XkbOpenDisplay(dpyName, NULL, NULL, &mjr, &mnr, &error);
    if (dpy == NULL)
    {
        switch (error)
        {
        case XkbOD_BadLibraryVersion:
            INFO3("%s was compiled with XKB version %d.%02d\n",
                  program, XkbMajorVersion, XkbMinorVersion);
            ERROR2("X library supports incompatible version %d.%02d\n",
                   mjr, mnr);
            break;
        case XkbOD_ConnectionRefused:
            ERROR1("Cannot open display \"%s\"\n", dpyName);
            break;
        case XkbOD_NonXkbServer:
            ERROR1("XKB extension not present on %s\n", dpyName);
            break;
        case XkbOD_BadServerVersion:
            INFO3("%s was compiled with XKB version %d.%02d\n",
                  program, XkbMajorVersion, XkbMinorVersion);
            ERROR3("Server %s uses incompatible version %d.%02d\n",
                   dpyName, mjr, mnr);
            break;
        default:
            WSGO1("Unknown error %d from XkbOpenDisplay\n", error);
        }
    }
    else if (synch)
        XSynchronize(dpy, True);
    return dpy;
}

/***====================================================================***/

#ifdef DEBUG
extern int yydebug;
#endif

int
main(int argc, char *argv[])
{
    FILE *file;         /* input file (or stdin) */
    XkbFile *rtrn;
    XkbFile *mapToUse;
    int ok;
    XkbFileInfo result;
    Status status;

    scan_set_file(stdin);
    uSetDebugFile(NullString);
    uSetErrorFile(NullString);

    XkbInitIncludePath();
    if (!parseArgs(argc, argv))
        exit(1);
#ifdef DEBUG
    if (debugFlags & 0x2)
        yydebug = 1;
#endif
    if (preErrorMsg)
        uSetPreErrorMessage(preErrorMsg);
    if (errorPrefix)
        uSetErrorPrefix(errorPrefix);
    if (postErrorMsg)
        uSetPostErrorMessage(postErrorMsg);
    file = NULL;
    XkbInitAtoms(NULL);
    XkbAddDefaultDirectoriesToPath();
    if (xkblist)
    {
        Bool gotSome;
        gotSome = GenerateListing(outputFile);
        if ((warningLevel > 7) && (!gotSome))
            return -1;
        return 0;
    }
    if (inputFile != NULL)
    {
        if (uStringEqual(inputFile, "-"))
        {
            file = stdin;
            inputFile = "stdin";
        }
        else
        {
            file = fopen(inputFile, "r");
        }
    }
    else if (inDpyName != NULL)
    {
        inDpy = GetDisplay(argv[0], inDpyName);
        if (!inDpy)
        {
            ACTION("Exiting\n");
            exit(1);
        }
    }
    if (outDpyName != NULL)
    {
        outDpy = GetDisplay(argv[0], outDpyName);
        if (!outDpy)
        {
            ACTION("Exiting\n");
            exit(1);
        }
    }
    if ((inDpy == NULL) && (outDpy == NULL))
    {
        int mjr, mnr;
        mjr = XkbMajorVersion;
        mnr = XkbMinorVersion;
        if (!XkbLibraryVersion(&mjr, &mnr))
        {
            INFO3("%s was compiled with XKB version %d.%02d\n",
                  argv[0], XkbMajorVersion, XkbMinorVersion);
            ERROR2("X library supports incompatible version %d.%02d\n",
                   mjr, mnr);
            ACTION("Exiting\n");
            exit(1);
        }
    }
    if (file)
    {
        ok = True;
        setScanState(inputFile, 1);
        if ((inputFormat == INPUT_XKB) /* parse .xkb file */
            && (XKBParseFile(file, &rtrn) && (rtrn != NULL)))
        {
            fclose(file);
            mapToUse = rtrn;
            if (inputMap != NULL) /* map specified on cmdline? */
            {
                while ((mapToUse)
                       && (!uStringEqual(mapToUse->name, inputMap)))
                {
                    mapToUse = (XkbFile *) mapToUse->common.next;
                }
                if (!mapToUse)
                {
                    FATAL2("No map named \"%s\" in \"%s\"\n",
                           inputMap, inputFile);
                    /* NOTREACHED */
                }
            }
            else if (rtrn->common.next != NULL)
            {
                /* look for map with XkbLC_Default flag. */
                mapToUse = rtrn;
                for (; mapToUse; mapToUse = (XkbFile *) mapToUse->common.next)
                {
                    if (mapToUse->flags & XkbLC_Default)
                        break;
                }
                if (!mapToUse)
                {
                    mapToUse = rtrn;
                    if (warningLevel > 4)
                    {
                        WARN1
                            ("No map specified, but \"%s\" has several\n",
                             inputFile);
                        ACTION1
                            ("Using the first defined map, \"%s\"\n",
                             mapToUse->name);
                    }
                }
            }
            bzero((char *) &result, sizeof(result));
            result.type = mapToUse->type;
            if ((result.xkb = XkbAllocKeyboard()) == NULL)
            {
                WSGO("Cannot allocate keyboard description\n");
                /* NOTREACHED */
            }
            switch (mapToUse->type)
            {
            case XkmSemanticsFile:
            case XkmLayoutFile:
            case XkmKeymapFile:
                ok = CompileKeymap(mapToUse, &result, MergeReplace);
                break;
            case XkmKeyNamesIndex:
                ok = CompileKeycodes(mapToUse, &result, MergeReplace);
                break;
            case XkmTypesIndex:
                ok = CompileKeyTypes(mapToUse, &result, MergeReplace);
                break;
            case XkmSymbolsIndex:
                /* if it's just symbols, invent key names */
                result.xkb->flags |= AutoKeyNames;
                ok = False;
                break;
            case XkmCompatMapIndex:
                ok = CompileCompatMap(mapToUse, &result, MergeReplace, NULL);
                break;
            case XkmGeometryFile:
            case XkmGeometryIndex:
                /* if it's just a geometry, invent key names */
                result.xkb->flags |= AutoKeyNames;
                ok = CompileGeometry(mapToUse, &result, MergeReplace);
                break;
            default:
                WSGO1("Unknown file type %d\n", mapToUse->type);
                ok = False;
                break;
            }
            result.xkb->device_spec = device_id;
        }
        else if (inputFormat == INPUT_XKM) /* parse xkm file */
        {
            unsigned tmp;
            bzero((char *) &result, sizeof(result));
            if ((result.xkb = XkbAllocKeyboard()) == NULL)
            {
                WSGO("Cannot allocate keyboard description\n");
                /* NOTREACHED */
            }
            tmp = XkmReadFile(file, 0, XkmKeymapLegal, &result);
            if (tmp == XkmKeymapLegal)
            {
                ERROR1("Cannot read XKM file \"%s\"\n", inputFile);
                ok = False;
            }
            result.xkb->device_spec = device_id;
        }
        else
        {
            INFO1("Errors encountered in %s; not compiled.\n", inputFile);
            ok = False;
        }
    }
    else if (inDpy != NULL)
    {
        bzero((char *) &result, sizeof(result));
        result.type = XkmKeymapFile;
        result.xkb = XkbGetMap(inDpy, XkbAllMapComponentsMask, device_id);
        if (result.xkb == NULL)
            WSGO("Cannot load keyboard description\n");
        if (XkbGetIndicatorMap(inDpy, ~0, result.xkb) != Success)
            WSGO("Could not load indicator map\n");
        if (XkbGetControls(inDpy, XkbAllControlsMask, result.xkb) != Success)
            WSGO("Could not load keyboard controls\n");
        if (XkbGetCompatMap(inDpy, XkbAllCompatMask, result.xkb) != Success)
            WSGO("Could not load compatibility map\n");
        if (XkbGetNames(inDpy, XkbAllNamesMask, result.xkb) != Success)
            WSGO("Could not load names\n");
        if ((status = XkbGetGeometry(inDpy, result.xkb)) != Success)
        {
            if (warningLevel > 3)
            {
                char buf[100];
                buf[0] = '\0';
                XGetErrorText(inDpy, status, buf, 100);
                WARN1("Could not load keyboard geometry for %s\n", inDpyName);
                ACTION1("%s\n", buf);
                ACTION("Resulting keymap file will not describe geometry\n");
            }
        }
        if (computeDflts)
            ok = (ComputeKbdDefaults(result.xkb) == Success);
        else
            ok = True;
    }
    else
    {
        fprintf(stderr, "Cannot open \"%s\" to compile\n", inputFile);
        ok = 0;
    }
    if (ok)
    {
        FILE *out = stdout;
        if ((inDpy != outDpy) &&
            (XkbChangeKbdDisplay(outDpy, &result) != Success))
        {
            WSGO2("Error converting keyboard display from %s to %s\n",
                  inDpyName, outDpyName);
            exit(1);
        }
        if (outputFile != NULL)
        {
            if (uStringEqual(outputFile, "-"))
                outputFile = "stdout";
            else
            {
                /*
                 * fix to prevent symlink attack (e.g.,
                 * ln -s /etc/passwd /var/tmp/server-0.xkm)
                 */
                /*
                 * this patch may have POSIX, Linux, or GNU libc bias
                 * -- Branden Robinson
                 */
                int outputFileFd;
                int binMode = 0;
                const char *openMode = "w";
                unlink(outputFile);
#ifdef O_BINARY
                switch (outputFormat)
                {
                case WANT_XKM_FILE:
                    binMode = O_BINARY;
                    openMode = "wb";
                    break;
                default:
                    binMode = 0;
                    break;
                }
#endif
                outputFileFd =
                    open(outputFile, O_WRONLY | O_CREAT | O_EXCL,
                         S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH
                         | S_IWOTH | binMode);
                if (outputFileFd < 0)
                {
                    ERROR1
                        ("Cannot open \"%s\" to write keyboard description\n",
                         outputFile);
                    ACTION("Exiting\n");
                    exit(1);
                }
#ifndef WIN32
                out = fdopen(outputFileFd, openMode);
#else
                close(outputFileFd);
                out = fopen(outputFile, "wb");
#endif
                /* end BR */
                if (out == NULL)
                {
                    ERROR1
                        ("Cannot open \"%s\" to write keyboard description\n",
                         outputFile);
                    ACTION("Exiting\n");
                    exit(1);
                }
            }
        }
        switch (outputFormat)
        {
        case WANT_XKM_FILE:
            ok = XkbWriteXKMFile(out, &result);
            break;
        case WANT_XKB_FILE:
            ok = XkbWriteXKBFile(out, &result, showImplicit, NULL, NULL);
            break;
        case WANT_C_HDR:
            ok = XkbWriteCFile(out, outputFile, &result);
            break;
        case WANT_X_SERVER:
            if (!(ok = XkbWriteToServer(&result)))
            {
                ERROR2("%s in %s\n", _XkbErrMessages[_XkbErrCode],
                       _XkbErrLocation ? _XkbErrLocation : "unknown");
                ACTION1("Couldn't write keyboard description to %s\n",
                        outDpyName);
            }
            break;
        default:
            WSGO1("Unknown output format %d\n", outputFormat);
            ACTION("No output file created\n");
            ok = False;
            break;
        }
        if (outputFormat != WANT_X_SERVER)
        {
            if (fclose(out))
            {
                ERROR1("Cannot close \"%s\" properly (not enough space?)\n",
                       outputFile);
                ok= False;
            }
            else if (!ok)
            {
                ERROR2("%s in %s\n", _XkbErrMessages[_XkbErrCode],
                       _XkbErrLocation ? _XkbErrLocation : "unknown");
            }
            if (!ok)
            {
                ACTION1("Output file \"%s\" removed\n", outputFile);
                unlink(outputFile);
            }
        }
    }
    if (inDpy)
        XCloseDisplay(inDpy);
    inDpy = NULL;
    if (outDpy)
        XCloseDisplay(outDpy);
    uFinishUp();
    return (ok == 0);
}
