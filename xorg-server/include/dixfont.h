/***********************************************************
Copyright 1987 by Digital Equipment Corporation, Maynard, Massachusetts.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the name of Digital not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

******************************************************************/

#ifndef DIXFONT_H
#define DIXFONT_H 1

#include "dix.h"
#include <X11/fonts/font.h>
#include "closure.h"
#include <X11/fonts/fontstruct.h>

#define NullDIXFontProp ((DIXFontPropPtr)0)

typedef struct _DIXFontProp *DIXFontPropPtr;

extern _X_EXPORT Bool SetDefaultFont(char * /*defaultfontname */ );

extern _X_EXPORT void QueueFontWakeup(FontPathElementPtr /*fpe */ );

extern _X_EXPORT void RemoveFontWakeup(FontPathElementPtr /*fpe */ );

extern _X_EXPORT void FontWakeup(pointer /*data */ ,
                                 int /*count */ ,
                                 pointer /*LastSelectMask */ );

extern _X_EXPORT int OpenFont(ClientPtr /*client */ ,
                              XID /*fid */ ,
                              Mask /*flags */ ,
                              unsigned /*lenfname */ ,
                              char * /*pfontname */ );

extern _X_EXPORT int CloseFont(pointer /*pfont */ ,
                               XID /*fid */ );

typedef struct _xQueryFontReply *xQueryFontReplyPtr;

extern _X_EXPORT void QueryFont(FontPtr /*pFont */ ,
                                xQueryFontReplyPtr /*pReply */ ,
                                int /*nProtoCCIStructs */ );

extern _X_EXPORT int ListFonts(ClientPtr /*client */ ,
                               unsigned char * /*pattern */ ,
                               unsigned int /*length */ ,
                               unsigned int /*max_names */ );

extern _X_EXPORT int
 doListFontsWithInfo(ClientPtr /*client */ ,
                     LFWIclosurePtr /*c */ );

extern _X_EXPORT int doPolyText(ClientPtr /*client */ ,
                                PTclosurePtr    /*c */
    );

extern _X_EXPORT int PolyText(ClientPtr /*client */ ,
                              DrawablePtr /*pDraw */ ,
                              GCPtr /*pGC */ ,
                              unsigned char * /*pElt */ ,
                              unsigned char * /*endReq */ ,
                              int /*xorg */ ,
                              int /*yorg */ ,
                              int /*reqType */ ,
                              XID /*did */ );

extern _X_EXPORT int doImageText(ClientPtr /*client */ ,
                                 ITclosurePtr /*c */ );

extern _X_EXPORT int ImageText(ClientPtr /*client */ ,
                               DrawablePtr /*pDraw */ ,
                               GCPtr /*pGC */ ,
                               int /*nChars */ ,
                               unsigned char * /*data */ ,
                               int /*xorg */ ,
                               int /*yorg */ ,
                               int /*reqType */ ,
                               XID /*did */ );

extern _X_EXPORT int SetFontPath(ClientPtr /*client */ ,
                                 int /*npaths */ ,
                                 unsigned char * /*paths */ );

extern _X_EXPORT int SetDefaultFontPath(char * /*path */ );

extern _X_EXPORT int GetFontPath(ClientPtr client,
                                 int *count,
                                 int *length, unsigned char **result);

extern _X_EXPORT void DeleteClientFontStuff(ClientPtr /*client */ );

/* Quartz support on Mac OS X pulls in the QuickDraw
   framework whose InitFonts function conflicts here. */
#ifdef __APPLE__
#define InitFonts Darwin_X_InitFonts
#endif
extern _X_EXPORT void InitFonts(void);

extern _X_EXPORT void FreeFonts(void);

extern _X_EXPORT FontPtr find_old_font(XID /*id */ );

#define GetGlyphs dixGetGlyphs
extern _X_EXPORT void dixGetGlyphs(FontPtr /*font */ ,
                                   unsigned long /*count */ ,
                                   unsigned char * /*chars */ ,
                                   FontEncoding /*fontEncoding */ ,
                                   unsigned long * /*glyphcount */ ,
                                   CharInfoPtr * /*glyphs */ );

extern _X_EXPORT void QueryGlyphExtents(FontPtr /*pFont */ ,
                                        CharInfoPtr * /*charinfo */ ,
                                        unsigned long /*count */ ,
                                        ExtentInfoPtr /*info */ );

extern _X_EXPORT Bool QueryTextExtents(FontPtr /*pFont */ ,
                                       unsigned long /*count */ ,
                                       unsigned char * /*chars */ ,
                                       ExtentInfoPtr /*info */ );

extern _X_EXPORT Bool ParseGlyphCachingMode(char * /*str */ );

extern _X_EXPORT void InitGlyphCaching(void);

extern _X_EXPORT void SetGlyphCachingMode(int /*newmode */ );

extern _X_EXPORT void register_fpe_functions(void);

/*
 * libXfont stubs.
 */
extern _X_EXPORT int client_auth_generation(ClientPtr client);

extern _X_EXPORT void DeleteFontClientID(Font id);

extern _X_EXPORT FontResolutionPtr GetClientResolutions(int *num);

extern _X_EXPORT int GetDefaultPointSize(void);

extern _X_EXPORT Font GetNewFontClientID(void);

extern _X_EXPORT int init_fs_handlers(FontPathElementPtr fpe,
                                      BlockHandlerProcPtr block_handler);

extern _X_EXPORT int RegisterFPEFunctions(NameCheckFunc name_func,
                                          InitFpeFunc init_func,
                                          FreeFpeFunc free_func,
                                          ResetFpeFunc reset_func,
                                          OpenFontFunc open_func,
                                          CloseFontFunc close_func,
                                          ListFontsFunc list_func,
                                          StartLfwiFunc start_lfwi_func,
                                          NextLfwiFunc next_lfwi_func,
                                          WakeupFpeFunc wakeup_func,
                                          ClientDiedFunc client_died,
                                          LoadGlyphsFunc load_glyphs,
                                          StartLaFunc start_list_alias_func,
                                          NextLaFunc next_list_alias_func,
                                          SetPathFunc set_path_func);

extern _X_EXPORT void remove_fs_handlers(FontPathElementPtr fpe,
                                         BlockHandlerProcPtr blockHandler,
                                         Bool all);

extern _X_EXPORT int StoreFontClientFont(FontPtr pfont, Font id);

#endif                          /* DIXFONT_H */
