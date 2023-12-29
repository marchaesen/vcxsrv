/*

Copyright 1985, 1986, 1987, 1988, 1989, 1994, 1998  The Open Group

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
#include "IntrinsicI.h"
#include "VarargsI.h"
#include "StringDefs.h"

static String XtNxtConvertVarToArgList = "xtConvertVarToArgList";

/*
 *    Given a nested list, _XtCountNestedList() returns counts of the
 *    total number of attribute-value pairs and the count of those
 *    attributes that are typed. The list is counted recursively.
 */
static void
_XtCountNestedList(XtTypedArgList avlist, int *total_count, int *typed_count)
{
    for (; avlist->name != NULL; avlist++) {
        if (strcmp(avlist->name, XtVaNestedList) == 0) {
            _XtCountNestedList((XtTypedArgList) avlist->value, total_count,
                               typed_count);
        }
        else {
            if (avlist->type != NULL) {
                ++(*typed_count);
            }
            ++(*total_count);
        }
    }
}

/*
 *    Given a variable length attribute-value list, _XtCountVaList()
 *    returns counts of the total number of attribute-value pairs,
 *    and the count of the number of those attributes that are typed.
 *    The list is counted recursively.
 */
void
_XtCountVaList(va_list var, int *total_count, int *typed_count)
{
    String attr;

    *total_count = 0;
    *typed_count = 0;

    for (attr = va_arg(var, String); attr != NULL; attr = va_arg(var, String)) {
        if (strcmp(attr, XtVaTypedArg) == 0) {
            (void) va_arg(var, String);
            (void) va_arg(var, String);
            (void) va_arg(var, XtArgVal);
            (void) va_arg(var, int);

            ++(*total_count);
            ++(*typed_count);
        }
        else if (strcmp(attr, XtVaNestedList) == 0) {
            _XtCountNestedList(va_arg(var, XtTypedArgList), total_count,
                               typed_count);
        }
        else {
            (void) va_arg(var, XtArgVal);
            ++(*total_count);
        }
    }
}

/*
 *   Given a variable length attribute-value list, XtVaCreateArgsList()
 *   constructs an attribute-value list of type XtTypedArgList and
 *   returns the list.
 */
XtVarArgsList
XtVaCreateArgsList(XtPointer unused, ...)
{
    va_list var;
    XtTypedArgList avlist;
    int count = 0;
    String attr;

    /*
     * Count the number of attribute-value pairs in the list.
     * Note: The count is required only to allocate enough space to store
     * the list. Therefore nested lists are not counted recursively.
     */
    va_start(var, unused);
    for (attr = va_arg(var, String); attr != NULL; attr = va_arg(var, String)) {
        ++count;
        if (strcmp(attr, XtVaTypedArg) == 0) {
            (void) va_arg(var, String);
            (void) va_arg(var, String);
            (void) va_arg(var, XtArgVal);
            (void) va_arg(var, int);
        }
        else {
            (void) va_arg(var, XtArgVal);
        }
    }
    va_end(var);

    va_start(var, unused);
    avlist = _XtVaCreateTypedArgList(var, count);
    va_end(var);
    return (XtVarArgsList) avlist;
}

XtTypedArgList
_XtVaCreateTypedArgList(va_list var, register int count)
{
    String attr;
    XtTypedArgList avlist;

    avlist = (XtTypedArgList)
        __XtCalloc((Cardinal) count + 1, (unsigned) sizeof(XtTypedArg));

    for (attr = va_arg(var, String), count = 0; attr != NULL;
         attr = va_arg(var, String)) {
        if (strcmp(attr, XtVaTypedArg) == 0) {
            avlist[count].name = va_arg(var, String);
            avlist[count].type = va_arg(var, String);
            avlist[count].value = va_arg(var, XtArgVal);
            avlist[count].size = va_arg(var, int);
        }
        else {
            avlist[count].name = attr;
            avlist[count].type = NULL;
            avlist[count].value = va_arg(var, XtArgVal);
        }
        ++count;
    }
    avlist[count].name = NULL;

    return avlist;
}

/*
 *    TypedArgToArg() invokes a resource converter to convert the
 *    passed typed arg into a name/value pair and stores the name/value
 *    pair in the passed Arg structure.  If memory is allocated for the
 *    converted value, the address is returned in the value field of
 *    memory_return; otherwise that field is NULL.  The function returns
 *    1 if the conversion succeeded and 0 if the conversion failed.
 */
static int
TypedArgToArg(Widget widget,
              XtTypedArgList typed_arg,
              ArgList arg_return,
              XtResourceList resources,
              Cardinal num_resources,
              ArgList memory_return)
{
    String to_type = NULL;
    XrmValue from_val, to_val;

    if (widget == NULL) {
        XtAppWarningMsg(XtWidgetToApplicationContext(widget),
                        "nullWidget", XtNxtConvertVarToArgList,
                        XtCXtToolkitError,
                        "XtVaTypedArg conversion needs non-NULL widget handle",
                        NULL, NULL);
        return (0);
    }

    /* again we assume that the XtResourceList is un-compiled */

    for (; num_resources--; resources++)
        if (strcmp(typed_arg->name, resources->resource_name) == 0) {
            to_type = resources->resource_type;
            break;
        }

    if (to_type == NULL) {
        XtAppWarningMsg(XtWidgetToApplicationContext(widget),
                        "unknownType", XtNxtConvertVarToArgList,
                        XtCXtToolkitError,
                        "Unable to find type of resource for conversion", NULL,
                        NULL);
        return (0);
    }

    to_val.addr = NULL;
    from_val.size = (Cardinal) typed_arg->size;
    if ((strcmp(typed_arg->type, XtRString) == 0) ||
        ((unsigned) typed_arg->size > sizeof(XtArgVal))) {
        from_val.addr = (XPointer) typed_arg->value;
    }
    else {
        from_val.addr = (XPointer) &typed_arg->value;
    }

    LOCK_PROCESS;
    XtConvertAndStore(widget, typed_arg->type, &from_val, to_type, &to_val);

    if (to_val.addr == NULL) {
        UNLOCK_PROCESS;
        XtAppWarningMsg(XtWidgetToApplicationContext(widget),
                        "conversionFailed", XtNxtConvertVarToArgList,
                        XtCXtToolkitError, "Type conversion failed", NULL,
                        NULL);
        return (0);
    }

    arg_return->name = typed_arg->name;
    memory_return->value = (XtArgVal) NULL;

    if (strcmp(to_type, XtRString) == 0) {
        arg_return->value = (XtArgVal) to_val.addr;
    }
    else {
        if (to_val.size == sizeof(long))
            arg_return->value = (XtArgVal) * (long *) to_val.addr;
        else if (to_val.size == sizeof(int))
            arg_return->value = (XtArgVal) * (int *) to_val.addr;
        else if (to_val.size == sizeof(short))
            arg_return->value = (XtArgVal) * (short *) to_val.addr;
        else if (to_val.size == sizeof(char))
            arg_return->value = (XtArgVal) * (char *) to_val.addr;
        else if (to_val.size == sizeof(XtArgVal))
            arg_return->value = *(XtArgVal *) to_val.addr;
        else if (to_val.size > sizeof(XtArgVal)) {
            arg_return->value = (XtArgVal) (void *) __XtMalloc(to_val.size);
            if ((memory_return->value = arg_return->value) != 0)
                memcpy((void *) arg_return->value, to_val.addr, to_val.size);
        }
    }
    UNLOCK_PROCESS;

    return (1);
}

/*
 *    NestedArgtoArg() converts the passed nested list into
 *    an ArgList/count.
 */
static int
NestedArgtoArg(Widget widget,
               XtTypedArgList avlist,
               ArgList args,
               XtResourceList resources,
               Cardinal num_resources,
               ArgList memory_return)
{
    int count = 0;

    for (; avlist->name != NULL; avlist++) {
        if (avlist->type != NULL) {
            /* If widget is NULL, the typed arg is ignored */
            if (widget != NULL) {
                /* this is a typed arg */
                count += TypedArgToArg(widget, avlist, (args + count),
                                       resources, num_resources,
                                       (memory_return + count));
            }
        }
        else if (strcmp(avlist->name, XtVaNestedList) == 0) {
            count += NestedArgtoArg(widget, (XtTypedArgList) avlist->value,
                                    (args + count), resources, num_resources,
                                    (memory_return + count));
        }
        else {
            (args + count)->name = avlist->name;
            (args + count)->value = avlist->value;
            ++count;
        }
    }

    return (count);
}

/*
 * Free memory allocated through _XtVaToArgList.  The actual args array
 * size is expected to be total_count * 2, where total_count is the number
 * of elements needed for resource representations.  The lower half of the
 * array contains pairs of resource names and values as usual.  For each
 * element [n] in the lower half of the array, the value field of the
 * corresponding element [n + total_count] in the upper half of the array
 * has been pressed into service in order to note whether the resource value
 * is a pointer to memory that was allocated in TypedArgToArg.  In the
 * upper half, if the value field is not NULL, it contains the address of
 * memory which should now be freed.  That memory could have been allocated
 * only as a result of the conversion of typed arguments.  Therefore, if
 * there were no typed arguments in the original varargs, there is no need
 * to examine the upper half of the array.  In the choice of data structure
 * to make this representation, priority was given to the wish to retrofit
 * the release of memory around the existing signature of _XtVaToArgList.
 */
void
_XtFreeArgList(ArgList args,    /* as returned by _XtVaToArgList */
               int total_count, /* argument count returned by _XtCountVaList */
               int typed_count) /* typed arg count returned by _XtCountVaList */
{
    if (args) {
        if (typed_count) {
            ArgList p;

            for (p = args + total_count; total_count--; ++p) {
                XtFree((char *) p->value);
            }
        }
        XtFree((char *) args);
    }
}

static void GetResources(Widget widget, XtResourceList *res_list,
                         Cardinal *number);

/*
 *    Given a variable argument list, _XtVaToArgList() returns the
 *    equivalent ArgList and count. _XtVaToArgList() handles nested
 *    lists and typed arguments.  If typed arguments are present, the
 *    ArgList should be freed with _XtFreeArgList.
 */
void
_XtVaToArgList(Widget widget,
               va_list var,
               int max_count,
               ArgList *args_return,
               Cardinal *num_args_return)
{
    String		attr;
    int			count;
    ArgList		args = (ArgList)NULL;
    XtTypedArg		typed_arg;
    XtResourceList	resources = (XtResourceList)NULL;
    Cardinal		num_resources = 0;
    Boolean		fetched_resource_list = False;

    *num_args_return = 0;
    *args_return = (ArgList)NULL;

    if (max_count  == 0)
	return;

    args = (ArgList)__XtCalloc((Cardinal)(max_count * 2),  sizeof(Arg));
    if (!args)
	return;

    count = 0;

    for(attr = va_arg(var, String) ; attr != NULL;
			    attr = va_arg(var, String)) {
	    if (strcmp(attr, XtVaTypedArg) == 0) {
		typed_arg.name = va_arg(var, String);
		typed_arg.type = va_arg(var, String);
		typed_arg.value = va_arg(var, XtArgVal);
		typed_arg.size = va_arg(var, int);

		/* if widget is NULL, typed args are ignored */
		if (widget != NULL) {
		    if (!fetched_resource_list) {
			GetResources(widget, &resources, &num_resources);
			fetched_resource_list = True;
		    }
		    count += TypedArgToArg(widget, &typed_arg, &args[count],
					      resources, num_resources,
					      &args[max_count + count]);
		}
	    } else if (strcmp(attr, XtVaNestedList) == 0) {
		if (widget != NULL) {
		    if (!fetched_resource_list) {
			GetResources(widget, &resources, &num_resources);
			fetched_resource_list = True;
		    }
		}

		count += NestedArgtoArg(widget, va_arg(var, XtTypedArgList),
					   &args[count], resources, num_resources,
					   &args[max_count + count]);
	    } else {
		args[count].name = attr;
		args[count].value = va_arg(var, XtArgVal);
		count ++;
	    }
    }
    XtFree((XtPointer) resources);

    *num_args_return = (Cardinal) count;
    *args_return = (ArgList) args;
}

/*      Function Name: GetResources
 *      Description: Retrieves the normal and constraint resources
 *                   for this widget.
 *      Arguments: widget - the widget.
 * RETURNED        res_list - the list of resource for this widget
 * RETURNED        number - the number of resources in the above list.
 *      Returns: none
 */

static void
GetResources(Widget widget, XtResourceList *res_list, Cardinal *number)
{
    Widget parent = XtParent(widget);

    XtInitializeWidgetClass(XtClass(widget));
    XtGetResourceList(XtClass(widget), res_list, number);

    if (!XtIsShell(widget) && parent && XtIsConstraint(parent)) {
        XtResourceList res, constraint, cons_top;
        Cardinal num_constraint, temp;

        XtGetConstraintResourceList(XtClass(parent), &constraint,
                                    &num_constraint);

        cons_top = constraint;
        *res_list = XtReallocArray(*res_list, *number + num_constraint,
                                   (Cardinal) sizeof(XtResource));

        for (temp = num_constraint, res = *res_list + *number; temp != 0;
             temp--)
            *res++ = *constraint++;

        *number += num_constraint;
        XtFree((XtPointer) cons_top);
    }
}

static int
NestedArgtoTypedArg(XtTypedArgList args, XtTypedArgList avlist)
{
    int count = 0;

    for (; avlist->name != NULL; avlist++) {
        if (avlist->type != NULL) {
            (args + count)->name = avlist->name;
            (args + count)->type = avlist->type;
            (args + count)->size = avlist->size;
            (args + count)->value = avlist->value;
            ++count;
        }
        else if (strcmp(avlist->name, XtVaNestedList) == 0) {
            count += NestedArgtoTypedArg((args + count),
                                         (XtTypedArgList) avlist->value);
        }
        else {
            (args + count)->name = avlist->name;
            (args + count)->type = NULL;
            (args + count)->value = avlist->value;
            ++count;
        }
    }
    return (count);
}

/*
 *    Given a variable argument list, _XtVaToTypedArgList() returns
 *    the equivalent TypedArgList. _XtVaToTypedArgList() handles nested
 *    lists.
 *    Note: _XtVaToTypedArgList() does not do type conversions.
 */
void
_XtVaToTypedArgList(va_list var,
                    int max_count,
                    XtTypedArgList *args_return,
                    Cardinal *num_args_return)
{
    XtTypedArgList args;
    String attr;
    int count;

    *args_return = NULL;
    *num_args_return = 0;

    if (max_count  == 0)
	return;

    args = (XtTypedArgList)
        __XtCalloc((Cardinal) max_count , sizeof(XtTypedArg));
    if (!args)
    	return;	    

    count=0;

    for (attr = va_arg(var, String); attr != NULL;
         attr = va_arg(var, String)) {
        if (strcmp(attr, XtVaTypedArg) == 0) {
            args[count].name = va_arg(var, String);
            args[count].type = va_arg(var, String);
            args[count].value = va_arg(var, XtArgVal);
            args[count].size = va_arg(var, int);

            ++count;
        }
        else if (strcmp(attr, XtVaNestedList) == 0) {
            count += NestedArgtoTypedArg(&args[count],
                                         va_arg(var, XtTypedArgList));
        }
        else {
            args[count].name = attr;
            args[count].type = NULL;
            args[count].value = va_arg(var, XtArgVal);
            ++count;
        }
    }

    *args_return = args;
    *num_args_return = (Cardinal) count;
}
