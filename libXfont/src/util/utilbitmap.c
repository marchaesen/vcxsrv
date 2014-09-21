/*

Copyright 1990, 1994, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.

*/

/*
 * Author:  Keith Packard, MIT X Consortium
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/fonts/fontmisc.h>

/*
 *	Repad a bitmap
 */

int
RepadBitmap (char *pSrc, char *pDst,
	     unsigned int srcPad, unsigned int dstPad,
	     int width, int height)
{
    int	    srcWidthBytes,dstWidthBytes;
    int	    row,col;
    char    *pTmpSrc,*pTmpDst;

    switch (srcPad) {
    case 1:
	srcWidthBytes = (width+7)>>3;
	break;
    case 2:
	srcWidthBytes = ((width+15)>>4)<<1;
	break;
    case 4:
	srcWidthBytes = ((width+31)>>5)<<2;
	break;
    case 8:
	srcWidthBytes = ((width+63)>>6)<<3;
	break;
    default:
	return 0;
    }
    switch (dstPad) {
    case 1:
	dstWidthBytes = (width+7)>>3;
	break;
    case 2:
	dstWidthBytes = ((width+15)>>4)<<1;
	break;
    case 4:
	dstWidthBytes = ((width+31)>>5)<<2;
	break;
    case 8:
	dstWidthBytes = ((width+63)>>6)<<3;
	break;
    default:
	return 0;
    }

    width = srcWidthBytes;
    if (width > dstWidthBytes)
	width = dstWidthBytes;
    pTmpSrc= pSrc;
    pTmpDst= pDst;
    for (row = 0; row < height; row++)
    {
	for (col = 0; col < width; col++)
	    *pTmpDst++ = *pTmpSrc++;
	while (col < dstWidthBytes)
 	{
	    *pTmpDst++ = '\0';
	    col++;
	}
	pTmpSrc += srcWidthBytes - width;
    }
    return dstWidthBytes * height;
}


