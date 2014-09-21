#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "stubs.h"

#ifdef __SUNPRO_C
#pragma weak RegisterFPEFunctions
#endif

weak int
RegisterFPEFunctions(NameCheckFunc name_func,
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
		     SetPathFunc set_path_func)
{
    OVERRIDE_SYMBOL(RegisterFPEFunctions, name_func, init_func, free_func,
                    reset_func, open_func, close_func, list_func, start_lfwi_func,
                    next_lfwi_func, wakeup_func, client_died, load_glyphs,
                    start_list_alias_func, next_list_alias_func, set_path_func);
    return 0;
}
