/*
 * Copyright 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <nativebase/nativebase.h>
#include <stdarg.h>

// apex is a superset of the NDK
#include <android/native_window.h>

__BEGIN_DECLS

/*
 * perform bits that can be used with ANativeWindow_perform()
 *
 * This is only to support the intercepting methods below - these should notbe
 * used directly otherwise.
 */
enum ANativeWindowPerform {
    // clang-format off
    ANATIVEWINDOW_PERFORM_SET_USAGE            = 0,
    ANATIVEWINDOW_PERFORM_SET_BUFFERS_GEOMETRY = 5,
    ANATIVEWINDOW_PERFORM_SET_BUFFERS_FORMAT   = 9,
    ANATIVEWINDOW_PERFORM_SET_USAGE64          = 30,
    // clang-format on
};

/**
 * Prototype of the function that an ANativeWindow implementation would call
 * when ANativeWindow_cancelBuffer is called.
 */
typedef int (*ANativeWindow_cancelBufferFn)(ANativeWindow* window, ANativeWindowBuffer* buffer,
                                            int fenceFd);

/**
 * Prototype of the function that intercepts an invocation of
 * ANativeWindow_cancelBufferFn, along with a data pointer that's passed by the
 * caller who set the interceptor, as well as arguments that would be
 * passed to ANativeWindow_cancelBufferFn if it were to be called.
 */
typedef int (*ANativeWindow_cancelBufferInterceptor)(ANativeWindow* window,
                                                     ANativeWindow_cancelBufferFn cancelBuffer,
                                                     void* data, ANativeWindowBuffer* buffer,
                                                     int fenceFd);

/**
 * Prototype of the function that an ANativeWindow implementation would call
 * when ANativeWindow_dequeueBuffer is called.
 */
typedef int (*ANativeWindow_dequeueBufferFn)(ANativeWindow* window, ANativeWindowBuffer** buffer,
                                             int* fenceFd);

/**
 * Prototype of the function that intercepts an invocation of
 * ANativeWindow_dequeueBufferFn, along with a data pointer that's passed by the
 * caller who set the interceptor, as well as arguments that would be
 * passed to ANativeWindow_dequeueBufferFn if it were to be called.
 */
typedef int (*ANativeWindow_dequeueBufferInterceptor)(ANativeWindow* window,
                                                      ANativeWindow_dequeueBufferFn dequeueBuffer,
                                                      void* data, ANativeWindowBuffer** buffer,
                                                      int* fenceFd);

/**
 * Prototype of the function that an ANativeWindow implementation would call
 * when ANativeWindow_perform is called.
 */
typedef int (*ANativeWindow_performFn)(ANativeWindow* window, int operation, va_list args);

/**
 * Prototype of the function that intercepts an invocation of
 * ANativeWindow_performFn, along with a data pointer that's passed by the
 * caller who set the interceptor, as well as arguments that would be
 * passed to ANativeWindow_performFn if it were to be called.
 */
typedef int (*ANativeWindow_performInterceptor)(ANativeWindow* window,
                                                ANativeWindow_performFn perform, void* data,
                                                int operation, va_list args);

/**
 * Prototype of the function that an ANativeWindow implementation would call
 * when ANativeWindow_queueBuffer is called.
 */
typedef int (*ANativeWindow_queueBufferFn)(ANativeWindow* window, ANativeWindowBuffer* buffer,
                                           int fenceFd);

/**
 * Prototype of the function that intercepts an invocation of
 * ANativeWindow_queueBufferFn, along with a data pointer that's passed by the
 * caller who set the interceptor, as well as arguments that would be
 * passed to ANativeWindow_queueBufferFn if it were to be called.
 */
typedef int (*ANativeWindow_queueBufferInterceptor)(ANativeWindow* window,
                                                    ANativeWindow_queueBufferFn queueBuffer,
                                                    void* data, ANativeWindowBuffer* buffer,
                                                    int fenceFd);

/**
 * Registers an interceptor for ANativeWindow_cancelBuffer. Instead of calling
 * the underlying cancelBuffer function, instead the provided interceptor is
 * called, which may optionally call the underlying cancelBuffer function. An
 * optional data pointer is also provided to side-channel additional arguments.
 *
 * Note that usage of this should only be used for specialized use-cases by
 * either the system partition or to Mainline modules. This should never be
 * exposed to NDK or LL-NDK.
 *
 * Returns NO_ERROR on success, -errno if registration failed.
 */
int ANativeWindow_setCancelBufferInterceptor(ANativeWindow* window,
                                             ANativeWindow_cancelBufferInterceptor interceptor,
                                             void* data);

/**
 * Registers an interceptor for ANativeWindow_dequeueBuffer. Instead of calling
 * the underlying dequeueBuffer function, instead the provided interceptor is
 * called, which may optionally call the underlying dequeueBuffer function. An
 * optional data pointer is also provided to side-channel additional arguments.
 *
 * Note that usage of this should only be used for specialized use-cases by
 * either the system partition or to Mainline modules. This should never be
 * exposed to NDK or LL-NDK.
 *
 * Returns NO_ERROR on success, -errno if registration failed.
 */
int ANativeWindow_setDequeueBufferInterceptor(ANativeWindow* window,
                                              ANativeWindow_dequeueBufferInterceptor interceptor,
                                              void* data);
/**
 * Registers an interceptor for ANativeWindow_perform. Instead of calling
 * the underlying perform function, instead the provided interceptor is
 * called, which may optionally call the underlying perform function. An
 * optional data pointer is also provided to side-channel additional arguments.
 *
 * Note that usage of this should only be used for specialized use-cases by
 * either the system partition or to Mainline modules. This should never be
 * exposed to NDK or LL-NDK.
 *
 * Returns NO_ERROR on success, -errno if registration failed.
 */
int ANativeWindow_setPerformInterceptor(ANativeWindow* window,
                                        ANativeWindow_performInterceptor interceptor, void* data);
/**
 * Registers an interceptor for ANativeWindow_queueBuffer. Instead of calling
 * the underlying queueBuffer function, instead the provided interceptor is
 * called, which may optionally call the underlying queueBuffer function. An
 * optional data pointer is also provided to side-channel additional arguments.
 *
 * Note that usage of this should only be used for specialized use-cases by
 * either the system partition or to Mainline modules. This should never be
 * exposed to NDK or LL-NDK.
 *
 * Returns NO_ERROR on success, -errno if registration failed.
 */
int ANativeWindow_setQueueBufferInterceptor(ANativeWindow* window,
                                            ANativeWindow_queueBufferInterceptor interceptor,
                                            void* data);

/**
 * Retrieves how long it took for the last time a buffer was dequeued.
 *
 * \return the dequeue duration in nanoseconds
 */
int64_t ANativeWindow_getLastDequeueDuration(ANativeWindow* window);

/**
 * Retrieves how long it took for the last time a buffer was queued.
 *
 * \return the queue duration in nanoseconds
 */
int64_t ANativeWindow_getLastQueueDuration(ANativeWindow* window);

/**
 * Retrieves the system time in nanoseconds when the last time a buffer
 * started to be dequeued.
 *
 * \return the start time in nanoseconds
 */
int64_t ANativeWindow_getLastDequeueStartTime(ANativeWindow* window);

/**
 * Sets a timeout in nanoseconds for dequeue calls. All subsequent dequeue calls
 * made by the window will return -ETIMEDOUT after the timeout if the dequeue
 * takes too long.
 *
 * If the provided timeout is negative, hen this removes the previously configured
 * timeout. The window then behaves as if ANativeWindow_setDequeueTimeout was
 * never called.
 *
 * \return NO_ERROR on success
 * \return BAD_VALUE if the dequeue timeout was unabled to be updated, as
 * updating the dequeue timeout may change internals of the underlying window.
 */
int ANativeWindow_setDequeueTimeout(ANativeWindow* window, int64_t timeout);

__END_DECLS
