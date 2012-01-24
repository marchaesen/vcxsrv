/*
 *
 * Copyright (c) 1995 David E. Wexelblat.  All rights reserved
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL DAVID E. WEXELBLAT BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of David E. Wexelblat shall
 * not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization
 * from David E. Wexelblat.
 *
 */

/*
 * Stubs for thread functions needed by the X library.  Supports
 * UnixWare 2.x threads; may support Solaris 2 threads as well, but not
 * tested.  Defining things this way removes the dependency of the X
 * library on the threads library, but still supports threads if the user
 * specificies the thread library on the link line.
 */

/*
 * Modifications by Carlos A M dos Santos, XFree86 Project, November 1999.
 *
 * Explanation from <X11/Xos_r.h>:
 * The structure below is complicated, mostly because P1003.1c (the
 * IEEE POSIX Threads spec) went through lots of drafts, and some
 * vendors shipped systems based on draft API that were changed later.
 * Unfortunately POSIX did not provide a feature-test macro for
 * distinguishing each of the drafts.
 */

#ifdef CTHREADS
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <cthreads.h>
typedef cthread_t xthread_t;
#define xthread_self cthread_self
#pragma weak cthread_self = _Xthr_self_stub_
#define xmutex_init mutex_init
#pragma weak mutex_init = _Xthr_zero_stub_
#pragma weak mutex_clear = _Xthr_zero_stub_
#pragma weak mutex_lock = _Xthr_zero_stub_
#pragma weak mutex_unlock = _Xthr_zero_stub_
#pragma weak condition_init = _Xthr_zero_stub_
#pragma weak condition_clear = _Xthr_zero_stub_
#pragma weak condition_wait = _Xthr_zero_stub_
#pragma weak condition_signal = _Xthr_zero_stub_
#pragma weak condition_broadcast = _Xthr_zero_stub_
#else /* !CTHREADS */
#if defined(SVR4) && !defined(__sgi)
#include <thread.h>
typedef thread_t xthread_t;
#pragma weak thr_self = _Xthr_self_stub_
#pragma weak mutex_init = _Xthr_zero_stub_
#pragma weak mutex_destroy = _Xthr_zero_stub_
#pragma weak mutex_lock = _Xthr_zero_stub_
#pragma weak mutex_unlock = _Xthr_zero_stub_
#pragma weak cond_init = _Xthr_zero_stub_
#pragma weak cond_destroy = _Xthr_zero_stub_
#pragma weak cond_wait = _Xthr_zero_stub_
#pragma weak cond_signal = _Xthr_zero_stub_
#pragma weak cond_broadcast = _Xthr_zero_stub_
#else /* !SVR4 */
#ifdef WIN32
#include <X11/Xthreads.h>
#else /* !WIN32 */
#ifdef USE_TIS_SUPPORT
#include <tis.h>
typedef pthread_t xthread_t;
#pragma weak tis_self = _Xthr_self_stub_
#pragma weak tis_mutex_init = _Xthr_zero_stub_
#pragma weak tis_mutex_destroy = _Xthr_zero_stub_
#pragma weak tis_mutex_lock = _Xthr_zero_stub_
#pragma weak tis_mutex_unlock = _Xthr_zero_stub_
#pragma weak tis_cond_init = _Xthr_zero_stub_
#pragma weak tis_cond_destroy = _Xthr_zero_stub_
#pragma weak tis_cond_wait = _Xthr_zero_stub_
#pragma weak tis_cond_signal = _Xthr_zero_stub_
#pragma weak tis_cond_broadcast = _Xthr_zero_stub_
#else
#include <pthread.h>
typedef pthread_t xthread_t;
#if __GNUC__ >= 3 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 95)
xthread_t pthread_self()    __attribute__ ((weak, alias ("_Xthr_self_stub_")));
int pthread_mutex_init()    __attribute__ ((weak, alias ("_Xthr_zero_stub_")));
int pthread_mutex_destroy() __attribute__ ((weak, alias ("_Xthr_zero_stub_")));
int pthread_mutex_lock()    __attribute__ ((weak, alias ("_Xthr_zero_stub_")));
int pthread_mutex_unlock()  __attribute__ ((weak, alias ("_Xthr_zero_stub_")));
int pthread_cond_init()     __attribute__ ((weak, alias ("_Xthr_zero_stub_")));
int pthread_cond_destroy()  __attribute__ ((weak, alias ("_Xthr_zero_stub_")));
int pthread_cond_wait()     __attribute__ ((weak, alias ("_Xthr_zero_stub_")));
int pthread_cond_signal()   __attribute__ ((weak, alias ("_Xthr_zero_stub_")));
int pthread_cond_broadcast() __attribute__ ((weak, alias ("_Xthr_zero_stub_")));
int pthread_key_create()    __attribute__ ((weak, alias ("_Xthr_zero_stub_")));
void *pthread_getspecific()  __attribute__ ((weak, alias ("_Xthr_zero_stub_")));
int pthread_setspecific()   __attribute__ ((weak, alias ("_Xthr_zero_stub_")));
#else	/* __GNUC__ */
#pragma weak pthread_self = _Xthr_self_stub_
#pragma weak pthread_mutex_init = _Xthr_zero_stub_
#pragma weak pthread_mutex_destroy = _Xthr_zero_stub_
#pragma weak pthread_mutex_lock = _Xthr_zero_stub_
#pragma weak pthread_mutex_unlock = _Xthr_zero_stub_
#pragma weak pthread_cond_init = _Xthr_zero_stub_
#pragma weak pthread_cond_destroy = _Xthr_zero_stub_
#pragma weak pthread_cond_wait = _Xthr_zero_stub_
#pragma weak pthread_cond_signal = _Xthr_zero_stub_
#pragma weak pthread_cond_broadcast = _Xthr_zero_stub_
/* These are added for libGL */
#pragma weak pthread_key_create = _Xthr_zero_stub_
#pragma weak pthread_getspecific = _Xthr_zero_stub_
#pragma weak pthread_setspecific = _Xthr_zero_stub_
#endif	/* __GNUC__ */
#if defined(_DECTHREADS_) || defined(linux)
#pragma weak pthread_equal = _Xthr_equal_stub_	/* See Xthreads.h! */
int
_Xthr_equal_stub_()
{
    return(1);
}
#endif /* _DECTHREADS_ || linux */
#endif /* USE_TIS_SUPPORT */
#endif /* WIN32 */
#endif /* SVR4 */
#endif /* CTHREADS */

static xthread_t
_Xthr_self_stub_()
{
    static xthread_t _X_no_thread_id;

    return(_X_no_thread_id);	/* defined by <X11/Xthreads.h> */
}

static int
_Xthr_zero_stub_()
{
    return(0);
}
