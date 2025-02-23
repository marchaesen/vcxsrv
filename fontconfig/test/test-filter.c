#include <fontconfig/fontconfig.h>

#include <stdio.h>
#include <time.h>

static FcBool
filter (const FcPattern *f, void *user_data)
{
    FcChar8 *s = NULL;

    if (FcPatternGetString (f, FC_FONT_WRAPPER, 0, &s) == FcResultMatch) {
	/* accept "SFNT" only */
	if (FcStrCmp (s, (FcChar8 *)"SFNT") == 0)
	    return FcTrue;
    }
    return FcFalse;
}

int
main (void)
{
    FcPattern   *p;
    FcObjectSet *os;
    FcFontSet   *fs;
    int          i, ret = 0;
    FcChar8     *s = NULL, *f;

    FcConfigSetFontSetFilter (NULL, filter, NULL, NULL);
    p = FcPatternCreate();
    os = FcObjectSetBuild (FC_FAMILY, FC_STYLE, FC_FILE, FC_FONT_WRAPPER, NULL);
    fs = FcFontList (NULL, p, os);
    FcObjectSetDestroy (os);
    FcPatternDestroy (p);

    printf ("%d matched\n", fs->nfont);
    for (i = 0; i < fs->nfont; i++) {
	if (FcPatternGetString (fs->fonts[i], FC_FONT_WRAPPER, 0, &s) == FcResultMatch) {
	    f = FcPatternFormat (fs->fonts[i], (FcChar8 *)"%{=fclist}\n");
	    printf ("%s", f);
	    FcStrFree (f);
	    if (FcStrCmp (s, (FcChar8 *)"SFNT") != 0) {
		printf ("failed:\n");
	    fail:
		ret = 1;
	    }
	} else {
	    printf ("no font wrapper\n");
	    goto fail;
	}
    }
    FcFontSetDestroy (fs);

    return ret;
}
