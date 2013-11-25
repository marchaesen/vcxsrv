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

/*
 * This file contains routines to cache atoms, avoiding multiple
 * server round-trips.  Not so useful now that Xlib caches them.
 *
 * Public entry points:
 *
 *	XmuMakeAtom		creates & initializes an opaque AtomRec
 *	XmuInternAtom		fetches an Atom from cache or Display
 *	XmuInternStrings	fetches multiple Atoms as strings
 *	XmuGetAtomName		returns name of an Atom
 *	XmuNameOfAtom		returns name from an AtomPtr
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/Intrinsic.h>
#include "Atoms.h"

typedef struct _DisplayRec {
    struct _DisplayRec* next;
    Display *dpy;
    Atom atom;
} DisplayRec;

struct _AtomRec {
    _Xconst char *name;
    DisplayRec* head;
};

#define DeclareAtom(atom,text) \
static struct _AtomRec __##atom = { text, NULL }; \
AtomPtr _##atom = &__##atom;

DeclareAtom(XA_ATOM_PAIR,		"ATOM_PAIR"		)
DeclareAtom(XA_CHARACTER_POSITION,	"CHARACTER_POSITION"	)
DeclareAtom(XA_CLASS,			"CLASS"			)
DeclareAtom(XA_CLIENT_WINDOW,		"CLIENT_WINDOW"		)
DeclareAtom(XA_CLIPBOARD,		"CLIPBOARD"		)
DeclareAtom(XA_COMPOUND_TEXT,		"COMPOUND_TEXT"		)
DeclareAtom(XA_DECNET_ADDRESS,		"DECNET_ADDRESS"	)
DeclareAtom(XA_DELETE,			"DELETE"		)
DeclareAtom(XA_FILENAME,		"FILENAME"		)
DeclareAtom(XA_HOSTNAME,		"HOSTNAME"		)
DeclareAtom(XA_IP_ADDRESS,		"IP_ADDRESS"		)
DeclareAtom(XA_LENGTH,			"LENGTH"		)
DeclareAtom(XA_LIST_LENGTH,		"LIST_LENGTH"		)
DeclareAtom(XA_NAME,			"NAME"			)
DeclareAtom(XA_NET_ADDRESS,		"NET_ADDRESS"		)
DeclareAtom(XA_NULL,			"NULL"			)
DeclareAtom(XA_OWNER_OS,		"OWNER_OS"		)
DeclareAtom(XA_SPAN,			"SPAN"			)
DeclareAtom(XA_TARGETS,			"TARGETS"		)
DeclareAtom(XA_TEXT,			"TEXT"			)
DeclareAtom(XA_TIMESTAMP,		"TIMESTAMP"		)
DeclareAtom(XA_USER,			"USER"			)
DeclareAtom(XA_UTF8_STRING,		"UTF8_STRING"		)

/******************************************************************

  Public procedures

 ******************************************************************/


AtomPtr
XmuMakeAtom(_Xconst char *name)
{
    AtomPtr ptr = XtNew(struct _AtomRec);
    ptr->name = name;
    ptr->head = NULL;
    return ptr;
}

char *
XmuNameOfAtom(AtomPtr atom_ptr)
{
    return (char *) atom_ptr->name;
}


Atom
XmuInternAtom(Display *d, AtomPtr atom_ptr)
{
    DisplayRec* display_rec;
    for (display_rec = atom_ptr->head; display_rec != NULL;
	 display_rec = display_rec->next) {
	if (display_rec->dpy == d)
	    return display_rec->atom;
    }
    display_rec = XtNew(DisplayRec);
    display_rec->next = atom_ptr->head;
    atom_ptr->head = display_rec;
    display_rec->dpy = d;
    display_rec->atom = XInternAtom(d, atom_ptr->name, False);
    return display_rec->atom;
}


char *
XmuGetAtomName(Display *d, Atom atom)
{
    if (atom == 0) return (NULL);
    return XGetAtomName(d, atom);
}

/* convert (names, count) to a list of atoms. Caller allocates list */
void
XmuInternStrings(Display *d, register String *names,
		 register Cardinal count, register Atom *atoms)
{
    (void) XInternAtoms(d, (char**)names, (int)count, FALSE, atoms);
}
