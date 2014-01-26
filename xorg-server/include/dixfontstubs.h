#ifndef DIXFONTSTUBS_H
#define DIXFONTSTUBS_H 1

/*
 * libXfont stubs replacements
 * This header exists solely for the purpose of sdksyms generation;
 * source code should #include "dixfonts.h" instead, which pulls in these
 * declarations from <X11/fonts/fontproto.h>
 */
extern _X_EXPORT int client_auth_generation(ClientPtr client);

extern _X_EXPORT void DeleteFontClientID(Font id);

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

#endif
