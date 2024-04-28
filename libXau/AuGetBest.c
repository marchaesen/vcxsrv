/*

Copyright 1988, 1998  The Open Group

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/Xauth.h>
#include <X11/Xos.h>
#ifdef XTHREADS
#include <X11/Xthreads.h>
#endif

#ifdef O_CLOEXEC
#define FOPEN_CLOEXEC "e"
#else
#define FOPEN_CLOEXEC ""
#endif

#define binaryEqual(a, b, len) (memcmp(a, b, len) == 0)

Xauth *
XauGetBestAuthByAddr (
#if NeedWidePrototypes
    unsigned int	family,
    unsigned int	address_length,
#else
    unsigned short	family,
    unsigned short	address_length,
#endif
    _Xconst char*	address,
#if NeedWidePrototypes
    unsigned int	number_length,
#else
    unsigned short	number_length,
#endif
    _Xconst char*	number,
    int			types_length,
    char**		types,
    _Xconst int*	type_lengths)
{
    FILE    *auth_file;
    char    *auth_name;
    Xauth   *entry;
    Xauth   *best;
    int	    best_type;
    int	    type;

    auth_name = XauFileName ();
    if (!auth_name)
	return NULL;
    if (access (auth_name, R_OK) != 0)		/* checks REAL id */
	return NULL;
    auth_file = fopen (auth_name, "rb" FOPEN_CLOEXEC);
    if (!auth_file)
	return NULL;

    best = NULL;
    best_type = types_length;
    for (;;) {
	entry = XauReadAuth (auth_file);
	if (!entry)
	    break;
	/*
	 * Match when:
	 *   either family or entry->family are FamilyWild or
	 *    family and entry->family are the same and
	 *     address and entry->address are the same
	 *  and
	 *   either number or entry->number are empty or
	 *    number and entry->number are the same
	 *  and
	 *   either name or entry->name are empty or
	 *    name and entry->name are the same
	 */

	if ((family == FamilyWild || entry->family == FamilyWild ||
	     (entry->family == family &&
	     ((address_length == entry->address_length &&
	      binaryEqual (entry->address, address, address_length))
	    ))) &&
	    (number_length == 0 || entry->number_length == 0 ||
	     (number_length == entry->number_length &&
	      binaryEqual (entry->number, number, number_length))))
	{
	    if (best_type == 0)
	    {
		best = entry;
		break;
	    }
	    for (type = 0; type < best_type; type++)
		if (type_lengths[type] == entry->name_length &&
		    !(strncmp (types[type], entry->name, entry->name_length)))
		{
		    break;
		}
	    if (type < best_type)
	    {
		if (best)
		    XauDisposeAuth (best);
		best = entry;
		best_type = type;
		if (type == 0)
		    break;
		continue;
	    }
	}
	XauDisposeAuth (entry);
    }
    (void) fclose (auth_file);
    return best;
}
