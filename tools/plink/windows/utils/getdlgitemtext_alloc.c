/*
 * Handy wrappers around GetDlgItemText (A and W) which don't make you
 * invent an arbitrary length limit on the output string. Returned
 * string is dynamically allocated; caller must free.
 */

#include <wchar.h>

#include "putty.h"

char *GetDlgItemText_alloc(HWND hwnd, int id)
{
    HWND item = GetDlgItem(hwnd, id);
    size_t size = GetWindowTextLengthA(item) + 1;
    char *text = snewn(size, char);
    GetWindowTextA(item, text, size);
    return text;
}

wchar_t *GetDlgItemTextW_alloc(HWND hwnd, int id)
{
    HWND item = GetDlgItem(hwnd, id);
    size_t size = GetWindowTextLengthW(item) + 1;
    wchar_t *text = snewn(size, wchar_t);
    GetWindowTextW(item, text, size);
    return text;
}
