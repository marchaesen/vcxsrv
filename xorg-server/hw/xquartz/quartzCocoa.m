/**************************************************************
 *
 * Quartz-specific support for the Darwin X Server
 * that requires Cocoa and Objective-C.
 *
 * This file is separate from the parts of Quartz support
 * that use X include files to avoid symbol collisions.
 *
 * Copyright (c) 2001-2004 Torrey T. Lyons and Greg Parker.
 *                 All Rights Reserved.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE ABOVE LISTED COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name(s) of the above copyright
 * holders shall not be used in advertising or otherwise to promote the sale,
 * use or other dealings in this Software without prior written authorization.
 */

#include "sanitizedCocoa.h"

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "quartzCommon.h"
#include "inputstr.h"

#include "darwin.h"

/*
 * QuartzBlockHandler
 *  Clean out any autoreleased objects.
 */
void
QuartzBlockHandler(void *blockData,
                   OSTimePtr pTimeout,
                   void *pReadmask)
{
    static NSAutoreleasePool *aPool = nil;

    [aPool release];
    aPool = [[NSAutoreleasePool alloc] init];
}

/*
 * QuartzWakeupHandler
 */
void
QuartzWakeupHandler(void *blockData,
                    int result,
                    void *pReadmask)
{
    // nothing here
}
