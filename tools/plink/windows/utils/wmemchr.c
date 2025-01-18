/*
 * Work around lack of wmemchr in older MSVC libraries.
 */

#include <wchar.h>

#include "defs.h"

wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n)
{
    for (; n != 0; s++, n--)
        if (*s == c)
            return (wchar_t *)s;
    return NULL;
}
