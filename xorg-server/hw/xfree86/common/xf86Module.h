/*
 * Copyright (c) 1997-2003 by The XFree86 Project, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the copyright holder(s)
 * and author(s) shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from the copyright holder(s) and author(s).
 */

/*
 * This file contains the parts of the loader interface that are visible
 * to modules.  This is the only loader-related header that modules should
 * include.
 *
 * It should include a bare minimum of other headers.
 *
 * Longer term, the module/loader code should probably live directly under
 * Xserver/.
 *
 * XXX This file arguably belongs in xfree86/loader/.
 */

#ifndef _XF86MODULE_H
#define _XF86MODULE_H

#include "misc.h"
#include "extension.h"
#ifndef NULL
#define NULL ((void *)0)
#endif

typedef enum {
    LD_RESOLV_IFDONE = 0,       /* only check if no more
                                   delays pending */
    LD_RESOLV_NOW = 1,          /* finish one delay step */
    LD_RESOLV_FORCE = 2         /* force checking... */
} LoaderResolveOptions;

#define DEFAULT_LIST ((char *)-1)

/* Built-in ABI classes.  These definitions must not be changed. */
#define ABI_CLASS_NONE		NULL
#define ABI_CLASS_ANSIC		"X.Org ANSI C Emulation"
#define ABI_CLASS_VIDEODRV	"X.Org Video Driver"
#define ABI_CLASS_XINPUT	"X.Org XInput driver"
#define ABI_CLASS_EXTENSION	"X.Org Server Extension"
#define ABI_CLASS_FONT		"X.Org Font Renderer"

#define ABI_MINOR_MASK		0x0000FFFF
#define ABI_MAJOR_MASK		0xFFFF0000
#define GET_ABI_MINOR(v)	((v) & ABI_MINOR_MASK)
#define GET_ABI_MAJOR(v)	(((v) & ABI_MAJOR_MASK) >> 16)
#define SET_ABI_VERSION(maj, min) \
		((((maj) << 16) & ABI_MAJOR_MASK) | ((min) & ABI_MINOR_MASK))

/*
 * ABI versions.  Each version has a major and minor revision.  Modules
 * using lower minor revisions must work with servers of a higher minor
 * revision.  There is no compatibility between different major revisions.
 * Whenever the ABI_ANSIC_VERSION is changed, the others must also be
 * changed.  The minor revision mask is 0x0000FFFF and the major revision
 * mask is 0xFFFF0000.
 */
#define ABI_ANSIC_VERSION	SET_ABI_VERSION(0, 4)
#define ABI_VIDEODRV_VERSION	SET_ABI_VERSION(15, 0)
#define ABI_XINPUT_VERSION	SET_ABI_VERSION(20, 0)
#define ABI_EXTENSION_VERSION	SET_ABI_VERSION(8, 0)
#define ABI_FONT_VERSION	SET_ABI_VERSION(0, 6)

#define MODINFOSTRING1	0xef23fdc5
#define MODINFOSTRING2	0x10dc023a

#ifndef MODULEVENDORSTRING
#define MODULEVENDORSTRING	"X.Org Foundation"
#endif

/* Error return codes for errmaj.  New codes must only be added at the end. */
typedef enum {
    LDR_NOERROR = 0,
    LDR_NOMEM,                  /* memory allocation failed */
    LDR_NOENT,                  /* Module file does not exist */
    LDR_NOSUBENT,               /* pre-requsite file to be sub-loaded does not exist */
    LDR_NOSPACE,                /* internal module array full */
    LDR_NOMODOPEN,              /* module file could not be opened (check errmin) */
    LDR_UNKTYPE,                /* file is not a recognized module type */
    LDR_NOLOAD,                 /* type specific loader failed */
    LDR_ONCEONLY,               /* Module should only be loaded once (not an error) */
    LDR_NOPORTOPEN,             /* could not open port (check errmin) */
    LDR_NOHARDWARE,             /* could not query/initialize the hardware device */
    LDR_MISMATCH,               /* the module didn't match the spec'd requirments */
    LDR_BADUSAGE,               /* LoadModule is called with bad arguments */
    LDR_INVALID,                /* The module doesn't have a valid ModuleData object */
    LDR_BADOS,                  /* The module doesn't support the OS */
    LDR_MODSPECIFIC             /* A module-specific error in the SetupProc */
} LoaderErrorCode;

/*
 * Some common module classes.  The moduleclass can be used to identify
 * that modules loaded are of the correct type.  This is a finer
 * classification than the ABI classes even though the default set of
 * classes have the same names.  For example, not all modules that require
 * the video driver ABI are themselves video drivers.
 */
#define MOD_CLASS_NONE		NULL
#define MOD_CLASS_VIDEODRV	"X.Org Video Driver"
#define MOD_CLASS_XINPUT	"X.Org XInput Driver"
#define MOD_CLASS_FONT		"X.Org Font Renderer"
#define MOD_CLASS_EXTENSION	"X.Org Server Extension"

/* This structure is expected to be returned by the initfunc */
typedef struct {
    const char *modname;        /* name of module, e.g. "foo" */
    const char *vendor;         /* vendor specific string */
    CARD32 _modinfo1_;          /* constant MODINFOSTRING1/2 to find */
    CARD32 _modinfo2_;          /* infoarea with a binary editor or sign tool */
    CARD32 xf86version;         /* contains XF86_VERSION_CURRENT */
    CARD8 majorversion;         /* module-specific major version */
    CARD8 minorversion;         /* module-specific minor version */
    CARD16 patchlevel;          /* module-specific patch level */
    const char *abiclass;       /* ABI class that the module uses */
    CARD32 abiversion;          /* ABI version */
    const char *moduleclass;    /* module class description */
    CARD32 checksum[4];         /* contains a digital signature of the */
    /* version info structure */
} XF86ModuleVersionInfo;

/*
 * This structure can be used to callers of LoadModule and LoadSubModule to
 * specify version and/or ABI requirements.
 */
typedef struct {
    CARD8 majorversion;         /* module-specific major version */
    CARD8 minorversion;         /* moudle-specific minor version */
    CARD16 patchlevel;          /* module-specific patch level */
    const char *abiclass;       /* ABI class that the module uses */
    CARD32 abiversion;          /* ABI version */
    const char *moduleclass;    /* module class */
} XF86ModReqInfo;

/* values to indicate unspecified fields in XF86ModReqInfo. */
#define MAJOR_UNSPEC		0xFF
#define MINOR_UNSPEC		0xFF
#define PATCH_UNSPEC		0xFFFF
#define ABI_VERS_UNSPEC		0xFFFFFFFF

#define MODULE_VERSION_NUMERIC(maj, min, patch) \
	((((maj) & 0xFF) << 24) | (((min) & 0xFF) << 16) | (patch & 0xFFFF))
#define GET_MODULE_MAJOR_VERSION(vers)	(((vers) >> 24) & 0xFF)
#define GET_MODULE_MINOR_VERSION(vers)	(((vers) >> 16) & 0xFF)
#define GET_MODULE_PATCHLEVEL(vers)	((vers) & 0xFFFF)

#define INITARGS void

/* Prototypes for Loader functions that are exported to modules */
extern _X_EXPORT pointer LoadSubModule(pointer, const char *, const char **,
                                       const char **, pointer,
                                       const XF86ModReqInfo *, int *, int *);
extern _X_EXPORT void UnloadSubModule(pointer);
extern _X_EXPORT void UnloadModule(pointer);
extern _X_EXPORT pointer LoaderSymbol(const char *);
extern _X_EXPORT char **LoaderListDirs(const char **, const char **);
extern _X_EXPORT void LoaderFreeDirList(char **);
extern _X_EXPORT void LoaderErrorMsg(const char *, const char *, int, int);
extern _X_EXPORT void LoaderGetOS(const char **name, int *major, int *minor,
                                  int *teeny);
extern _X_EXPORT Bool LoaderShouldIgnoreABI(void);
extern _X_EXPORT int LoaderGetABIVersion(const char *abiclass);

typedef pointer (*ModuleSetupProc) (pointer, pointer, int *, int *);
typedef void (*ModuleTearDownProc) (pointer);

#define MODULESETUPPROTO(func) pointer func(pointer, pointer, int*, int*)
#define MODULETEARDOWNPROTO(func) void func(pointer)

typedef struct {
    XF86ModuleVersionInfo *vers;
    ModuleSetupProc setup;
    ModuleTearDownProc teardown;
} XF86ModuleData;

#endif                          /* _XF86STR_H */
