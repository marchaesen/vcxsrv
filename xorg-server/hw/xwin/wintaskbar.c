/*
  Permission is hereby granted, free of charge, to any person obtaining a
  copy of this software and associated documentation files (the "Software"),
  to deal in the Software without restriction, including without limitation
  the rights to use, copy, modify, merge, publish, distribute, sublicense,
  and/or sell copies of the Software, and to permit persons to whom the
  Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice (including the next
  paragraph) shall be included in all copies or substantial portions of the
  Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
  DEALINGS IN THE SOFTWARE.
*/

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif

#include "win.h"
#include "winwindow.h"

#include <shobjidl.h>

/*
  This is unnecessarily heavyweight, we could just call CoInitialize() once at
   startup and CoUninitialize() once at shutdown
*/

/*
  The documentation for ITaskbarList::AddTab says that we are responsible for
   deleting the tab ourselves when the window is deleted, but that doesn't actually
   seem to be the case
*/

void winShowWindowOnTaskbar(HWND hWnd, Bool show)
{
  ITaskbarList* pTaskbarList = NULL;

  if (SUCCEEDED(CoInitialize(NULL)))
    {
      if (SUCCEEDED(CoCreateInstance((const CLSID *)&CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER, (const IID *)&IID_ITaskbarList, (void**)&pTaskbarList)))
        {
          if (SUCCEEDED(pTaskbarList->lpVtbl->HrInit(pTaskbarList)))
            {
              if (show)
                {
                  pTaskbarList->lpVtbl->AddTab(pTaskbarList,hWnd);
                }
              else
                {
                  pTaskbarList->lpVtbl->DeleteTab(pTaskbarList,hWnd);
                }
            }
          pTaskbarList->lpVtbl->Release(pTaskbarList);
        }
      CoUninitialize();
    }
}
