/*
 *	File:  xtestext1lib.c
 *
 *	This file contains the Xlib parts of the input synthesis extension
 */

/*


Copyright 1986, 1987, 1988, 1998   The Open Group

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


Copyright 1986, 1987, 1988 by Hewlett-Packard Corporation

Permission to use, copy, modify, and distribute this
software and its documentation for any purpose and without
fee is hereby granted, provided that the above copyright
notice appear in all copies and that both that copyright
notice and this permission notice appear in supporting
documentation, and that the name of Hewlett-Packard not be used in
advertising or publicity pertaining to distribution of the
software without specific, written prior permission.

Hewlett-Packard makes no representations about the
suitability of this software for any purpose.  It is provided
"as is" without express or implied warranty.

This software is not subject to any license of the American
Telephone and Telegraph Company or of the Regents of the
University of California.

*/

/******************************************************************************
 * include files
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <X11/Xproto.h>
#include <X11/Xlibint.h>
#include <X11/extensions/xtestext1.h>
#include <X11/extensions/xtestext1proto.h>

/******************************************************************************
 * variables
 *****************************************************************************/

/*
 * Holds the request type code for this extension.  The request type code
 * for this extension may vary depending on how many extensions are installed
 * already, so the initial value given below will be added to the base request
 * code that is acquired when this extension is installed.
 */
static int		XTestReqCode = 0;
/*
 * Holds the two event type codes for this extension.  The event type codes
 * for this extension may vary depending on how many extensions are installed
 * already, so the initial values given below will be added to the base event
 * code that is acquired when this extension is installed.
 *
 * These two variables must be available to programs that use this extension.
 */
int			XTestInputActionType = 0;
int			XTestFakeAckType   = 1;
/*
 * holds the current x and y coordinates for XTestMovePointer
 */
static int	current_x = 0;
static int	current_y = 0;
/*
 * Holds input actions being accumulated until the input action buffer is
 * full or until XTestFlush is called.
 */
static CARD8		action_buf[XTestMAX_ACTION_LIST_SIZE];
/*
 * the index into the input action buffer
 */
static int		action_index = 0;
/*
 * the number of input actions that the server can handle at one time
 */
static unsigned long	action_array_size = 0;
/*
 * the current number of input actions
 */
static unsigned long	action_count = 0;

/******************************************************************************
 * function declarations
 *****************************************************************************/

static int	XTestWireToEvent(Display *dpy, XEvent *reTemp, xEvent *eventTemp);
static int	XTestCheckExtInit(register Display *dpy);
static Bool	XTestIdentifyMyEvent(Display *display, XEvent *event_ptr, char *args);
static int	XTestInitExtension(register Display *dpy);
static int	XTestKeyOrButton(Display *display, int device_id, long unsigned int delay, unsigned int code, unsigned int action);
static int	XTestCheckDelay(Display *display, long unsigned int *delay_addr);
static int	XTestPackInputAction(Display *display, CARD8 *action_addr, int action_size);
static int	XTestWriteInputActions(Display *display, char *action_list_addr, int action_list_size, int ack_flag);

/******************************************************************************
 *
 *	XTestFakeInput
 *
 *	Send a a request containing one or more input actions to be sent
 *	to the server by this extension.
 */
int
XTestFakeInput(
/*
 * the connection to the X server
 */
register Display	*dpy,
/*
 * the address of a list of input actions to be sent to the server
 */
char			*action_list_addr,
/*
 * the size (in bytes) of the list of input actions
 */
int			action_list_size,
/*
 * specifies whether the server needs to send an event to indicate that its
 * input action buffer is empty
 */
int			ack_flag)
{
	/*
	 * pointer to xTestFakeInputReq structure
	 */
	xTestFakeInputReq	*req;
	/*
	 * loop index
	 */
	int			i;

	LockDisplay(dpy);
	if ((XTestCheckExtInit(dpy) == -1) ||
	    (action_list_size > XTestMAX_ACTION_LIST_SIZE))
	{
		/*
		 * if the extension is not installed in the server or the
		 * action list will not fit in the request, then unlock
		 * the display and return -1.
		 */
		UnlockDisplay(dpy);
		return(-1);
	}
	else
	{
		/*
		 * Get the next available X request packet in the buffer.
		 * It sets the `length' field to the size (in 32-bit words)
		 * of the request.  It also sets the `reqType' field in the
		 * request to X_TestFakeInput, which is not what is needed.
		 *
		 * GetReq is a macro defined in Xlibint.h.
		 */
		GetReq(TestFakeInput, req);
		/*
		 * fix up the request type code to what is needed
		 */
		req->reqType = XTestReqCode;
		/*
		 * set the minor request type code to X_TestFakeInput
		 */
		req->XTestReqType = X_TestFakeInput;
		/*
		 * set the ack code
		 */
		req->ack = ack_flag;
		/*
		 * Set the action_list area to all 0's. An input action header
		 * value of 0 is interpreted as a flag to the input action
		 * list handling code in the server part of this extension
		 * that there are no more input actions in this request.
		 */
		for (i = 0; i < XTestMAX_ACTION_LIST_SIZE; i++)
		{
			req->action_list[i] = 0;
		}
		/*
		 * copy the input actions into the request
		 */
		for (i = 0; i < action_list_size; i++)
		{
			req->action_list[i] = *(action_list_addr++);
		}
		UnlockDisplay(dpy);
		SyncHandle();
		return(0);
	}
}

/******************************************************************************
 *
 *	XTestGetInput
 *
 *	Request the server to begin putting user input actions into events
 *	to be sent to the client that called this function.
 */
int
XTestGetInput(
/*
 * the connection to the X server
 */
register Display	*dpy,
/*
 * tells the server what to do with the user input actions
 */
int			action_handling)
{
	/*
	 * pointer to xTestGetInputReq structure
	 */
	xTestGetInputReq 	*req;

	LockDisplay(dpy);
	if (XTestCheckExtInit(dpy) == -1)
	{
		/*
		 * if the extension is not installed in the server
		 * then unlock the display and return -1.
		 */
		UnlockDisplay(dpy);
		return(-1);
	}
	else
	{
		/*
		 * Get the next available X request packet in the buffer.
		 * It sets the `length' field to the size (in 32-bit words)
		 * of the request.  It also sets the `reqType' field in the
		 * request to X_TestGetInput, which is not what is needed.
		 *
		 * GetReq is a macro defined in Xlibint.h.
		 */
		GetReq(TestGetInput, req);
		/*
		 * fix up the request type code to what is needed
		 */
		req->reqType = XTestReqCode;
		/*
		 * set the minor request type code to X_TestGetInput
		 */
		req->XTestReqType = X_TestGetInput;
		/*
		 * set the action handling mode
		 */
		req->mode = action_handling;
		UnlockDisplay(dpy);
		SyncHandle();
		return(0);
	}
}

/******************************************************************************
 *
 *	XTestStopInput
 *
 *	Tell the server to stop putting information about user input actions
 *	into events.
 */
int
XTestStopInput(
/*
 * the connection to the X server
 */
register Display	*dpy)
{
	/*
	 * pointer to xTestStopInputReq structure
	 */
	xTestStopInputReq 	*req;

	LockDisplay(dpy);
	if (XTestCheckExtInit(dpy) == -1)
	{
		/*
		 * if the extension is not installed in the server
		 * then unlock the display and return -1.
		 */
		UnlockDisplay(dpy);
		return(-1);
	}
	else
	{
		/*
		 * Get the next available X request packet in the buffer.
		 * It sets the `length' field to the size (in 32-bit words)
		 * of the request.  It also sets the `reqType' field in the
		 * request to X_TestStopInput, which is not what is needed.
		 *
		 * GetReq is a macro defined in Xlibint.h.
		 */
		GetReq(TestStopInput, req);
		/*
		 * fix up the request type code to what is needed
		 */
		req->reqType = XTestReqCode;
		/*
		 * set the minor request type code to X_TestStopInput
		 */
		req->XTestReqType = X_TestStopInput;
		UnlockDisplay(dpy);
		SyncHandle();
		return(0);
	}
}

/******************************************************************************
 *
 *	XTestReset
 *
 *	Tell the server to set everything having to do with this extension
 *	back to its initial state.
 */
int
XTestReset(
/*
 * the connection to the X server
 */
register Display	*dpy)
{
	/*
	 * pointer to xTestReset structure
	 */
	xTestResetReq 	*req;

	LockDisplay(dpy);
	if (XTestCheckExtInit(dpy) == -1)
	{
		/*
		 * if the extension is not installed in the server
		 * then unlock the display and return -1.
		 */
		UnlockDisplay(dpy);
		return(-1);
	}
	else
	{
		/*
		 * Get the next available X request packet in the buffer.
		 * It sets the `length' field to the size (in 32-bit words)
		 * of the request.  It also sets the `reqType' field in the
		 * request to X_TestReset, which is not what is needed.
		 *
		 * GetReq is a macro defined in Xlibint.h.
		 */
		GetReq(TestReset, req);
		/*
		 * fix up the request type code to what is needed
		 */
		req->reqType = XTestReqCode;
		/*
		 * set the minor request type code to X_TestReset
		 */
		req->XTestReqType = X_TestReset;
		UnlockDisplay(dpy);
		SyncHandle();
		return(0);
	}
}

/******************************************************************************
 *
 *	XTestQueryInputSize
 *
 *	Returns the number of input actions in the server's input action buffer.
 */
int
XTestQueryInputSize(
/*
 * the connection to the X server
 */
register Display	*dpy,
/*
 * the address of the place to put the number of input actions in the
 * server's input action buffer
 */
unsigned long		*size_return)
{
	/*
	 * pointer to xTestQueryInputSize structure
	 */
	xTestQueryInputSizeReq 		*req;
	/*
	 * pointer to xTestQueryInputSize structure
	 */
	xTestQueryInputSizeReply 	rep;

	LockDisplay(dpy);
	if (XTestCheckExtInit(dpy) == -1)
	{
		/*
		 * if the extension is not installed in the server
		 * then unlock the display and return -1.
		 */
		UnlockDisplay(dpy);
		return(-1);
	}
	else
	{
		/*
		 * Get the next available X request packet in the buffer.
		 * It sets the `length' field to the size (in 32-bit words)
		 * of the request.  It also sets the `reqType' field in the
		 * request to X_TestQueryInputSize, which is not what is needed.
		 *
		 * GetReq is a macro defined in Xlibint.h.
		 */
		GetReq(TestQueryInputSize, req);
		/*
		 * fix up the request type code to what is needed
		 */
		req->reqType = XTestReqCode;
		/*
		 * set the minor request type code to X_TestQueryInputSize
		 */
		req->XTestReqType = X_TestQueryInputSize;
		/*
		 * get a reply from the server
		 */
		(void) _XReply (dpy, (xReply *) &rep, 0, xTrue);
		/*
		 * put the size in the caller's variable
		 */
		*size_return = (unsigned long) rep.size_return;
		UnlockDisplay(dpy);
		SyncHandle();
		return(0);
	}
}

/******************************************************************************
 *
 *	XTestCheckExtInit
 *
 *	Check to see if the XTest extension is installed in the server.
 */
static int
XTestCheckExtInit(
/*
 * the connection to the X server
 */
register Display	*dpy)
{
	/*
	 * if the extension has not been initialized, then do so
	 */
	if (!XTestReqCode)
	{
		return(XTestInitExtension(dpy));
	}
	return(0);
}

/******************************************************************************
 *
 *	XTestInitExtension
 *
 *	Attempt to initialize this extension in the server.  Return 0 if it
 *	succeeds, -1 if it does not succeed.
 */
static int
XTestInitExtension(
/*
 * the connection to the X server
 */
register Display	*dpy)
{
	/*
	 * loop index
	 */
	int			i;
	/*
	 * return value from XInitExtension
	 */
	XExtCodes		*ret;

	/*
	 * attempt to initialize the extension
	 */
	ret = XInitExtension(dpy, XTestEXTENSION_NAME);
	/*
	 * if the initialize failed, return -1
	 */
	if (ret == NULL)
	{
		return (-1);
	}
	/*
	 * the initialize succeeded, remember the major opcode
	 * for this extension
	 */
	XTestReqCode = ret->major_opcode;
	/*
	 * set up the event handler for any events from
	 * this extension
	 */
	for (i = 0; i < XTestEVENT_COUNT; i++)
	{
		XESetWireToEvent(dpy,
				 ret->first_event+i,
				 XTestWireToEvent);
	}
	/*
	 * compute the event type codes for the events
	 * in this extension
	 */
	XTestInputActionType += ret->first_event;
	XTestFakeAckType += ret->first_event;
	/*
	 * everything worked ok
	 */
	return(0);
}

/******************************************************************************
 *
 *	XTestWireToEvent
 *
 *	Handle XTest extension events.
 *	Reformat a wire event into an XEvent structure of the right type.
 */
static Bool
XTestWireToEvent(
/*
 * the connection to the X server
 */
Display	*dpy,
/*
 * a pointer to where a host formatted event should be stored
 * with the information copied to it
 */
XEvent	*reTemp,
/*
 * a pointer to the wire event
 */
xEvent	*eventTemp)
{
	XTestInputActionEvent *re    = (XTestInputActionEvent *) reTemp;
	xTestInputActionEvent *event = (xTestInputActionEvent *) eventTemp;

	/*
	 * loop index
	 */
	int	i;
	/*
	 * pointer to where the input actions go in the host format event
	 */
	CARD8	*to;
	/*
	 * pointer to the input actions in the wire event
	 */
	CARD8	*from;

	/*
	 * Copy the type of the wire event to the new event.
	 * This will work for either event type because the type,
	 * display, and window fields in the events have to be the same.
	 */
	re->type = event->type;
	/*
	 * set the display parameter in case it is needed (by who?)
	 */
	re->display = dpy;
	if (re->type == XTestInputActionType)
	{
		/*
		 * point at the first byte of input actions in the wire event
		 */
		from = &(event->actions[0]);
		/*
		 * point at where the input action bytes go in the new event
		 */
		to = &(re->actions[0]);
		/*
		 * copy the input action bytes from the wire event to
		 * the new event
		 */
		for (i = 0; i < XTestACTIONS_SIZE; i++)
		{
			*(to++) = *(from++);
		}
	}
	else if (re->type == XTestFakeAckType)
	{
		/*
		 * nothing else needs to be done
		 */
	}
	else
	{
		printf("XTestWireToEvent: UNKNOWN WIRE EVENT! type=%d\n",
			(int) event->type);
		printf("%s is giving up.\n", XTestEXTENSION_NAME);
		exit (1);
	}
	return 1;
}

/******************************************************************************
 *
 *	XTestPressKey
 *
 *	Send input actions to the server to cause the server to think
 *	that the specified key on the keyboard was moved as specified.
 */
int
XTestPressKey(
Display		*display,
int		device_id,
unsigned long	delay,
unsigned int	keycode,
unsigned int	key_action)
{
	/*
	 * bounds check the key code
	 */
	if (keycode < 8 || keycode > 255)
	{
		return(-1);
	}
	/*
	 * use the common key/button handling routine
	 */
	return(XTestKeyOrButton(display,
				device_id,
				delay,
				keycode,
				key_action));
}

/******************************************************************************
 *
 *	XTestPressButton
 *
 *	Send input actions to the server to cause the server to think
 *	that the specified button on the mouse was moved as specified.
 */
int
XTestPressButton(
Display		*display,
int		device_id,
unsigned long	delay,
unsigned int	button_number,
unsigned int	button_action)
{
	/*
	 * bounds check the button number
	 */
	if (button_number > 7)
	{
		return(-1);
	}
	/*
	 * use the common key/button handling routine
	 */
	return(XTestKeyOrButton(display,
				device_id,
				delay,
				button_number,
				button_action));
}

/******************************************************************************
 *
 *	XTestKeyOrButton
 *
 *	Send input actions to the server to cause the server to think
 *	that the specified key/button was moved as specified.
 */
static int
XTestKeyOrButton(
Display		*display,
int		device_id,
unsigned long	delay,
unsigned int	code,
unsigned int	action)
{
	/*
	 * holds a key input action to be filled out and sent to the server
	 */
	XTestKeyInfo	keyinfo;

	/*
	 * bounds check the device id
	 */
	if (device_id < 0 || device_id > XTestMAX_DEVICE_ID)
	{
		return(-1);
	}
	/*
	 * fill out the key input action(s) as appropriate
	 */
	switch(action)
	{
	case XTestPRESS:
		/*
		 * Check the delay.  If it is larger than will fit in the
		 * key input action, send a delay input action.
		 */
		if(XTestCheckDelay(display, &delay) == -1)
		{
			/*
			 * an error occurred, return -1
			 */
			return(-1);
		}
		/*
		 * create the header
		 */
		keyinfo.header = XTestPackDeviceID(device_id) |
				 XTestKEY_ACTION |
				 XTestKEY_DOWN;
		/*
		 * set the key/button code
		 */
		keyinfo.keycode = code;
		/*
		 * set the delay time
		 */
		keyinfo.delay_time = delay;
		/*
		 * pack the input action into a request to be sent to the
		 * server when the request is full or XTestFlush is called
		 */
		return(XTestPackInputAction(display,
					    (CARD8 *) &keyinfo,
					    sizeof(XTestKeyInfo)));
	case XTestRELEASE:
		/*
		 * Check the delay.  If it is larger than will fit in the
		 * key input action, send a delay input action.
		 */
		if(XTestCheckDelay(display, &delay) == -1)
		{
			/*
			 * an error occurred, return -1
			 */
			return(-1);
		}
		/*
		 * create the header
		 */
		keyinfo.header = XTestPackDeviceID(device_id) |
				 XTestKEY_ACTION |
				 XTestKEY_UP;
		/*
		 * set the key/button code
		 */
		keyinfo.keycode = code;
		/*
		 * set the delay time
		 */
		keyinfo.delay_time = delay;
		/*
		 * pack the input action into a request to be sent to the
		 * server when the request is full or XTestFlush is called
		 */
		return(XTestPackInputAction(display,
					    (CARD8 *) &keyinfo,
					    sizeof(XTestKeyInfo)));
	case XTestSTROKE:
		/*
		 * Check the delay.  If it is larger than will fit in the
		 * key input action, send a delay input action.
		 */
		if(XTestCheckDelay(display, &delay) == -1)
		{
			/*
			 * an error occurred, return -1
			 */
			return(-1);
		}
		/*
		 * create a key/button-down input action header
		 */
		keyinfo.header = XTestPackDeviceID(device_id) |
				 XTestKEY_ACTION |
				 XTestKEY_DOWN;
		/*
		 * set the key/button code
		 */
		keyinfo.keycode = code;
		/*
		 * set the delay time
		 */
		keyinfo.delay_time = delay;
		/*
		 * pack the input action into a request to be sent to the
		 * server when the request is full or XTestFlush is called
		 */
		if (XTestPackInputAction(display,
					 (CARD8 *) &keyinfo,
					 sizeof(XTestKeyInfo)) == -1)
		{
			/*
			 * an error occurred, return -1
			 */
			return(-1);
		}
		/*
		 * set the delay to XTestSTROKE_DELAY_TIME
		 */
		delay = XTestSTROKE_DELAY_TIME;
		/*
		 * Check the delay.  If it is larger than will fit in the
		 * key input action, send a delay input action.
		 */
		if(XTestCheckDelay(display, &delay) == -1)
		{
			/*
			 * an error occurred, return -1
			 */
			return(-1);
		}
		/*
		 * create a key/button-up input action header
		 */
		keyinfo.header = XTestPackDeviceID(device_id) |
				 XTestKEY_ACTION |
				 XTestKEY_UP;
		/*
		 * set the key/button code
		 */
		keyinfo.keycode = code;
		/*
		 * set the delay time
		 */
		keyinfo.delay_time = delay;
		/*
		 * pack the input action into a request to be sent to the
		 * server when the request is full or XTestFlush is called
		 */
		return(XTestPackInputAction(display,
					    (CARD8 *) &keyinfo,
					    sizeof(XTestKeyInfo)));
	default:
		/*
		 * invalid action value, return -1
		 */
		 return(-1);
	}
}

/******************************************************************************
 *
 *	XTestMovePointer
 *
 *	Send input actions to the server to cause the server to think
 *	that the mouse was moved as specified.
 */
int
XTestMovePointer(
Display		*display,
int		device_id,
unsigned long	delay[],
int		x[],
int		y[],
unsigned int	count)
{
	/*
	 * holds a motion input action to be filled out and sent to the server
	 */
	XTestMotionInfo	motioninfo;
	/*
	 * holds a jump input action to be filled out and sent to the server
	 */
	XTestJumpInfo	jumpinfo;
	/*
	 * loop index
	 */
	unsigned int	i;
	/*
	 * holds the change in x and y directions from the current x and y
	 * coordinates
	 */
	int	dx;
	int	dy;

	/*
	 * bounds check the device id
	 */
	if (device_id < 0 || device_id > XTestMAX_DEVICE_ID)
	{
		return(-1);
	}
	/*
	 * if the count is 0, there is nothing to do.  return 0
	 */
	if (count == 0)
	{
		return(0);
	}
	/*
	 * loop through the pointer motions, creating the appropriate
	 * input actions for each motion
	 */
	for (i = 0; i < count; i++)
	{
		/*
		 * Check the delay.  If it is larger than will fit in the
		 * input action, send a delay input action.
		 */
		if(XTestCheckDelay(display, &(delay[i])) == -1)
		{
			/*
			 * an error occurred, return -1
			 */
			return(-1);
		}
		/*
		 * compute the change from the current x and y coordinates
		 * to the new x and y coordinates
		 */
		dx = x[i] - current_x;
		dy = y[i] - current_y;
		/*
		 * update the current x and y coordinates
		 */
		current_x = x[i];
		current_y = y[i];
		/*
		 * If the pointer motion range is too large to fit into
		 * a motion input action, then use a jump input action.
		 * Otherwise, use a motion input action.
		 */
		 if ((dx > XTestMOTION_MAX) || (dx < XTestMOTION_MIN) ||
		     (dy > XTestMOTION_MAX) || (dy < XTestMOTION_MIN))
		{
			/*
			 * create a jump input action header
			 */
			jumpinfo.header = XTestPackDeviceID(device_id) |
					  XTestJUMP_ACTION;
			/*
			 * set the x and y coordinates to jump to
			 */
			jumpinfo.jumpx = x[i];
			jumpinfo.jumpy = y[i];
			/*
			 * set the delay time
			 */
			jumpinfo.delay_time = delay[i];
			/*
			 * pack the jump input action into a request to be
			 * sent to the server when the request is full
			 * or XTestFlush is called
			 */
			if (XTestPackInputAction(display,
						 (CARD8 *) &jumpinfo,
						 sizeof(XTestJumpInfo)) == -1)
			{
				/*
				 * an error occurred, return -1
				 */
				return(-1);
			}
		}
		else
		{
			/*
			 * create a motion input action header
			 */
			motioninfo.header = XTestPackDeviceID(device_id) |
					    XTestMOTION_ACTION;
			/*
			 * compute the motion data byte
			 */
			if (dx < 0)
			{
				motioninfo.header |= XTestX_NEGATIVE;
				dx = abs(dx);
			}
			if (dy < 0)
			{
				motioninfo.header |= XTestY_NEGATIVE;
				dy = abs(dy);
			}
			motioninfo.motion_data = XTestPackXMotionValue(dx);
			motioninfo.motion_data |= XTestPackYMotionValue(dy);
			/*
			 * set the delay time
			 */
			motioninfo.delay_time = delay[i];
			/*
			 * pack the motion input action into a request to be
			 * sent to the server when the request is full
			 * or XTestFlush is called
			 */
			if (XTestPackInputAction(display,
						 (CARD8 *) &motioninfo,
						 sizeof(XTestMotionInfo)) == -1)
			{
				/*
				 * an error occurred, return -1
				 */
				return(-1);
			}
		}
	}
	/*
	 * if you get here, everything went ok
	 */
	return(0);
}

/******************************************************************************
 *
 *	XTestCheckDelay
 *
 *	Check the delay value at the passed-in address.  If it is larger than
 *	will fit in a normal input action, then send a delay input action.
 */
static int
XTestCheckDelay(
Display		*display,
unsigned long	*delay_addr)
{
	/*
	 * holds a delay input action to be filled out and sent to the server
	 */
	XTestDelayInfo	delayinfo;

	/*
	 * if the delay value will fit in the input action,
	 * then there is no need for a delay input action
	 */
	if (*delay_addr <= XTestSHORT_DELAY_TIME)
	{
		return(0);
	}
	/*
	 * fill out a delay input action
	 */
	delayinfo.header = XTestPackDeviceID(XTestDELAY_DEVICE_ID);
	delayinfo.delay_time = *delay_addr;
	/*
	 * all of the delay time will be accounted for in the
	 * delay input action, so set the original delay value to 0
	 */
	*delay_addr = 0;
	/*
	 * pack the delay input action into a request to be sent to the
	 * server when the request is full or XTestFlush is called
	 */
	return(XTestPackInputAction(display,
				    (CARD8 *) &delayinfo,
				    sizeof(XTestDelayInfo)));
}

/******************************************************************************
 *
 *	XTestPackInputAction
 *
 *	If the input action buffer is full or the number of input actions
 *	has reached the maximum that the server can handle at one time,
 *	then send the input actions to the server using XTestFakeInput.
 */
static int
XTestPackInputAction(
Display	*display,
CARD8	*action_addr,
int	action_size)
{
	/*
	 * loop index
	 */
	int	i;
	/*
	 * acknowledge flag
	 */
	int	ack_flag;

	/*
	 * if we don't already know it, find out how many input actions
	 * the server can handle at one time
	 */
	if (action_array_size == 0)
	{
		if(XTestQueryInputSize(display, &action_array_size) == -1)
		{
			/*
			 * if an error, return -1
			 */
			return(-1);
		}
	}
	/*
	 * if the specified input action will fit in the the input
	 * action buffer and won't exceed the server's capacity, then
	 * put the input action into the input buffer
	 */
	if(((action_index + action_size) <= XTestMAX_ACTION_LIST_SIZE) &&
	   ((action_count + 1) < action_array_size))
	{
		/*
		 * copy the input action into the buffer
		 */
		for (i = 0; i < action_size; i++)
		{
			action_buf[action_index++] = *(action_addr++);
		}
		/*
		 * increment the action count
		 */
		action_count++;
		/*
		 * everything went ok, return 0
		 */
		return(0);
	}
	/*
	 * We have to write input actions to the server.  If the server's
	 * input action capacity will be reached, then ask for an
	 * acknowledge event when the server has processed all of the
	 * input actions.  Otherwise, an acknowledge event is not needed.
	 */
	if (action_count >= action_array_size)
	{
		ack_flag = XTestFAKE_ACK_REQUEST;
	}
	else
	{
		ack_flag = XTestFAKE_ACK_NOT_NEEDED;
	}
	/*
	 * write the input actions to the server
	 */
	if (XTestWriteInputActions(display,
				   (char *) &(action_buf[0]),
				   action_index,
				   ack_flag) == -1)
	{
		/*
		 * error, return -1
		 */
		return(-1);
	}
	/*
	 * copy the input action into the buffer
	 */
	for (i = 0; i < action_size; i++)
	{
		action_buf[action_index++] = *(action_addr++);
	}
	/*
	 * increment the action count
	 */
	action_count++;
	return(0);
}

/******************************************************************************
 *
 *	XTestWriteInputActions
 *
 *	Send input actions to the server.
 */
static int
XTestWriteInputActions(
Display	*display,
char	*action_list_addr,
int	action_list_size,
int	ack_flag)
{
	/*
	 * Holds an event.  Used while waiting for an acknowledge event
	 */
	XEvent	event;
	/*
	 * points to XTestIdentifyMyEvent
	 */
	Bool	(*func_ptr)(Display *, XEvent *, XPointer);

	/*
	 * write the input actions to the server
	 */
	if (XTestFakeInput(display,
			   action_list_addr,
			   action_list_size,
			   ack_flag) == -1)
	{
		/*
		 * if an error, return -1
		 */
		return(-1);
	}
	/*
	 * flush X's buffers to make sure that the server really gets
	 * the input actions
	 */
	XFlush(display);
	/*
	 * mark the input action buffer as empty
	 */
	action_index = 0;
	/*
	 * if we asked for an acknowledge event, then wait for it
	 */
	if (ack_flag == XTestFAKE_ACK_REQUEST)
	{
		/*
		 * point func_ptr at XTestIdentifyMyEvent
		 */
		func_ptr = XTestIdentifyMyEvent;
		/*
		 * Wait until the acknowledge event comes.  When the
		 * acknowledge event comes, it is removed from the event
		 * queue without disturbing any other events that might
		 * be in the queue.
		 */
		XIfEvent(display, &event, func_ptr, NULL);
		/*
		 * set the input action count back to 0
		 */
		action_count = 0;
	}
	/*
	 * if we got here, then everything is ok, return 0
	 */
	return(0);
}

/******************************************************************************
 *
 *	XTestIdentifyMyEvent
 *
 *	This function is called by XIfEvent to look at an event and see if
 *	it is of XTestFakeAckType.
 */
static	Bool
XTestIdentifyMyEvent(
Display	*display,
/*
 * Holds the event that this routine is supposed to look at.
 */
XEvent	*event_ptr,
/*
 * this points to any user-specified arguments (ignored)
 */
char	*args)
{
	/*
	 * if the event if of the correct type, return the Bool True,
	 * otherwise return the Bool False.
	 */
	if (event_ptr->type == XTestFakeAckType)
	{
		return(True);
	}
	else
	{
		return(False);
	}
}

/******************************************************************************
 *
 *	XTestFlush
 *
 *	Send any input actions in the input action buffer to the server.
 */
int
XTestFlush(Display *display)
{
	/*
	 * acknowledge flag
	 */
	int	ack_flag;

	/*
	 * if there are no input actions in the input action buffer,
	 * then return 0
	 */
	if (action_index == 0)
	{
		return(0);
	}
	/*
	 * We have input actions to write to the server.  We will
	 * wait until the server has finished processing the input actions.
	 */
	ack_flag = XTestFAKE_ACK_REQUEST;
	/*
	 * write the input actions to the server
	 */
	return(XTestWriteInputActions(display,
				      (char *) &(action_buf[0]),
				      action_index,
				      ack_flag));
}
