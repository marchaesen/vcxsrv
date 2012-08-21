/*
 * Copyright (c) 2011-2012 Apple Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE ABOVE LISTED COPYRIGHT
 * HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name(s) of the above
 * copyright holders shall not be used in advertising or otherwise to
 * promote the sale, use or other dealings in this Software without
 * prior written authorization.
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#else
#define DEBUG_CONSOLE_REDIRECT 1
#define HAVE_LIBDISPATCH       1
#endif

#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/event.h>
#include <asl.h>
#include <errno.h>
#include <fcntl.h>

#include "console_redirect.h"

#define BUF_SIZE 512

#ifdef HAVE_LIBDISPATCH
#include <dispatch/dispatch.h>

static dispatch_queue_t redirect_serial_q;
static dispatch_group_t read_source_group;
#else
#include <pthread.h>

static pthread_t redirect_pthread;
static pthread_mutex_t redirect_fds_lock = PTHREAD_MUTEX_INITIALIZER;

static int kq;

/* Notifications to our reader thread */
#define ASL_REDIRECT_TERMINATE ((void *)(uintptr_t)1)
#endif

typedef struct {
    int level;
    aslclient asl;
    aslmsg msg;

    /* Buffered reading */
    char *buf;
    char *w;

#ifdef HAVE_LIBDISPATCH
    dispatch_source_t read_source;
#endif
} asl_redirect;

static asl_redirect *redirect_fds = NULL;
static int n_redirect_fds = 0;

/* Read from the FD until there is no more to read and redirect to ASL.
 * Preconditions:
 *      1: pthread_mutex_lock lock is held (pthreads) or called
 *         from the appropriate serial queue for operating on
 *         redirect_fds
 *      2: fd corresponds to a valid entry in redirect_fds
 *
 * Return values:
 *      If the pipe is closed, EOF is returned regardless of how many bytes
 *      were processed.  If the pipe is still open, the number of read bytes
 *      is returned.
 */
static inline int
_read_redirect(int fd, int flush)
{
    int total_read = 0;
    int nbytes;
    asl_redirect *aslr = &redirect_fds[fd];

    while ((nbytes =
                read(fd, aslr->w,
                     BUF_SIZE - (aslr->w - aslr->buf) - 1)) > 0) {
        char *s, *p;

        /* Increment our returned number read */
        total_read += nbytes;

        /* Increment our write location */
        aslr->w += nbytes;
        aslr->w[0] = '\0';

        /* One line at a time */
        for (p = aslr->buf; p < aslr->w; p = s + 1) {
            // Find null or \n
            for (s = p; *s && *s != '\n'; s++) ;
            if (*s == '\n') {
                *s = '\0';
            }

            if (s < aslr->w || aslr->buf == p) {
                /* Either the first of multiple messages or one message which is larger than our buffer */
                asl_log(aslr->asl, aslr->msg, aslr->level, "%s", p);
            }
            else {
                /* We reached the end of the buffer, move this chunk to the start. */
                memmove(aslr->buf, p, BUF_SIZE - (p - aslr->buf));
                aslr->w = aslr->buf + (s - p);
                break;
            }
        }

        if (p == aslr->w) {
            /* Start writing at the beginning in the case where we flushed */
            aslr->w = aslr->buf;
        }
    }

    /* Flush if requested or we're at EOF */
    if (flush || nbytes == 0) {
        if (aslr->w > aslr->buf) {
            *aslr->w = '\0';
            asl_log(aslr->asl, aslr->msg, aslr->level, "%s", aslr->buf);
        }
    }

    if (nbytes == 0)
        return EOF;
    return total_read;
}

#ifdef HAVE_LIBDISPATCH
static void
read_from_source(void *_source)
{
    dispatch_source_t source = (dispatch_source_t)_source;
    int fd = dispatch_source_get_handle(source);
    if (_read_redirect(fd, 0) == EOF) {
        dispatch_source_cancel(source);
    }
}

static void
cancel_source(void *_source)
{
    dispatch_source_t source = (dispatch_source_t)_source;
    int fd = dispatch_source_get_handle(source);
    asl_redirect *aslr = &redirect_fds[fd];

    /* Flush the buffer */
    _read_redirect(fd, 1);

    close(fd);
    free(aslr->buf);
    memset(aslr, 0, sizeof(*aslr));
    dispatch_release(source);
    dispatch_group_leave(read_source_group);
}

#else /* !HAVE_LIBDISPATCH */
static void *
redirect_thread(void *ctx __unused)
{
    struct kevent ev;
    int n;

    while (1) {
        n = kevent(kq, NULL, 0, &ev, 1, NULL);

        /* Bail on errors */
        if (n < 0) {
            asl_log(NULL, NULL, ASL_LEVEL_ERR, "kevent failure: %s",
                    strerror(errno));
            break;
        }

        /* This should not happen */
        if (n == 0)
            continue;

        switch (ev.filter) {
        case EVFILT_READ:
            pthread_mutex_lock(&redirect_fds_lock);
            {
                int fd = ev.ident;
                int close_fd = 0;
                asl_redirect *aslr = &redirect_fds[fd];

                if (fd < 0 || fd >= n_redirect_fds || aslr->buf == NULL) {
                    asl_log(NULL, NULL, ASL_LEVEL_ERR,
                            "Unexpected file descriptor: %d",
                            fd);
                    goto next;
                }

                if (ev.flags & EV_EOF) {
                    close_fd = 1;
                    if (EOF != _read_redirect(fd, 1)) {
                        asl_log(
                            NULL, NULL, ASL_LEVEL_ERR,
                            "kevent reported EOF on %d, but read doesn't concur.",
                            fd);
                    }
                }
                else {
                    close_fd = (EOF == _read_redirect(fd, 0));
                }

                if (close_fd) {
                    EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
                    kevent(kq, &ev, 1, NULL, 0, NULL);
                    close(fd);
                    free(aslr->buf);
                    memset(aslr, 0, sizeof(*aslr));
                }
            }
next:
            pthread_mutex_unlock(&redirect_fds_lock);

        case EVFILT_TIMER:
            if (ev.udata == ASL_REDIRECT_TERMINATE)
                return NULL;

        default:
            ;
            ;
        }
    }

    return NULL;
}
#endif

static void
redirect_atexit(void)
{
    /* stdout is linebuffered, so flush the buffer */
    if (redirect_fds[STDOUT_FILENO].buf)
        fflush(stdout);

#ifdef HAVE_LIBDISPATCH
    {
        int i;

        /* Cancel all of our dispatch sources, so they flush to ASL */
        for (i = 0; i < n_redirect_fds; i++)
            if (redirect_fds[i].read_source)
                dispatch_source_cancel(redirect_fds[i].read_source);

        /* Wait at least three seconds for our sources to flush to ASL */
        dispatch_group_wait(read_source_group,
                            dispatch_time(DISPATCH_TIME_NOW, 3LL *
                                          NSEC_PER_SEC));
    }
#else
    {
        struct kevent ev;

        /* Tell our reader thread it is time to pack up and go home */
        EV_SET(&ev, 0, EVFILT_TIMER, EV_ADD | EV_ONESHOT, 0, 0,
               ASL_REDIRECT_TERMINATE);
        kevent(kq, &ev, 1, NULL, 0, NULL);

        pthread_join(redirect_pthread, NULL);
    }
#endif
}

#ifdef HAVE_LIBDISPATCH
static void
xq_asl_init(void *ctx __unused)
#else
static void
xq_asl_init(void)
#endif
{
    assert((redirect_fds = calloc(16, sizeof(*redirect_fds))) != NULL);
    n_redirect_fds = 16;

#ifdef HAVE_LIBDISPATCH
    redirect_serial_q = dispatch_queue_create("com.apple.asl-redirect", NULL);
    assert(redirect_serial_q != NULL);

    read_source_group = dispatch_group_create();
    assert(read_source_group != NULL);
#else
    assert((kq = kqueue()) != -1);
    assert(pthread_create(&redirect_pthread, NULL, redirect_thread,
                          NULL) == 0);
#endif

    atexit(redirect_atexit);
}

#if MAC_OS_X_VERSION_MIN_REQUIRED < 1050
#define fls(v) xq_fls(v)

static inline int fls(int value) {
    unsigned int b, v;

    v = *((unsigned int *)&value);

    for(b=0 ; v ; v >>= 1 , b++);

    return b;
}
#endif

int
xq_asl_log_fd(aslclient asl, aslmsg msg, int level, int fd)
{
#ifdef HAVE_LIBDISPATCH
    int err __block = 0;
    static dispatch_once_t once_control;
    dispatch_once_f(&once_control, NULL, xq_asl_init);
#else
    int err = 0;
    static pthread_once_t once_control = PTHREAD_ONCE_INIT;
    assert(pthread_once(&once_control, xq_asl_init) == 0);
#endif

    if (fd < 0)
        return EBADF;

#ifdef HAVE_LIBDISPATCH
#define BLOCK_DONE return
    dispatch_sync(redirect_serial_q, ^
#else
#define BLOCK_DONE goto done
    assert(pthread_mutex_lock(&redirect_fds_lock) == 0);
#endif
                  {
                      /* Reallocate if we need more space */
                      if (fd >= n_redirect_fds) {
                          size_t new_n = 1 << (fls(fd) + 1);
                          asl_redirect *new_array =
                              realloc(redirect_fds, new_n *
                                      sizeof(*redirect_fds));
                          if (!new_array) {
                              err = errno;
                              BLOCK_DONE;
                          }
                          redirect_fds = new_array;
                          memset(redirect_fds + n_redirect_fds, 0, (new_n -
                                 n_redirect_fds) * sizeof(*redirect_fds));
                          n_redirect_fds = new_n;
                      }

                      /* If we're already listening on it, return error. */
                      if (redirect_fds[fd].buf != NULL) {
                          err = EBADF;
                          BLOCK_DONE;
                      }

                      /* Initialize our buffer */
                      redirect_fds[fd].buf = (char *)malloc(BUF_SIZE);
                      if (redirect_fds[fd].buf == NULL) {
                          err = errno;
                          BLOCK_DONE;
                      }
                      redirect_fds[fd].w = redirect_fds[fd].buf;

                      /* Store our ASL settings */
                      redirect_fds[fd].level = level;
                      redirect_fds[fd].asl = asl;
                      redirect_fds[fd].msg = msg;

                      /* Don't block on reads from this fd */
                      fcntl(fd, F_SETFL,
                            O_NONBLOCK);

                      /* Start listening */
#ifdef HAVE_LIBDISPATCH
                      {
                          dispatch_source_t read_source =
                              dispatch_source_create(
                                  DISPATCH_SOURCE_TYPE_READ, fd, 0,
                                  redirect_serial_q);
                          redirect_fds[fd].read_source = read_source;
                          dispatch_set_context(read_source, read_source);
                          dispatch_source_set_event_handler_f(read_source,
                                                              read_from_source);
                          dispatch_source_set_cancel_handler_f(read_source,
                                                               cancel_source);
                          dispatch_group_enter(read_source_group);
                          dispatch_resume(read_source);
                      }
#else
                      {
                          struct kevent ev;
                          EV_SET(&ev, fd, EVFILT_READ, EV_ADD, 0, 0, 0);
                          kevent(kq, &ev, 1, NULL, 0, NULL);
                      }
#endif
                  }
#ifdef HAVE_LIBDISPATCH
                  );
#else
done:
                  assert(pthread_mutex_unlock(&redirect_fds_lock) == 0);
#endif
#undef BLOCK_DONE

    return err;
}

int
xq_asl_capture_fd(aslclient asl, aslmsg msg, int level, int fd)
{
    int pipepair[2];

    /* Create pipe */
    if (pipe(pipepair) == -1)
        return errno;

    /* Close the read fd but not the write fd on exec */
    if (fcntl(pipepair[0], F_SETFD, FD_CLOEXEC) == -1)
        return errno;

    /* Replace the existing fd */
    if (dup2(pipepair[1], fd) == -1) {
        close(pipepair[0]);
        close(pipepair[1]);
        return errno;
    }

    /* If we capture STDOUT_FILENO, make sure we linebuffer stdout */
    if (fd == STDOUT_FILENO)
        setlinebuf(stdout);

    /* Close the duplicate fds since they've been reassigned */
    close(pipepair[1]);

    /* Hand off the read end of our pipe to xq_asl_log_fd */
    return xq_asl_log_fd(asl, msg, level, pipepair[0]);
}

#ifdef DEBUG_CONSOLE_REDIRECT
int
main(int argc __unused, char * *argv __unused)
{
    xq_asl_capture_fd(NULL, NULL, ASL_LEVEL_NOTICE, STDOUT_FILENO);
    xq_asl_capture_fd(NULL, NULL, ASL_LEVEL_ERR, STDERR_FILENO);

    fprintf(stderr, "TEST ERR1\n");
    fprintf(stdout, "TEST OUT1\n");
    fprintf(stderr, "TEST ERR2\n");
    fprintf(stdout, "TEST OUT2\n");
    system("/bin/echo SYST OUT");
    system("/bin/echo SYST ERR >&2");
    fprintf(stdout, "TEST OUT3\n");
    fprintf(stdout, "TEST OUT4\n");
    fprintf(stderr, "TEST ERR3\n");
    fprintf(stderr, "TEST ERR4\n");

    exit(0);
}
#endif
