/*
 * Copyright (c) 2000 by The XFree86 Project, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the copyright holder(s)
 * and author(s) shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from the copyright holder(s) and author(s).
 */

/* Maybe this file belongs elsewhere? */

#define LOADERDECLARATIONS
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "loaderProcs.h"
#include "misc.h"
#include "xf86.h"

/*
 * This should be static, but miinitext wants it.  FIXME: make extension
 * initialization not completely terrible.
 */
ExtensionModule *ExtensionModuleList = NULL;
static int numExtensionModules = 0;

static ExtensionModule *
NewExtensionModule(void)
{
    ExtensionModule *save = ExtensionModuleList;
    int n;

    /* Sanity check */
    if (!ExtensionModuleList)
        numExtensionModules = 0;

    n = numExtensionModules + 1;
    ExtensionModuleList = realloc(ExtensionModuleList,
                                  (n + 1) * sizeof(ExtensionModule));
    if (ExtensionModuleList == NULL) {
        ExtensionModuleList = save;
        return NULL;
    }
    else {
        numExtensionModules++;
        ExtensionModuleList[numExtensionModules].name = NULL;
        return ExtensionModuleList + (numExtensionModules - 1);
    }
}

void
LoadExtension(ExtensionModule * e, Bool builtin)
{
    ExtensionModule *newext;

    if (e == NULL || e->name == NULL)
        return;

    if (!(newext = NewExtensionModule()))
        return;

    if (builtin)
        xf86MsgVerb(X_INFO, 2, "Initializing built-in extension %s\n", e->name);
    else
        xf86MsgVerb(X_INFO, 2, "Loading extension %s\n", e->name);

    newext->name = e->name;
    newext->initFunc = e->initFunc;
    newext->disablePtr = e->disablePtr;
    newext->setupFunc = e->setupFunc;
    newext->initDependencies = e->initDependencies;

    if (e->setupFunc != NULL)
        e->setupFunc();
}

/*
 * Sort ExtensionModuleList according to the initialisation order
 * dependencies.  The code for this is taken from BSD's tsort,
 * and carries the following copyright/license:
 *
 *
 * Copyright (c) 1989, 1993, 1994
 *      The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Michael Rendell of Memorial University of Newfoundland.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#define	NF_MARK		0x1     /* marker for cycle detection */
#define	NF_ACYCLIC	0x2     /* this node is cycle free */
#define	NF_NODEST	0x4     /* Unreachable */

typedef struct node_str NODE;
struct node_str {
    NODE **n_prevp;             /* pointer to previous node's n_next */
    NODE *n_next;               /* next node in graph */
    NODE **n_arcs;              /* array of arcs to other nodes */
    int n_narcs;                /* number of arcs in n_arcs[] */
    int n_arcsize;              /* size of n_arcs[] array */
    int n_refcnt;               /* # of arcs pointing to this node */
    int n_flags;                /* NF_* */
    const char *n_name;         /* name of this node */
};

static NODE *graph = NULL, **cycle_buf = NULL, **longest_cycle = NULL;
static int longest = 0;
static NODE *sorted = NULL, *last = NULL;

/* Find a node in the graph (insert if not found) and return a pointer to it. */
static NODE *
get_node(const char *name)
{
    NODE *n;

    for (n = graph; n && n->n_name && strcmp(n->n_name, name); n = n->n_next);
    if (n)
        return n;

    n = xnfalloc(sizeof(NODE));

    n->n_narcs = 0;
    n->n_arcsize = 0;
    n->n_arcs = NULL;
    n->n_refcnt = 0;
    n->n_flags = 0;
    n->n_name = name;

    /* Add to linked list. */
    if ((n->n_next = graph) != NULL)
        graph->n_prevp = &n->n_next;
    n->n_prevp = &graph;
    graph = n;

    return n;
}

/*
 * add an arc from node s1 to node s2 in the graph.  If s1 or s2 are not in
 * the graph, then add them.
 */
static void
add_arc(const char *s1, const char *s2)
{
    NODE *n1;
    NODE *n2;
    int bsize, i;

    n1 = get_node(s1);

    if (!strcmp(s1, s2))
        return;

    n2 = get_node(s2);

    /*
     * Check if this arc is already here.
     */
    for (i = 0; i < n1->n_narcs; i++)
        if (n1->n_arcs[i] == n2)
            return;
    /*
     * Add it.
     */
    if (n1->n_narcs == n1->n_arcsize) {
        if (!n1->n_arcsize)
            n1->n_arcsize = 10;
        bsize = n1->n_arcsize * sizeof(*n1->n_arcs) * 2;
        n1->n_arcs = xnfrealloc(n1->n_arcs, bsize);
        n1->n_arcsize = bsize / sizeof(*n1->n_arcs);
    }
    n1->n_arcs[n1->n_narcs++] = n2;
    ++n2->n_refcnt;
}

/*
 * Clear the NODEST flag from all nodes.
 */
static void
clear_cycle(void)
{
    NODE *n;

    for (n = graph; n != NULL; n = n->n_next)
        n->n_flags &= ~NF_NODEST;
}

/* print node and remove from graph (does not actually free node) */
static void
remove_node(NODE * n)
{
    NODE **np;
    NODE *newnode;
    int i;

#ifdef DEBUG
    ErrorF("%s\n", n->n_name);
#endif
    newnode = xnfalloc(sizeof(NODE));
    memcpy(newnode, n, sizeof(NODE));
    if (last)
        last->n_next = newnode;
    else
        sorted = newnode;
    last = newnode;
    newnode->n_next = NULL;

    for (np = n->n_arcs, i = n->n_narcs; --i >= 0; np++)
        --(*np)->n_refcnt;
    n->n_narcs = 0;
    *n->n_prevp = n->n_next;
    if (n->n_next)
        n->n_next->n_prevp = n->n_prevp;
}

static void
free_nodes(NODE * nodelist)
{
    NODE *n, *nextnode;

    for (n = nodelist; n;) {
        nextnode = n->n_next;
        free(n);
        n = nextnode;
    }
}

/* look for the longest? cycle from node from to node to. */
static int
find_cycle(NODE * from, NODE * to, int longest_len, int depth)
{
    NODE **np;
    int i, len;

    /*
     * avoid infinite loops and ignore portions of the graph known
     * to be acyclic
     */
    if (from->n_flags & (NF_NODEST | NF_MARK | NF_ACYCLIC))
        return 0;
    from->n_flags |= NF_MARK;

    for (np = from->n_arcs, i = from->n_narcs; --i >= 0; np++) {
        cycle_buf[depth] = *np;
        if (*np == to) {
            if (depth + 1 > longest_len) {
                longest_len = depth + 1;
                memcpy((char *) longest_cycle,
                       (char *) cycle_buf, longest_len * sizeof(NODE *));
            }
        }
        else {
            if ((*np)->n_flags & (NF_MARK | NF_ACYCLIC | NF_NODEST))
                continue;
            len = find_cycle(*np, to, longest_len, depth + 1);

#ifdef DEBUG
            ErrorF("%*s %s->%s %d\n", depth, "", from->n_name, to->n_name, len);
#endif

            if (len == 0)
                (*np)->n_flags |= NF_NODEST;

            if (len > longest_len)
                longest_len = len;

            if (len > 0 && !longest)
                break;
        }
    }
    from->n_flags &= ~NF_MARK;
    return longest_len;
}

/* do topological sort on graph */
static void
tsort(void)
{
    NODE *n, *next;
    int cnt, i;

    while (graph != NULL) {
        /*
         * Keep getting rid of simple cases until there are none left,
         * if there are any nodes still in the graph, then there is
         * a cycle in it.
         */
        do {
            for (cnt = 0, n = graph; n != NULL; n = next) {
                next = n->n_next;
                if (n->n_refcnt == 0) {
                    remove_node(n);
                    ++cnt;
                }
            }
        } while (graph != NULL && cnt);

        if (graph == NULL)
            break;

        if (!cycle_buf) {
            /*
             * Allocate space for two cycle logs - one to be used
             * as scratch space, the other to save the longest
             * cycle.
             */
            for (cnt = 0, n = graph; n != NULL; n = n->n_next)
                ++cnt;
            cycle_buf = xnfalloc(sizeof(NODE *) * cnt);
            longest_cycle = xnfalloc(sizeof(NODE *) * cnt);
            if (cycle_buf == NULL || longest_cycle == NULL)
                return;
        }
        for (n = graph; n != NULL; n = n->n_next)
            if (!(n->n_flags & NF_ACYCLIC)) {
                if ((cnt = find_cycle(n, n, 0, 0))) {
                    ErrorF("tsort: cycle in data");
                    for (i = 0; i < cnt; i++)
                        ErrorF("%s", longest_cycle[i]->n_name);
                    remove_node(n);
                    clear_cycle();
                    break;
                }
                else {
                    /* to avoid further checks */
                    n->n_flags |= NF_ACYCLIC;
                    clear_cycle();
                }
            }

        if (n == NULL)
            ErrorF("tsort: internal error -- could not find cycle");
    }
    free(cycle_buf);
    free(longest_cycle);
    if (graph)
        free_nodes(graph);
}

void
LoaderSortExtensions(void)
{
    int i, j;
    ExtensionModule *ext, *newList;
    NODE *node;

    graph = NULL;
    longest = 0;
    sorted = NULL;
    last = NULL;
    cycle_buf = NULL;
    longest_cycle = NULL;

    /*
     * Parse list and build the graph.  Enter them in reverse order
     * because tsort() will reverse those that have no depedencies.
     */
    for (i = numExtensionModules - 1; i >= 0; i--) {
        ext = &ExtensionModuleList[i];
        add_arc(ext->name, ext->name);
#ifdef DEBUG
        ErrorF("Extension %s:\n", ext->name);
#endif
        if (ext->initDependencies)
            for (j = 0; ext->initDependencies[j]; j++) {
                add_arc(ext->initDependencies[j], ext->name);
#ifdef DEBUG
                ErrorF("\t%s\n", ext->initDependencies[j]);
#endif
            }
    }
    tsort();
    newList = xnfalloc((numExtensionModules + 1) * sizeof(ExtensionModule));
    i = 0;
    for (node = sorted; node; node = node->n_next) {
        for (j = 0; j < numExtensionModules; j++)
            if (!strcmp(node->n_name, ExtensionModuleList[j].name))
                break;
        if (j != numExtensionModules)
            newList[i++] = ExtensionModuleList[j];
    }
    if (sorted)
        free_nodes(sorted);
    if (graph)
        free_nodes(graph);
    newList[i].name = NULL;
    free(ExtensionModuleList);
    ExtensionModuleList = newList;
#ifdef DEBUG
    for (i = 0; ExtensionModuleList[i].name; i++)
        ErrorF("Extension %s\n", ExtensionModuleList[i].name);
#endif
}
