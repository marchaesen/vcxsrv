/*
 * Copyright 2005 Red Hat Inc., Raleigh, North Carolina.
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
 *   Kevin E. Martin <kem@redhat.com>
 *
 */

/** \file
 * Provide configuration define's and undef's to build Xdmx in X.Org's
 * modular source tree.
 */

#ifndef DMX_CONFIG_H
#define DMX_CONFIG_H

#include <dix-config.h>

/*
 * Note 1: This is a signed int that is printed as a decimal number.
 *         Since we want to make it human-interpretable, the fields are
 *         defined as:
 *         2147483648
 *         AAbbyymmdd
 *         AA: major version 01-20
 *         bb: minor version 00-99
 *         yy: year          00-99 [See Note 2]
 *         mm: month         01-12
 *         dd: day           01-31
 *
 * Note 2: The default epoch for the year is 2000.
 *         To change the default epoch, change the DMX_VENDOR_RELEASE
 *         macro below, bumb the minor version number, and change
 *         xdpyinfo to key off the major/minor version to determine the
 *         new epoch.  Remember to do this on January 1, 2100 and every
 *         100 years thereafter.
 */
#define DMX_VENDOR_RELEASE(major,minor,year,month,day) \
    ((major)     * 100000000) + \
    ((minor)     *   1000000) + \
    ((year-2000) *     10000) + \
    ((month)     *       100) + \
    ((day)       *         1)
#define VENDOR_RELEASE  DMX_VENDOR_RELEASE(1,2,2007,4,24)
#define VENDOR_STRING   "DMX Project"

/* Enable the DMX extension */
#define DMXEXT

#endif                          /* DMX_CONFIG_H */
