/*

Copyright 1993, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall
not be used in advertising or otherwise to promote the sale, use or
other dealings in this Software without prior written authorization
from The Open Group.

*/
/*
 * Copyright © 2010, Keith Packard
 * Copyright © 2010, Jamey Sharp
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <stddef.h>
#include "windowstr.h"
#include "resource.h"
#include "privates.h"
#include "gcstruct.h"
#include "cursorstr.h"
#include "colormapst.h"
#include "inputstr.h"
#include "scrnintstr.h"
#include "extnsionst.h"

static struct {
    DevPrivateKey key;
    unsigned offset;
    int created;
    int allocated;
} keys[PRIVATE_LAST];

static const Bool xselinux_private[PRIVATE_LAST] = {
    [PRIVATE_SCREEN] = TRUE,
    [PRIVATE_CLIENT] = TRUE,
    [PRIVATE_WINDOW] = TRUE,
    [PRIVATE_PIXMAP] = TRUE,
    [PRIVATE_GC] = TRUE,
    [PRIVATE_CURSOR] = TRUE,
    [PRIVATE_COLORMAP] = TRUE,
    [PRIVATE_DEVICE] = TRUE,
    [PRIVATE_EXTENSION] = TRUE,
    [PRIVATE_SELECTION] = TRUE,
    [PRIVATE_PROPERTY] = TRUE,
    [PRIVATE_PICTURE] = TRUE,
    [PRIVATE_GLYPHSET] = TRUE,
};

typedef Bool (*FixupFunc) (PrivatePtr *privates, int offset, unsigned bytes);

static Bool
dixReallocPrivates(PrivatePtr *privates, int old_offset, unsigned bytes)
{
    void *new_privates;

    new_privates = realloc(*privates, old_offset + bytes);
    if (!new_privates)
        return FALSE;
    memset((char *) new_privates + old_offset, '\0', bytes);
    *privates = new_privates;
    return TRUE;
}

static Bool
dixMovePrivates(PrivatePtr *privates, int new_offset, unsigned bytes)
{
    memmove((char *) *privates + bytes, *privates, new_offset - bytes);
    memset(*privates, '\0', bytes);
    return TRUE;
}

static Bool
fixupScreens(FixupFunc fixup, unsigned bytes)
{
    int s;

    for (s = 0; s < screenInfo.numScreens; s++)
        if (!fixup
            (&screenInfo.screens[s]->devPrivates, keys[PRIVATE_SCREEN].offset,
             bytes))
            return FALSE;
    return TRUE;
}

static Bool
fixupServerClient(FixupFunc fixup, unsigned bytes)
{
    if (serverClient)
        return fixup(&serverClient->devPrivates, keys[PRIVATE_CLIENT].offset,
                     bytes);
    return TRUE;
}

static Bool
fixupExtensions(FixupFunc fixup, unsigned bytes)
{
    unsigned char major;
    ExtensionEntry *extension;

    for (major = EXTENSION_BASE; (extension = GetExtensionEntry(major));
         major++)
        if (!fixup
            (&extension->devPrivates, keys[PRIVATE_EXTENSION].offset, bytes))
            return FALSE;
    return TRUE;
}

static Bool
fixupDefaultColormaps(FixupFunc fixup, unsigned bytes)
{
    int s;

    for (s = 0; s < screenInfo.numScreens; s++) {
        ColormapPtr cmap;

        dixLookupResourceByType((pointer *) &cmap,
                                screenInfo.screens[s]->defColormap, RT_COLORMAP,
                                serverClient, DixCreateAccess);
        if (cmap &&
            !fixup(&cmap->devPrivates, keys[PRIVATE_COLORMAP].offset, bytes))
            return FALSE;
    }
    return TRUE;
}

static Bool (*const allocated_early[PRIVATE_LAST]) (FixupFunc, unsigned) = {
[PRIVATE_SCREEN] = fixupScreens,
        [PRIVATE_CLIENT] = fixupServerClient,
        [PRIVATE_EXTENSION] = fixupExtensions,
        [PRIVATE_COLORMAP] = fixupDefaultColormaps,};

/*
 * Register a private key. This takes the type of object the key will
 * be used with, which may be PRIVATE_ALL indicating that this key
 * will be used with all of the private objects. If 'size' is
 * non-zero, then the specified amount of space will be allocated in
 * the private storage. Otherwise, space for a single pointer will
 * be allocated which can be set with dixSetPrivate
 */
Bool
dixRegisterPrivateKey(DevPrivateKey key, DevPrivateType type, unsigned size)
{
    DevPrivateType t;
    int offset;
    unsigned bytes;

    if (key->initialized) {
        assert(size == key->size);
        return TRUE;
    }

    /* Compute required space */
    bytes = size;
    if (size == 0)
        bytes = sizeof(void *);

    /* align to void * size */
    bytes = (bytes + sizeof(void *) - 1) & ~(sizeof(void *) - 1);

    /* Update offsets for all affected keys */
    if (type == PRIVATE_XSELINUX) {
        DevPrivateKey k;

        /* Resize if we can, or make sure nothing's allocated if we can't
         */
        for (t = PRIVATE_XSELINUX; t < PRIVATE_LAST; t++)
            if (xselinux_private[t]) {
                if (!allocated_early[t])
                    assert(!keys[t].created);
                else if (!allocated_early[t] (dixReallocPrivates, bytes))
                    return FALSE;
            }

        /* Move all existing keys up in the privates space to make
         * room for this new global key
         */
        for (t = PRIVATE_XSELINUX; t < PRIVATE_LAST; t++) {
            if (xselinux_private[t]) {
                for (k = keys[t].key; k; k = k->next)
                    k->offset += bytes;
                keys[t].offset += bytes;
                if (allocated_early[t])
                    allocated_early[t] (dixMovePrivates, bytes);
            }
        }

        offset = 0;
    }
    else {
        /* Resize if we can, or make sure nothing's allocated if we can't */
        if (!allocated_early[type])
            assert(!keys[type].created);
        else if (!allocated_early[type] (dixReallocPrivates, bytes))
            return FALSE;
        offset = keys[type].offset;
        keys[type].offset += bytes;
    }

    /* Setup this key */
    key->offset = offset;
    key->size = size;
    key->initialized = TRUE;
    key->type = type;
    key->allocated = FALSE;
    key->next = keys[type].key;
    keys[type].key = key;

    return TRUE;
}

Bool
dixRegisterScreenPrivateKey(DevScreenPrivateKey screenKey, ScreenPtr pScreen,
                            DevPrivateType type, unsigned size)
{
    DevPrivateKey key;

    if (!dixRegisterPrivateKey(&screenKey->screenKey, PRIVATE_SCREEN, 0))
        return FALSE;
    key = dixGetPrivate(&pScreen->devPrivates, &screenKey->screenKey);
    if (key != NULL) {
        assert(key->size == size);
        assert(key->type == type);
        return TRUE;
    }
    key = calloc(sizeof(DevPrivateKeyRec), 1);
    if (!key)
        return FALSE;
    if (!dixRegisterPrivateKey(key, type, size)) {
        free(key);
        return FALSE;
    }
    key->allocated = TRUE;
    dixSetPrivate(&pScreen->devPrivates, &screenKey->screenKey, key);
    return TRUE;
}

DevPrivateKey
_dixGetScreenPrivateKey(const DevScreenPrivateKey key, ScreenPtr pScreen)
{
    return dixGetPrivate(&pScreen->devPrivates, &key->screenKey);
}

/*
 * Initialize privates by zeroing them
 */
void
_dixInitPrivates(PrivatePtr *privates, void *addr, DevPrivateType type)
{
    keys[type].created++;
    if (xselinux_private[type])
        keys[PRIVATE_XSELINUX].created++;
    if (keys[type].offset == 0)
        addr = 0;
    *privates = addr;
    memset(addr, '\0', keys[type].offset);
}

/*
 * Clean up privates
 */
void
_dixFiniPrivates(PrivatePtr privates, DevPrivateType type)
{
    keys[type].created--;
    if (xselinux_private[type])
        keys[PRIVATE_XSELINUX].created--;
}

/*
 * Allocate new object with privates.
 *
 * This is expected to be invoked from the
 * dixAllocateObjectWithPrivates macro
 */
void *
_dixAllocateObjectWithPrivates(unsigned baseSize, unsigned clear,
                               unsigned offset, DevPrivateType type)
{
    unsigned totalSize;
    void *object;
    PrivatePtr privates;
    PrivatePtr *devPrivates;

    assert(type > PRIVATE_SCREEN && type < PRIVATE_LAST);

    /* round up so that void * is aligned */
    baseSize = (baseSize + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
    totalSize = baseSize + keys[type].offset;
    object = malloc(totalSize);
    if (!object)
        return NULL;

    memset(object, '\0', clear);
    privates = (PrivatePtr) (((char *) object) + baseSize);
    devPrivates = (PrivatePtr *) ((char *) object + offset);

    _dixInitPrivates(devPrivates, privates, type);

    return object;
}

/*
 * Allocate privates separately from containing object.
 * Used for clients and screens.
 */
Bool
dixAllocatePrivates(PrivatePtr *privates, DevPrivateType type)
{
    unsigned size;
    PrivatePtr p;

    assert(type > PRIVATE_XSELINUX && type < PRIVATE_LAST);

    size = keys[type].offset;
    if (!size) {
        p = NULL;
    }
    else {
        if (!(p = malloc(size)))
            return FALSE;
    }

    _dixInitPrivates(privates, p, type);
    ++keys[type].allocated;

    return TRUE;
}

/*
 * Free an object that has privates
 *
 * This is expected to be invoked from the
 * dixFreeObjectWithPrivates macro
 */
void
_dixFreeObjectWithPrivates(void *object, PrivatePtr privates,
                           DevPrivateType type)
{
    _dixFiniPrivates(privates, type);
    free(object);
}

/*
 * Called to free screen or client privates
 */
void
dixFreePrivates(PrivatePtr privates, DevPrivateType type)
{
    _dixFiniPrivates(privates, type);
    --keys[type].allocated;
    free(privates);
}

/*
 * Return size of privates for the specified type
 */
extern _X_EXPORT int
dixPrivatesSize(DevPrivateType type)
{
    assert(type >= PRIVATE_SCREEN && type < PRIVATE_LAST);

    return keys[type].offset;
}

/* Table of devPrivates offsets */
static const int offsets[] = {
    -1,                         /* RT_NONE */
    offsetof(WindowRec, devPrivates),   /* RT_WINDOW */
    offsetof(PixmapRec, devPrivates),   /* RT_PIXMAP */
    offsetof(GC, devPrivates),  /* RT_GC */
    -1,                         /* RT_FONT */
    offsetof(CursorRec, devPrivates),   /* RT_CURSOR */
    offsetof(ColormapRec, devPrivates), /* RT_COLORMAP */
};

#define NUM_OFFSETS	(sizeof (offsets) / sizeof (offsets[0]))

int
dixLookupPrivateOffset(RESTYPE type)
{
    /*
     * Special kludge for DBE which registers a new resource type that
     * points at pixmaps (thanks, DBE)
     */
    if (type & RC_DRAWABLE) {
        if (type == RT_WINDOW)
            return offsets[RT_WINDOW & TypeMask];
        else
            return offsets[RT_PIXMAP & TypeMask];
    }
    type = type & TypeMask;
    if (type < NUM_OFFSETS)
        return offsets[type];
    return -1;
}

static const char *key_names[PRIVATE_LAST] = {
    /* XSELinux uses the same private keys for numerous objects */
    [PRIVATE_XSELINUX] = "XSELINUX",

    /* Otherwise, you get a private in just the requested structure
     */
    /* These can have objects created before all of the keys are registered */
    [PRIVATE_SCREEN] = "SCREEN",
    [PRIVATE_EXTENSION] = "EXTENSION",
    [PRIVATE_COLORMAP] = "COLORMAP",

    /* These cannot have any objects before all relevant keys are registered */
    [PRIVATE_DEVICE] = "DEVICE",
    [PRIVATE_CLIENT] = "CLIENT",
    [PRIVATE_PROPERTY] = "PROPERTY",
    [PRIVATE_SELECTION] = "SELECTION",
    [PRIVATE_WINDOW] = "WINDOW",
    [PRIVATE_PIXMAP] = "PIXMAP",
    [PRIVATE_GC] = "GC",
    [PRIVATE_CURSOR] = "CURSOR",
    [PRIVATE_CURSOR_BITS] = "CURSOR_BITS",

    /* extension privates */
    [PRIVATE_DBE_WINDOW] = "DBE_WINDOW",
    [PRIVATE_DAMAGE] = "DAMAGE",
    [PRIVATE_GLYPH] = "GLYPH",
    [PRIVATE_GLYPHSET] = "GLYPHSET",
    [PRIVATE_PICTURE] = "PICTURE",
    [PRIVATE_SYNC_FENCE] = "SYNC_FENCE",
};

void
dixPrivateUsage(void)
{
    int objects = 0;
    int bytes = 0;
    int alloc = 0;
    DevPrivateType t;

    for (t = PRIVATE_XSELINUX + 1; t < PRIVATE_LAST; t++) {
        if (keys[t].offset) {
            ErrorF
                ("%s: %d objects of %d bytes = %d total bytes %d private allocs\n",
                 key_names[t], keys[t].created, keys[t].offset,
                 keys[t].created * keys[t].offset, keys[t].allocated);
            bytes += keys[t].created * keys[t].offset;
            objects += keys[t].created;
            alloc += keys[t].allocated;
        }
    }
    ErrorF("TOTAL: %d objects, %d bytes, %d allocs\n", objects, bytes, alloc);
}

void
dixResetPrivates(void)
{
    DevPrivateType t;

    for (t = PRIVATE_XSELINUX; t < PRIVATE_LAST; t++) {
        DevPrivateKey key, next;

        for (key = keys[t].key; key; key = next) {
            next = key->next;
            key->offset = 0;
            key->initialized = FALSE;
            key->size = 0;
            key->type = 0;
            if (key->allocated)
                free(key);
        }
        if (keys[t].created) {
            ErrorF("%d %ss still allocated at reset\n",
                   keys[t].created, key_names[t]);
            dixPrivateUsage();
        }
        keys[t].key = NULL;
        keys[t].offset = 0;
        keys[t].created = 0;
        keys[t].allocated = 0;
    }
}
