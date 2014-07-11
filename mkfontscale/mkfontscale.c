/*
  Copyright (c) 2002-2003 by Juliusz Chroboczek

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>

#include <X11/Xos.h>
#include <X11/Xfuncproto.h>
#include <X11/fonts/fontenc.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_TABLES_H
#include FT_TRUETYPE_IDS_H
#include FT_TYPE1_TABLES_H
#include FT_BDF_H
#include FT_XFREE86_H

#include "list.h"
#include "hash.h"
#include "data.h"
#include "ident.h"

#define NPREFIX 1024

#ifndef MAXFONTFILENAMELEN
#define MAXFONTFILENAMELEN 1024
#endif
#ifndef MAXFONTNAMELEN
#define MAXFONTNAMELEN 1024
#endif

/* Two levels of macro calls are needed so that we stringify the value
   of MAXFONT... and not the string "MAXFONT..." */
#define QUOTE(x)	#x
#define STRINGIFY(x)	QUOTE(x)

static const char *encodings_array[] =
    { "ascii-0",
      "iso8859-1", "iso8859-2", "iso8859-3", "iso8859-4", "iso8859-5",
      "iso8859-6", "iso8859-6.8", "iso8859-6.8x", "iso8859-6.16",
      "iso8859-7", "iso8859-8", "iso8859-9", "iso8859-10",
      "iso8859-11", "iso8859-12", "iso8859-13", "iso8859-14",
      "iso8859-15", "iso8859-16",
      "ansi-1251", "koi8-r", "koi8-u", "koi8-ru", "koi8-e", "koi8-uni",
      "tis620-2",
      "sun.unicode.india-0", "suneu-greek",
      "adobe-standard", "adobe-symbol",
      "ibm-cp437", "ibm-cp850", "ibm-cp852", "ibm-cp866", "microsoft-cp1252",
      /* But not "adobe-dingbats", as it uses generic glyph names. */
      "cns11643-1", "cns11643-2", "cns11643-3",
      "jisx0201.1976-0", "jisx0208.1983-0", "jisx0208.1990-0",
      "jisx0212.1990-0", "big5-0", "big5.eten-0", "big5hkscs-0",
      "gb2312.1980-0", "gb18030.2000-0", "gb18030.2000-1",
      "ksc5601.1987-0", "ksc5601.1992-3"};

static const char *extra_encodings_array[] =
    { "iso10646-1", "adobe-fontspecific", "microsoft-symbol" };

static ListPtr encodings, extra_encodings;
static const char *outfilename;

#define countof(_a) (sizeof(_a)/sizeof((_a)[0]))

static int doDirectory(const char*, int, ListPtr);
static int checkEncoding(FT_Face face, char *encoding_name);
static int checkExtraEncoding(FT_Face face, char *encoding_name, int found);
static int find_cmap(int type, int pid, int eid, FT_Face face);
static const char* notice_foundry(const char *notice);
static const char* vendor_foundry(const signed char *vendor);
static int readFontScale(HashTablePtr entries, char *dirname);
ListPtr makeXLFD(char *filename, FT_Face face, int);
static int readEncodings(ListPtr encodings, char *dirname);

static FT_Library ft_library;
static float bigEncodingFuzz = 0.02;

static int relative;
static int doScalable;
static int doBitmaps;
static int doISO10646_1_encoding;
static int onlyEncodings;
static ListPtr encodingsToDo;
static int reencodeLegacy;
static char *encodingPrefix;
static char *exclusionSuffix;
static char *ProgramName;

static void _X_NORETURN _X_COLD
usage(void)
{
    fprintf(stderr, "Usage:\n"
            "mkfontscale [ -b ] [ -s ] [ -o filename ] [-x suffix ]\n"
            "            [ -a encoding ] [ -f fuzz ] [ -l ]\n"
            "            [ -e directory ] [ -p prefix ] [ -n ] [ -r ] \n"
            "            [-u] [-U] [-v] [ directory ]...\n");
    exit(1);
}

static void _X_NORETURN _X_COLD
missing_arg (const char *option)
{
    fprintf(stderr, "%s: %s requires an argument\n", ProgramName, option);
    usage();
}

int
main(int argc, char **argv)
{
    int argn;
    FT_Error ftrc;
    int rc, ll = 0;
    char prefix[NPREFIX];

    ProgramName = argv[0];
    encodingPrefix = NULL;
    exclusionSuffix = NULL;

    if(getcwd(prefix, NPREFIX - 1) == NULL) {
        perror("Couldn't get cwd");
        exit(1);
    }
    if(prefix[strlen(prefix) - 1] != '/')
        strcat(prefix, "/");
    encodingPrefix = dsprintf("%s", prefix);

    outfilename = NULL;

    encodings = makeList(encodings_array, countof(encodings_array), NULL, 0);

    extra_encodings = makeList(extra_encodings_array,
                               countof(extra_encodings_array),
                               NULL, 0);
    doBitmaps = 0;
    doISO10646_1_encoding = 1;
    doScalable = 1;
    onlyEncodings = 0;
    relative = 0;
    reencodeLegacy = 1;
    encodingsToDo = NULL;

    argn = 1;
    while(argn < argc) {
        if(argv[argn][0] == '\0' || argv[argn][0] != '-')
            break;
        if(argv[argn][1] == '-') {
            argn++;
            break;
        } else if (strcmp(argv[argn], "-x") == 0) {
            if(argn >= argc - 1) {
                missing_arg("-x");
            }
            exclusionSuffix = argv[argn + 1];
            argn += 2;
        } else if(strcmp(argv[argn], "-a") == 0) {
            if(argn >= argc - 1) {
                missing_arg("-a");
            }
            makeList(&argv[argn + 1], 1, encodings, 0);
            argn += 2;
        } else if(strcmp(argv[argn], "-p") == 0) {
            if(argn >= argc - 1) {
                missing_arg("-p");
            }
            if(strlen(argv[argn + 1]) > NPREFIX - 1) {
                fprintf(stderr, "%s: argument to -p cannot be longer than "
                        "%d characters\n", ProgramName, NPREFIX - 1);
                usage();
            }
            free(encodingPrefix);
            encodingPrefix = dsprintf("%s", argv[argn + 1]);
            argn += 2;
        } else if(strcmp(argv[argn], "-e") == 0) {
            if(argn >= argc - 1) {
                missing_arg("-e");
            }
            rc = readEncodings(encodingsToDo, argv[argn + 1]);
            if(rc < 0)
                exit(1);
            argn += 2;
        } else if(strcmp(argv[argn], "-b") == 0) {
            doBitmaps = 1;
            argn++;
        } else if(strcmp(argv[argn], "-u") == 0) {
            doISO10646_1_encoding = 0;
            argn++;
        } else if(strcmp(argv[argn], "-U") == 0) {
            doISO10646_1_encoding = 1;
            argn++;
        } else if(strcmp(argv[argn], "-s") == 0) {
            doScalable = 0;
            argn++;
        } else if(strcmp(argv[argn], "-n") == 0) {
            onlyEncodings = 1;
            argn++;
        } else if(strcmp(argv[argn], "-r") == 0) {
            relative = 1;
            argn++;
        } else if(strcmp(argv[argn], "-l") == 0) {
            reencodeLegacy = !reencodeLegacy;
            argn++;
        } else if(strcmp(argv[argn], "-o") == 0) {
            if(argn >= argc - 1) {
                missing_arg("-o");
            }
            outfilename = argv[argn + 1];
            argn += 2;
        } else if(strcmp(argv[argn], "-f") == 0) {
            if(argn >= argc - 1) {
                missing_arg("-f");
            }
            bigEncodingFuzz = atof(argv[argn + 1]) / 100.0;
            argn += 2;
	} else if (strcmp(argv[argn], "-v") == 0) {
	    printf("%s\n", PACKAGE_STRING);
	    exit(0);
	} else {
            usage();
        }
    }

    if(outfilename == NULL) {
        if(doBitmaps)
            outfilename = "fonts.dir";
        else
            outfilename = "fonts.scale";
    }

    ftrc = FT_Init_FreeType(&ft_library);
    if(ftrc) {
        fprintf(stderr, "Could not initialise FreeType library: %d\n", ftrc);
        exit(1);
    }

    ll = listLength(encodingsToDo);

    if (argn == argc)
        doDirectory(".", ll, encodingsToDo);
    else
        while(argn < argc) {
            doDirectory(argv[argn], ll, encodingsToDo);
            argn++;
        }
    return 0;
}

static int
getNameHelper(FT_Face face, int nid, int pid, int eid,
              FT_SfntName *name_return)
{
    FT_SfntName name;
    int n, i;

    n = FT_Get_Sfnt_Name_Count(face);
    if(n <= 0)
        return 0;

    for(i = 0; i < n; i++) {
        if(FT_Get_Sfnt_Name(face, i, &name))
            continue;
        if(name.name_id == nid &&
           name.platform_id == pid &&
           (eid < 0 || name.encoding_id == eid)) {
            switch(name.platform_id) {
            case TT_PLATFORM_APPLE_UNICODE:
            case TT_PLATFORM_MACINTOSH:
                if(name.language_id != TT_MAC_LANGID_ENGLISH)
                    continue;
                break;
            case TT_PLATFORM_MICROSOFT:
                if(name.language_id != TT_MS_LANGID_ENGLISH_UNITED_STATES &&
                   name.language_id != TT_MS_LANGID_ENGLISH_UNITED_KINGDOM)
                    continue;
                break;
            default:
                continue;
            }
            if(name.string_len > 0) {
                *name_return = name;
                return 1;
            }
        }
    }
    return 0;
}

static char *
getName(FT_Face face, int nid)
{
    FT_SfntName name;
    char *string;
    int i;

    if(getNameHelper(face, nid,
                     TT_PLATFORM_MICROSOFT, TT_MS_ID_UNICODE_CS, &name) ||
       getNameHelper(face, nid,
                     TT_PLATFORM_APPLE_UNICODE, -1, &name)) {
        string = malloc(name.string_len / 2 + 1);
        if(string == NULL) {
            fprintf(stderr, "Couldn't allocate name\n");
            exit(1);
        }
        for(i = 0; i < name.string_len / 2; i++) {
            if(name.string[2 * i] != 0)
                string[i] = '?';
            else
                string[i] = name.string[2 * i + 1];
        }
        string[i] = '\0';
        return string;
    }

    /* Pretend that Apple Roman is ISO 8859-1. */
    if(getNameHelper(face, nid, TT_PLATFORM_MACINTOSH, TT_MAC_ID_ROMAN,
                     &name)) {
        string = malloc(name.string_len + 1);
        if(string == NULL) {
            fprintf(stderr, "Couldn't allocate name\n");
            exit(1);
        }
        memcpy(string, name.string, name.string_len);
        string[name.string_len] = '\0';
        return string;
    }

    return NULL;
}

static const char*
os2Weight(int weight)
{
    if(weight < 150)
        return "thin";
    else if(weight < 250)
        return "extralight";
    else if(weight < 350)
        return "light";
    else if(weight < 450)
        return "medium";        /* officially "normal" */
    else if(weight < 550)
        return "medium";
    else if(weight < 650)
        return "semibold";
    else if(weight < 750)
        return "bold";
    else if(weight < 850)
        return "extrabold";
    else
        return "black";
}

static const char*
os2Width(int width)
{
    if(width <= 1)
        return "ultracondensed";
    else if(width <= 2)
        return "extracondensed";
    else if(width <= 3)
        return "condensed";
    else if(width <= 4)
        return "semicondensed";
    else if(width <= 5)
        return "normal";
    else if(width <= 6)
        return "semiexpanded";
    else if(width <= 7)
        return "expanded";
    else if(width <= 8)
        return "extraexpanded";
    else
        return "ultraexpanded";
}

static const char *widths[] = {
    "ultracondensed", "extracondensed", "condensed", "semicondensed",
    "normal", "semiexpanded", "expanded", "extraexpanded", "ultraexpanded"
};

#define NUMWIDTHS (sizeof(widths) / sizeof(widths[0]))

static const char*
nameWidth(const char *name)
{
    char buf[500];
    int i;
    int n = strlen(name);

    if(n >= 499) return NULL;
    for(i = 0; i < n; i++)
        buf[i] = tolower(name[i]);
    buf[i] = '\0';

    for(i = 0; i < NUMWIDTHS; i++)
        if(strstr(buf, widths[i]))
            return widths[i];
    return NULL;
}

static const char*
t1Weight(const char *weight)
{
    if(!weight)
        return NULL;
    if(strcmp(weight, "Thin") == 0)
        return "thin";
    if(strcmp(weight, "ExtraLight") == 0) /* FontForge uses this for 200*/
        return "extralight";
    if(strcmp(weight, "Light") == 0)
        return "light";
    if(strcmp(weight, "Regular") == 0)
        return "medium";
    if(strcmp(weight, "Normal") == 0)
        return "medium";
    if(strcmp(weight, "Medium") == 0)
        return "medium";
    if(strcmp(weight, "Book") == 0)
        return "medium";
    if(strcmp(weight, "Roman") == 0) /* Some URW++ fonts do that! */
        return "medium";
    if(strcmp(weight, "Demi") == 0)
        return "semibold";
    if(strcmp(weight, "DemiBold") == 0)
        return "semibold";
    if(strcmp(weight, "SemiBold") == 0) /* some TeX fonts apparently do that */
        return "semibold";
    else if(strcmp(weight, "Bold") == 0)
        return "bold";
    else if(strcmp(weight, "Heavy") == 0) /* FontForge uses this for 800*/
        return "extrabold";
    else if(strcmp(weight, "Black") == 0)
        return "black";
    else {
        fprintf(stderr, "Unknown Type 1 weight \"%s\"\n", weight);
        return NULL;
    }
}

static int
unsafe(char c)
{
    return
        c < 0x20 || c > 0x7E ||
        c == '[' || c == ']' || c == '(' || c == ')' || c == '\\' || c == '-';
}

static const char *
safe(const char* s)
{
    int i, len, safe_flag = 1;
    char *t;

    i = 0;
    while(s[i] != '\0') {
        if(unsafe(s[i]))
            safe_flag = 0;
        i++;
    }

    if(safe_flag) return strdup(s);

    len = i;
    t = malloc(len + 1);
    if(t == NULL) {
        perror("Couldn't allocate string");
        exit(1);
    }

    for(i = 0; i < len; i++) {
        if(unsafe(s[i]))
            t[i] = ' ';
        else
            t[i] = s[i];
    }
    t[i] = '\0';
    return t;
}

ListPtr
makeXLFD(char *filename, FT_Face face, int isBitmap)
{
    ListPtr xlfd = NULL;
    const char *foundry, *family, *weight, *slant, *sWidth, *adstyle,
        *spacing, *full_name, *tmp;
    TT_Header *head;
    TT_HoriHeader *hhea;
    TT_OS2 *os2;
    TT_Postscript *post;
    PS_FontInfoRec *t1info, t1info_rec;
    int rc;

    foundry = NULL;
    family = NULL;
    weight = NULL;
    slant = NULL;
    sWidth = NULL;
    adstyle = NULL;
    spacing = NULL;
    full_name = NULL;

    head = FT_Get_Sfnt_Table(face, ft_sfnt_head);
    hhea = FT_Get_Sfnt_Table(face, ft_sfnt_hhea);
    os2 = FT_Get_Sfnt_Table(face, ft_sfnt_os2);
    post = FT_Get_Sfnt_Table(face, ft_sfnt_post);

    rc = FT_Get_PS_Font_Info(face, &t1info_rec);
    if(rc == 0)
        t1info = &t1info_rec;
    else
        t1info = NULL;

    if(!family)
        family = getName(face, TT_NAME_ID_FONT_FAMILY);
    if(!family)
        family = getName(face, TT_NAME_ID_FULL_NAME);
    if(!family)
        family = getName(face, TT_NAME_ID_PS_NAME);

    if(!full_name)
        full_name = getName(face, TT_NAME_ID_FULL_NAME);
    if(!full_name)
        full_name = getName(face, TT_NAME_ID_PS_NAME);

    if(os2 && os2->version != 0xFFFF) {
        if(!weight)
            weight = os2Weight(os2->usWeightClass);
        if(!sWidth)
            sWidth = os2Width(os2->usWidthClass);
        if(!foundry)
            foundry = vendor_foundry(os2->achVendID);
        if(!slant)
            slant = os2->fsSelection & 1 ? "i" : "r";
    }

    if(post) {
        if(!spacing) {
            if(post->isFixedPitch) {
                if(hhea->min_Left_Side_Bearing >= 0 &&
                   hhea->xMax_Extent <= hhea->advance_Width_Max) {
                    spacing = "c";
                } else {
                    spacing = "m";
                }
            } else {
                spacing = "p";
            }
        }
    }

    if(t1info) {
        if(!family)
            family = strdup(t1info->family_name);
        if(!family)
            family = strdup(t1info->full_name);
        if(!full_name)
            full_name = strdup(t1info->full_name);
        if(!foundry)
            foundry = notice_foundry(t1info->notice);
        if(!weight)
            weight = t1Weight(t1info->weight);
        if(!spacing)
            spacing = t1info->is_fixed_pitch ? "m" : "p";
        if(!slant) {
            /* Bitstream fonts have positive italic angle. */
            slant =
                t1info->italic_angle <= -4 || t1info->italic_angle >= 4 ?
                "i" : "r";
        }
    }

    if(!full_name) {
        fprintf(stderr, "Couldn't determine full name for %s\n", filename);
        full_name = strdup(filename);
    }

    if(head) {
        if(!slant)
            slant = head->Mac_Style & 2 ? "i" : "r";
        if(!weight)
            weight = head->Mac_Style & 1 ? "bold" : "medium";
    }

    if(!slant) {
        fprintf(stderr, "Couldn't determine slant for %s\n", filename);
        slant = "r";
    }

    if(!weight) {
        fprintf(stderr, "Couldn't determine weight for %s\n", filename);
        weight = "medium";
    }

    if(!foundry) {
        char *notice;
        notice = getName(face, TT_NAME_ID_TRADEMARK);
        if(notice) {
            foundry = notice_foundry(notice);
            free(notice);
        }
        if(!foundry) {
            notice = getName(face, TT_NAME_ID_MANUFACTURER);
            if(notice) {
                foundry = notice_foundry(notice);
                free(notice);
            }
        }
    }

    if(strcmp(slant, "i") == 0) {
        if(strstr(full_name, "Oblique"))
            slant = "o";
        if(strstr(full_name, "Slanted"))
            slant = "o";
    }

    if(!sWidth)
        sWidth = nameWidth(full_name);

    if(!foundry) foundry = "misc";
    if(!family) {
        fprintf(stderr, "Couldn't get family name for %s\n", filename);
        family = strdup(filename);
    }

    if(!weight) weight = "medium";
    if(!slant) slant = "r";
    if(!sWidth) sWidth = "normal";
    if(!adstyle) adstyle = "";
    if(!spacing) spacing = "p";

    foundry = safe(foundry);

    tmp = family;
    family = safe(family);
    free((void *)tmp);

    if(!isBitmap) {
        xlfd = listConsF(xlfd,
                         "-%s-%s-%s-%s-%s-%s-0-0-0-0-%s-0",
                         foundry, family,
                         weight, slant, sWidth, adstyle, spacing);
    } else {
        int i, w, h, xres, yres;
        for(i = 0; i < face->num_fixed_sizes; i++) {
            w = face->available_sizes[i].width;
            h = face->available_sizes[i].height;
            xres = 75;
            yres = (double)h / w * xres;
            xlfd = listConsF(xlfd,
                             "-%s-%s-%s-%s-%s-%s-%d-%d-%d-%d-%s-%d",
                             foundry, family,
                             weight, slant, sWidth, adstyle,
                             h, (int)(h / (double)yres * 72.27 * 10 + 0.5),
                             xres, yres,
                             spacing, 60);
        }
    }

    free((void *)family);
    free((void *)foundry);
    free((void *)full_name);
    return xlfd;
}

static int
readFontScale(HashTablePtr entries, char *dirname)
{
    int n = strlen(dirname);
    char *filename;
    FILE *in;
    int rc, count, i;
    char file[MAXFONTFILENAMELEN+1], font[MAXFONTNAMELEN+1];

    if(dirname[n - 1] == '/')
        filename = dsprintf("%sfonts.scale", dirname);
    else
        filename = dsprintf("%s/fonts.scale", dirname);
    if(filename == NULL)
        return -1;

    in = fopen(filename, "r");
    free(filename);
    if(in == NULL) {
        if(errno != ENOENT)
            perror("open(fonts.scale)");
        return -1;
    }

    rc = fscanf(in, "%d\n", &count);
    if(rc != 1) {
        fprintf(stderr, "Invalid fonts.scale in %s.\n", dirname);
        fclose(in);
        return -1;
    }

    for(i = 0; i < count; i++) {
        rc = fscanf(in,
		    "%" STRINGIFY(MAXFONTFILENAMELEN) "s "
		    "%" STRINGIFY(MAXFONTNAMELEN) "[^\n]\n",
		    file, font);
        if(rc != 2)
            break;
        putHash(entries, font, file, 100);
    }
    fclose(in);
    return 1;
}

static int
filePrio(char *filename)
{
    int n = strlen(filename);
    if(n < 4)
        return 0;
    if(strcmp(filename + n - 4, ".otf") == 0)
        return 6;
    if(strcmp(filename + n - 4, ".OTF") == 0)
        return 6;
    if(strcmp(filename + n - 4, ".ttf") == 0)
        return 5;
    if(strcmp(filename + n - 4, ".TTF") == 0)
        return 5;
    if(strcmp(filename + n - 4, ".pcf") == 0)
        return 4;
    if(strcmp(filename + n - 4, ".PCF") == 0)
        return 4;
    if(strcmp(filename + n - 3, ".gz") == 0)
        return 3;
#ifdef X_BZIP2_FONT_COMPRESSION
    if(strcmp(filename + n - 4, ".bz2") == 0)
        return 2;
#endif
    if(strcmp(filename + n - 2, ".Z") == 0)
        return 2;
    if(strcmp(filename + n - 4, ".bdf") == 0)
        return 1;
    if(strcmp(filename + n - 4, ".BDF") == 0)
        return 1;
    return 0;
}

static int
doDirectory(const char *dirname_given, int numEncodings, ListPtr encodingsToDo)
{
    char *dirname, *fontscale_name, *filename, *encdir;
    FILE *fontscale, *encfile;
    DIR *dirp;
    struct dirent *entry;
    FT_Error ftrc;
    FT_Face face;
    ListPtr encoding, xlfd, lp;
    HashTablePtr entries;
    HashBucketPtr *array;
    int i, n, found, rc;
    int isBitmap=0,xl=0;

    if (exclusionSuffix)
        xl = strlen (exclusionSuffix);

    i = strlen(dirname_given);
    if(i == 0)
        dirname = dsprintf("./");
    else if(dirname_given[i - 1] != '/')
        dirname = dsprintf("%s/", dirname_given);
    else
        dirname = dsprintf("%s", dirname_given);

    if(dirname == NULL) {
        perror("dirname");
        exit(1);
    }

    if (onlyEncodings)
	goto encodings;

    entries = makeHashTable();
    if(doBitmaps && !doScalable) {
        readFontScale(entries, dirname);
    }

    if(strcmp(outfilename, "-") == 0)
        fontscale_name = NULL;
    else {
        if(outfilename[0] == '/')
            fontscale_name = dsprintf("%s", outfilename);
        else
            fontscale_name = dsprintf("%s%s", dirname, outfilename);
        if(fontscale_name == NULL) {
            perror("fontscale_name");
            exit(1);
        }
    }

    dirp = opendir(dirname);
    if(dirp == NULL) {
        fprintf(stderr, "%s: ", dirname);
        perror("opendir");
        return 0;
    }

    if(fontscale_name == NULL)
        fontscale = stdout;
    else
        fontscale = fopen(fontscale_name, "wb");

    if(fontscale == NULL) {
        fprintf(stderr, "%s: ", fontscale_name);
        perror("fopen(w)");
        return 0;
    }

    while((entry = readdir(dirp)) != NULL) {
        int have_face = 0;
        char *xlfd_name = NULL;
	struct stat f_stat;
	int tprio = 1;

        xlfd = NULL;

	if (xl) {
	    int dl = strlen (entry->d_name);
	    if (strcmp (entry->d_name + dl - xl, exclusionSuffix) == 0)
		continue;
	}

        filename = dsprintf("%s%s", dirname, entry->d_name);

#define PRIO(x) ((x << 1) + tprio)
#ifdef DT_LNK
	if (entry->d_type != DT_UNKNOWN) {
	    if (entry->d_type == DT_LNK)
		tprio = 0;
	} else
#endif
#ifdef S_ISLNK
	{
	    if (lstat(filename, &f_stat))
		goto done;
	    if (S_ISLNK(f_stat.st_mode))
		tprio = 0;
	}
#else
	;
#endif
        if(doBitmaps)
            rc = bitmapIdentify(filename, &xlfd_name);
        else
            rc = 0;

        if(rc < 0)
            goto done;

        if(rc == 0) {
            ftrc = FT_New_Face(ft_library, filename, 0, &face);
            if(ftrc)
                goto done;
            have_face = 1;

            isBitmap = ((face->face_flags & FT_FACE_FLAG_SCALABLE) == 0);

            if(!isBitmap) {
                /* Workaround for bitmap-only SFNT fonts */
                if(FT_IS_SFNT(face) && face->num_fixed_sizes > 0 &&
                   strcmp(FT_Get_X11_Font_Format(face), "TrueType") == 0) {
                    TT_MaxProfile *maxp;
                    maxp = FT_Get_Sfnt_Table(face, ft_sfnt_maxp);
                    if(maxp != NULL && maxp->maxContours == 0)
                        isBitmap = 1;
                }
            }

            if(isBitmap) {
                if(!doBitmaps)
                    goto done;
            } else {
                if(!doScalable)
                    goto done;
            }

            if(isBitmap) {
                BDF_PropertyRec prop;
                rc = FT_Get_BDF_Property(face, "FONT", &prop);
                if(rc == 0 && prop.type == BDF_PROPERTY_TYPE_ATOM) {
                    xlfd_name = strdup(prop.u.atom);
                    if(xlfd_name == NULL)
                        goto done;
                }
            }
        }

        if(xlfd_name) {
            /* We know it's a bitmap font, and we know its XLFD */
            int n = strlen(xlfd_name);
            if(reencodeLegacy &&
               n >= 12 && strcasecmp(xlfd_name + n - 11, "-iso10646-1") == 0) {
                char *s;

                s = malloc(n - 10);
                memcpy(s, xlfd_name, n - 11);
                s[n - 11] = '\0';
                xlfd = listCons(s, xlfd);
            } else {
                /* Not a reencodable font -- skip all the rest of the loop body */
                putHash(entries, xlfd_name, entry->d_name, PRIO(filePrio(entry->d_name)));
                goto done;
            }
        }

        if(!have_face) {
            ftrc = FT_New_Face(ft_library, filename, 0, &face);
            if(ftrc)
                goto done;
            have_face = 1;
            isBitmap = ((face->face_flags & FT_FACE_FLAG_SCALABLE) == 0);

            if(!isBitmap) {
                if(face->num_fixed_sizes > 0) {
                    TT_MaxProfile *maxp;
                    maxp = FT_Get_Sfnt_Table(face, ft_sfnt_maxp);
                    if(maxp != NULL && maxp->maxContours == 0)
                        isBitmap = 1;
                }
            }
        }

        if(xlfd == NULL)
            xlfd = makeXLFD(entry->d_name, face, isBitmap);

        found = 0;

        for(lp = xlfd; lp; lp = lp->next) {
            char buf[MAXFONTNAMELEN];
            for(encoding = encodings; encoding; encoding = encoding->next) {
                if(checkEncoding(face, encoding->value)) {
                    found = 1;
                    snprintf(buf, MAXFONTNAMELEN, "%s-%s",
                            lp->value, encoding->value);
                    putHash(entries, buf, entry->d_name, PRIO(filePrio(entry->d_name)));
                }
            }
            for(encoding = extra_encodings; encoding;
                encoding = encoding->next) {
                if(checkExtraEncoding(face, encoding->value, found)) {
                    /* Do not set found! */
                    snprintf(buf, MAXFONTNAMELEN, "%s-%s",
                            lp->value, encoding->value);
                    putHash(entries, buf, entry->d_name, PRIO(filePrio(entry->d_name)));
                }
            }
        }
    done:
        if(have_face)
            FT_Done_Face(face);
        deepDestroyList(xlfd);
        xlfd = NULL;
        free(filename);
#undef PRIO
    }

    closedir(dirp);
    n = hashElements(entries);
    fprintf(fontscale, "%d\n", n);
    array = hashArray(entries, 1);
    for(i = 0; i < n; i++)
        fprintf(fontscale, "%s %s\n", array[i]->value, array[i]->key);
    destroyHashArray(array);
    entries = NULL;
    if(fontscale_name) {
        fclose(fontscale);
        free(fontscale_name);
    }

 encodings:
    encdir = dsprintf("%s%s", dirname, "encodings.dir");

    if(encdir == NULL) {
	perror("encodings");
	exit(1);
    }
    unlink(encdir);

    if (numEncodings) {
	encfile = fopen(encdir, "w");
	if(encfile == NULL) {
	    perror("open(encodings.dir)");
	    exit(1);
	}
        fprintf(encfile, "%d\n", numEncodings);
        encodingsToDo = sortList(encodingsToDo);
        for(lp = encodingsToDo; lp; lp = lp->next) {
            fprintf(encfile, "%s\n", lp->value);
        }
	fclose (encfile);
    }

    free(dirname);
    return 1;
}

#define CODE_IGNORED(c) ((c) < 0x20 || \
                         ((c) >= 0x7F && (c) <= 0xA0) || \
                         (c) == 0xAD || (c) == 0xF71B)

static int
checkEncoding(FT_Face face, char *encoding_name)
{
    FontEncPtr encoding;
    FontMapPtr mapping;
    int i, j, c, koi8;
    char *n;

    encoding = FontEncFind(encoding_name, NULL);
    if(!encoding)
        return 0;

    /* An encoding is ``small'' if one of the following is true:
         - it is linear and has no more than 256 codepoints; or
         - it is a matrix encoding and has no more than one column.

       For small encodings using Unicode indices, we require perfect
       coverage except for CODE_IGNORED and KOI-8 IBM-PC compatibility.

       For large encodings, we require coverage up to bigEncodingFuzz.

       For encodings using PS names (currently Adobe Standard and
       Adobe Symbol only), we require perfect coverage. */


    if(FT_Has_PS_Glyph_Names(face)) {
        for(mapping = encoding->mappings; mapping; mapping = mapping->next) {
            if(mapping->type == FONT_ENCODING_POSTSCRIPT) {
                if(encoding->row_size > 0) {
                    for(i = encoding->first; i < encoding->size; i++) {
                        for(j = encoding->first_col;
                            j < encoding->row_size;
                            j++) {
                            n = FontEncName((i<<8) | j, mapping);
                            if(n && FT_Get_Name_Index(face, n) == 0) {
                                return 0;
                            }
                        }
                    }
                    return 1;
                } else {
                    for(i = encoding->first; i < encoding->size; i++) {
                        n = FontEncName(i, mapping);
                        if(n && FT_Get_Name_Index(face, n) == 0) {
                            return 0;
                        }
                    }
                    return 1;
                }
            }
        }
    }

    for(mapping = encoding->mappings; mapping; mapping = mapping->next) {
        if(find_cmap(mapping->type, mapping->pid, mapping->eid, face)) {
            int total = 0, failed = 0;
            if(encoding->row_size > 0) {
                int estimate =
                    (encoding->size - encoding->first) *
                    (encoding->row_size - encoding->first_col);
                for(i = encoding->first; i < encoding->size; i++) {
                    for(j = encoding->first_col;
                        j < encoding->row_size;
                        j++) {
                        c = FontEncRecode((i<<8) | j, mapping);
                        if(CODE_IGNORED(c)) {
                            continue;
                        } else {
                            if(FT_Get_Char_Index(face, c) == 0) {
                                failed++;
                            }
                            total++;
                            if((encoding->size <= 1 && failed > 0) ||
                               ((float)failed >= bigEncodingFuzz * estimate)) {
                                return 0;
                            }
                        }
                    }
                }
                if((float)failed >= total * bigEncodingFuzz)
                    return 0;
                else
                    return 1;
            } else {
                int estimate = encoding->size - encoding->first;
                /* For the KOI8 encodings, we ignore the lack of
                   linedrawing and pseudo-math characters */
                if(strncmp(encoding->name, "koi8-", 5) == 0)
                    koi8 = 1;
                else
                    koi8 = 0;
                for(i = encoding->first; i < encoding->size; i++) {
                    c = FontEncRecode(i, mapping);
                    if(CODE_IGNORED(c) ||
                       (koi8 && ((c >= 0x2200 && c < 0x2600) || c == 0x00b2))) {
                        continue;
                    } else {
                        if(FT_Get_Char_Index(face, c) == 0) {
                            failed++;
                        }
                        total++;
                        if((encoding->size <= 256 && failed > 0) ||
                           ((float)failed >= bigEncodingFuzz * estimate)) {
                            return 0;
                        }
                    }
                }
                if((float)failed >= total * bigEncodingFuzz)
                    return 0;
                else
                    return 1;
            }
        }
    }
    return 0;
}

static int
find_cmap(int type, int pid, int eid, FT_Face face)
{
    int i, n, rc;
    FT_CharMap cmap = NULL;

    n = face->num_charmaps;

    switch(type) {
    case FONT_ENCODING_TRUETYPE:  /* specific cmap */
        for(i=0; i<n; i++) {
            cmap = face->charmaps[i];
            if(cmap->platform_id == pid && cmap->encoding_id == eid) {
                rc = FT_Set_Charmap(face, cmap);
                if(rc == 0)
                    return 1;
            }
        }
        break;
    case FONT_ENCODING_UNICODE:   /* any Unicode cmap */
        /* prefer Microsoft Unicode */
        for(i=0; i<n; i++) {
            cmap = face->charmaps[i];
            if(cmap->platform_id == TT_PLATFORM_MICROSOFT &&
               cmap->encoding_id == TT_MS_ID_UNICODE_CS) {
                rc = FT_Set_Charmap(face, cmap);
                if(rc == 0)
                    return 1;
            }
        }
        /* Try Apple Unicode */
        for(i=0; i<n; i++) {
            cmap = face->charmaps[i];
            if(cmap->platform_id == TT_PLATFORM_APPLE_UNICODE) {
                rc = FT_Set_Charmap(face, cmap);
                if(rc == 0)
                    return 1;
            }
        }
        /* ISO Unicode? */
        for(i=0; i<n; i++) {
            cmap = face->charmaps[i];
            if(cmap->platform_id == TT_PLATFORM_ISO) {
                rc = FT_Set_Charmap(face, cmap);
                if(rc == 0)
                    return 1;
            }
        }
        break;
    default:
        return 0;
    }
    return 0;
}

static int
checkExtraEncoding(FT_Face face, char *encoding_name, int found)
{
    int c;

    if(strcasecmp(encoding_name, "iso10646-1") == 0) {
        if(doISO10646_1_encoding && find_cmap(FONT_ENCODING_UNICODE, -1, -1, face)) {
            int found = 0;
             /* Export as Unicode if there are at least 15 BMP
               characters that are not a space or ignored. */
            for(c = 0x21; c < 0x10000; c++) {
                if(CODE_IGNORED(c))
                    continue;
                if(FT_Get_Char_Index(face, c) > 0)
                    found++;
                if(found >= 15)
                    return 1;
            }
            return 0;
        } else
            return 0;
    } else if(strcasecmp(encoding_name, "microsoft-symbol") == 0) {
        if(find_cmap(FONT_ENCODING_TRUETYPE,
                     TT_PLATFORM_MICROSOFT, TT_MS_ID_SYMBOL_CS,
                     face))
            return 1;
        else
            return 0;
    } else if(strcasecmp(encoding_name, "adobe-fontspecific") == 0) {
        if(!found) {
            if(FT_Has_PS_Glyph_Names(face))
                return 1;
            else
                return 0;
        } else
            return 0;
    } else {
        fprintf(stderr, "Unknown extra encoding %s\n", encoding_name);
        return 0;
    }
}

static const char*
notice_foundry(const char *notice)
{
    int i;
    for(i = 0; i < countof(notice_foundries); i++)
        if(notice && strstr(notice, notice_foundries[i][0]))
            return notice_foundries[i][1];
    return NULL;
}

static int
vendor_match(const signed char *vendor, const char *vendor_string)
{
    /* vendor is not necessarily NUL-terminated. */
    int i, len;
    len = strlen(vendor_string);
    if(memcmp(vendor, vendor_string, len) != 0)
        return 0;
    for(i = len; i < 4; i++)
        if(vendor[i] != ' ' && vendor[i] != '\0')
            return 0;
    return 1;
}

static const char*
vendor_foundry(const signed char *vendor)
{
    int i;
    for(i = 0; i < countof(vendor_foundries); i++)
        if(vendor_match(vendor, vendor_foundries[i][0]))
            return vendor_foundries[i][1];
    return NULL;
}

static int
readEncodings(ListPtr encodings, char *dirname)
{
    char *fullname;
    DIR *dirp;
    struct dirent *file;
    char **names, **name;

    if(strlen(dirname) > 1 && dirname[strlen(dirname) - 1] == '/')
        dirname[strlen(dirname) - 1] = '\0';

    dirp = opendir(dirname);
    if(dirp == NULL) {
        perror("opendir");
        return -1;
    }

    while((file = readdir(dirp)) != NULL) {
        fullname = dsprintf("%s/%s", dirname, file->d_name);
        if(fullname == NULL) {
            fprintf(stderr, "Couldn't allocate fullname\n");
            closedir(dirp);
            return -1;
        }

        names = FontEncIdentify(fullname);
        if(!names)
            continue;

        for(name = names; *name; name++) {
            if(fullname[0] != '/' && !relative) {
                char *n;
                n = dsprintf("%s%s", encodingPrefix, fullname);
                if(n == NULL) {
                    fprintf(stderr, "Couldn't allocate name\n");
                    closedir(dirp);
                    return -1;
                }
                encodingsToDo = listConsF(encodingsToDo, "%s %s", *name, n);
                free(n);
            } else {
                encodingsToDo =
                    listConsF(encodingsToDo, "%s %s", *name, fullname);
            }
            if(encodingsToDo == NULL) {
                fprintf(stderr, "Couldn't allocate encodings\n");
                closedir(dirp);
                return -1;
            }
        }
        free(names);            /* only the spine */
    }
    closedir(dirp);
    return 0;
}
