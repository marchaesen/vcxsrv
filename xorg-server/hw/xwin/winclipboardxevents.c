/*
 *Copyright (C) 2003-2004 Harold L Hunt II All Rights Reserved.
 *Copyright (C) Colin Harrison 2005-2008
 *
 *Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 *"Software"), to deal in the Software without restriction, including
 *without limitation the rights to use, copy, modify, merge, publish,
 *distribute, sublicense, and/or sell copies of the Software, and to
 *permit persons to whom the Software is furnished to do so, subject to
 *the following conditions:
 *
 *The above copyright notice and this permission notice shall be
 *included in all copies or substantial portions of the Software.
 *
 *THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *NONINFRINGEMENT. IN NO EVENT SHALL HAROLD L HUNT II BE LIABLE FOR
 *ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 *CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *Except as contained in this notice, the name of the copyright holder(s)
 *and author(s) shall not be used in advertising or otherwise to promote
 *the sale, use or other dealings in this Software without prior written
 *authorization from the copyright holder(s) and author(s).
 *
 * Authors:	Harold L Hunt II
 *              Colin Harrison
 */

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif
#include "winclipboard.h"
#include "misc.h"

/*
 * Process any pending X events
 */

int
winClipboardFlushXEvents(HWND hwnd,
                         int iWindow, Display * pDisplay, Bool fUseUnicode)
{
    static Atom atomLocalProperty;
    static Atom atomCompoundText;
    static Atom atomUTF8String;
    static Atom atomTargets;
    static int generation;

    if (generation != serverGeneration) {
        generation = serverGeneration;
        atomLocalProperty = XInternAtom(pDisplay, WIN_LOCAL_PROPERTY, False);
        atomUTF8String = XInternAtom(pDisplay, "UTF8_STRING", False);
        atomCompoundText = XInternAtom(pDisplay, "COMPOUND_TEXT", False);
        atomTargets = XInternAtom(pDisplay, "TARGETS", False);
    }

    /* Process all pending events */
    while (XPending(pDisplay)) {
        XTextProperty xtpText = { 0 };
        XEvent event;
        XSelectionEvent eventSelection;
        unsigned long ulReturnBytesLeft;
        char *pszReturnData = NULL;
        char *pszGlobalData = NULL;
        int iReturn;
        HGLOBAL hGlobal = NULL;
        XICCEncodingStyle xiccesStyle;
        int iConvertDataLen = 0;
        char *pszConvertData = NULL;
        char *pszTextList[2] = { NULL };
        int iCount;
        char **ppszTextList = NULL;
        wchar_t *pwszUnicodeStr = NULL;
        int iUnicodeLen = 0;
        int iReturnDataLen = 0;
        int i;
        Bool fAbort = FALSE;
        Bool fCloseClipboard = FALSE;
        Bool fSetClipboardData = TRUE;

        /* Get the next event - will not block because one is ready */
        XNextEvent(pDisplay, &event);

        /* Branch on the event type */
        switch (event.type) {
            /*
             * SelectionRequest
             */

        case SelectionRequest:
        {
            char *pszAtomName = NULL;

            winDebug("SelectionRequest - target %d\n",
                     event.xselectionrequest.target);

            pszAtomName = XGetAtomName(pDisplay,
                                       event.xselectionrequest.target);
            winDebug("SelectionRequest - Target atom name %s\n", pszAtomName);
            XFree(pszAtomName);
            pszAtomName = NULL;
        }

            /* Abort if invalid target type */
            if (event.xselectionrequest.target != XA_STRING
                && event.xselectionrequest.target != atomUTF8String
                && event.xselectionrequest.target != atomCompoundText
                && event.xselectionrequest.target != atomTargets) {
                /* Abort */
                fAbort = TRUE;
                goto winClipboardFlushXEvents_SelectionRequest_Done;
            }

            /* Handle targets type of request */
            if (event.xselectionrequest.target == atomTargets) {
                Atom atomTargetArr[] = { atomTargets,
                    atomCompoundText,
                    atomUTF8String,
                    XA_STRING
                };

                /* Try to change the property */
                iReturn = XChangeProperty(pDisplay,
                                          event.xselectionrequest.requestor,
                                          event.xselectionrequest.property,
                                          XA_ATOM,
                                          32,
                                          PropModeReplace,
                                          (unsigned char *) atomTargetArr,
                                          (sizeof(atomTargetArr)
                                           / sizeof(atomTargetArr[0])));
                if (iReturn == BadAlloc
                    || iReturn == BadAtom
                    || iReturn == BadMatch
                    || iReturn == BadValue || iReturn == BadWindow) {
                    ErrorF("winClipboardFlushXEvents - SelectionRequest - "
                           "XChangeProperty failed: %d\n", iReturn);
                }

                /* Setup selection notify xevent */
                eventSelection.type = SelectionNotify;
                eventSelection.send_event = True;
                eventSelection.display = pDisplay;
                eventSelection.requestor = event.xselectionrequest.requestor;
                eventSelection.selection = event.xselectionrequest.selection;
                eventSelection.target = event.xselectionrequest.target;
                eventSelection.property = event.xselectionrequest.property;
                eventSelection.time = event.xselectionrequest.time;

                /*
                 * Notify the requesting window that
                 * the operation has completed
                 */
                iReturn = XSendEvent(pDisplay,
                                     eventSelection.requestor,
                                     False, 0L, (XEvent *) & eventSelection);
                if (iReturn == BadValue || iReturn == BadWindow) {
                    ErrorF("winClipboardFlushXEvents - SelectionRequest - "
                           "XSendEvent () failed\n");
                }
                break;
            }

            /* Check that clipboard format is available */
            if (fUseUnicode && !IsClipboardFormatAvailable(CF_UNICODETEXT)) {
                static int count;       /* Hack to stop acroread spamming the log */
                static HWND lasthwnd;   /* I've not seen any other client get here repeatedly? */

                if (hwnd != lasthwnd)
                    count = 0;
                count++;
                if (count < 6)
                    ErrorF("winClipboardFlushXEvents - CF_UNICODETEXT is not "
                           "available from Win32 clipboard.  Aborting %d.\n",
                           count);
                lasthwnd = hwnd;

                /* Abort */
                fAbort = TRUE;
                goto winClipboardFlushXEvents_SelectionRequest_Done;
            }
            else if (!fUseUnicode && !IsClipboardFormatAvailable(CF_TEXT)) {
                ErrorF("winClipboardFlushXEvents - CF_TEXT is not "
                       "available from Win32 clipboard.  Aborting.\n");

                /* Abort */
                fAbort = TRUE;
                goto winClipboardFlushXEvents_SelectionRequest_Done;
            }

            /* Close clipboard if we have it open already */
            if (GetOpenClipboardWindow() == hwnd) {
                CloseClipboard();
            }

            /* Access the clipboard */
            if (!OpenClipboard(hwnd)) {
                ErrorF("winClipboardFlushXEvents - SelectionRequest - "
                       "OpenClipboard () failed: %08lx\n", GetLastError());

                /* Abort */
                fAbort = TRUE;
                goto winClipboardFlushXEvents_SelectionRequest_Done;
            }

            /* Indicate that clipboard was opened */
            fCloseClipboard = TRUE;

            /* Setup the string style */
            if (event.xselectionrequest.target == XA_STRING)
                xiccesStyle = XStringStyle;
#ifdef X_HAVE_UTF8_STRING
            else if (event.xselectionrequest.target == atomUTF8String)
                xiccesStyle = XUTF8StringStyle;
#endif
            else if (event.xselectionrequest.target == atomCompoundText)
                xiccesStyle = XCompoundTextStyle;
            else
                xiccesStyle = XStringStyle;

            /* Get a pointer to the clipboard text, in desired format */
            if (fUseUnicode) {
                /* Retrieve clipboard data */
                hGlobal = GetClipboardData(CF_UNICODETEXT);
            }
            else {
                /* Retrieve clipboard data */
                hGlobal = GetClipboardData(CF_TEXT);
            }
            if (!hGlobal) {
                ErrorF("winClipboardFlushXEvents - SelectionRequest - "
                       "GetClipboardData () failed: %08lx\n", GetLastError());

                /* Abort */
                fAbort = TRUE;
                goto winClipboardFlushXEvents_SelectionRequest_Done;
            }
            pszGlobalData = (char *) GlobalLock(hGlobal);

            /* Convert the Unicode string to UTF8 (MBCS) */
            if (fUseUnicode) {
                iConvertDataLen = WideCharToMultiByte(CP_UTF8,
                                                      0,
                                                      (LPCWSTR) pszGlobalData,
                                                      -1, NULL, 0, NULL, NULL);
                /* NOTE: iConvertDataLen includes space for null terminator */
                pszConvertData = (char *) malloc(iConvertDataLen);
                WideCharToMultiByte(CP_UTF8,
                                    0,
                                    (LPCWSTR) pszGlobalData,
                                    -1,
                                    pszConvertData,
                                    iConvertDataLen, NULL, NULL);
            }
            else {
                pszConvertData = strdup(pszGlobalData);
                iConvertDataLen = strlen(pszConvertData) + 1;
            }

            /* Convert DOS string to UNIX string */
            winClipboardDOStoUNIX(pszConvertData, strlen(pszConvertData));

            /* Setup our text list */
            pszTextList[0] = pszConvertData;
            pszTextList[1] = NULL;

            /* Initialize the text property */
            xtpText.value = NULL;
            xtpText.nitems = 0;

            /* Create the text property from the text list */
            if (fUseUnicode) {
#ifdef X_HAVE_UTF8_STRING
                iReturn = Xutf8TextListToTextProperty(pDisplay,
                                                      pszTextList,
                                                      1, xiccesStyle, &xtpText);
#endif
            }
            else {
                iReturn = XmbTextListToTextProperty(pDisplay,
                                                    pszTextList,
                                                    1, xiccesStyle, &xtpText);
            }
            if (iReturn == XNoMemory || iReturn == XLocaleNotSupported) {
                ErrorF("winClipboardFlushXEvents - SelectionRequest - "
                       "X*TextListToTextProperty failed: %d\n", iReturn);

                /* Abort */
                fAbort = TRUE;
                goto winClipboardFlushXEvents_SelectionRequest_Done;
            }

            /* Free the converted string */
            free(pszConvertData);
            pszConvertData = NULL;

            /* Copy the clipboard text to the requesting window */
            iReturn = XChangeProperty(pDisplay,
                                      event.xselectionrequest.requestor,
                                      event.xselectionrequest.property,
                                      event.xselectionrequest.target,
                                      8,
                                      PropModeReplace,
                                      xtpText.value, xtpText.nitems);
            if (iReturn == BadAlloc || iReturn == BadAtom
                || iReturn == BadMatch || iReturn == BadValue
                || iReturn == BadWindow) {
                ErrorF("winClipboardFlushXEvents - SelectionRequest - "
                       "XChangeProperty failed: %d\n", iReturn);

                /* Abort */
                fAbort = TRUE;
                goto winClipboardFlushXEvents_SelectionRequest_Done;
            }

            /* Release the clipboard data */
            GlobalUnlock(hGlobal);
            pszGlobalData = NULL;
            fCloseClipboard = FALSE;
            CloseClipboard();

            /* Clean up */
            XFree(xtpText.value);
            xtpText.value = NULL;
            xtpText.nitems = 0;

            /* Setup selection notify event */
            eventSelection.type = SelectionNotify;
            eventSelection.send_event = True;
            eventSelection.display = pDisplay;
            eventSelection.requestor = event.xselectionrequest.requestor;
            eventSelection.selection = event.xselectionrequest.selection;
            eventSelection.target = event.xselectionrequest.target;
            eventSelection.property = event.xselectionrequest.property;
            eventSelection.time = event.xselectionrequest.time;

            /* Notify the requesting window that the operation has completed */
            iReturn = XSendEvent(pDisplay,
                                 eventSelection.requestor,
                                 False, 0L, (XEvent *) & eventSelection);
            if (iReturn == BadValue || iReturn == BadWindow) {
                ErrorF("winClipboardFlushXEvents - SelectionRequest - "
                       "XSendEvent () failed\n");

                /* Abort */
                fAbort = TRUE;
                goto winClipboardFlushXEvents_SelectionRequest_Done;
            }

 winClipboardFlushXEvents_SelectionRequest_Done:
            /* Free allocated resources */
            if (xtpText.value) {
                XFree(xtpText.value);
                xtpText.value = NULL;
                xtpText.nitems = 0;
            }
            free(pszConvertData);
            if (hGlobal && pszGlobalData)
                GlobalUnlock(hGlobal);

            /*
             * Send a SelectionNotify event to the requesting
             * client when we abort.
             */
            if (fAbort) {
                /* Setup selection notify event */
                eventSelection.type = SelectionNotify;
                eventSelection.send_event = True;
                eventSelection.display = pDisplay;
                eventSelection.requestor = event.xselectionrequest.requestor;
                eventSelection.selection = event.xselectionrequest.selection;
                eventSelection.target = event.xselectionrequest.target;
                eventSelection.property = None;
                eventSelection.time = event.xselectionrequest.time;

                /* Notify the requesting window that the operation is complete */
                iReturn = XSendEvent(pDisplay,
                                     eventSelection.requestor,
                                     False, 0L, (XEvent *) & eventSelection);
                if (iReturn == BadValue || iReturn == BadWindow) {
                    /*
                     * Should not be a problem if XSendEvent fails because
                     * the client may simply have exited.
                     */
                    ErrorF("winClipboardFlushXEvents - SelectionRequest - "
                           "XSendEvent () failed for abort event.\n");
                }
            }

            /* Close clipboard if it was opened */
            if (fCloseClipboard) {
                fCloseClipboard = FALSE;
                CloseClipboard();
            }
            break;

            /*
             * SelectionNotify
             */

        case SelectionNotify:

            winDebug("winClipboardFlushXEvents - SelectionNotify\n");
            {
                char *pszAtomName;

                pszAtomName = XGetAtomName(pDisplay,
                                           event.xselection.selection);

                winDebug
                    ("winClipboardFlushXEvents - SelectionNotify - ATOM: %s\n",
                     pszAtomName);
                XFree(pszAtomName);
            }

            /*
             * Request conversion of UTF8 and CompoundText targets.
             */
            if (event.xselection.property == None) {
                if (event.xselection.target == XA_STRING) {
                    winDebug("winClipboardFlushXEvents - SelectionNotify - "
                             "XA_STRING\n");

                    return WIN_XEVENTS_CONVERT;
                }
                else if (event.xselection.target == atomUTF8String) {
                    winDebug("winClipboardFlushXEvents - SelectionNotify - "
                             "Requesting conversion of UTF8 target.\n");

                    XConvertSelection(pDisplay,
                                      event.xselection.selection,
                                      XA_STRING,
                                      atomLocalProperty, iWindow, CurrentTime);

                    /* Process the ConvertSelection event */
                    XFlush(pDisplay);
                    return WIN_XEVENTS_CONVERT;
                }
#ifdef X_HAVE_UTF8_STRING
                else if (event.xselection.target == atomCompoundText) {
                    winDebug("winClipboardFlushXEvents - SelectionNotify - "
                             "Requesting conversion of CompoundText target.\n");

                    XConvertSelection(pDisplay,
                                      event.xselection.selection,
                                      atomUTF8String,
                                      atomLocalProperty, iWindow, CurrentTime);

                    /* Process the ConvertSelection event */
                    XFlush(pDisplay);
                    return WIN_XEVENTS_CONVERT;
                }
#endif
                else {
                    ErrorF("winClipboardFlushXEvents - SelectionNotify - "
                           "Unknown format.  Cannot request conversion, "
                           "aborting.\n");
                    break;
                }
            }

            /* Retrieve the size of the stored data */
            iReturn = XGetWindowProperty(pDisplay, iWindow, atomLocalProperty, 0, 0,    /* Don't get data, just size */
                                         False,
                                         AnyPropertyType,
                                         &xtpText.encoding,
                                         &xtpText.format,
                                         &xtpText.nitems,
                                         &ulReturnBytesLeft, &xtpText.value);
            if (iReturn != Success) {
                ErrorF("winClipboardFlushXEvents - SelectionNotify - "
                       "XGetWindowProperty () failed, aborting: %d\n", iReturn);
                break;
            }

            winDebug("SelectionNotify - returned data %d left %d\n",
                     xtpText.nitems, ulReturnBytesLeft);

            /* Request the selection data */
            iReturn = XGetWindowProperty(pDisplay,
                                         iWindow,
                                         atomLocalProperty,
                                         0,
                                         ulReturnBytesLeft,
                                         False,
                                         AnyPropertyType,
                                         &xtpText.encoding,
                                         &xtpText.format,
                                         &xtpText.nitems,
                                         &ulReturnBytesLeft, &xtpText.value);
            if (iReturn != Success) {
                ErrorF("winClipboardFlushXEvents - SelectionNotify - "
                       "XGetWindowProperty () failed, aborting: %d\n", iReturn);
                break;
            }

            {
                char *pszAtomName = NULL;

                winDebug("SelectionNotify - returned data %d left %d\n",
                         xtpText.nitems, ulReturnBytesLeft);
                pszAtomName = XGetAtomName(pDisplay, xtpText.encoding);
                winDebug("Notify atom name %s\n", pszAtomName);
                XFree(pszAtomName);
                pszAtomName = NULL;
            }

            if (fUseUnicode) {
#ifdef X_HAVE_UTF8_STRING
                /* Convert the text property to a text list */
                iReturn = Xutf8TextPropertyToTextList(pDisplay,
                                                      &xtpText,
                                                      &ppszTextList, &iCount);
#endif
            }
            else {
                iReturn = XmbTextPropertyToTextList(pDisplay,
                                                    &xtpText,
                                                    &ppszTextList, &iCount);
            }
            if (iReturn == Success || iReturn > 0) {
                /* Conversion succeeded or some unconvertible characters */
                if (ppszTextList != NULL) {
                    iReturnDataLen = 0;
                    for (i = 0; i < iCount; i++) {
                        iReturnDataLen += strlen(ppszTextList[i]);
                    }
                    pszReturnData = malloc(iReturnDataLen + 1);
                    pszReturnData[0] = '\0';
                    for (i = 0; i < iCount; i++) {
                        strcat(pszReturnData, ppszTextList[i]);
                    }
                }
                else {
                    ErrorF("winClipboardFlushXEvents - SelectionNotify - "
                           "X*TextPropertyToTextList list_return is NULL.\n");
                    pszReturnData = malloc(1);
                    pszReturnData[0] = '\0';
                }
            }
            else {
                ErrorF("winClipboardFlushXEvents - SelectionNotify - "
                       "X*TextPropertyToTextList returned: ");
                switch (iReturn) {
                case XNoMemory:
                    ErrorF("XNoMemory\n");
                    break;
                case XLocaleNotSupported:
                    ErrorF("XLocaleNotSupported\n");
                    break;
                case XConverterNotFound:
                    ErrorF("XConverterNotFound\n");
                    break;
                default:
                    ErrorF("%d\n", iReturn);
                    break;
                }
                pszReturnData = malloc(1);
                pszReturnData[0] = '\0';
            }

            /* Free the data returned from XGetWindowProperty */
            if (ppszTextList)
                XFreeStringList(ppszTextList);
            ppszTextList = NULL;
            XFree(xtpText.value);
            xtpText.value = NULL;
            xtpText.nitems = 0;

            /* Convert the X clipboard string to DOS format */
            winClipboardUNIXtoDOS(&pszReturnData, strlen(pszReturnData));

            if (fUseUnicode) {
                /* Find out how much space needed to convert MBCS to Unicode */
                iUnicodeLen = MultiByteToWideChar(CP_UTF8,
                                                  0,
                                                  pszReturnData, -1, NULL, 0);

                /* Allocate memory for the Unicode string */
                pwszUnicodeStr
                    = (wchar_t *) malloc(sizeof(wchar_t) * (iUnicodeLen + 1));
                if (!pwszUnicodeStr) {
                    ErrorF("winClipboardFlushXEvents - SelectionNotify "
                           "malloc failed for pwszUnicodeStr, aborting.\n");

                    /* Abort */
                    fAbort = TRUE;
                    goto winClipboardFlushXEvents_SelectionNotify_Done;
                }

                /* Do the actual conversion */
                MultiByteToWideChar(CP_UTF8,
                                    0,
                                    pszReturnData,
                                    -1, pwszUnicodeStr, iUnicodeLen);

                /* Allocate global memory for the X clipboard data */
                hGlobal = GlobalAlloc(GMEM_MOVEABLE,
                                      sizeof(wchar_t) * (iUnicodeLen + 1));
            }
            else {
                pszConvertData = strdup(pszReturnData);
                iConvertDataLen = strlen(pszConvertData) + 1;

                /* Allocate global memory for the X clipboard data */
                hGlobal = GlobalAlloc(GMEM_MOVEABLE, iConvertDataLen);
            }

            free(pszReturnData);

            /* Check that global memory was allocated */
            if (!hGlobal) {
                ErrorF("winClipboardFlushXEvents - SelectionNotify "
                       "GlobalAlloc failed, aborting: %ld\n", GetLastError());

                /* Abort */
                fAbort = TRUE;
                goto winClipboardFlushXEvents_SelectionNotify_Done;
            }

            /* Obtain a pointer to the global memory */
            pszGlobalData = GlobalLock(hGlobal);
            if (pszGlobalData == NULL) {
                ErrorF("winClipboardFlushXEvents - Could not lock global "
                       "memory for clipboard transfer\n");

                /* Abort */
                fAbort = TRUE;
                goto winClipboardFlushXEvents_SelectionNotify_Done;
            }

            /* Copy the returned string into the global memory */
            if (fUseUnicode) {
                memcpy(pszGlobalData,
                       pwszUnicodeStr, sizeof(wchar_t) * (iUnicodeLen + 1));
                free(pwszUnicodeStr);
                pwszUnicodeStr = NULL;
            }
            else {
                strcpy(pszGlobalData, pszConvertData);
                free(pszConvertData);
                pszConvertData = NULL;
            }

            /* Release the pointer to the global memory */
            GlobalUnlock(hGlobal);
            pszGlobalData = NULL;

            /* Push the selection data to the Windows clipboard */
            if (fUseUnicode)
                SetClipboardData(CF_UNICODETEXT, hGlobal);
            else
                SetClipboardData(CF_TEXT, hGlobal);

            /* Flag that SetClipboardData has been called */
            fSetClipboardData = FALSE;

            /*
             * NOTE: Do not try to free pszGlobalData, it is owned by
             * Windows after the call to SetClipboardData ().
             */

 winClipboardFlushXEvents_SelectionNotify_Done:
            /* Free allocated resources */
            if (ppszTextList)
                XFreeStringList(ppszTextList);
            if (xtpText.value) {
                XFree(xtpText.value);
                xtpText.value = NULL;
                xtpText.nitems = 0;
            }
            free(pszConvertData);
            free(pwszUnicodeStr);
            if (hGlobal && pszGlobalData)
                GlobalUnlock(hGlobal);
            if (fSetClipboardData) {
                SetClipboardData(CF_UNICODETEXT, NULL);
                SetClipboardData(CF_TEXT, NULL);
            }
            return WIN_XEVENTS_NOTIFY;

        case SelectionClear:
            winDebug("SelectionClear - doing nothing\n");
            break;

        case PropertyNotify:
            break;

        case MappingNotify:
            break;

        default:
            ErrorF("winClipboardFlushXEvents - unexpected event type %d\n",
                   event.type);
            break;
        }
    }

    return WIN_XEVENTS_SUCCESS;
}
