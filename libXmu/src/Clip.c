/*
 * Copyright (c) 1998 by The XFree86 Project, Inc.
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
 * THE XFREE86 PROJECT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Except as contained in this notice, the name of the XFree86 Project shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from the
 * XFree86 Project.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdlib.h>

#include <X11/IntrinsicP.h>
#include <X11/Xmu/Xmu.h>

#define XmuMax(a, b)	((a) > (b) ? (a) : (b))
#define XmuMin(a, b)	((a) < (b) ? (a) : (b))

/*
 * Function:
 *	XmuNewArea
 *
 * Parameters:
 *	x1 - Coordinates of the rectangle
 *	y1 - ""
 *	x2 - ""
 *	y2 - ""
 *
 * Description:
 *	Creates a new rectangular clipping area
 */
XmuArea *
XmuNewArea(int x1, int y1, int x2, int y2)
{
  XmuArea *area;

  area = (XmuArea *)XtMalloc(sizeof(XmuArea));
  if (x2 > x1 && y2 > y1)
    {
      area->scanline = XmuNewScanline(y1, x1, x2);
      area->scanline->next = XmuNewScanline(y2, 0, 0);
    }
  else
    area->scanline = (XmuScanline *)NULL;

  return (area);
}

/*
 * Function:
 *	XmuAreaDup
 *
 * Parameters:
 *	area - Area to copy
 *
 * Description:
 *	Returns a copy of its argument
 */
XmuArea *
XmuAreaDup(XmuArea *area)
{
  XmuArea *dst;

  if (!area)
    return ((XmuArea *)NULL);

  dst = XmuCreateArea();
  XmuAreaCopy(dst, area);
  return (dst);
}

/*
 * Function:
 *	XmuAreaCopy
 *
 * Parameters:
 *	dst - destination area
 *	src - source area
 *
 * Description:
 *	Minimizes memory allocation, trying to use already allocated memory
 *	in dst, freeing what is not required anymore.
 */
XmuArea *
XmuAreaCopy(XmuArea *dst, XmuArea *src)
{
  XmuScanline *z, *p, *Z;

  if (!dst || !src || dst == src)
    return (dst);

  z = p = dst->scanline;
  Z = src->scanline;

  /*CONSTCOND*/
  while (1)
    {
      if (!Z)
	{
	  if (z == dst->scanline)
	    {
	      XmuDestroyScanlineList(dst->scanline);
	      dst->scanline = (XmuScanline *)NULL;
	    }
	  else
	    {
	      XmuDestroyScanlineList(p->next);
	      p->next = (XmuScanline *)NULL;
	    }
	  return (dst);
	}
      if (z)
	{
	  XmuScanlineCopy(z, Z);
	  z->y = Z->y;
	}
      else
	{
	  z = XmuNewScanline(Z->y, 0, 0);
	  XmuScanlineCopy(z, Z);
	  if (p == dst->scanline && !dst->scanline)
	    p = dst->scanline = z;
	  else
	    p->next = z;
	}
      p = z;
      z = z->next;
      Z = Z->next;
    }

  return (dst);
}

/*
 * Function:
 *   XmuAreaNot
 *
 * Parameters:
 *	area - area to operate
 *	x1   - retangle to clip the result against
 *	y1   - ""
 *	x2   - ""
 *	y2   - ""
 *
 * Description:
 * (Input)
 * (x1, y1)                                                (x2, y1)
 *                   +-------------+
 *      +------------+             +----+
 *      |                +--------------+
 *      +----------------+
 * (x1, y2)                                                (x2, y2)
 *
 * (Output)
 * (x1, y1)                                                (x2, y1)
 *    +--------------+             +--------------------------+
 *    | +------------+             +----+                     |
 *    | |                +--------------+                     |
 *    +-+                +------------------------------------+
 * (x1, y2)                                                (x2, y2)
 */
XmuArea *
XmuAreaNot(XmuArea *area, int x1, int y1, int x2, int y2)
{
  XmuScanline *z;
  XmuArea *and;

  if (!area)
    return (area);

  if (x1 > x2)
    {
      x1 ^= x2; x2 ^= x1; x1 ^= x2;
    }
    if (y1 > y2)
      {
	y1 ^= y2; y2 ^= y1; y1 ^= y2;
      }
    if (!area->scanline)
      {
	if ((area->scanline = XmuNewScanline(y1, x1, x2)) != NULL)
	  area->scanline->next = XmuNewScanline(y2, 0, 0);
	return (area);
      }
    and = XmuNewArea(x1, y1, x2, y2);
    XmuAreaAnd(area, and);
    XmuDestroyArea(and);
    z = area->scanline;
    if (z->y != y1)
      {
	XmuScanline *q = XmuNewScanline(y1, x1, x2);
	q->next = z;
	area->scanline = q;
      }
    else
      {
	area->scanline = area->scanline->next;
	XmuDestroyScanline(z);
	XmuOptimizeArea(area);
	if((z = area->scanline) == (XmuScanline *)NULL)
	  return (area);
      }

    /* CONSTCOND */
    while (1)
      {
	XmuScanlineNot(z, x1, x2);
	if (!z->next)
	  {
	    z->next = XmuNewScanline(y2, 0, 0);
	    break;
	  }
	if (z->next->y == y2)
	  {
	    XmuDestroyScanlineList(z->next);
	    z->next = XmuNewScanline(y2, 0, 0);
	    break;
	  }
	z = z->next;
      }

    return (area);
}

/*
 * Function:
 *	XmuAreaOrXor
 *
 * Parameters:
 *	dst - destination area
 *	src - source area
 *	or  - or operation if true, else xor operation
 *
 * Description:
 *	Executes Or (Union) or Xor (Reverse intersection) of the areas
 */
XmuArea *
XmuAreaOrXor(XmuArea *dst, XmuArea *src, Bool or)
{
  XmuScanline *z, *p, *Z, *P, *ins, *top;

  if (!dst || !src)
    return (dst);

  if (dst == src)
    {
      if (or)
	return (dst);
      XmuDestroyScanlineList(dst->scanline);
      dst->scanline = (XmuScanline *)NULL;
      return (dst);
    }
  if (!XmuValidArea(src))
    return (dst);
  if (!XmuValidArea(dst))
    {
      XmuAreaCopy(dst, src);
      return (dst);
    }

  p = z = dst->scanline;
  P = Z = src->scanline;
  ins = XmuNewScanline(dst->scanline->y, 0, 0);
  top = XmuNewScanline(dst->scanline->y, 0, 0);
  XmuScanlineCopy(ins, dst->scanline);
  XmuScanlineCopy(top, dst->scanline);

  /*CONSTCOND*/
  while (1)
    {
      if (!Z)
	break;
      else if (Z->y < z->y)
	{
	  XmuScanline  *q = XmuNewScanline(Z->y, 0, 0);
	  XmuScanlineCopy(q, Z);

	  if (z == dst->scanline)
	    {
	      dst->scanline = p = q;
	      q->next = z;
            }
	  else
	    {
	      p->next = q;
	      q->next = z;
	      if (Z->y >= p->y)
		{
		  if (ins->y >= top->y
		      && (p->y != P->y || !XmuScanlineEqu(p, P)
			  || (ins->y <= P->y && !XmuScanlineEqu(ins, P))))
		    {
		      if (or)
			XmuScanlineOr(q, ins);
		      else
			XmuScanlineXor(q, ins);
                    }
		  else if (Z->y >= top->y
			   && (top->y == p->y || top->y > ins->y
			       || !XmuValidScanline(Z)
			       || (p->y == P->y && XmuValidScanline(p)
				   && XmuValidScanline(P))
			       || XmuScanlineEqu(ins, top)))
		    {
		      if (or)
			XmuScanlineOr(q, top);
		      else
			XmuScanlineXor(q, top);
		    }
		  if (ins->y != p->y && p->y != P->y)
		    {
		      XmuScanlineCopy(ins, p);
		      ins->y = p->y;
		    }
		}
	      if (!XmuValidScanline(p) || Z->y <= p->y)
		{
		  XmuScanlineCopy(top, p);
		  top->y = p->y;
                }
	      p = q;
	    }
	  P = Z;
	  Z = Z->next;
	  continue;
        }
      else if (Z->y == z->y)
	{
	  if (top->y != z->y)
	    {
	      XmuScanlineCopy(top, z);
	      top->y = z->y;
	    }
	  if (or)
	    XmuScanlineOr(z, Z);
	  else
	    XmuScanlineXor(z, Z);
	  P = Z;
	  Z = Z->next;
	}
      else if (P != Z)		/* && Z->y > z->y */
	{
	  if (top->y == ins->y && top->y != z->y)
	    {
	      XmuScanlineCopy(top, z);
	      top->y = z->y;
	    }
	  if (ins->y != z->y)
	    {
	      XmuScanlineCopy(ins, z);
	      ins->y = z->y;
	    }
	  if (or)
	    XmuScanlineOr(z, P);
	  else
	    XmuScanlineXor(z, P);
	}
      else if (ins->y != z->y)
	{
	  XmuScanlineCopy(ins, z);
	  ins->y = z->y;
	}
      p = z;
      z = z->next;
      if (!z)
	{
	  while (Z)
	    {
	      p->next = XmuNewScanline(Z->y, 0, 0);
	      XmuScanlineCopy(p->next, Z);
	      p = p->next;
	      Z = Z->next;
	    }
	  break;
	}
      else if (ins->y > top->y && !XmuValidScanline(z)
	       && XmuValidScanline(ins))
	{
	  XmuScanlineCopy(top, ins);
	  top->y = ins->y;
	}
    }
  XmuOptimizeArea(dst);
  XmuDestroyScanline(ins);
  XmuDestroyScanline(top);

  return (dst);
}

/*
 * Function:
 *	XmuAreaAnd(dst, src)
 *
 * Parameters:
 *	dst - destination area
 *	src - source area
 *
 * Description:
 *	Executes And (intersection) of the areas
 */
XmuArea *
XmuAreaAnd(XmuArea *dst, XmuArea *src)
{
  XmuScanline *z, *p, *Z, *P, *top;

  if (!dst || !src || dst == src)
    return (dst);
  if (!XmuValidArea(dst) || !XmuValidArea(src))
    {
      XmuDestroyScanlineList(dst->scanline);
      dst->scanline = (XmuScanline *)NULL;
      return (dst);
    }
  z = p = dst->scanline;
  Z = P = src->scanline;
  top = XmuNewScanline(dst->scanline->y, 0, 0);
  XmuScanlineCopy(top, dst->scanline);

  while (z)
    {
      while (Z->next && Z->next->y < z->y)
	{
	  P = Z;
	  Z = Z->next;
	  if (Z->y >= p->y)
	    {
	      XmuScanline *q = XmuNewScanline(Z->y, 0, 0);
	      XmuScanlineCopy(q, Z);

	      XmuScanlineAnd(q, top);
	      if (p->y != P->y)
		{
		  XmuScanlineAnd(p, P);
		  p->y = XmuMax(p->y, P->y);
		}
	      p->next = q;
	      q->next = z;
	      p = q;
	    }
	}
      if (!z->next)
	{
	  z->y = XmuMax(z->y, Z->y);
	  break;
        }
      while (Z->y >= z->next->y)
	{
	  if (z == dst->scanline)
	    {
	      p = dst->scanline = dst->scanline->next;
	      XmuDestroyScanline(z);
	      z = dst->scanline;
	    }
	  else
	    {
	      p->next = z->next;
	      XmuDestroyScanline(z);
	      z = p;
	    }
	  if (!z || !z->next)
	    {
	      XmuOptimizeArea(dst);
	      XmuDestroyScanline(top);

	      return (dst);
	    }
	}
      if (Z->y > p->y)
	z->y = XmuMax(z->y, Z->y);
      if (top->y != z->y)
	{
	  XmuScanlineCopy(top, z);
	  top->y = z->y;
	}
      XmuScanlineAnd(z, Z);
      p = z;
      z = z->next;
    }
  XmuOptimizeArea(dst);
  XmuDestroyScanline(top);

  return (dst);
}

/*
 * Function:
 *   XmuValidArea(area)
 *
 * Parameters:
 *	area - area to verify
 *
 * Description:
 *	Verifies if the area is valid and/or useful
 */
Bool
XmuValidArea(XmuArea *area)
{
  XmuScanline *at;

  if (!area || !area->scanline)
    return (False);

  at = area->scanline;
  while (at)
    {
      if (XmuValidScanline(at))
	return (True);
      at = at->next;
    }

  return (False);
}

/*
 * Function:
 *	XmuValidScanline
 *
 * Parameters:
 *	scanline - scanline to verify
 *
 * Description:
 *	Verifies if a scanline is useful
 */
Bool
XmuValidScanline(XmuScanline *scanline)
{
  XmuSegment *z;

  if (!scanline)
    return (False);

  z = scanline->segment;
  while (z)
    {
      if (XmuValidSegment(z))
	return (True);
      z = z->next;
    }

  return (False);
}

/*
 * Function:
 *   XmuScanlineEqu
 *
 * Parameters:
 *	s1 - scanline 1
 *	s2 - scanline 2
 *
 * Description:
 *	Checks if s1 and s2 are equal
 */
Bool
XmuScanlineEqu(XmuScanline *s1, XmuScanline *s2)
{
  XmuSegment *z, *Z;

  if ((!s1 && !s2) || s1 == s2)
    return (True);
  if (!s1 || !s2)
    return (False);

  z = s1->segment;
  Z = s2->segment;

  /*CONSTCOND*/
  while (1)
    {
      if (!z && !Z)
	return (True);
      if (!z || !Z)
	return (False);
      if (!XmuSegmentEqu(z, Z))
	return (False);
      z = z->next;
      Z = Z->next;
    }
  /*NOTREACHED*/
}

/*
 * Function:
 *	XmuNewSegment
 *
 * Parameters:
 *	x1 - coordinates of the segment
 *	x2 - ""
 *
 * Description:
 *	Creates a new segments with the coordinates x1 and x2
 *
 * Returns:
 *	New Segment of NULL
 */
XmuSegment *
XmuNewSegment(int x1, int x2)
{
  XmuSegment *segment;

  if ((segment = (XmuSegment *)XtMalloc(sizeof(XmuSegment))) == NULL)
    return (segment);

  segment->x1 = x1;
  segment->x2 = x2;
  segment->next = (XmuSegment *)NULL;

  return (segment);
}

/*
 * Function:
 *	XmuDestroySegmentList
 *
 * Parameters:
 *	segment - Segment to destroy
 *
 * Description:
 *	Frees the memory used by the list headed by segment
 */
void
XmuDestroySegmentList(XmuSegment *segment)
{
  XmuSegment *z;

  if (!segment)
    return;

  while (segment)
    {
      z = segment;
      segment = segment->next;
      XmuDestroySegment(z);
    }
}

/*
 * Function:
 *	XmuScanlineCopy
 *
 * Parameters:
 *	dst - destination scanline
 *	src - source scanline
 *
 * Description:
 *	Makes dst contain the same data as src
 */
XmuScanline *
XmuScanlineCopy(XmuScanline *dst, XmuScanline *src)
{
  XmuSegment *z, *p, *Z;

  if (!dst || !src || dst == src)
    return (dst);

  z = p = dst->segment;
  Z = src->segment;

  /*CONSTCOND*/
  while (1)
    {
      if (!Z)
	{
	  if (z == dst->segment)
	    dst->segment = (XmuSegment *)NULL;
	  else
	    p->next = (XmuSegment *)NULL;
	  XmuDestroySegmentList(z);
	  return (dst);
	}
      if (z)
	{
	  z->x1 = Z->x1;
	  z->x2 = Z->x2;
	}
      else
	{
	  z = XmuNewSegment(Z->x1, Z->x2);
	  if (p == dst->segment && !dst->segment)
	    p = dst->segment = z;
	  else
	    p->next = z;
	}
      p = z;
      z = z->next;
      Z = Z->next;
    }
  /*NOTREACHED*/
}

/*
 * Function:
 *	XmuAppendSegment
 *
 * Parameters:
 *	segment - destination segment
 *	append  - segment to add
 *
 * Description:
 *	Adds a copy of the append list at the end of the segment list
 */
Bool
XmuAppendSegment(XmuSegment *segment, XmuSegment *append)
{
  if (!segment || !append)
    return (False);

  if (segment->next)
    /* Should not happen! */
    XmuDestroySegmentList(segment->next);

  while (append)
    {
      if (XmuValidSegment(append))
	{
	  if ((segment->next = XmuNewSegment(append->x1, append->x2)) == NULL)
	    return (False);
	  segment = segment->next;
	}
      append = append->next;
    }

  return (True);
}

/*
 * Function:
 *	XmuOptimizeScanline
 *
 * Parameters:
 *	scanline - scanline to optimize
 *
 * Description:
 *	  Some functions, when transforming Segments of Scanlines, left these
 *	with unnecessary data (that may cause error in these same functions).
 *	  This function corrects these incorrect segments.
 */
XmuScanline *
XmuOptimizeScanline(XmuScanline *scanline)
{
  XmuSegment *z, *p;

  while (scanline->segment && !XmuValidSegment(scanline->segment))
    {
      XmuSegment *s = scanline->segment;

      scanline->segment = scanline->segment->next;
      XmuDestroySegment(s);
    }
  for (z = p = scanline->segment; z; p = z, z = z->next)
    {
      if (!XmuValidSegment(z))
	{
	  p->next = z->next;
	  XmuDestroySegment(z);
	  z = p;
	}
    }
  return (scanline);
}

/*
 * Name:
 *	XmuScanlineNot(scanline, minx, maxx)
 *
 * Parameters:
 *	scanline - scanlines operate
 *	minx	 - minimum x coordinate
 *	maxx	 - maximum x coordinate
 *
 * Description:
 *         (minx)                                                        (maxx)
 *           +                                                             +
 * (input)         +---------+     +--------+        +--------+
 * (output)  +-----+         +-----+        +--------+        +------------+
 */
XmuScanline *
XmuScanlineNot(XmuScanline *scanline, int minx, int maxx)
{
  XmuSegment *z;
  static XmuSegment x = { 0, 0, NULL };
  static XmuScanline and = { 0, &x, NULL };

  if (!scanline)
    return (scanline);

  XmuOptimizeScanline(scanline);
  if (minx > maxx)
    {
      minx ^= maxx; maxx ^= minx; minx ^= maxx;
    }
  and.segment->x1 = minx;
  and.segment->x2 = maxx;
  XmuScanlineAnd(scanline, &and);
  if (!scanline->segment)
    {
      scanline->segment = XmuNewSegment(minx, maxx);
      return (scanline);
    }
  z = scanline->segment;
  if (z->x1 != minx)
    {
      XmuSegment *q = XmuNewSegment(minx, z->x1);

      q->next = z;
      scanline->segment = q;
    }

  /*CONSTCOND*/
  while (1)
    {
      z->x1 = z->x2;
      if (!z->next)
	{
	  z->x2 = maxx;
	  break;
	}
      z->x2 = z->next->x1;
      if (z->next->x2 == maxx)
	{
	  XmuDestroySegment(z->next);
	  z->next = (XmuSegment *)NULL;
	  break;
	}
      z = z->next;
    }

  return (scanline);
}


/*
 * Function:
 *	XmuScanlineOrSegment
 *
 * Parameters:
 *	dst - destination scanline
 *	src - source segment
 *
 * Description:
 * (input)      +-----------+  +--------+    +---------+
 * (src)              +-------------------+
 * (output)     +-------------------------+  +---------+
 */
XmuScanline *
XmuScanlineOrSegment(XmuScanline *dst, XmuSegment *src)
{
  XmuSegment *z, *p, ins;

  if (!src || !dst || !XmuValidSegment(src))
    return (dst);

  if (!dst->segment)
    {
      dst->segment = XmuNewSegment(src->x1, src->x2);
      return (dst);
    }

  z = p = dst->segment;
  ins.x1 = src->x1;
  ins.x2 = src->x2;

  /*CONSTCOND*/
  while (1)
    {
      if (!z)
	{
            XmuSegment *q = XmuNewSegment(ins.x1, ins.x2);

	    if (p == dst->segment && z == p)
	      dst->segment = q;
	    else
	      p->next = q;
	    break;
	}
      else if (ins.x2 < z->x1)
	{
	  XmuSegment *q = XmuNewSegment(ins.x1, ins.x2);

	  if (p == dst->segment && z == p)
	    {
	      q->next = dst->segment;
	      dst->segment = q;
	    }
	  else
	    {
	      p->next = q;
	      q->next = z;
	    }
	  break;
        }
      else if (ins.x2 <= z->x2)
	{
	  z->x1 = XmuMin(z->x1, ins.x1);
	  break;
	}
      else if (ins.x1 <= z->x2)
	{
	  ins.x1 = XmuMin(z->x1, ins.x1);
	  if (!z->next)
	    {
	      z->x1 = ins.x1;
	      z->x2 = ins.x2;
	      break;
	    }
	  else
	    {
	      if (z == dst->segment)
		{
		  p = dst->segment = dst->segment->next;
		  XmuDestroySegment(z);
		  z = dst->segment;
		  continue;
		}
	      else
		{
		  p->next = z->next;
		  XmuDestroySegment(z);
		  z = p;
		}
	    }
	}
      p = z;
      z = z->next;
    }

  return (dst);
}

/*
 * Function:
 *	XmuScanlineAndSegment
 *
 * Parameters:
 *	dst - destination scanline
 *	src - source segment
 *
 * Description:
 * (input)      +------------+   +------+     +----------+
 * (src)             +---------------------+
 * (output)          +-------+   +------+
 */
XmuScanline *
XmuScanlineAndSegment(XmuScanline *dst, XmuSegment *src)
{
  XmuSegment *z, *p;

  if (!dst || !src)
    return (dst);

  if (!XmuValidSegment(src))
    {
      XmuDestroySegmentList(dst->segment);
      dst->segment = (XmuSegment *)NULL;
      return (dst);
    }
  if (!dst->segment)
    return (dst);

  z = p = dst->segment;
  while (z)
    {
      if (src->x2 <= z->x1 || src->x1 >= z->x2)
	{
	  if (z == dst->segment)
	    {
	      p = dst->segment = dst->segment->next;
	      XmuDestroySegment(z);
	      z = dst->segment;
	      continue;
	    }
	  else
	    {
	      p->next = z->next;
	      XmuDestroySegment(z);
	      z = p;
	    }
	}
      else
	{
	  z->x1 = XmuMax(z->x1, src->x1);
	  z->x2 = XmuMin(z->x2, src->x2);
	}
      p = z;
      z = z->next;
    }

  return (dst);
}

/*
 * Function:
 *	XmuScanlineXorSegment
 *
 * Parameters:
 *	dst - destination scanline
 *	src - source segment
 *
 * Description:
 * (input)     +------------+  +----------+    +-----------+
 * (src)           +------------------------+
 * (output)    +---+        +--+          +-+  +-----------+
 */
XmuScanline *
XmuScanlineXorSegment(XmuScanline *dst, XmuSegment *src)
{
  XmuSegment *p, *z, ins;
  int tmp1, tmp2;

  if (!dst || !src || !XmuValidSegment(src))
    return (dst);
  if (!dst->segment)
    {
      dst->segment = XmuNewSegment(src->x1, src->x2);
      return (dst);
    }

  p = z = dst->segment;
  ins.x1 = src->x1;
  ins.x2 = src->x2;

  /*CONSTCOND*/
  while (1)
    {
      if (!XmuValidSegment((&ins)))
	break;
      if (!z || ins.x2 < z->x1)
	{
	  XmuSegment *q = XmuNewSegment(ins.x1, ins.x2);

	  q->next = z;
	  if (z == dst->segment)
	    dst->segment = q;
	  else
	    p->next = q;
	  break;
	}
      else if (ins.x2 == z->x1)
	{
	  z->x1 = ins.x1;
	  break;
	}
      else if (ins.x1 < z->x2)
	{
	  if (ins.x1 < z->x1)
	    {
	      tmp1 = ins.x2;
	      tmp2 = z->x2;
	      ins.x2 = XmuMax(ins.x2, z->x2);
	      z->x2 = z->x1;
	      z->x1 = ins.x1;
	      ins.x1 = XmuMin(tmp1, tmp2);
	    }
	  else if (ins.x1 > z->x1)
	    {
	      tmp1 = ins.x1;
	      ins.x1 = XmuMin(ins.x2, z->x2);
	      ins.x2 = XmuMax(z->x2, ins.x2);
	      z->x2 = tmp1;
	    }
	  else	/* ins.x1 == z->x1 */
	    {
	      if (ins.x2 < z->x2)
		{
		  z->x1 = ins.x2;
		  break;
		}
	      else
		{
		  ins.x1 = z->x2;
		  if (z == dst->segment)
		    p = dst->segment = dst->segment->next;
		  else
		    p->next = z->next;
		  XmuDestroySegment(z);
		  z = p;
		  continue;
		}
	    }
	}
      else if (ins.x1 == z->x2)
	{
	  ins.x1 = z->x1;
	  if (z == dst->segment)
	    p = dst->segment = dst->segment->next;
	  else
	    p->next = z->next;
	  XmuDestroySegment(z);
	  z = p;
	  continue;
	}
      p = z;
      z = z->next;
    }

  return (dst);
}

/*
 * Function:
 *	ScanlineOr
 *
 * Parameters:
 *	dst - destination scanline
 *	src - source scanline
 *
 * Description:
 * (input)    +--------------+  +-----+    +----------+
 * (src)          +---------------------+       +-----------+
 * (output)   +-------------------------+  +----------------+
 */
XmuScanline *
XmuScanlineOr(XmuScanline *dst, XmuScanline *src)
{
  XmuSegment *z, *p, *Z, ins;

  if (!src || !src->segment || !dst || dst == src)
    return (dst);
  if (!dst->segment)
    {
      XmuScanlineCopy(dst, src);
      return (dst);
    }

  z = p = dst->segment;
  Z = src->segment;
  ins.x1 = Z->x1;
  ins.x2 = Z->x2;

  /*CONSTCOND*/
  while (1)
    {
      while (!XmuValidSegment((&ins)))
	{
	  if ((Z = Z->next) == (XmuSegment *)NULL)
	    return (dst);
	  ins.x1 = Z->x1;
	  ins.x2 = Z->x2;
	}
        if (!z)
	  {
            XmuSegment *q = XmuNewSegment(ins.x1, ins.x2);

            if (p == dst->segment && z == p)
	      dst->segment = p = q;
            else
	      {
		p->next = q;
		p = q;
	      }
	    Z = Z->next;
	    XmuAppendSegment(p, Z);
	    break;
	  }
        else if (ins.x2 < z->x1)
	  {
	    XmuSegment *r = XmuNewSegment(ins.x1, ins.x2);

	    if (p == dst->segment && z == p)
	      {
		r->next = dst->segment;
		dst->segment = p = r;
	      }
	    else
	      {
		p->next = r;
		r->next = z;
		p = r;
	      }
	    Z = Z->next;
            if (!Z)
	      break;
            else
	      {
		ins.x1 = Z->x1;
		ins.x2 = Z->x2;
		continue;
	      }
	  }
	else if (ins.x2 <= z->x2)
	  {
	    z->x1 = XmuMin(z->x1, ins.x1);
	    Z = Z->next;
	    if (!Z)
	      break;
            else
	      {
		ins.x1 = Z->x1;
		ins.x2 = Z->x2;
		continue;
	      }
	  }
	else if (ins.x1 <= z->x2)
	  {
	    ins.x1 = XmuMin(z->x1, ins.x1);
	    if (!z->next)
	      {
		z->x1 = ins.x1;
		z->x2 = ins.x2;
		p = z;
		Z = Z->next;
		XmuAppendSegment(p, Z);
		break;
	      }
            else
	      {
		if (z == dst->segment)
		  {
		    p = dst->segment = dst->segment->next;
		    XmuDestroySegment(z);
		    z = p;
		    continue;
		  }
                else
		  {
		    p->next = z->next;
		    XmuDestroySegment(z);
		    z = p;
		  }
	      }
	  }
	p = z;
	z = z->next;
    }

  return (dst);
}

/*
 * Function:
 *	XmuScanlineAnd
 *
 * Parameters:
 *	dst - destination scanline
 *	src - source scanline
 *
 * Description:
 * (input)    +--------------+  +-----+    +----------+
 * (src)          +---------------------+       +-----------+
 * (output)       +----------+  +-----+         +-----+
 */
XmuScanline *
XmuScanlineAnd(XmuScanline *dst, XmuScanline *src)
{
  XmuSegment  *z, *p, *Z;

  if (!dst || !src || dst == src || !dst->segment) {
        return (dst);
  }
  if (!src->segment)
    {
      XmuDestroySegmentList(dst->segment);
      dst->segment = (XmuSegment *)NULL;
      return (dst);
    }
  z = p = dst->segment;
  Z = src->segment;

  while (z)
    {
      while (!XmuValidSegment(Z) || Z->x2 <= z->x1)
	{
	  Z = Z->next;
	  if (!Z)
	    {
	      if (z == dst->segment)
		dst->segment = (XmuSegment *)NULL;
	      else
		p->next = (XmuSegment *)0;
	      XmuDestroySegmentList(z);
	      return (dst);
	    }
	}
      if (Z->x1 >= z->x2)
	{
	  if (z == dst->segment)
	    {
	      p = dst->segment = dst->segment->next;
	      XmuDestroySegment(z);
	      z = dst->segment;
	    }
	  else
	    {
	      p->next = z->next;
	      XmuDestroySegment(z);
	      z = p->next;
	    }
	  if (!z)
	    return (dst);
	  else
	    continue;
	}
        z->x1 = XmuMax(z->x1, Z->x1);
	if (z->x2 > Z->x2)
	  {
            if (Z->next)
	      {
                XmuSegment *q = XmuNewSegment(Z->x2, z->x2);

		q->next = z->next;
		z->next = q;
	      }
	    z->x2 = Z->x2;
	  }
	p = z;
	z = z->next;
    }

  return (dst);
}

/*
 * Function:
 *	ScanlineXor
 *
 * Parameters:
 *	dst - destination scanline
 *	src - source scanline
 *
 * Description:
 * (input)    +--------------+  +-----+    +----------+
 * (src)          +---------------------+       +-----------+
 * (output)   +---+          +--+     +-+  +----+     +-----+
 */
XmuScanline *
XmuScanlineXor(XmuScanline *dst, XmuScanline *src)
{
  XmuSegment *z, *p, *Z, ins;
  int tmp1, tmp2;

  if (!src || !dst || !src->segment)
    return (dst);
  if (src == dst)
    {
      XmuDestroySegmentList(dst->segment);
      dst->segment = (XmuSegment *)NULL;
      return (dst);
    }
  if (!dst->segment)
    {
      XmuScanlineCopy(dst, src);
      return (dst);
    }

  z = p = dst->segment;
  Z = src->segment;
  ins.x1 = Z->x1;
  ins.x2 = Z->x2;

  /*CONSTCOND*/
  while (1)
    {
      while (!XmuValidSegment((&ins)))
	{
	  if ((Z = Z->next) == (XmuSegment *)NULL)
	    return (dst);
	  ins.x1 = Z->x1;
	  ins.x2 = Z->x2;
	}
      if (!z)
	{
	  XmuSegment *q = XmuNewSegment(ins.x1, ins.x2);

            if (!dst->segment)
	      dst->segment = q;
            else
	      p->next = q;
	    p = q;
	    Z = Z->next;
	    XmuAppendSegment(p, Z);
	    break;
	}
      else if (ins.x2 < z->x1)
	{
	  XmuSegment *q = XmuNewSegment(ins.x1, ins.x2);

	  q->next = z;
	  if (z == dst->segment)
	    dst->segment = q;
	  else
	    p->next = q;
	  if ((Z = Z->next) == (XmuSegment *)NULL)
	    return (dst);

	  p = q;
	  ins.x1 = Z->x1;
	  ins.x2 = Z->x2;
	  continue;
	}
      else if (ins.x2 == z->x1)
	{
	  z->x1 = ins.x1;
	  if ((Z = Z->next) == (XmuSegment *)NULL)
	    break;
	  ins.x1 = Z->x1;
	  ins.x2 = Z->x2;
	  continue;
	}
      else if (ins.x1 < z->x2)
	{
	  if (ins.x1 == z->x1)
	    {
	      if (ins.x2 < z->x2)
		{
		  z->x1 = ins.x2;
		  if ((Z = Z->next) == (XmuSegment *)NULL)
		    break;
		  ins.x1 = Z->x1;
		  ins.x2 = Z->x2;
		  continue;
		}
	      else
		{
		  ins.x1 = z->x2;
		  if (z == dst->segment)
		    p = dst->segment = dst->segment->next;
		  else
		    p->next = z->next;
		  XmuDestroySegment(z);
		  z = p;
		  continue;
		}
	    }
	  else
	    {
	      if (Z->x2 < z->x2)
		{
		  XmuSegment  *q = XmuNewSegment(XmuMin(ins.x1, z->x1),
						 XmuMax(z->x1, ins.x1));

		  q->next = z;
		  if (z == dst->segment)
		    dst->segment = q;
		  else
		    p->next = q;
		  ins.x1 = z->x2;
		  z->x1 = ins.x2;
		  p = q;
		  continue;
		}
	      else
		{
		  tmp1 = ins.x2;
		  tmp2 = z->x2;
		  ins.x2 = XmuMax(ins.x2, z->x2);
		  z->x2 = XmuMax(z->x1, ins.x1);
		  z->x1 = XmuMin(ins.x1, z->x1);
		  ins.x1 = XmuMin(tmp1, tmp2);
		}
	    }
	}
      else if (ins.x1 == z->x2)
	{
	  ins.x1 = z->x1;
	  if (z == dst->segment)
	    p = dst->segment = dst->segment->next;
	  else
	    p->next = z->next;
	  XmuDestroySegment(z);
	  z = p;
	  continue;
	}
      p = z;
      z = z->next;
    }

  return (dst);
}

/*
 * Function:
 *	XmuNewScanline
 *
 * Parameters:
 *	y  - y coordinate
 *	x1 - left coordinate
 *	x2 - right coordinate
 *
 * Description:
 *	Creates a new Scanline
 */
XmuScanline *
XmuNewScanline(int y, int x1, int x2)
{
  XmuScanline *scanline;

  scanline = (XmuScanline *)XtMalloc(sizeof(XmuScanline));
  scanline->y = y;
  if (x1 < x2)
    scanline->segment = XmuNewSegment(x1, x2);
  else
    scanline->segment = (XmuSegment *)NULL;

  scanline->next = (XmuScanline *)NULL;

  return (scanline);
}

/*
 * Function:
 *	XmuDestroyScanlineList
 *
 * Parameters:
 *	scanline - scanline list to destroy
 *
 * Description:
 *	Destroy a scanline list
 *
 * Observation:
 *	Use as follow:
 *	XmuDestroyScanlineList(area->scanline);
 *	area->scanline = (XmuScanline *)NULL;
 */
void
XmuDestroyScanlineList(XmuScanline *scanline)
{
  XmuScanline *z;

  if (!scanline)
    return;

  while (scanline)
    {
      z = scanline;
      scanline = scanline->next;
      XmuDestroyScanline(z);
    }
}

/*
 * Function:
 *	XmuOptimizeArea
 *
 * Parameters:
 *	area - area to optimize
 *
 * Description:
 *	  Optimizes an area. This function is called when finishing a
 *	operation between areas, since they can end with redundant data,
 *	and the algorithms for area combination waits a area with
 *	correct data (but can left unnecessary data in the area, to avoid
 *	to much paranoia tests).
 */
XmuArea *XmuOptimizeArea(XmuArea *area)
{
  XmuScanline *pr, *at;

  if (!area || !area->scanline)
    return (area);

  if (!area->scanline->next)
    {
      XmuDestroyScanlineList(area->scanline);
      area->scanline = (XmuScanline *)0;
      return (area);
    }

  pr = area->scanline;
  at = area->scanline->next;
  while (area->scanline && (!XmuValidScanline(area->scanline)
			    || (area->scanline->next && area->scanline->y
				>= area->scanline->next->y)))
    {
      area->scanline = area->scanline->next;
      XmuDestroyScanline(pr);
      pr = area->scanline;
      if (pr)
	at = pr->next;
    }

  for (; at; pr = at, at = at->next)
    {
      if (XmuScanlineEqu(at, pr)
	  || (!XmuValidScanline(at) && !XmuValidScanline(pr))
	  || (at->next && at->y >= at->next->y))
	{
	  pr->next = at->next;
	  XmuDestroyScanline(at);
	  at = pr;
	}
    }
  if (pr && XmuValidScanline(pr))
    {
      XmuDestroySegmentList(pr->segment);
      pr->segment = (XmuSegment *)NULL;
    }
  if (area->scanline && !area->scanline->next)
    {
      XmuDestroyScanlineList(area->scanline);
      area->scanline = (XmuScanline *)NULL;
    }

  return (area);
}
