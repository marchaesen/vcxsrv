/***********************************************************
Copyright (c) 1993, Oracle and/or its affiliates.

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

Copyright 1987, 1988 by Digital Equipment Corporation, Maynard, Massachusetts.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the name of Digital not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

******************************************************************/

/*

Copyright 1987, 1988, 1994, 1998, 2001  The Open Group

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
#include <stdio.h>
#include <errno.h>

#ifdef _WIN32
typedef long suseconds_t;
#endif

static TimerEventRec *freeTimerRecs;
static WorkProcRec *freeWorkRecs;
static SignalEventRec *freeSignalRecs;

/* Some systems running NTP daemons are known to return strange usec
 * values from gettimeofday.
 */

#ifndef NEEDS_NTPD_FIXUP
#if defined(sun) || defined(MOTOROLA) || (defined(__osf__) && defined(__alpha))
#define NEEDS_NTPD_FIXUP 1
#else
#define NEEDS_NTPD_FIXUP 0
#endif
#endif

#if NEEDS_NTPD_FIXUP
#define FIXUP_TIMEVAL(t) { \
        while ((t).tv_usec >= 1000000) { \
            (t).tv_usec -= 1000000; \
            (t).tv_sec++; \
        } \
        while ((t).tv_usec < 0) { \
            if ((t).tv_sec > 0) { \
                (t).tv_usec += 1000000; \
                (t).tv_sec--; \
            } else { \
                (t).tv_usec = 0; \
                break; \
            } \
        }}
#else
#define FIXUP_TIMEVAL(t)
#endif                          /*NEEDS_NTPD_FIXUP */

/*
 * Private routines
 */
#define ADD_TIME(dest, src1, src2) { \
        if(((dest).tv_usec = (src1).tv_usec + (src2).tv_usec) >= 1000000) {\
              (dest).tv_usec -= 1000000;\
              (dest).tv_sec = (src1).tv_sec + (src2).tv_sec + 1 ; \
        } else { (dest).tv_sec = (src1).tv_sec + (src2).tv_sec ; \
           if(((dest).tv_sec >= 1) && (((dest).tv_usec <0))) { \
            (dest).tv_sec --;(dest).tv_usec += 1000000; } } }

#define TIMEDELTA(dest, src1, src2) { \
        if(((dest).tv_usec = (src1).tv_usec - (src2).tv_usec) < 0) {\
              (dest).tv_usec += 1000000;\
              (dest).tv_sec = (src1).tv_sec - (src2).tv_sec - 1;\
        } else  (dest).tv_sec = (src1).tv_sec - (src2).tv_sec;  }

#define IS_AFTER(t1, t2) (((t2).tv_sec > (t1).tv_sec) \
        || (((t2).tv_sec == (t1).tv_sec)&& ((t2).tv_usec > (t1).tv_usec)))

#define IS_AT_OR_AFTER(t1, t2) (((t2).tv_sec > (t1).tv_sec) \
        || (((t2).tv_sec == (t1).tv_sec)&& ((t2).tv_usec >= (t1).tv_usec)))

#ifdef USE_POLL
#ifndef XT_DEFAULT_FDLIST_SIZE
#define XT_DEFAULT_FDLIST_SIZE 32
#endif
#endif

static void
AdjustHowLong(unsigned long *howlong, const struct timeval *start_time)
{
    struct timeval new_time, time_spent, lstart_time;

    lstart_time = *start_time;
    X_GETTIMEOFDAY(&new_time);
    FIXUP_TIMEVAL(new_time);
    TIMEDELTA(time_spent, new_time, lstart_time);
    if (*howlong <=
        (unsigned long) (time_spent.tv_sec * 1000 + time_spent.tv_usec / 1000))
        *howlong = (unsigned long) 0;   /* Timed out */
    else
        *howlong =
            (*howlong -
             (unsigned long) (time_spent.tv_sec * 1000 +
                              time_spent.tv_usec / 1000));
}

typedef struct {
    struct timeval cur_time;
    struct timeval start_time;
    struct timeval wait_time;
    struct timeval new_time;
    struct timeval time_spent;
    struct timeval max_wait_time;
#ifdef USE_POLL
    int poll_wait;
#else
    struct timeval *wait_time_ptr;
#endif
} wait_times_t, *wait_times_ptr_t;

static struct timeval zero_time = { 0, 0 };

#ifdef USE_POLL
#define X_BLOCK -1
#define X_DONT_BLOCK 0
#else
static fd_set zero_fd;
#endif

static void
InitTimes(Boolean block,
          unsigned long *howlong,
          wait_times_ptr_t wt)
{
    if (block) {
        X_GETTIMEOFDAY(&wt->cur_time);
        FIXUP_TIMEVAL(wt->cur_time);
        wt->start_time = wt->cur_time;
        if (howlong == NULL) {  /* special case for ever */
#ifdef USE_POLL
            wt->poll_wait = X_BLOCK;
#else
            wt->wait_time_ptr = NULL;
#endif
        }
        else {                  /* block until at most */
            wt->max_wait_time.tv_sec = (time_t) (*howlong / 1000);
            wt->max_wait_time.tv_usec =
                (suseconds_t) ((*howlong % 1000) * 1000);
#ifdef USE_POLL
            wt->poll_wait = (int) *howlong;
#else
            wt->wait_time_ptr = &wt->max_wait_time;
#endif
        }
    }
    else {                      /* don't block */
        wt->max_wait_time = zero_time;
#ifdef USE_POLL
        wt->poll_wait = X_DONT_BLOCK;
#else
        wt->wait_time_ptr = &wt->max_wait_time;
#endif
    }
}

typedef struct {
#ifdef USE_POLL
    struct pollfd *fdlist;
    struct pollfd *stack;
    int fdlistlen, num_dpys;
#else
    fd_set rmask, wmask, emask;
    int nfds;
#endif
} wait_fds_t, *wait_fds_ptr_t;

static void
InitFds(XtAppContext app,
        Boolean ignoreEvents,
        Boolean ignoreInputs,
        wait_fds_ptr_t wf)
{
    int ii;

    app->rebuild_fdlist = FALSE;
#ifdef USE_POLL
#ifndef POLLRDNORM
#define POLLRDNORM 0
#endif

#ifndef POLLRDBAND
#define POLLRDBAND 0
#endif

#ifndef POLLWRNORM
#define POLLWRNORM 0
#endif

#ifndef POLLWRBAND
#define POLLWRBAND 0
#endif

#define XPOLL_READ (POLLIN|POLLRDNORM|POLLPRI|POLLRDBAND)
#define XPOLL_WRITE (POLLOUT|POLLWRNORM|POLLWRBAND)
#define XPOLL_EXCEPT 0

    if (!ignoreEvents)
        wf->fdlistlen = wf->num_dpys = app->count;
    else
        wf->fdlistlen = wf->num_dpys = 0;

    if (!ignoreInputs && app->input_list != NULL) {
        for (ii = 0; ii < (int) app->input_max; ii++)
            if (app->input_list[ii] != NULL)
                wf->fdlistlen++;
    }

    if (!wf->fdlist || wf->fdlist == wf->stack) {
        wf->fdlist = (struct pollfd *)
            XtStackAlloc(sizeof(struct pollfd) * (size_t) wf->fdlistlen,
                         wf->stack);
    }
    else {
        wf->fdlist = XtReallocArray(wf->fdlist, (Cardinal) wf->fdlistlen,
                                    (Cardinal) sizeof(struct pollfd));
    }

    if (wf->fdlistlen) {
        struct pollfd *fdlp = wf->fdlist;
        InputEvent *iep;

        if (!ignoreEvents)
            for (ii = 0; ii < wf->num_dpys; ii++, fdlp++) {
                fdlp->fd = ConnectionNumber(app->list[ii]);
                fdlp->events = POLLIN;
            }
        if (!ignoreInputs && app->input_list != NULL)
            for (ii = 0; ii < app->input_max; ii++)
                if (app->input_list[ii] != NULL) {
                    iep = app->input_list[ii];
                    fdlp->fd = ii;
                    fdlp->events = 0;
                    for (; iep; iep = iep->ie_next) {
                        if (iep->ie_condition & XtInputReadMask)
                            fdlp->events |= XPOLL_READ;
                        if (iep->ie_condition & XtInputWriteMask)
                            fdlp->events |= XPOLL_WRITE;
                        if (iep->ie_condition & XtInputExceptMask)
                            fdlp->events |= XPOLL_EXCEPT;
                    }
                    fdlp++;
                }
    }
#else
    wf->nfds = app->fds.nfds;
    if (!ignoreInputs) {
        wf->rmask = app->fds.rmask;
        wf->wmask = app->fds.wmask;
        wf->emask = app->fds.emask;
    }
    else
        wf->rmask = wf->wmask = wf->emask = zero_fd;

    if (!ignoreEvents)
        for (ii = 0; ii < app->count; ii++) {
            FD_SET(ConnectionNumber(app->list[ii]), &wf->rmask);
        }
#endif
}

static void
AdjustTimes(XtAppContext app,
            Boolean block,
            const unsigned long *howlong,
            Boolean ignoreTimers,
            wait_times_ptr_t wt)
{
    if (app->timerQueue != NULL && !ignoreTimers && block) {
#ifdef USE_POLL
        if (IS_AFTER(wt->cur_time, app->timerQueue->te_timer_value)) {
            TIMEDELTA(wt->wait_time, app->timerQueue->te_timer_value,
                      wt->cur_time);
            if (howlong == NULL || IS_AFTER(wt->wait_time, wt->max_wait_time))
                wt->poll_wait =
                    (int) (wt->wait_time.tv_sec * 1000 +
                           wt->wait_time.tv_usec / 1000);
            else
                wt->poll_wait =
                    (int) (wt->max_wait_time.tv_sec * 1000 +
                           wt->max_wait_time.tv_usec / 1000);
        }
        else
            wt->poll_wait = X_DONT_BLOCK;
#else
        if (IS_AFTER(wt->cur_time, app->timerQueue->te_timer_value)) {
            TIMEDELTA(wt->wait_time, app->timerQueue->te_timer_value,
                      wt->cur_time);
            if (howlong == NULL || IS_AFTER(wt->wait_time, wt->max_wait_time))
                wt->wait_time_ptr = &wt->wait_time;
            else
                wt->wait_time_ptr = &wt->max_wait_time;
        }
        else
            wt->wait_time_ptr = &zero_time;
#endif
    }
}

static int
IoWait(wait_times_ptr_t wt, wait_fds_ptr_t wf)
{
#ifdef USE_POLL
    return poll(wf->fdlist, (nfds_t) wf->fdlistlen, wt->poll_wait);
#else
    return Select (wf->nfds, &wf->rmask, &wf->wmask, &wf->emask,
                   wt->wait_time_ptr);
#endif
}

static void
FindInputs(XtAppContext app,
           wait_fds_ptr_t wf,
           int nfds _X_UNUSED,
           Boolean ignoreEvents,
           Boolean ignoreInputs,
           int *dpy_no,
           int *found_input)
{
    InputEvent *ep;
    int ii;

#ifdef USE_POLL                 /* { check ready file descriptors block */
    struct pollfd *fdlp;

    *dpy_no = -1;
    *found_input = False;

    if (!ignoreEvents) {
        fdlp = wf->fdlist;
        for (ii = 0; ii < wf->num_dpys; ii++, fdlp++) {
            if (*dpy_no == -1 && fdlp->revents & (POLLIN | POLLHUP | POLLERR) &&
#ifdef XTHREADS
                !(fdlp->revents & POLLNVAL) &&
#endif
                XEventsQueued(app->list[ii], QueuedAfterReading)) {
                *dpy_no = ii;
                break;
            }
        }
    }

    if (!ignoreInputs) {
        fdlp = &wf->fdlist[wf->num_dpys];
        for (ii = wf->num_dpys; ii < wf->fdlistlen; ii++, fdlp++) {
            XtInputMask condition = 0;

            if (fdlp->revents) {
                if (fdlp->revents & (XPOLL_READ | POLLHUP | POLLERR)
#ifdef XTHREADS
                    && !(fdlp->revents & POLLNVAL)
#endif
                    )
                    condition = XtInputReadMask;
                if (fdlp->revents & XPOLL_WRITE)
                    condition |= XtInputWriteMask;
                if (fdlp->revents & XPOLL_EXCEPT)
                    condition |= XtInputExceptMask;
            }
            if (condition) {
                *found_input = True;
                for (ep = app->input_list[fdlp->fd]; ep; ep = ep->ie_next)
                    if (condition & ep->ie_condition) {
                        InputEvent *oq;

                        /* make sure this input isn't already marked outstanding */
                        for (oq = app->outstandingQueue; oq; oq = oq->ie_oq)
                            if (oq == ep)
                                break;
                        if (!oq) {
                            ep->ie_oq = app->outstandingQueue;
                            app->outstandingQueue = ep;
                        }
                    }
            }
        }
    }
#else                           /* }{ */
#ifdef XTHREADS
    fd_set rmask;
#endif
    int dd;

    *dpy_no = -1;
    *found_input = False;

#ifdef XTHREADS
    rmask = app->fds.rmask;
    for (dd = app->count; dd-- > 0;)
        FD_SET(ConnectionNumber(app->list[dd]), &rmask);
#endif

    for (ii = 0; ii < wf->nfds && nfds > 0; ii++) {
        XtInputMask condition = 0;

        if (FD_ISSET(ii, &wf->rmask)
#ifdef XTHREADS
            && FD_ISSET(ii, &rmask)
#endif
            ) {
            nfds--;
            if (!ignoreEvents) {
                for (dd = 0; dd < app->count; dd++) {
                    if (ii == ConnectionNumber(app->list[dd])) {
                        if (*dpy_no == -1) {
                            if (XEventsQueued
                                (app->list[dd], QueuedAfterReading))
                                *dpy_no = dd;
                            /*
                             * An error event could have arrived
                             * without any real events, or events
                             * could have been swallowed by Xlib,
                             * or the connection may be broken.
                             * We can't tell the difference, so
                             * assume Xlib will eventually discover
                             * a broken connection.
                             */
                        }
                        goto ENDILOOP;
                    }
                }
            }
            condition = XtInputReadMask;
        }
        if (FD_ISSET(ii, &wf->wmask)
#ifdef XTHREADS
            && FD_ISSET(ii, &app->fds.wmask)
#endif
            ) {
            condition |= XtInputWriteMask;
            nfds--;
        }
        if (FD_ISSET(ii, &wf->emask)
#ifdef XTHREADS
            && FD_ISSET(ii, &app->fds.emask)
#endif
            ) {
            condition |= XtInputExceptMask;
            nfds--;
        }
        if (condition) {
            for (ep = app->input_list[ii]; ep; ep = ep->ie_next)
                if (condition & ep->ie_condition) {
                    /* make sure this input isn't already marked outstanding */
                    InputEvent *oq;

                    for (oq = app->outstandingQueue; oq; oq = oq->ie_oq)
                        if (oq == ep)
                            break;
                    if (!oq) {
                        ep->ie_oq = app->outstandingQueue;
                        app->outstandingQueue = ep;
                    }
                }
            *found_input = True;
        }
 ENDILOOP:;
    }                           /* endfor */
#endif                          /* } */
}

/*
 * Routine to block in the toolkit.  This should be the only call to select.
 *
 * This routine returns when there is something to be done.
 *
 * Before calling this with ignoreInputs==False, app->outstandingQueue should
 * be checked; this routine will not verify that an alternate input source
 * has not already been enqueued.
 *
 *
 * _XtWaitForSomething( appContext,
 *                      ignoreEvent, ignoreTimers, ignoreInputs, ignoreSignals,
 *                      block, drop_lock, howlong)
 * XtAppContext app;         (Displays to check wait on)
 *
 * Boolean ignoreEvents;     (Don't return if XEvents are available
 *                              Also implies forget XEvents exist)
 *
 * Boolean ignoreTimers;     (Ditto for timers)
 *
 * Boolean ignoreInputs;     (Ditto for input callbacks )
 *
 * Boolean ignoreSignals;    (Ditto for signals)
 *
 * Boolean block;            (Okay to block)
 *
 * Boolean drop_lock         (drop lock before going into select/poll)
 *
 * TimeVal howlong;          (howlong to wait for if blocking and not
 *                              doing Timers... Null means forever.
 *                              Maybe should mean shortest of both)
 * Returns display for which input is available, if any
 * and if ignoreEvents==False, else returns -1
 *
 * if ignoring everything && block=True && howlong=NULL, you'll have
 * lots of time for coffee; better not try it!  In fact, it probably
 * makes little sense to do this regardless of the value of howlong
 * (bottom line is, we don't bother checking here).
 *
 * If drop_lock is FALSE, the app->lock->mutex is not unlocked before
 * entering select/poll. It is illegal for drop_lock to be FALSE if
 * ignoreTimers, ignoreInputs, or ignoreSignals is FALSE.
 */
int
_XtWaitForSomething(XtAppContext app,
                    _XtBoolean ignoreEvents,
                    _XtBoolean ignoreTimers,
                    _XtBoolean ignoreInputs,
                    _XtBoolean ignoreSignals,
                    _XtBoolean block,
                    _XtBoolean drop_lock, /* only needed with XTHREADS */
                    unsigned long *howlong)
{
    wait_times_t wt;
    wait_fds_t wf;
    int nfds, dpy_no, found_input, dd;

#ifdef XTHREADS
    Boolean push_thread = TRUE;
    Boolean pushed_thread = FALSE;
    int level = 0;
#endif
#ifdef USE_POLL
    struct pollfd fdlist[XT_DEFAULT_FDLIST_SIZE];
#endif

#ifdef XTHREADS
    /* assert ((ignoreTimers && ignoreInputs && ignoreSignals) || drop_lock); */
    /* If not multi-threaded, never drop lock */
    if (app->lock == (ThreadAppProc) NULL)
        drop_lock = FALSE;
#else
    (void) drop_lock;           /* avoid unused warning */
#endif

    InitTimes((Boolean) block, howlong, &wt);

#ifdef USE_POLL
    wf.fdlist = NULL;
    wf.stack = fdlist;
    wf.fdlistlen = wf.num_dpys = 0;
#endif

 WaitLoop:
    app->rebuild_fdlist = TRUE;

    while (1) {
        AdjustTimes(app, (Boolean) block, howlong, (Boolean) ignoreTimers, &wt);

        if (block && app->block_hook_list) {
            BlockHook hook;

            for (hook = app->block_hook_list; hook != NULL; hook = hook->next)
                (*hook->proc) (hook->closure);

            if (!ignoreEvents)
                /* see if the hook(s) generated any protocol */
                for (dd = 0; dd < app->count; dd++)
                    if (XEventsQueued(app->list[dd], QueuedAlready)) {
#ifdef USE_POLL
                        XtStackFree((XtPointer) wf.fdlist, fdlist);
#endif
                        return dd;
                    }
        }

        if (app->rebuild_fdlist)
            InitFds(app, (Boolean) ignoreEvents, (Boolean) ignoreInputs, &wf);

#ifdef XTHREADS                 /* { */
        if (drop_lock) {
            YIELD_APP_LOCK(app, &push_thread, &pushed_thread, &level);
            nfds = IoWait(&wt, &wf);
            RESTORE_APP_LOCK(app, level, &pushed_thread);
        }
        else
#endif                          /* } */
            nfds = IoWait(&wt, &wf);
        if (nfds == -1) {
            /*
             *  interrupt occurred recalculate time value and wait again.
             */
            if (errno == EINTR || errno == EAGAIN) {
                if (errno == EAGAIN) {
                    errno = 0;  /* errno is not self resetting */
                    continue;
                }
                errno = 0;      /* errno is not self resetting */

                /* was it interrupted by a signal that we care about? */
                if (!ignoreSignals && app->signalQueue != NULL) {
                    SignalEventRec *se_ptr = app->signalQueue;

                    while (se_ptr != NULL) {
                        if (se_ptr->se_notice) {
                            if (block && howlong != NULL)
                                AdjustHowLong(howlong, &wt.start_time);
#ifdef USE_POLL
                            XtStackFree((XtPointer) wf.fdlist, fdlist);
#endif
                            return -1;
                        }
                        se_ptr = se_ptr->se_next;
                    }
                }

                if (!ignoreEvents)
                    /* get Xlib to detect a bad connection */
                    for (dd = 0; dd < app->count; dd++)
                        if (XEventsQueued(app->list[dd], QueuedAfterReading)) {
#ifdef USE_POLL
                            XtStackFree((XtPointer) wf.fdlist, fdlist);
#endif
                            return dd;
                        }

                if (block) {
#ifdef USE_POLL
                    if (wt.poll_wait == X_BLOCK)
#else
                    if (wt.wait_time_ptr == NULL)
#endif
                        continue;
                    X_GETTIMEOFDAY(&wt.new_time);
                    FIXUP_TIMEVAL(wt.new_time);
                    TIMEDELTA(wt.time_spent, wt.new_time, wt.cur_time);
                    wt.cur_time = wt.new_time;
#ifdef USE_POLL
                    if ((wt.time_spent.tv_sec * 1000 +
                         wt.time_spent.tv_usec / 1000) < wt.poll_wait) {
                        wt.poll_wait -=
                            (int) (wt.time_spent.tv_sec * 1000 +
                                   wt.time_spent.tv_usec / 1000);
                        continue;
                    }
                    else
#else
                    if (IS_AFTER(wt.time_spent, *wt.wait_time_ptr)) {
                        TIMEDELTA(wt.wait_time, *wt.wait_time_ptr,
                                  wt.time_spent);
                        wt.wait_time_ptr = &wt.wait_time;
                        continue;
                    }
                    else
#endif
                        nfds = 0;
                }
            }
            else {
                char Errno[12];
                String param = Errno;
                Cardinal param_count = 1;

                sprintf(Errno, "%d", errno);
                XtAppWarningMsg(app, "communicationError", "select",
                                XtCXtToolkitError,
                                "Select failed; error code %s", &param,
                                &param_count);
                continue;
            }
        }                       /* timed out or input available */
        break;
    }

    if (nfds == 0) {
        /* Timed out */
        if (howlong)
            *howlong = (unsigned long) 0;
#ifdef USE_POLL
        XtStackFree((XtPointer) wf.fdlist, fdlist);
#endif
        return -1;
    }

    if (block && howlong != NULL)
        AdjustHowLong(howlong, &wt.start_time);

    if (ignoreInputs && ignoreEvents) {
#ifdef USE_POLL
        XtStackFree((XtPointer) wf.fdlist, fdlist);
#endif
        return -1;
    }
    else
        FindInputs(app, &wf, nfds,
                   (Boolean) ignoreEvents, (Boolean) ignoreInputs,
                   &dpy_no, &found_input);

    if (dpy_no >= 0 || found_input) {
#ifdef USE_POLL
        XtStackFree((XtPointer) wf.fdlist, fdlist);
#endif
        return dpy_no;
    }
    if (block)
        goto WaitLoop;
    else {
#ifdef USE_POLL
        XtStackFree((XtPointer) wf.fdlist, fdlist);
#endif
        return -1;
    }
}

#define IeCallProc(ptr) \
    (*ptr->ie_proc) (ptr->ie_closure, &ptr->ie_source, (XtInputId*)&ptr);

#define TeCallProc(ptr) \
    (*ptr->te_proc) (ptr->te_closure, (XtIntervalId*)&ptr);

#define SeCallProc(ptr) \
    (*ptr->se_proc) (ptr->se_closure, (XtSignalId*)&ptr);

/*
 * Public Routines
 */

XtIntervalId
XtAddTimeOut(unsigned long interval,
             XtTimerCallbackProc proc,
             XtPointer closure)
{
    return XtAppAddTimeOut(_XtDefaultAppContext(), interval, proc, closure);
}

static void
QueueTimerEvent(XtAppContext app, TimerEventRec *ptr)
{
    TimerEventRec *t, **tt;

    tt = &app->timerQueue;
    t = *tt;
    while (t != NULL && IS_AFTER(t->te_timer_value, ptr->te_timer_value)) {
        tt = &t->te_next;
        t = *tt;
    }
    ptr->te_next = t;
    *tt = ptr;
}

XtIntervalId
XtAppAddTimeOut(XtAppContext app,
                unsigned long interval,
                XtTimerCallbackProc proc,
                XtPointer closure)
{
    TimerEventRec *tptr;
    struct timeval current_time;

    LOCK_APP(app);
    LOCK_PROCESS;
    if (freeTimerRecs) {
        tptr = freeTimerRecs;
        freeTimerRecs = tptr->te_next;
    }
    else
        tptr = XtNew(TimerEventRec);

    UNLOCK_PROCESS;
    tptr->te_next = NULL;
    tptr->te_closure = closure;
    tptr->te_proc = proc;
    tptr->app = app;
    tptr->te_timer_value.tv_sec = (time_t) (interval / 1000);
    tptr->te_timer_value.tv_usec = (suseconds_t) ((interval % 1000) * 1000);
    X_GETTIMEOFDAY(&current_time);
    FIXUP_TIMEVAL(current_time);
    ADD_TIME(tptr->te_timer_value, tptr->te_timer_value, current_time);
    QueueTimerEvent(app, tptr);
    UNLOCK_APP(app);

    return ((XtIntervalId) tptr);
}

void
XtRemoveTimeOut(XtIntervalId id)
{
    TimerEventRec *t, *last, *tid = (TimerEventRec *) id;
    XtAppContext app = tid->app;

    /* find it */
    LOCK_APP(app);
    for (t = app->timerQueue, last = NULL;
         t != NULL && t != tid; t = t->te_next)
        last = t;

    if (t == NULL) {
        UNLOCK_APP(app);
        return;                 /* couldn't find it */
    }
    if (last == NULL) {         /* first one on the list */
        app->timerQueue = t->te_next;
    }
    else
        last->te_next = t->te_next;

    LOCK_PROCESS;
    t->te_next = freeTimerRecs;
    freeTimerRecs = t;
    UNLOCK_PROCESS;
    UNLOCK_APP(app);
}

XtWorkProcId
XtAddWorkProc(XtWorkProc proc, XtPointer closure)
{
    return XtAppAddWorkProc(_XtDefaultAppContext(), proc, closure);
}

XtWorkProcId
XtAppAddWorkProc(XtAppContext app, XtWorkProc proc, XtPointer closure)
{
    WorkProcRec *wptr;

    LOCK_APP(app);
    LOCK_PROCESS;
    if (freeWorkRecs) {
        wptr = freeWorkRecs;
        freeWorkRecs = wptr->next;
    }
    else
        wptr = XtNew(WorkProcRec);

    UNLOCK_PROCESS;
    wptr->next = app->workQueue;
    wptr->closure = closure;
    wptr->proc = proc;
    wptr->app = app;
    app->workQueue = wptr;
    UNLOCK_APP(app);

    return (XtWorkProcId) wptr;
}

void
XtRemoveWorkProc(XtWorkProcId id)
{
    WorkProcRec *wid = (WorkProcRec *) id, *w, *last;
    XtAppContext app = wid->app;

    LOCK_APP(app);
    /* find it */
    for (w = app->workQueue, last = NULL; w != NULL && w != wid; w = w->next)
        last = w;

    if (w == NULL) {
        UNLOCK_APP(app);
        return;                 /* couldn't find it */
    }

    if (last == NULL)
        app->workQueue = w->next;
    else
        last->next = w->next;
    LOCK_PROCESS;
    w->next = freeWorkRecs;
    freeWorkRecs = w;
    UNLOCK_PROCESS;
    UNLOCK_APP(app);
}

XtSignalId
XtAddSignal(XtSignalCallbackProc proc, XtPointer closure)
{
    return XtAppAddSignal(_XtDefaultAppContext(), proc, closure);
}

XtSignalId
XtAppAddSignal(XtAppContext app, XtSignalCallbackProc proc, XtPointer closure)
{
    SignalEventRec *sptr;

    LOCK_APP(app);
    LOCK_PROCESS;
    if (freeSignalRecs) {
        sptr = freeSignalRecs;
        freeSignalRecs = sptr->se_next;
    }
    else
        sptr = XtNew(SignalEventRec);

    UNLOCK_PROCESS;
    sptr->se_next = app->signalQueue;
    sptr->se_closure = closure;
    sptr->se_proc = proc;
    sptr->app = app;
    sptr->se_notice = FALSE;
    app->signalQueue = sptr;
    UNLOCK_APP(app);
    return (XtSignalId) sptr;
}

void
XtRemoveSignal(XtSignalId id)
{
    SignalEventRec *sid = (SignalEventRec *) id, *s, *last = NULL;
    XtAppContext app = sid->app;

    LOCK_APP(app);
    for (s = app->signalQueue; s != NULL && s != sid; s = s->se_next)
        last = s;
    if (s == NULL) {
        UNLOCK_APP(app);
        return;
    }
    if (last == NULL)
        app->signalQueue = s->se_next;
    else
        last->se_next = s->se_next;
    LOCK_PROCESS;
    s->se_next = freeSignalRecs;
    freeSignalRecs = s;
    UNLOCK_PROCESS;
    UNLOCK_APP(app);
}

void
XtNoticeSignal(XtSignalId id)
{
    /*
     * It would be overkill to lock the app to set this flag.
     * In the worst case, 2..n threads would be modifying this
     * flag. The last one wins. Since signals occur asynchronously
     * anyway, this can occur with or without threads.
     *
     * The other issue is that thread t1 sets the flag in a
     * signalrec that has been deleted in thread t2. We rely
     * on a detail of the implementation, i.e. free'd signalrecs
     * aren't really free'd, they're just moved to a list of
     * free recs, so deref'ing one won't hurt anything.
     *
     * Lastly, and perhaps most importantly, since POSIX threads
     * says that the handling of asynchronous signals in a synchronous
     * threads environment is undefined. Therefore it would be an
     * error for both signals and threads to be in use in the same
     * program.
     */
    SignalEventRec *sid = (SignalEventRec *) id;

    sid->se_notice = TRUE;
}

XtInputId
XtAddInput(int source,
           XtPointer Condition,
           XtInputCallbackProc proc,
           XtPointer closure)
{
    return XtAppAddInput(_XtDefaultAppContext(),
                         source, Condition, proc, closure);
}

XtInputId
XtAppAddInput(XtAppContext app,
              int source,
              XtPointer Condition,
              XtInputCallbackProc proc,
              XtPointer closure)
{
    InputEvent *sptr;
    XtInputMask condition = (XtInputMask) Condition;

    LOCK_APP(app);
    if (!condition ||
        condition & (unsigned
                     long) (~(XtInputReadMask | XtInputWriteMask |
                              XtInputExceptMask)))
        XtAppErrorMsg(app, "invalidParameter", "xtAddInput", XtCXtToolkitError,
                      "invalid condition passed to XtAppAddInput", NULL, NULL);

    if (app->input_max <= source) {
        Cardinal n = (Cardinal) (source + 1);
        int ii;

        app->input_list = XtReallocArray(app->input_list, n,
                                         (Cardinal) sizeof(InputEvent *));
        for (ii = app->input_max; ii < (int) n; ii++)
            app->input_list[ii] = (InputEvent *) NULL;
        app->input_max = (short) n;
    }
    sptr = XtNew(InputEvent);

    sptr->ie_proc = proc;
    sptr->ie_closure = closure;
    sptr->app = app;
    sptr->ie_oq = NULL;
    sptr->ie_source = source;
    sptr->ie_condition = condition;
    sptr->ie_next = app->input_list[source];
    app->input_list[source] = sptr;

#ifdef USE_POLL
    if (sptr->ie_next == NULL)
        app->fds.nfds++;
#else
    if (condition & XtInputReadMask)
        FD_SET(source, &app->fds.rmask);
    if (condition & XtInputWriteMask)
        FD_SET(source, &app->fds.wmask);
    if (condition & XtInputExceptMask)
        FD_SET(source, &app->fds.emask);

    if (app->fds.nfds < (source + 1))
        app->fds.nfds = source + 1;
#endif
    app->input_count++;
    app->rebuild_fdlist = TRUE;
    UNLOCK_APP(app);
    return ((XtInputId) sptr);
}

void
XtRemoveInput(register XtInputId id)
{
    register InputEvent *sptr, *lptr;
    XtAppContext app = ((InputEvent *) id)->app;
    register int source = ((InputEvent *) id)->ie_source;
    Boolean found = False;

    LOCK_APP(app);
    sptr = app->outstandingQueue;
    lptr = NULL;
    for (; sptr != NULL; sptr = sptr->ie_oq) {
        if (sptr == (InputEvent *) id) {
            if (lptr == NULL)
                app->outstandingQueue = sptr->ie_oq;
            else
                lptr->ie_oq = sptr->ie_oq;
        }
        lptr = sptr;
    }

    if (app->input_list && (sptr = app->input_list[source]) != NULL) {
        for (lptr = NULL; sptr; sptr = sptr->ie_next) {
            if (sptr == (InputEvent *) id) {
#ifndef USE_POLL
                XtInputMask condition = 0;
#endif
                if (lptr == NULL) {
                    app->input_list[source] = sptr->ie_next;
                }
                else {
                    lptr->ie_next = sptr->ie_next;
                }
#ifndef USE_POLL
                for (lptr = app->input_list[source]; lptr; lptr = lptr->ie_next)
                    condition |= lptr->ie_condition;
                if ((sptr->ie_condition & XtInputReadMask) &&
                    !(condition & XtInputReadMask))
                    FD_CLR(source, &app->fds.rmask);
                if ((sptr->ie_condition & XtInputWriteMask) &&
                    !(condition & XtInputWriteMask))
                    FD_CLR(source, &app->fds.wmask);
                if ((sptr->ie_condition & XtInputExceptMask) &&
                    !(condition & XtInputExceptMask))
                    FD_CLR(source, &app->fds.emask);
#endif
                XtFree((char *) sptr);
                found = True;
                break;
            }
            lptr = sptr;
        }
    }

    if (found) {
        app->input_count--;
#ifdef USE_POLL
        if (app->input_list[source] == NULL)
            app->fds.nfds--;
#endif
        app->rebuild_fdlist = TRUE;
    }
    else
        XtAppWarningMsg(app, "invalidProcedure", "inputHandler",
                        XtCXtToolkitError,
                        "XtRemoveInput: Input handler not found", NULL, NULL);
    UNLOCK_APP(app);
}

void
_XtRemoveAllInputs(XtAppContext app)
{
    int i;

    for (i = 0; i < app->input_max; i++) {
        InputEvent *ep = app->input_list[i];

        while (ep) {
            InputEvent *next = ep->ie_next;

            XtFree((char *) ep);
            ep = next;
        }
    }
    XtFree((char *) app->input_list);
}

/* Do alternate input and timer callbacks if there are any */

static void
DoOtherSources(XtAppContext app)
{
    TimerEventRec *te_ptr;
    InputEvent *ie_ptr;
    struct timeval cur_time;

#define DrainQueue() \
        for (ie_ptr = app->outstandingQueue; ie_ptr != NULL;) { \
            app->outstandingQueue = ie_ptr->ie_oq;              \
            ie_ptr ->ie_oq = NULL;                              \
            IeCallProc(ie_ptr);                                 \
            ie_ptr = app->outstandingQueue;                     \
        }
/*enddef*/
    DrainQueue();
    if (app->input_count > 0) {
        /* Call _XtWaitForSomething to get input queued up */
        (void) _XtWaitForSomething(app,
                                   TRUE, TRUE, FALSE, TRUE,
                                   FALSE, TRUE, (unsigned long *) NULL);
        DrainQueue();
    }
    if (app->timerQueue != NULL) {      /* check timeout queue */
        X_GETTIMEOFDAY(&cur_time);
        FIXUP_TIMEVAL(cur_time);
        while (IS_AT_OR_AFTER(app->timerQueue->te_timer_value, cur_time)) {
            te_ptr = app->timerQueue;
            app->timerQueue = te_ptr->te_next;
            te_ptr->te_next = NULL;
            if (te_ptr->te_proc != NULL)
                TeCallProc(te_ptr);
            LOCK_PROCESS;
            te_ptr->te_next = freeTimerRecs;
            freeTimerRecs = te_ptr;
            UNLOCK_PROCESS;
            if (app->timerQueue == NULL)
                break;
        }
    }
    if (app->signalQueue != NULL) {
        SignalEventRec *se_ptr = app->signalQueue;

        while (se_ptr != NULL) {
            if (se_ptr->se_notice) {
                se_ptr->se_notice = FALSE;
                if (se_ptr->se_proc != NULL)
                    SeCallProc(se_ptr);
            }
            se_ptr = se_ptr->se_next;
        }
    }
#undef DrainQueue
}

/* If there are any work procs, call them.  Return whether we did so */

static Boolean
CallWorkProc(XtAppContext app)
{
    register WorkProcRec *w = app->workQueue;
    Boolean delete;

    if (w == NULL)
        return FALSE;

    app->workQueue = w->next;

    delete = (*(w->proc)) (w->closure);

    if (delete) {
        LOCK_PROCESS;
        w->next = freeWorkRecs;
        freeWorkRecs = w;
        UNLOCK_PROCESS;
    }
    else {
        w->next = app->workQueue;
        app->workQueue = w;
    }
    return TRUE;
}

/*
 * XtNextEvent()
 * return next event;
 */

void
XtNextEvent(XEvent *event)
{
    XtAppNextEvent(_XtDefaultAppContext(), event);
}

void
_XtRefreshMapping(XEvent *event, _XtBoolean dispatch)
{
    XtPerDisplay pd;

    LOCK_PROCESS;
    pd = _XtGetPerDisplay(event->xmapping.display);

    if (event->xmapping.request != MappingPointer &&
        pd && pd->keysyms && (event->xmapping.serial >= pd->keysyms_serial))
        _XtBuildKeysymTables(event->xmapping.display, pd);

    XRefreshKeyboardMapping(&event->xmapping);
    if (dispatch && pd && pd->mapping_callbacks)
        XtCallCallbackList((Widget) NULL,
                           (XtCallbackList) pd->mapping_callbacks,
                           (XtPointer) event);
    UNLOCK_PROCESS;
}

void
XtAppNextEvent(XtAppContext app, XEvent *event)
{
    int i, d;

    LOCK_APP(app);
    for (;;) {
        if (app->count == 0)
            DoOtherSources(app);
        else {
            for (i = 1; i <= app->count; i++) {
                d = (i + app->last) % app->count;
                if (d == 0)
                    DoOtherSources(app);
                if (XEventsQueued(app->list[d], QueuedAfterReading))
                    goto GotEvent;
            }
            for (i = 1; i <= app->count; i++) {
                d = (i + app->last) % app->count;
                if (XEventsQueued(app->list[d], QueuedAfterFlush))
                    goto GotEvent;
            }
        }

        /* We're ready to wait...if there is a work proc, call it */
        if (CallWorkProc(app))
            continue;

        d = _XtWaitForSomething(app,
                                FALSE, FALSE, FALSE, FALSE,
                                TRUE, TRUE, (unsigned long *) NULL);

        if (d != -1) {
 GotEvent:
            XNextEvent(app->list[d], event);
            app->last = (short) d;
            if (event->xany.type == MappingNotify)
                _XtRefreshMapping(event, False);
            UNLOCK_APP(app);
            return;
        }

    }                           /* for */
}

void
XtProcessEvent(XtInputMask mask)
{
    XtAppProcessEvent(_XtDefaultAppContext(), mask);
}

void
XtAppProcessEvent(XtAppContext app, XtInputMask mask)
{
    int i, d;
    XEvent event;
    struct timeval cur_time;

    LOCK_APP(app);
    if (mask == 0) {
        UNLOCK_APP(app);
        return;
    }

    for (;;) {

        if (mask & XtIMSignal && app->signalQueue != NULL) {
            SignalEventRec *se_ptr = app->signalQueue;

            while (se_ptr != NULL) {
                if (se_ptr->se_notice) {
                    se_ptr->se_notice = FALSE;
                    SeCallProc(se_ptr);
                    UNLOCK_APP(app);
                    return;
                }
                se_ptr = se_ptr->se_next;
            }
        }

        if (mask & XtIMTimer && app->timerQueue != NULL) {
            X_GETTIMEOFDAY(&cur_time);
            FIXUP_TIMEVAL(cur_time);
            if (IS_AT_OR_AFTER(app->timerQueue->te_timer_value, cur_time)) {
                TimerEventRec *te_ptr = app->timerQueue;

                app->timerQueue = app->timerQueue->te_next;
                te_ptr->te_next = NULL;
                if (te_ptr->te_proc != NULL)
                    TeCallProc(te_ptr);
                LOCK_PROCESS;
                te_ptr->te_next = freeTimerRecs;
                freeTimerRecs = te_ptr;
                UNLOCK_PROCESS;
                UNLOCK_APP(app);
                return;
            }
        }

        if (mask & XtIMAlternateInput) {
            if (app->input_count > 0 && app->outstandingQueue == NULL) {
                /* Call _XtWaitForSomething to get input queued up */
                (void) _XtWaitForSomething(app,
                                           TRUE, TRUE, FALSE, TRUE,
                                           FALSE, TRUE, (unsigned long *) NULL);
            }
            if (app->outstandingQueue != NULL) {
                InputEvent *ie_ptr = app->outstandingQueue;

                app->outstandingQueue = ie_ptr->ie_oq;
                ie_ptr->ie_oq = NULL;
                IeCallProc(ie_ptr);
                UNLOCK_APP(app);
                return;
            }
        }

        if (mask & XtIMXEvent) {
            for (i = 1; i <= app->count; i++) {
                d = (i + app->last) % app->count;
                if (XEventsQueued(app->list[d], QueuedAfterReading))
                    goto GotEvent;
            }
            for (i = 1; i <= app->count; i++) {
                d = (i + app->last) % app->count;
                if (XEventsQueued(app->list[d], QueuedAfterFlush))
                    goto GotEvent;
            }
        }

        /* Nothing to do...wait for something */

        if (CallWorkProc(app))
            continue;

        d = _XtWaitForSomething(app,
                                ((mask & XtIMXEvent) ? FALSE : TRUE),
                                ((mask & XtIMTimer) ? FALSE : TRUE),
                                ((mask & XtIMAlternateInput) ? FALSE : TRUE),
                                ((mask & XtIMSignal) ? FALSE : TRUE),
                                TRUE, TRUE, (unsigned long *) NULL);

        if (mask & XtIMXEvent && d != -1) {
 GotEvent:
            XNextEvent(app->list[d], &event);
            app->last = (short) d;
            if (event.xany.type == MappingNotify) {
                _XtRefreshMapping(&event, False);
            }
            XtDispatchEvent(&event);
            UNLOCK_APP(app);
            return;
        }

    }
}

Boolean
XtPending(void)
{
    return (XtAppPending(_XtDefaultAppContext()) != 0);
}

XtInputMask
XtAppPending(XtAppContext app)
{
    struct timeval cur_time;
    int d;
    XtInputMask ret = 0;

/*
 * Check for pending X events
 */
    LOCK_APP(app);
    for (d = 0; d < app->count; d++) {
        if (XEventsQueued(app->list[d], QueuedAfterReading)) {
            ret = XtIMXEvent;
            break;
        }
    }
    if (ret == 0) {
        for (d = 0; d < app->count; d++) {
            if (XEventsQueued(app->list[d], QueuedAfterFlush)) {
                ret = XtIMXEvent;
                break;
            }
        }
    }

    if (app->signalQueue != NULL) {
        SignalEventRec *se_ptr = app->signalQueue;

        while (se_ptr != NULL) {
            if (se_ptr->se_notice) {
                ret |= XtIMSignal;
                break;
            }
            se_ptr = se_ptr->se_next;
        }
    }

/*
 * Check for pending alternate input
 */
    if (app->timerQueue != NULL) {      /* check timeout queue */
        X_GETTIMEOFDAY(&cur_time);
        FIXUP_TIMEVAL(cur_time);
        if ((IS_AT_OR_AFTER(app->timerQueue->te_timer_value, cur_time)) &&
            (app->timerQueue->te_proc != NULL)) {
            ret |= XtIMTimer;
        }
    }

    if (app->outstandingQueue != NULL)
        ret |= XtIMAlternateInput;
    else {
        /* This won't cause a wait, but will enqueue any input */

        if (_XtWaitForSomething(app,
                                FALSE, TRUE, FALSE, TRUE,
                                FALSE, TRUE, (unsigned long *) NULL) != -1)
            ret |= XtIMXEvent;
        if (app->outstandingQueue != NULL)
            ret |= XtIMAlternateInput;
    }
    UNLOCK_APP(app);
    return ret;
}

/* Peek at alternate input and timer callbacks if there are any */

static Boolean
PeekOtherSources(XtAppContext app)
{
    struct timeval cur_time;

    if (app->outstandingQueue != NULL)
        return TRUE;

    if (app->signalQueue != NULL) {
        SignalEventRec *se_ptr = app->signalQueue;

        while (se_ptr != NULL) {
            if (se_ptr->se_notice)
                return TRUE;
            se_ptr = se_ptr->se_next;
        }
    }

    if (app->input_count > 0) {
        /* Call _XtWaitForSomething to get input queued up */
        (void) _XtWaitForSomething(app,
                                   TRUE, TRUE, FALSE, TRUE,
                                   FALSE, TRUE, (unsigned long *) NULL);
        if (app->outstandingQueue != NULL)
            return TRUE;
    }

    if (app->timerQueue != NULL) {      /* check timeout queue */
        X_GETTIMEOFDAY(&cur_time);
        FIXUP_TIMEVAL(cur_time);
        if (IS_AT_OR_AFTER(app->timerQueue->te_timer_value, cur_time))
            return TRUE;
    }

    return FALSE;
}

Boolean
XtPeekEvent(XEvent *event)
{
    return XtAppPeekEvent(_XtDefaultAppContext(), event);
}

Boolean XtAppPeekEvent_SkipTimer;

Boolean
XtAppPeekEvent(XtAppContext app, XEvent *event)
{
    int i, d;
    Boolean foundCall = FALSE;

    LOCK_APP(app);
    for (i = 1; i <= app->count; i++) {
        d = (i + app->last) % app->count;
        if (d == 0)
            foundCall = PeekOtherSources(app);
        if (XEventsQueued(app->list[d], QueuedAfterReading))
            goto GotEvent;
    }
    for (i = 1; i <= app->count; i++) {
        d = (i + app->last) % app->count;
        if (XEventsQueued(app->list[d], QueuedAfterFlush))
            goto GotEvent;
    }

    if (foundCall) {
        event->xany.type = 0;
        event->xany.display = NULL;

        event->xany.window = 0;
        UNLOCK_APP(app);
        return FALSE;
    }

    while (1) {
        d = _XtWaitForSomething(app,
                                FALSE, FALSE, FALSE, FALSE,
                                TRUE, TRUE, (unsigned long *) NULL);

        if (d != -1) {          /* event */
 GotEvent:
            XPeekEvent(app->list[d], event);
            app->last = (short) ((d == 0 ? app->count : d) - 1);
            UNLOCK_APP(app);
            return TRUE;
        }
        else {                  /* input or timer or signal */
            /*
             * Check to see why a -1 was returned, if a timer expired,
             * call it and block some more
             */
            if ((app->timerQueue != NULL) && !XtAppPeekEvent_SkipTimer) {       /* timer */
                struct timeval cur_time;
                Bool did_timer = False;

                X_GETTIMEOFDAY(&cur_time);
                FIXUP_TIMEVAL(cur_time);
                while (IS_AT_OR_AFTER
                       (app->timerQueue->te_timer_value, cur_time)) {
                    TimerEventRec *te_ptr = app->timerQueue;

                    app->timerQueue = app->timerQueue->te_next;
                    te_ptr->te_next = NULL;
                    if (te_ptr->te_proc != NULL) {
                        TeCallProc(te_ptr);
                        did_timer = True;
                    }
                    LOCK_PROCESS;
                    te_ptr->te_next = freeTimerRecs;
                    freeTimerRecs = te_ptr;
                    UNLOCK_PROCESS;
                    if (app->timerQueue == NULL)
                        break;
                }
                if (did_timer) {
                    for (d = 0; d < app->count; d++)
                        /* the timer's procedure may have caused an event */
                        if (XEventsQueued(app->list[d], QueuedAfterFlush)) {
                            goto GotEvent;
                        }
                    continue;   /* keep blocking */
                }
            }
            /*
             * spec is vague here; we'll assume signals also return FALSE,
             * of course to determine whether a signal is pending requires
             * walking the signalQueue looking for se_notice flags which
             * this code doesn't do.
             */
#if 0
            if (app->signalQueue != NULL) {     /* signal */
                event->xany.type = 0;
                event->xany.display = NULL;

                event->xany.window = 0;
                UNLOCK_APP(app);
                return FALSE;
            }
            else
#endif
            {                   /* input */
                event->xany.type = 0;
                event->xany.display = NULL;

                event->xany.window = 0;
                UNLOCK_APP(app);
                return FALSE;
            }
        }
    }                           /* end while */
}
