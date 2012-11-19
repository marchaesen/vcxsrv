/*

Copyright 1989, 1998  The Open Group

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
 *
 *                                 Global data
 *
 * This file should contain only those objects which must be predefined.
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/Xlibint.h>


/*
 * If possible, it is useful to have the global data default to a null value.
 * Some shared library implementations are *much* happier if there isn't any
 * global initialized data.
 */
#ifdef NULL_NOT_ZERO			/* then need to initialize */
#define SetZero(t,var,z) t var = z
#else
#define SetZero(t,var,z) t var
#endif

#ifdef USL_SHAREDLIB			/* then need extra variables */
/*
 * If we need to define extra variables for each global
 */
#if !defined(UNIXCPP) || defined(ANSICPP)
#define ZEROINIT(t,var,val) SetZero(t,var,val); \
  SetZero (long, _libX_##var##Flag, 0); \
  SetZero (void *, _libX_##var##Ptr, NULL)
#else /* else pcc concatenation */
#define ZEROINIT(t,var,val) SetZero(t,var,val); \
  SetZero (long, _libX_/**/var/**/Flag, 0); \
  SetZero (void *, _libX_/**/var/**/Ptr, NULL)
#endif /* concat ANSI C vs. pcc */

#else /* else not USL_SHAREDLIB */
/*
 * no extra crud
 */
#define ZEROINIT(t,var,val) SetZero (t, var, val)

#endif /* USL_SHAREDLIB */


/*
 * Error handlers; used to be in XlibInt.c
 */
ZEROINIT (XErrorHandler, _XErrorFunction, NULL);
ZEROINIT (XIOErrorHandler, _XIOErrorFunction, NULL);
ZEROINIT (_XQEvent *, _qfree, NULL);


/*
 * Debugging information and display list; used to be in XOpenDis.c
 */
ZEROINIT (int, _Xdebug, 0);
ZEROINIT (Display *, _XHeadOfDisplayList, NULL);



#if 0
#ifdef STREAMSCONN


/* The following are how the Xstream connections are used:              */
/*      1)      Local connections over pseudo-tty ports.                */
/*      2)      SVR4 local connections using named streams or SVR3.2    */
/*              local connections using streams.                        */
/*      3)      SVR4 stream pipe code. This code is proprietary and     */
/*              the actual code is not included in the XC distribution. */
/*      4)      remote connections using tcp                            */
/*      5)      remote connections using StarLan                        */

/*
 * descriptor block for streams connections
 */

#include "Xstreams.h"

char _XsTypeOfStream[100] = { 0 };

extern int write();
extern int close();
#ifdef SVR4
extern int _XsSetupSpStream();
extern int _XsSetupNamedStream();
#endif
extern int _XsSetupLocalStream();
extern int _XsConnectLocalClient();
extern int _XsCallLocalServer();
extern int _XsReadLocalStream();
extern int _XsErrorCall();
extern int _XsWriteLocalStream();
extern int _XsCloseLocalStream();
extern int _XsSetupTliStream();
extern int _XsConnectTliClient();
extern int _XsCallTliServer();
extern int _XsReadTliStream();
extern int _XsWriteTliStream();
extern int _XsCloseTliStream();


Xstream _XsStream[] = {

    {
	/* local connections using pseudo-ttys */

	_XsSetupLocalStream,
	_XsConnectLocalClient,
	_XsCallLocalServer,
	_XsReadLocalStream,
	_XsErrorCall,
	write,
	close,
	NULL
    },
    {
#ifdef SVR4
	/* local connections using named streams */

        _XsSetupNamedStream,
#else
	/* local connections using streams */
        _XsSetupLocalStream,
#endif
        _XsConnectLocalClient,
        _XsCallLocalServer,
        _XsReadLocalStream,
        _XsErrorCall,
        write,
        close,
        NULL
    },
    /* Enhanced Application Compatibility Support */
    {
#ifdef SVR4
	/* SVR4 stream pipe code */
	_XsSetupSpStream,
#else
	_XsSetupLocalStream,
#endif
	_XsConnectLocalClient,
	_XsCallLocalServer,
	_XsReadLocalStream,
	_XsErrorCall,
	write,
	close,
	NULL
    },
    /* End Enhanced Application Compatibility Support */

    {
	/* remote connections using tcp */
        _XsSetupTliStream,
        _XsConnectTliClient,
        _XsCallTliServer,
        _XsReadLocalStream,
        _XsErrorCall,
	write,
	close,
	NULL
    },
    {
	/* remote connections using StarLan */
        _XsSetupTliStream,
        _XsConnectTliClient,
        _XsCallTliServer,
        _XsReadLocalStream,
        _XsErrorCall,
        write,
        close,
        NULL
    }
};


#endif /* STREAMSCONN */
#endif


#ifdef XTEST1
/*
 * Stuff for input synthesis extension:
 */
/*
 * Holds the two event type codes for this extension.  The event type codes
 * for this extension may vary depending on how many extensions are installed
 * already, so the initial values given below will be added to the base event
 * code that is aquired when this extension is installed.
 *
 * These two variables must be available to programs that use this extension.
 */
int			XTestInputActionType = 0;
int			XTestFakeAckType   = 1;
#endif

/*
 * NOTE: any additional external definition NEED
 * to be inserted BELOW this point!!!
 */

/*
 * NOTE: any additional external definition NEED
 * to be inserted ABOVE this point!!!
 */
