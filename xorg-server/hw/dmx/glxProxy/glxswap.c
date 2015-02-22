/*
 * Copyright 2003 Red Hat Inc., Raleigh, North Carolina.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL RED HAT AND/OR THEIR SUPPLIERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Authors:
 *   Kevin E. Martin <kem@redhat.com>
 *
 */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmx.h"
#include "dmxwindow.h"
#include "glxserver.h"
#include "glxswap.h"
#include "glxcmds.h"

typedef struct _SwapGroup *SwapGroupPtr;

static Bool SwapBarrierIsReadyToSwap(GLuint barrier);
static void SwapSwapBarrier(GLuint barrier);
static void UpdateSwapBarrierList(GLuint barrier,
                                  SwapGroupPtr pOldSwap, SwapGroupPtr pNewSwap);

/************************************************************************
 *
 * Swap Groups
 *
 ************************************************************************/

typedef struct _SwapGroup {
    WindowPtr pWin;
    SwapGroupPtr pNext;

    Bool swapping;
    Bool sleeping;
    GLuint barrier;

    XID drawable;
    GLXContextTag tag;
    __GLXclientState *clState;
} SwapGroupRec;

static void
SwapSwapGroup(SwapGroupPtr pSwap)
{
    SwapGroupPtr pCur;

    /* All drawables in swap group are ready to swap, so just swap all
     * drawables buffers and then wake up those clients that were
     * previously sleeping */

    for (pCur = pSwap; pCur; pCur = pCur->pNext) {
        if (pCur->swapping) {
            /* Swap pCur's buffers */
            __glXDoSwapBuffers(pCur->clState, pCur->drawable, pCur->tag);
            pCur->swapping = FALSE;
        }

        /* Wakeup client */
        if (pCur->sleeping) {
            ClientWakeup(pCur->clState->client);
            pCur->sleeping = FALSE;
        }
    }
}

static Bool
SwapGroupIsReadyToSwap(SwapGroupPtr pSwap)
{
    Bool isReady = TRUE;

    /* The swap group is ready to swap when all drawables are ready to
     * swap.  NOTE: A drawable is also ready to swap if it is not
     * currently mapped */
    for (; pSwap; pSwap = pSwap->pNext) {
        isReady &= (pSwap->swapping || !pSwap->pWin->mapped);
        /* FIXME: Should we use pSwap->pWin->mapped or ...->realized ??? */
    }

    return isReady;
}

static Bool
SGSwapCleanup(ClientPtr client, void *closure)
{
    /* SwapGroupPtr  pSwap = (SwapGroupPtr)closure; */

    /* This should not be called unless the client has died in which
     * case we should remove the buffer from the swap list */

    return TRUE;
}

int
SGSwapBuffers(__GLXclientState * cl, XID drawId, GLXContextTag tag,
              DrawablePtr pDraw)
{
    WindowPtr pWin = (WindowPtr) pDraw;
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWin);
    SwapGroupPtr pSwap = pWinPriv->swapGroup;
    SwapGroupPtr pCur;

    for (pCur = pSwap; pCur && pCur->pWin != pWin; pCur = pCur->pNext);
    if (!pCur)
        return BadDrawable;

    pCur->clState = cl;
    pCur->drawable = drawId;
    pCur->tag = tag;

    /* We are now in the process of swapping */
    pCur->swapping = TRUE;

    if (pSwap->barrier && SwapBarrierIsReadyToSwap(pSwap->barrier)) {
        /* The swap group is bound to a barrier and the barrier is ready
         * to swap, so swap all the swap groups that are bound to this
         * group's swap barrier */
        SwapSwapBarrier(pSwap->barrier);
    }
    else if (!pSwap->barrier && SwapGroupIsReadyToSwap(pSwap)) {
        /* Do the swap if the entire swap group is ready to swap and the
         * group is not bound to a swap barrier */
        SwapSwapGroup(pSwap);
    }
    else {
        /* The swap group/barrier is not yet ready to swap, so put
         * client to sleep until the rest are ready to swap */
        ClientSleep(cl->client, SGSwapCleanup, (void *) pWin);
        pCur->sleeping = TRUE;
    }

    return Success;
}

static void
SGWindowUnmapped(WindowPtr pWin)
{
    dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWin);
    SwapGroupPtr pSwap = pWinPriv->swapGroup;

    /* Now that one of the windows in the swap group has been unmapped,
     * see if the entire swap group/barrier is ready to swap */

    if (pSwap->barrier && SwapBarrierIsReadyToSwap(pSwap->barrier)) {
        SwapSwapBarrier(pSwap->barrier);
    }
    else if (!pSwap->barrier && SwapGroupIsReadyToSwap(pSwap)) {
        SwapSwapGroup(pSwap);
    }
}

static void
SGWindowDestroyed(WindowPtr pWin)
{
    JoinSwapGroupSGIX((DrawablePtr) pWin, NULL);
}

static SwapGroupPtr
CreateSwapEntry(WindowPtr pWin)
{
    SwapGroupPtr pEntry;

    /* Allocate new swap group */
    pEntry = malloc(sizeof(*pEntry));
    if (!pEntry)
        return NULL;

    /* Initialize swap group */
    pEntry->pWin = pWin;
    pEntry->pNext = NULL;
    pEntry->swapping = FALSE;
    pEntry->sleeping = FALSE;
    pEntry->barrier = 0;
    /* The following are not initialized until SwapBuffers is called:
     *     pEntry->drawable
     *     pEntry->tag
     *     pEntry->clState
     */

    return pEntry;
}

static void
FreeSwapEntry(SwapGroupPtr pEntry)
{
    /* Since we have removed the drawable from its previous swap group
     * and it won't be added to another swap group, the only thing that
     * we need to do is to make sure that the drawable's client is not
     * sleeping.  This could happen if one thread is sleeping, while
     * another thread called glxJoinSwapGroup().  Note that all sleeping
     * threads should also be swapping, but there is a small window in
     * the SGSwapBuffer() logic, above, where swapping can be set but
     * sleeping is not.  We check both independently here just to be
     * pedantic. */

    /* Handle swap buffer request */
    if (pEntry->swapping)
        __glXDoSwapBuffers(pEntry->clState, pEntry->drawable, pEntry->tag);

    /* Wake up client */
    if (pEntry->sleeping)
        ClientWakeup(pEntry->clState->client);

    /* We can free the pEntry entry since it has already been removed
     * from the swap group list and it won't be needed any longer */
    free(pEntry);
}

int
JoinSwapGroupSGIX(DrawablePtr pDraw, DrawablePtr pMember)
{
    if (pDraw->type == DRAWABLE_WINDOW) {
        WindowPtr pWin = (WindowPtr) pDraw;
        dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWin);
        SwapGroupPtr pOldSwap = NULL;
        SwapGroupPtr pEntry;

        /* If pDraw and pMember are already members of the same swap
         * group, just return Success since there is nothing to do */
        for (pEntry = pWinPriv->swapGroup; pEntry; pEntry = pEntry->pNext)
            if (pEntry->pWin == (WindowPtr) pMember)
                return Success;

        /* Remove pDraw from its current swap group */
        if (pWinPriv->swapGroup) {
            SwapGroupPtr pSwapGroup = pWinPriv->swapGroup;
            SwapGroupPtr pPrev;

            /* Find old swap entry in swap group and save in pOldSwap
             * for later use */
            for (pOldSwap = pWinPriv->swapGroup, pPrev = NULL;
                 pOldSwap && pOldSwap->pWin != pWin;
                 pPrev = pOldSwap, pOldSwap = pOldSwap->pNext);
            if (!pOldSwap)
                return BadDrawable;

            /* Remove pDraw's swap group entry from swap group list */
            if (pPrev) {
                pPrev->pNext = pOldSwap->pNext;
            }
            else {
                /* pWin is at the head of the swap group list, so we
                 * need to update all other members of this swap
                 * group */
                for (pEntry = pOldSwap->pNext; pEntry; pEntry = pEntry->pNext)
                    DMX_GET_WINDOW_PRIV(pEntry->pWin)->swapGroup
                        = pOldSwap->pNext;

                /* Update the barrier list as well */
                if (pOldSwap->barrier)
                    UpdateSwapBarrierList(pOldSwap->barrier,
                                          pOldSwap, pOldSwap->pNext);

                /* Set pSwapGroup to point to the swap group without
                 * pOldSwap */
                pSwapGroup = pOldSwap->pNext;
            }

            /* Check to see if current swap group can now swap since we
             * know at this point that pDraw and pMember are guaranteed
             * to previously be in different swap groups */
            if (pSwapGroup && SwapGroupIsReadyToSwap(pSwapGroup)) {
                SwapSwapGroup(pSwapGroup);
            }

            /* Make the old swap entry a standalone group */
            pOldSwap->pNext = NULL;
            pOldSwap->barrier = 0;

            /* Reset pWin's swap group */
            pWinPriv->swapGroup = NULL;
            pWinPriv->windowDestroyed = NULL;
            pWinPriv->windowUnmapped = NULL;
        }

        if (!pMember || pMember->type != DRAWABLE_WINDOW) {
            /* Free old swap group since it is no longer needed */
            if (pOldSwap)
                FreeSwapEntry(pOldSwap);
        }
        else if (pDraw == pMember && pOldSwap) {
            /* Special case where pDraw was previously created and we
             * are now just putting it to its own swap group */
            pWinPriv->swapGroup = pOldSwap;
            pWinPriv->windowDestroyed = SGWindowDestroyed;
            pWinPriv->windowUnmapped = SGWindowUnmapped;

            /* Check to see if pDraw is ready to swap */
            if (SwapGroupIsReadyToSwap(pOldSwap))
                SwapSwapGroup(pOldSwap);
        }
        else if (pMember->type == DRAWABLE_WINDOW) {
            WindowPtr pMemberWin = (WindowPtr) pMember;
            dmxWinPrivPtr pMemberPriv = DMX_GET_WINDOW_PRIV(pMemberWin);
            SwapGroupPtr pMemberSwapGroup = pMemberPriv->swapGroup;

            /* Finally, how we can add pDraw to pMember's swap group */

            /* If pMember is not currently in a swap group, then create
             * one for it since we are just about to add pDraw to it. */
            if (!pMemberSwapGroup) {
                /* Create new swap group */
                pMemberSwapGroup = CreateSwapEntry(pMemberWin);
                if (!pMemberSwapGroup) {
                    if (pOldSwap)
                        FreeSwapEntry(pOldSwap);
                    return BadAlloc;
                }

                /* Set pMember's swap group */
                pMemberPriv->swapGroup = pMemberSwapGroup;
                pMemberPriv->windowDestroyed = SGWindowDestroyed;
                pMemberPriv->windowUnmapped = SGWindowUnmapped;
            }

            /* If pDraw == pMember, that means pDraw was not a member of
             * a group previously (or it would have been handled by the
             * special case above), so no additional work is required
             * since we just created a new swap group for pMember (i.e.,
             * pDraw). */

            if (pDraw != pMember) {
                /* If pDraw was not previously in a swap group, then create
                 * an entry for it */
                if (!pOldSwap) {
                    /* Create new swap group */
                    pOldSwap = CreateSwapEntry(pWin);
                    if (!pOldSwap) {
                        /* If we just created a swap group for pMember, we
                         * need to free it here */
                        if (pMemberSwapGroup->pNext == NULL) {
                            FreeSwapEntry(pMemberSwapGroup);
                            pMemberPriv->swapGroup = NULL;
                        }
                        return BadAlloc;
                    }
                }

                /* Find last entry in pMember's swap group */
                for (pEntry = pMemberSwapGroup;
                     pEntry->pNext; pEntry = pEntry->pNext);

                /* Add pDraw's swap group entry to pMember's swap group list */
                pEntry->pNext = pOldSwap;

                /* Add pDraw to pMember's swap barrier */
                pOldSwap->barrier = pEntry->barrier;

                /* Set pDraw's swap group */
                pWinPriv->swapGroup = pMemberSwapGroup;
                pWinPriv->windowDestroyed = SGWindowDestroyed;
                pWinPriv->windowUnmapped = SGWindowUnmapped;
            }
        }
    }

    return Success;
}

/************************************************************************
 *
 * Swap Barriers
 *
 ************************************************************************/

#define GLX_MAX_SWAP_BARRIERS 10

typedef struct _SwapBarrier *SwapBarrierPtr;
typedef struct _SwapBarrier {
    SwapGroupPtr pSwap;
    SwapBarrierPtr pNext;
} SwapBarrierRec;

static SwapBarrierPtr SwapBarrierList[GLX_MAX_SWAP_BARRIERS + 1];

void
SwapBarrierInit(void)
{
    int i;

    for (i = 0; i <= GLX_MAX_SWAP_BARRIERS; i++)
        SwapBarrierList[i] = NULL;
}

void
SwapBarrierReset(void)
{
    int i;

    for (i = 0; i <= GLX_MAX_SWAP_BARRIERS; i++) {
        SwapBarrierPtr pBarrier, pNextBarrier;

        for (pBarrier = SwapBarrierList[i]; pBarrier; pBarrier = pNextBarrier) {
            pNextBarrier = pBarrier->pNext;
            free(pBarrier);
        }
        SwapBarrierList[i] = NULL;
    }
}

int
QueryMaxSwapBarriersSGIX(int screen)
{
    return GLX_MAX_SWAP_BARRIERS;
}

static Bool
BindSwapGroupToBarrier(GLuint barrier, SwapGroupPtr pSwapGroup)
{
    SwapBarrierPtr pBarrier;

    pBarrier = malloc(sizeof(*pBarrier));
    if (!pBarrier)
        return FALSE;

    /* Add the swap group to barrier's list */
    pBarrier->pSwap = pSwapGroup;
    pBarrier->pNext = SwapBarrierList[barrier];
    SwapBarrierList[barrier] = pBarrier;

    return TRUE;
}

static Bool
UnbindSwapGroupFromBarrier(GLuint barrier, SwapGroupPtr pSwapGroup)
{
    SwapBarrierPtr pBarrier, pPrevBarrier;

    /* Find the swap group in barrier's list */
    for (pBarrier = SwapBarrierList[barrier], pPrevBarrier = NULL;
         pBarrier && pBarrier->pSwap != pSwapGroup;
         pPrevBarrier = pBarrier, pBarrier = pBarrier->pNext);
    if (!pBarrier)
        return FALSE;

    /* Remove the swap group from barrier's list */
    if (pPrevBarrier)
        pPrevBarrier->pNext = pBarrier->pNext;
    else
        SwapBarrierList[barrier] = pBarrier->pNext;

    /* Free memory */
    free(pBarrier);

    return TRUE;
}

static void
UpdateSwapBarrierList(GLuint barrier,
                      SwapGroupPtr pOldSwap, SwapGroupPtr pNewSwap)
{
    SwapBarrierPtr pBarrier;

    /* If the old swap group is being destroyed, then we need to remove
     * the swap group from the list entirely */
    if (!pNewSwap) {
        UnbindSwapGroupFromBarrier(barrier, pOldSwap);
        return;
    }

    /* Otherwise, find the old swap group in the barrier list and change
     * it to the new swap group */
    for (pBarrier = SwapBarrierList[barrier];
         pBarrier; pBarrier = pBarrier->pNext) {
        if (pBarrier->pSwap == pOldSwap) {
            pBarrier->pSwap = pNewSwap;
            return;
        }
    }
}

static Bool
SwapBarrierIsReadyToSwap(GLuint barrier)
{
    SwapBarrierPtr pBarrier;
    Bool isReady = TRUE;

    /* The swap barier is ready to swap when swap groups that are bound
     * to barrier are ready to swap */
    for (pBarrier = SwapBarrierList[barrier];
         pBarrier; pBarrier = pBarrier->pNext)
        isReady &= SwapGroupIsReadyToSwap(pBarrier->pSwap);

    return isReady;
}

static void
SwapSwapBarrier(GLuint barrier)
{
    SwapBarrierPtr pBarrier;

    /* Swap each group that is a member of this barrier */
    for (pBarrier = SwapBarrierList[barrier];
         pBarrier; pBarrier = pBarrier->pNext)
        SwapSwapGroup(pBarrier->pSwap);
}

int
BindSwapBarrierSGIX(DrawablePtr pDraw, int barrier)
{
    /* FIXME: Check for errors when pDraw->type != DRAWABLE_WINDOW */

    if (barrier < 0 || barrier > GLX_MAX_SWAP_BARRIERS)
        return BadValue;

    if (pDraw->type == DRAWABLE_WINDOW) {
        WindowPtr pWin = (WindowPtr) pDraw;
        dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV(pWin);
        SwapGroupPtr pSwapGroup = pWinPriv->swapGroup;
        SwapGroupPtr pCur;

        if (!pSwapGroup)
            return BadDrawable;
        if (barrier && pSwapGroup->barrier)
            return BadValue;

        /* Update the swap barrier list */
        if (barrier) {
            if (!BindSwapGroupToBarrier(barrier, pSwapGroup))
                return BadAlloc;
        }
        else {
            if (!UnbindSwapGroupFromBarrier(pSwapGroup->barrier, pSwapGroup))
                return BadDrawable;
        }

        /* Set the barrier for each member of this swap group */
        for (pCur = pSwapGroup; pCur; pCur = pCur->pNext)
            pCur->barrier = barrier;
    }

    return Success;
}
