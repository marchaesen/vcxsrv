/*
 * Export window information for the Windows-OpenGL GLX implementation.
 *
 * Authors: Alexander Gottwald
 */

#include <X11/Xwindows.h>
#include <windowstr.h>

#define WIN_GL_WINDOW_CLASS "XWinGLTest"

HWND winGetWindowInfo(WindowPtr pWin);
Bool winCheckScreenAiglxIsSupported(ScreenPtr pScreen);
void winSetScreenAiglxIsActive(ScreenPtr pScreen);
