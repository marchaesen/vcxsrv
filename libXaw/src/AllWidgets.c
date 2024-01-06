/*

Copyright (c) 1991, 1994, 1998  The Open Group

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
#include <X11/IntrinsicP.h>
#include <X11/Xaw/AllWidgets.h>

#include <X11/Composite.h>
#include <X11/Constraint.h>
#include <X11/Core.h>
#include <X11/Object.h>
#include <X11/RectObj.h>
#include <X11/Shell.h>
#include <X11/Vendor.h>
#include <X11/Xaw/AsciiSink.h>
#include <X11/Xaw/AsciiText.h>
#include <X11/Xaw/Box.h>
#include <X11/Xaw/Dialog.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Grip.h>
#include <X11/Xaw/List.h>
#include <X11/Xaw/MenuButton.h>
#include <X11/Xaw/MultiSink.h>
#include <X11/Xaw/Paned.h>
#include <X11/Xaw/Panner.h>
#include <X11/Xaw/Porthole.h>
#include <X11/Xaw/Repeater.h>
#include <X11/Xaw/Scrollbar.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/Sme.h>
#include <X11/Xaw/SmeBSB.h>
#include <X11/Xaw/SmeLine.h>
#include <X11/Xaw/StripChart.h>
#include <X11/Xaw/Toggle.h>
#include <X11/Xaw/Tree.h>
#include <X11/Xaw/Viewport.h>

#define DATA(name,class) { (char *)name, class }
XmuWidgetNode XawWidgetArray[] = {
DATA( "applicationShell", &applicationShellWidgetClass ),
DATA( "asciiSink", &asciiSinkObjectClass ),
DATA( "asciiSrc", &asciiSrcObjectClass ),
DATA( "asciiText", &asciiTextWidgetClass ),
DATA( "box", &boxWidgetClass ),
DATA( "command", &commandWidgetClass ),
DATA( "composite", &compositeWidgetClass ),
DATA( "constraint", &constraintWidgetClass ),
DATA( "core", &coreWidgetClass ),
DATA( "dialog", &dialogWidgetClass ),
DATA( "form", &formWidgetClass ),
DATA( "grip", &gripWidgetClass ),
DATA( "label", &labelWidgetClass ),
DATA( "list", &listWidgetClass ),
DATA( "menuButton", &menuButtonWidgetClass ),
DATA( "multiSink", &multiSinkObjectClass ),
DATA( "multiSrc", &multiSrcObjectClass ),
DATA( "object", &objectClass ),
DATA( "overrideShell", &overrideShellWidgetClass ),
DATA( "paned", &panedWidgetClass ),
DATA( "panner", &pannerWidgetClass ),
DATA( "porthole", &portholeWidgetClass ),
DATA( "rect", &rectObjClass ),
DATA( "repeater", &repeaterWidgetClass ),
DATA( "scrollbar", &scrollbarWidgetClass ),
DATA( "shell", &shellWidgetClass ),
DATA( "simpleMenu", &simpleMenuWidgetClass ),
DATA( "simple", &simpleWidgetClass ),
DATA( "smeBSB", &smeBSBObjectClass ),
DATA( "smeLine", &smeLineObjectClass ),
DATA( "sme", &smeObjectClass ),
DATA( "stripChart", &stripChartWidgetClass ),
DATA( "textSink", &textSinkObjectClass ),
DATA( "textSrc", &textSrcObjectClass ),
DATA( "text", &textWidgetClass ),
DATA( "toggle", &toggleWidgetClass ),
DATA( "topLevelShell", &topLevelShellWidgetClass ),
DATA( "transientShell", &transientShellWidgetClass ),
DATA( "tree", &treeWidgetClass ),
DATA( "vendorShell", &vendorShellWidgetClass ),
DATA( "viewport", &viewportWidgetClass ),
DATA( "wmShell", &wmShellWidgetClass ),
};
#undef DATA

int XawWidgetCount = XtNumber(XawWidgetArray);

