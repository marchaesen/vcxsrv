#ifndef __GLHEADER_H__
#define __GLHEADER_H__

#define STDC_HEADERS 1

#include <X11/Xwinsock.h>
#include <X11/Xwindows.h>
#include <assert.h>
#define strcasecmp _stricmp

#undef MINSHORT
#undef MAXSHORT

#define MINSHORT -32768
#define MAXSHORT 32767

#ifndef PUBLIC
#define PUBLIC
#endif

#define GL_GLEXT_PROTOTYPES
#ifdef __cplusplus
extern "C" {
#endif


/**
 * GL_FIXED is defined in glext.h version 64 but these typedefs aren't (yet).
 */
typedef int GLclampx;

#ifdef __cplusplus
}
#endif

#endif

