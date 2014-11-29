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

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif
#include "win.h"
#include "dixstruct.h"
#include "inputstr.h"

/*
 * Local function prototypes
 */

#ifdef XWIN_CLIPBOARD
int winProcEstablishConnection(ClientPtr /* client */ );
#endif

/*
 * Local global declarations
 */

DeviceIntPtr g_pwinPointer;
DeviceIntPtr g_pwinKeyboard;

/* Called from dix/devices.c */
/*
 * All of our keys generate up and down transition notifications,
 * so all of our keys can be used as modifiers.
 *
 * An example of a modifier is mapping the A key to the Control key.
 * A has to be a legal modifier.  I think.
 */

Bool
LegalModifier(unsigned int uiKey, DeviceIntPtr pDevice)
{
    return TRUE;
}

/* Called from dix/dispatch.c */
/*
 * Run through the Windows message queue(s) one more time.
 * Tell mi to dequeue the events that we have sent it.
 */
void
ProcessInputEvents(void)
{
#if 0
    ErrorF("ProcessInputEvents\n");
#endif

    mieqProcessInputEvents();

#if 0
    ErrorF("ProcessInputEvents - returning\n");
#endif
}

void
DDXRingBell(int volume, int pitch, int duration)
{
    /* winKeybdBell is used instead */
    return;
}

/* See Porting Layer Definition - p. 17 */
void
InitInput(int argc, char *argv[])
{
#if CYGDEBUG
    winDebug("InitInput\n");
#endif

#ifdef XWIN_CLIPBOARD
    /*
     * Wrap some functions at every generation of the server.
     */
    if (InitialVector[2] != winProcEstablishConnection) {
        winProcEstablishConnectionOrig = InitialVector[2];
        InitialVector[2] = winProcEstablishConnection;
    }
#endif

    if (AllocDevicePair(serverClient, "Windows",
                        &g_pwinPointer, &g_pwinKeyboard,
                        winMouseProc, winKeybdProc,
                        FALSE) != Success)
        FatalError("InitInput - Failed to allocate slave devices.\n");

    mieqInit();

    /* Initialize the mode key states */
    winInitializeModeKeyStates();

#ifdef HAS_DEVWINDOWS
    /* Only open the windows message queue device once */
    if (g_fdMessageQueue == WIN_FD_INVALID) {
        /* Open a file descriptor for the Windows message queue */
        g_fdMessageQueue = open(WIN_MSG_QUEUE_FNAME, O_RDONLY);

        if (g_fdMessageQueue == -1) {
            FatalError("InitInput - Failed opening %s\n", WIN_MSG_QUEUE_FNAME);
        }

        /* Add the message queue as a device to wait for in WaitForSomething */
        AddEnabledDevice(g_fdMessageQueue);
    }
#endif

#if CYGDEBUG
    winDebug("InitInput - returning\n");
#endif
}

void
CloseInput(void)
{
    mieqFini();
}
