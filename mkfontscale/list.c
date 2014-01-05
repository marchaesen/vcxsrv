/*
  Copyright (c) 2002-2003 by Juliusz Chroboczek

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
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "list.h"

int
listMember(const char *elt, ListPtr list)
{
    while(list != NULL) {
        if(strcmp(elt, list->value) == 0)
            return 1;
        list = list->next;
    }
    return 0;
}

ListPtr
listCons(char *car, ListPtr cdr)
{
    ListPtr lcar = malloc(sizeof(ListRec));
    if(!lcar)
        return NULL;
    lcar -> value = car;
    lcar -> next = cdr;
    return lcar;
}

ListPtr
listAdjoin(char *car, ListPtr cdr)
{
    if(listMember(car, cdr)) {
        free(car);
        return cdr;
    }
    return listCons(car, cdr);
}

char *
dsprintf(const char *f, ...)
{
    va_list args;
    char *string;
    {
	int n, size = 20;
	while(1) {
	    if(size > 4096)
		return NULL;
	    string = malloc(size);
	    if(!string)
		return NULL;
	    va_start(args, f);
	    n = vsnprintf(string, size, f, args);
	    va_end(args);
	    if(n >= 0 && n < size)
                return string;
	    else if(n >= size)
		size = n + 1;
	    else
		size = size * 3 / 2 + 1;
	    free(string);
	}
    }
}


ListPtr
listConsF(ListPtr cdr, const char *f, ...)
{
    va_list args;
    char *string;
    {
	int n, size = 20;
	while(1) {
	    if(size > 4096)
		return NULL;
	    string = malloc(size);
	    if(!string)
		return NULL;
	    va_start(args, f);
	    n = vsnprintf(string, size, f, args);
	    va_end(args);
	    if(n >= 0 && n < size)
		return listCons(string, cdr);
	    else if(n >= size)
		size = n + 1;
	    else
		size = size * 3 / 2 + 1;
	    free(string);
	}
    }
}

ListPtr
listAdjoinF(ListPtr cdr, const char *f, ...)
{
    va_list args;
    char *string;
    {
	int n, size = 20;
	while(1) {
	    if(size > 4096)
		return NULL;
	    string = malloc(size);
	    if(!string)
		return NULL;
	    va_start(args, f);
	    n = vsnprintf(string, size, f, args);
	    va_end(args);
	    if(n >= 0 && n < size)
		return listAdjoin(string, cdr);
	    else if(n >= size)
		size = n + 1;
	    else
		size = size * 3 / 2 + 1;
	    free(string);
	}
    }
}

int
listLength(ListPtr list)
{
    int n = 0;
    while(list) {
        n++;
        list = list->next;
    }
    return n;
}

ListPtr
appendList(ListPtr first, ListPtr second)
{
    ListPtr current;

    if(second == NULL)
        return first;

    if(first == NULL)
        return second;

    for(current = first; current->next; current = current->next)
        ;

    current->next = second;
    return first;
}

ListPtr
makeList(char **a, int n, ListPtr old, int begin)
{
    ListPtr first, current, next;
    int i;

    if(n == 0)
        return old;

    first = malloc(sizeof(ListRec));
    if(!first)
        return NULL;

    first->value = a[0];
    first->next = NULL;

    current = first;
    for(i = 1; i < n; i++) {
        next = malloc(sizeof(ListRec));
        if(!next) {
            destroyList(first);
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
        return appendList(old, first);
    }
}

ListPtr
reverseList(ListPtr old)
{
    ListPtr new = NULL, current;
    while(old) {
        current = old;
        old = old->next;
        current->next = new;
        new = current;
    }
    return new;
}

/* qsort helper for sorting list entries */
static int
compareListEntries(const void *a, const void *b)
{
    const ListPtr lista = *(const ListPtr *) a;
    const ListPtr listb = *(const ListPtr *) b;

    return strcmp(lista->value, listb->value);
}

ListPtr
sortList(ListPtr old)
{
    int i;
    int l = listLength(old);
    ListPtr n;
    ListPtr *sorted;

    if (l <= 0)
        return old;

    sorted = malloc(l * sizeof(ListPtr));

    if (sorted == NULL)
        return old;

    for (n = old, i = 0; n != NULL; n = n->next) {
        sorted[i++] = n;
    }
    qsort(sorted, i, sizeof(ListPtr), compareListEntries);
    n = sorted[0];
    for (i = 0; i < (l - 1); i++) {
        sorted[i]->next = sorted[i+1];
    }
    sorted[i]->next = NULL;
    free(sorted);
    return n;
}

void
destroyList(ListPtr old)
{
    ListPtr next;
    if(!old)
        return;
    while(old) {
        next = old->next;
        free(old);
        old = next;
    }
}

void
deepDestroyList(ListPtr old)
{
    ListPtr next;
    if(!old)
        return;
    while(old) {
        next = old->next;
        free(old->value);
        free(old);
        old = next;
    }
}
