/*
 * Copyright 2002 Red Hat Inc., Durham, North Carolina.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL RED HAT AND/OR THEIR SUPPLIERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Authors:
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *
 */

/** \file
 * Interface to DMX configuration file parser.  \see dmxparse.c */

#ifndef _DMXPARSE_H_
#define _DMXPARSE_H_

#include <stdio.h>              /* For FILE */

/** Stores tokens not stored in other structures (e.g., keywords and ;) */
typedef struct _DMXConfigToken {
    int token;
    int line;
    const char *comment;
} DMXConfigToken, *DMXConfigTokenPtr;

/** Stores parsed strings. */
typedef struct _DMXConfigString {
    int token;
    int line;
    const char *comment;
    const char *string;
    struct _DMXConfigString *next;
} DMXConfigString, *DMXConfigStringPtr;

/** Stores parsed numbers. */
typedef struct _DMXConfigNumber {
    int token;
    int line;
    const char *comment;
    int number;
} DMXConfigNumber, *DMXConfigNumberPtr;

/** Stores parsed pairs (e.g., x y) */
typedef struct _DMXConfigPair {
    int token;
    int line;
    const char *comment;
    int x;
    int y;
    int xsign;
    int ysign;
} DMXConfigPair, *DMXConfigPairPtr;

/** Stores parsed comments not stored with a token. */
typedef struct _DMXConfigComment {
    int token;
    int line;
    const char *comment;
} DMXConfigComment, *DMXConfigCommentPtr;

typedef enum {
    dmxConfigComment,
    dmxConfigVirtual,
    dmxConfigDisplay,
    dmxConfigWall,
    dmxConfigOption,
    dmxConfigParam
} DMXConfigType;

/** Stores a geometry specification. */
typedef struct _DMXConfigPartDim {
    DMXConfigPairPtr dim;
    DMXConfigPairPtr offset;
} DMXConfigPartDim, *DMXConfigPartDimPtr;

/** Stores a pair of geometry specifications. */
typedef struct _DMXConfigFullDim {
    DMXConfigPartDimPtr scrn;
    DMXConfigPartDimPtr root;
} DMXConfigFullDim, *DMXConfigFullDimPtr;

/** Stores parsed display information. */
typedef struct _DMXConfigDisplay {
    /* Summary information */
    const char *name;
    /* Screen Window Geometry */
    int scrnWidth, scrnHeight;
    int scrnX, scrnY;
    int scrnXSign, scrnYSign;
    /* Root Window Geometry */
    int rootWidth, rootHeight;
    int rootX, rootY;
    int rootXSign, rootYSign;
    /* Origin in global space */
    int rootXOrigin, rootYOrigin;

    /* Raw configuration information */
    DMXConfigTokenPtr start;
    DMXConfigStringPtr dname;
    DMXConfigFullDimPtr dim;
    DMXConfigPairPtr origin;
    DMXConfigTokenPtr end;
} DMXConfigDisplay, *DMXConfigDisplayPtr;

/** Stores parsed wall information. */
typedef struct _DMXConfigWall {
    /* Summary information */
    int width, height;          /* dimensions of displays */
    int xwall, ywall;           /* dimensions of wall, in tiles */

    /* Raw configuration informaiton */
    DMXConfigTokenPtr start;
    DMXConfigPairPtr wallDim;
    DMXConfigPairPtr displayDim;
    DMXConfigStringPtr nameList;
    DMXConfigTokenPtr end;
} DMXConfigWall, *DMXConfigWallPtr;

/** Stores parsed option information. */
typedef struct _DMXConfigOption {
    /* Summary information */
    char *string;

    /* Raw configuration informaiton */
    DMXConfigTokenPtr start;
    DMXConfigStringPtr option;
    DMXConfigTokenPtr end;
} DMXConfigOption, *DMXConfigOptionPtr;

/** Stores parsed param information. */
typedef struct _DMXConfigParam {
    int argc;
    const char **argv;

    DMXConfigTokenPtr start;
    DMXConfigTokenPtr open;
    DMXConfigStringPtr param;
    DMXConfigTokenPtr close;
    DMXConfigTokenPtr end;      /* Either open/close OR end */
    struct _DMXConfigParam *next;
} DMXConfigParam, *DMXConfigParamPtr;

/** Stores options under an entry (subentry). */
typedef struct _DMXConfigSub {
    DMXConfigType type;
    DMXConfigCommentPtr comment;
    DMXConfigDisplayPtr display;
    DMXConfigWallPtr wall;
    DMXConfigOptionPtr option;
    DMXConfigParamPtr param;
    struct _DMXConfigSub *next;
} DMXConfigSub, *DMXConfigSubPtr;

/** Stores parsed virtual information. */
typedef struct _DMXConfigVirtual {
    /* Summary information */
    const char *name;
    int width, height;

    /* Raw configuration information */
    DMXConfigTokenPtr start;
    DMXConfigStringPtr vname;
    DMXConfigPairPtr dim;
    DMXConfigTokenPtr open;
    DMXConfigSubPtr subentry;
    DMXConfigTokenPtr close;
} DMXConfigVirtual, *DMXConfigVirtualPtr;

/** Heads entry storage. */
typedef struct _DMXConfigEntry {
    DMXConfigType type;
    DMXConfigCommentPtr comment;
    DMXConfigVirtualPtr virtual;
    struct _DMXConfigEntry *next;
} DMXConfigEntry, *DMXConfigEntryPtr;

extern DMXConfigEntryPtr dmxConfigEntry;

extern int yylex(void);
extern int yydebug;
extern void yyerror(const char *message);

extern void dmxConfigLog(const char *format, ...);
extern void *dmxConfigAlloc(unsigned long bytes);
extern void *dmxConfigRealloc(void *orig,
                              unsigned long orig_bytes, unsigned long bytes);
extern const char *dmxConfigCopyString(const char *string, int length);
extern void dmxConfigFree(void *area);
extern DMXConfigTokenPtr dmxConfigCreateToken(int token, int line,
                                              const char *comment);
extern void dmxConfigFreeToken(DMXConfigTokenPtr p);
extern DMXConfigStringPtr dmxConfigCreateString(int token, int line,
                                                const char *comment,
                                                const char *string);
extern void dmxConfigFreeString(DMXConfigStringPtr p);
extern DMXConfigNumberPtr dmxConfigCreateNumber(int token, int line,
                                                const char *comment,
                                                int number);
extern void dmxConfigFreeNumber(DMXConfigNumberPtr p);
extern DMXConfigPairPtr dmxConfigCreatePair(int token, int line,
                                            const char *comment,
                                            int x, int y, int xsign, int ysign);
extern void dmxConfigFreePair(DMXConfigPairPtr p);
extern DMXConfigCommentPtr dmxConfigCreateComment(int token, int line,
                                                  const char *comment);
extern void dmxConfigFreeComment(DMXConfigCommentPtr p);
extern DMXConfigPartDimPtr dmxConfigCreatePartDim(DMXConfigPairPtr pDim,
                                                  DMXConfigPairPtr pOffset);
extern void dmxConfigFreePartDim(DMXConfigPartDimPtr p);
extern DMXConfigFullDimPtr dmxConfigCreateFullDim(DMXConfigPartDimPtr pScrn,
                                                  DMXConfigPartDimPtr pRoot);
extern void dmxConfigFreeFullDim(DMXConfigFullDimPtr p);
extern DMXConfigDisplayPtr dmxConfigCreateDisplay(DMXConfigTokenPtr pStart,
                                                  DMXConfigStringPtr pName,
                                                  DMXConfigFullDimPtr pDim,
                                                  DMXConfigPairPtr pOrigin,
                                                  DMXConfigTokenPtr pEnd);
extern void dmxConfigFreeDisplay(DMXConfigDisplayPtr p);
extern DMXConfigWallPtr dmxConfigCreateWall(DMXConfigTokenPtr pStart,
                                            DMXConfigPairPtr pWallDim,
                                            DMXConfigPairPtr pDisplayDim,
                                            DMXConfigStringPtr pNameList,
                                            DMXConfigTokenPtr pEnd);
extern void dmxConfigFreeWall(DMXConfigWallPtr p);
extern DMXConfigOptionPtr dmxConfigCreateOption(DMXConfigTokenPtr pStart,
                                                DMXConfigStringPtr pOption,
                                                DMXConfigTokenPtr pEnd);
extern void dmxConfigFreeOption(DMXConfigOptionPtr p);
extern DMXConfigParamPtr dmxConfigCreateParam(DMXConfigTokenPtr pStart,
                                              DMXConfigTokenPtr pOpen,
                                              DMXConfigStringPtr pParam,
                                              DMXConfigTokenPtr pClose,
                                              DMXConfigTokenPtr pEnd);
extern void dmxConfigFreeParam(DMXConfigParamPtr p);
extern const char **dmxConfigLookupParam(DMXConfigParamPtr p,
                                         const char *key, int *argc);
extern DMXConfigSubPtr dmxConfigCreateSub(DMXConfigType type,
                                          DMXConfigCommentPtr comment,
                                          DMXConfigDisplayPtr display,
                                          DMXConfigWallPtr wall,
                                          DMXConfigOptionPtr option,
                                          DMXConfigParamPtr param);
extern void dmxConfigFreeSub(DMXConfigSubPtr sub);
extern DMXConfigSubPtr dmxConfigSubComment(DMXConfigCommentPtr comment);
extern DMXConfigSubPtr dmxConfigSubDisplay(DMXConfigDisplayPtr display);
extern DMXConfigSubPtr dmxConfigSubWall(DMXConfigWallPtr wall);
extern DMXConfigSubPtr dmxConfigSubOption(DMXConfigOptionPtr option);
extern DMXConfigSubPtr dmxConfigSubParam(DMXConfigParamPtr param);
extern DMXConfigSubPtr dmxConfigAddSub(DMXConfigSubPtr head,
                                       DMXConfigSubPtr sub);
extern DMXConfigVirtualPtr dmxConfigCreateVirtual(DMXConfigTokenPtr pStart,
                                                  DMXConfigStringPtr pName,
                                                  DMXConfigPairPtr pDim,
                                                  DMXConfigTokenPtr pOpen,
                                                  DMXConfigSubPtr pSubentry,
                                                  DMXConfigTokenPtr pClose);
extern void dmxConfigFreeVirtual(DMXConfigVirtualPtr virtual);
extern DMXConfigEntryPtr dmxConfigCreateEntry(DMXConfigType type,
                                              DMXConfigCommentPtr comment,
                                              DMXConfigVirtualPtr virtual);
extern void dmxConfigFreeEntry(DMXConfigEntryPtr entry);
extern DMXConfigEntryPtr dmxConfigAddEntry(DMXConfigEntryPtr head,
                                           DMXConfigType type,
                                           DMXConfigCommentPtr comment,
                                           DMXConfigVirtualPtr virtual);
extern DMXConfigEntryPtr dmxConfigEntryComment(DMXConfigCommentPtr comment);
extern DMXConfigEntryPtr dmxConfigEntryVirtual(DMXConfigVirtualPtr virtual);

#endif
