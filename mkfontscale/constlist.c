/*
  Copyright (c) 2002-2003 by Juliusz Chroboczek
  Copyright (c) 2015 by Thomas Klausner

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

#include <stdlib.h>
#include "constlist.h"

ConstListPtr
appendConstList(ConstListPtr first, ConstListPtr second)
{
    ConstListPtr current;

    if(second == NULL)
        return first;

    if(first == NULL)
        return second;

    for(current = first; current->next; current = current->next)
        ;

    current->next = second;
    return first;
}

ConstListPtr
makeConstList(const char **a, int n, ConstListPtr old, int begin)
{
    ConstListPtr first, current, next;
    int i;

    if(n == 0)
        return old;

    first = malloc(sizeof(ConstListRec));
    if(!first)
        return NULL;

    first->value = a[0];
    first->next = NULL;

    current = first;
    for(i = 1; i < n; i++) {
        next = malloc(sizeof(ConstListRec));
        if(!next) {
            destroyConstList(first);
            return NULL;
        }
        next->value = a[i];
        next->next = NULL;

        current->next = next;
        current = next;
    }
    if(begin) {
        current->next = old;
        return first;
    } else {
        return appendConstList(old, first);
    }
}

void
destroyConstList(ConstListPtr old)
{
    ConstListPtr next;
    if(!old)
        return;
    while(old) {
        next = old->next;
        free(old);
        old = next;
    }
}
