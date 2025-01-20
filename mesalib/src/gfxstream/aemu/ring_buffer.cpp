/*
 * Copyright 2018 Google
 * SPDX-License-Identifier: MIT
 */
#include "ring_buffer.h"

#include <errno.h>
#include <string.h>
#ifdef _MSC_VER
#include "aemu/base/msvc.h"
#else
#include <sys/time.h>
#endif

#ifdef __x86_64__
#include <emmintrin.h>
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <sched.h>
#include <unistd.h>
#endif

#define RING_BUFFER_MASK (RING_BUFFER_SIZE - 1)

#define RING_BUFFER_VERSION 1

void ring_buffer_init(struct ring_buffer* r) {
    r->host_version = 1;
    r->write_pos = 0;
    r->read_pos = 0;

    r->read_live_count = 0;
    r->read_yield_count = 0;
    r->read_sleep_us_count = 0;

    r->state = 0;
}

static uint32_t get_ring_pos(uint32_t index) { return index & RING_BUFFER_MASK; }

bool ring_buffer_can_write(const struct ring_buffer* r, uint32_t bytes) {
    uint32_t read_view;
    __atomic_load(&r->read_pos, &read_view, __ATOMIC_SEQ_CST);
    return get_ring_pos(read_view - r->write_pos - 1) >= bytes;
}

bool ring_buffer_can_read(const struct ring_buffer* r, uint32_t bytes) {
    uint32_t write_view;
    __atomic_load(&r->write_pos, &write_view, __ATOMIC_SEQ_CST);
    return get_ring_pos(write_view - r->read_pos) >= bytes;
}

long ring_buffer_write(struct ring_buffer* r, const void* data, uint32_t step_size,
                       uint32_t steps) {
    const uint8_t* data_bytes = (const uint8_t*)data;
    uint32_t i;

    for (i = 0; i < steps; ++i) {
        if (!ring_buffer_can_write(r, step_size)) {
            errno = -EAGAIN;
            return (long)i;
        }

        // Needs to be split up into 2 writes for the edge case.
        uint32_t available_at_end = RING_BUFFER_SIZE - get_ring_pos(r->write_pos);

        if (step_size > available_at_end) {
            uint32_t remaining = step_size - available_at_end;
            memcpy(&r->buf[get_ring_pos(r->write_pos)], data_bytes + i * step_size,
                   available_at_end);
            memcpy(&r->buf[get_ring_pos(r->write_pos + available_at_end)],
                   data_bytes + i * step_size + available_at_end, remaining);
        } else {
            memcpy(&r->buf[get_ring_pos(r->write_pos)], data_bytes + i * step_size, step_size);
        }

        __atomic_add_fetch(&r->write_pos, step_size, __ATOMIC_SEQ_CST);
    }

    errno = 0;
    return (long)steps;
}

long ring_buffer_read(struct ring_buffer* r, void* data, uint32_t step_size, uint32_t steps) {
    uint8_t* data_bytes = (uint8_t*)data;
    uint32_t i;

    for (i = 0; i < steps; ++i) {
        if (!ring_buffer_can_read(r, step_size)) {
            errno = -EAGAIN;
            return (long)i;
        }

        // Needs to be split up into 2 reads for the edge case.
        uint32_t available_at_end = RING_BUFFER_SIZE - get_ring_pos(r->read_pos);

        if (step_size > available_at_end) {
            uint32_t remaining = step_size - available_at_end;
            memcpy(data_bytes + i * step_size, &r->buf[get_ring_pos(r->read_pos)],
                   available_at_end);
            memcpy(data_bytes + i * step_size + available_at_end,
                   &r->buf[get_ring_pos(r->read_pos + available_at_end)], remaining);
        } else {
            memcpy(data_bytes + i * step_size, &r->buf[get_ring_pos(r->read_pos)], step_size);
        }

        __atomic_add_fetch(&r->read_pos, step_size, __ATOMIC_SEQ_CST);
    }

    errno = 0;
    return (long)steps;
}

long ring_buffer_advance_write(struct ring_buffer* r, uint32_t step_size, uint32_t steps) {
    uint32_t i;

    for (i = 0; i < steps; ++i) {
        if (!ring_buffer_can_write(r, step_size)) {
            errno = -EAGAIN;
            return (long)i;
        }

        __atomic_add_fetch(&r->write_pos, step_size, __ATOMIC_SEQ_CST);
    }

    errno = 0;
    return (long)steps;
}

long ring_buffer_advance_read(struct ring_buffer* r, uint32_t step_size, uint32_t steps) {
    uint32_t i;

    for (i = 0; i < steps; ++i) {
        if (!ring_buffer_can_read(r, step_size)) {
            errno = -EAGAIN;
            return (long)i;
        }

        __atomic_add_fetch(&r->read_pos, step_size, __ATOMIC_SEQ_CST);
    }

    errno = 0;
    return (long)steps;
}

uint32_t ring_buffer_calc_shift(uint32_t size) {
    uint32_t shift = 0;
    while ((1 << shift) < size) {
        ++shift;
    }

    // if size is not a power of 2,
    if ((1 << shift) > size) {
        --shift;
    }
    return shift;
}

void ring_buffer_view_init(struct ring_buffer* r, struct ring_buffer_view* v, uint8_t* buf,
                           uint32_t size) {
    uint32_t shift = ring_buffer_calc_shift(size);

    ring_buffer_init(r);

    v->buf = buf;
    v->size = (1 << shift);
    v->mask = (1 << shift) - 1;
}

void ring_buffer_init_view_only(struct ring_buffer_view* v, uint8_t* buf, uint32_t size) {
    uint32_t shift = ring_buffer_calc_shift(size);

    v->buf = buf;
    v->size = (1 << shift);
    v->mask = (1 << shift) - 1;
}

uint32_t ring_buffer_view_get_ring_pos(const struct ring_buffer_view* v, uint32_t index) {
    return index & v->mask;
}

bool ring_buffer_view_can_write(const struct ring_buffer* r, const struct ring_buffer_view* v,
                                uint32_t bytes) {
    uint32_t read_view;
    __atomic_load(&r->read_pos, &read_view, __ATOMIC_SEQ_CST);
    return ring_buffer_view_get_ring_pos(v, read_view - r->write_pos - 1) >= bytes;
}

bool ring_buffer_view_can_read(const struct ring_buffer* r, const struct ring_buffer_view* v,
                               uint32_t bytes) {
    uint32_t write_view;
    __atomic_load(&r->write_pos, &write_view, __ATOMIC_SEQ_CST);
    return ring_buffer_view_get_ring_pos(v, write_view - r->read_pos) >= bytes;
}

uint32_t ring_buffer_available_read(const struct ring_buffer* r, const struct ring_buffer_view* v) {
    uint32_t write_view;
    __atomic_load(&r->write_pos, &write_view, __ATOMIC_SEQ_CST);
    if (v) {
        return ring_buffer_view_get_ring_pos(v, write_view - r->read_pos);
    } else {
        return get_ring_pos(write_view - r->read_pos);
    }
}

uint32_t ring_buffer_available_write(const struct ring_buffer* r,
                                     const struct ring_buffer_view* v) {
    uint32_t read_view;
    __atomic_load(&r->read_pos, &read_view, __ATOMIC_SEQ_CST);
    if (v) {
        return ring_buffer_view_get_ring_pos(v, read_view - r->write_pos - 1);
    } else {
        return get_ring_pos(read_view - r->write_pos - 1);
    }
}

int ring_buffer_copy_contents(const struct ring_buffer* r, const struct ring_buffer_view* v,
                              uint32_t wanted_bytes, uint8_t* res) {
    uint32_t total_available = ring_buffer_available_read(r, v);
    uint32_t available_at_end = 0;

    if (v) {
        available_at_end = v->size - ring_buffer_view_get_ring_pos(v, r->read_pos);
    } else {
        available_at_end = RING_BUFFER_SIZE - get_ring_pos(r->write_pos);
    }

    if (total_available < wanted_bytes) {
        return -1;
    }

    if (v) {
        if (wanted_bytes > available_at_end) {
            uint32_t remaining = wanted_bytes - available_at_end;
            memcpy(res, &v->buf[ring_buffer_view_get_ring_pos(v, r->read_pos)], available_at_end);
            memcpy(res + available_at_end,
                   &v->buf[ring_buffer_view_get_ring_pos(v, r->read_pos + available_at_end)],
                   remaining);
        } else {
            memcpy(res, &v->buf[ring_buffer_view_get_ring_pos(v, r->read_pos)], wanted_bytes);
        }
    } else {
        if (wanted_bytes > available_at_end) {
            uint32_t remaining = wanted_bytes - available_at_end;
            memcpy(res, &r->buf[get_ring_pos(r->read_pos)], available_at_end);
            memcpy(res + available_at_end, &r->buf[get_ring_pos(r->read_pos + available_at_end)],
                   remaining);
        } else {
            memcpy(res, &r->buf[get_ring_pos(r->read_pos)], wanted_bytes);
        }
    }
    return 0;
}

long ring_buffer_view_write(struct ring_buffer* r, struct ring_buffer_view* v, const void* data,
                            uint32_t step_size, uint32_t steps) {
    uint8_t* data_bytes = (uint8_t*)data;
    uint32_t i;

    for (i = 0; i < steps; ++i) {
        if (!ring_buffer_view_can_write(r, v, step_size)) {
            errno = -EAGAIN;
            return (long)i;
        }

        // Needs to be split up into 2 writes for the edge case.
        uint32_t available_at_end = v->size - ring_buffer_view_get_ring_pos(v, r->write_pos);

        if (step_size > available_at_end) {
            uint32_t remaining = step_size - available_at_end;
            memcpy(&v->buf[ring_buffer_view_get_ring_pos(v, r->write_pos)],
                   data_bytes + i * step_size, available_at_end);
            memcpy(&v->buf[ring_buffer_view_get_ring_pos(v, r->write_pos + available_at_end)],
                   data_bytes + i * step_size + available_at_end, remaining);
        } else {
            memcpy(&v->buf[ring_buffer_view_get_ring_pos(v, r->write_pos)],
                   data_bytes + i * step_size, step_size);
        }

        __atomic_add_fetch(&r->write_pos, step_size, __ATOMIC_SEQ_CST);
    }

    errno = 0;
    return (long)steps;
}

long ring_buffer_view_read(struct ring_buffer* r, struct ring_buffer_view* v, void* data,
                           uint32_t step_size, uint32_t steps) {
    uint8_t* data_bytes = (uint8_t*)data;
    uint32_t i;

    for (i = 0; i < steps; ++i) {
        if (!ring_buffer_view_can_read(r, v, step_size)) {
            errno = -EAGAIN;
            return (long)i;
        }

        // Needs to be split up into 2 reads for the edge case.
        uint32_t available_at_end = v->size - ring_buffer_view_get_ring_pos(v, r->read_pos);

        if (step_size > available_at_end) {
            uint32_t remaining = step_size - available_at_end;
            memcpy(data_bytes + i * step_size,
                   &v->buf[ring_buffer_view_get_ring_pos(v, r->read_pos)], available_at_end);
            memcpy(data_bytes + i * step_size + available_at_end,
                   &v->buf[ring_buffer_view_get_ring_pos(v, r->read_pos + available_at_end)],
                   remaining);
        } else {
            memcpy(data_bytes + i * step_size,
                   &v->buf[ring_buffer_view_get_ring_pos(v, r->read_pos)], step_size);
        }
        __atomic_add_fetch(&r->read_pos, step_size, __ATOMIC_SEQ_CST);
    }

    errno = 0;
    return (long)steps;
}

void ring_buffer_yield() {
#ifdef _WIN32
    _mm_pause();
#else
    sched_yield();
#endif
}

static void ring_buffer_sleep() {
#ifdef _WIN32
    Sleep(2);
#else
    usleep(2000);
#endif
}

static uint64_t ring_buffer_curr_us() {
    uint64_t res;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    res = tv.tv_sec * 1000000ULL + tv.tv_usec;
    return res;
}

static const uint32_t yield_backoff_us = 1000;
static const uint32_t sleep_backoff_us = 2000;

bool ring_buffer_wait_write(const struct ring_buffer* r, const struct ring_buffer_view* v,
                            uint32_t bytes, uint64_t timeout_us) {
    uint64_t start_us = ring_buffer_curr_us();
    uint64_t curr_wait_us;

    bool can_write = v ? ring_buffer_view_can_write(r, v, bytes) : ring_buffer_can_write(r, bytes);

    while (!can_write) {
#ifdef __x86_64
        _mm_pause();
#endif
        curr_wait_us = ring_buffer_curr_us() - start_us;

        if (curr_wait_us > yield_backoff_us) {
            ring_buffer_yield();
        }

        if (curr_wait_us > sleep_backoff_us) {
            ring_buffer_sleep();
        }

        if (curr_wait_us > timeout_us) {
            return false;
        }

        can_write = v ? ring_buffer_view_can_write(r, v, bytes) : ring_buffer_can_write(r, bytes);
    }

    return true;
}

bool ring_buffer_wait_read(const struct ring_buffer* r, const struct ring_buffer_view* v,
                           uint32_t bytes, uint64_t timeout_us) {
    uint64_t start_us = ring_buffer_curr_us();
    uint64_t curr_wait_us;

    bool can_read = v ? ring_buffer_view_can_read(r, v, bytes) : ring_buffer_can_read(r, bytes);

    while (!can_read) {
        // TODO(bohu): find aarch64 equivalent
#ifdef __x86_64
        _mm_pause();
#endif
        curr_wait_us = ring_buffer_curr_us() - start_us;

        if (curr_wait_us > yield_backoff_us) {
            ring_buffer_yield();
            ((struct ring_buffer*)r)->read_yield_count++;
        }

        if (curr_wait_us > sleep_backoff_us) {
            ring_buffer_sleep();
            ((struct ring_buffer*)r)->read_sleep_us_count += 2000;
        }

        if (curr_wait_us > timeout_us) {
            return false;
        }

        can_read = v ? ring_buffer_view_can_read(r, v, bytes) : ring_buffer_can_read(r, bytes);
    }

    ((struct ring_buffer*)r)->read_live_count++;
    return true;
}

static uint32_t get_step_size(struct ring_buffer* r, struct ring_buffer_view* v, uint32_t bytes) {
    uint32_t available = v ? (v->size >> 1) : (RING_BUFFER_SIZE >> 1);
    uint32_t res = available < bytes ? available : bytes;

    return res;
}

void ring_buffer_write_fully(struct ring_buffer* r, struct ring_buffer_view* v, const void* data,
                             uint32_t bytes) {
    ring_buffer_write_fully_with_abort(r, v, data, bytes, 0, 0);
}

void ring_buffer_read_fully(struct ring_buffer* r, struct ring_buffer_view* v, void* data,
                            uint32_t bytes) {
    ring_buffer_read_fully_with_abort(r, v, data, bytes, 0, 0);
}

uint32_t ring_buffer_write_fully_with_abort(struct ring_buffer* r, struct ring_buffer_view* v,
                                            const void* data, uint32_t bytes, uint32_t abort_value,
                                            const volatile uint32_t* abort_ptr) {
    uint32_t candidate_step = get_step_size(r, v, bytes);
    uint32_t processed = 0;

    uint8_t* dst = (uint8_t*)data;

    while (processed < bytes) {
        if (bytes - processed < candidate_step) {
            candidate_step = bytes - processed;
        }

        long processed_here = 0;
        ring_buffer_wait_write(r, v, candidate_step, (uint64_t)(-1));

        if (v) {
            processed_here = ring_buffer_view_write(r, v, dst + processed, candidate_step, 1);
        } else {
            processed_here = ring_buffer_write(r, dst + processed, candidate_step, 1);
        }

        processed += processed_here ? candidate_step : 0;

        if (abort_ptr && (abort_value == *abort_ptr)) {
            return processed;
        }
    }

    return processed;
}

uint32_t ring_buffer_read_fully_with_abort(struct ring_buffer* r, struct ring_buffer_view* v,
                                           void* data, uint32_t bytes, uint32_t abort_value,
                                           const volatile uint32_t* abort_ptr) {
    uint32_t candidate_step = get_step_size(r, v, bytes);
    uint32_t processed = 0;

    uint8_t* dst = (uint8_t*)data;

    while (processed < bytes) {
#ifdef __x86_64
        _mm_pause();
#endif
        if (bytes - processed < candidate_step) {
            candidate_step = bytes - processed;
        }

        long processed_here = 0;
        ring_buffer_wait_read(r, v, candidate_step, (uint64_t)(-1));

        if (v) {
            processed_here = ring_buffer_view_read(r, v, dst + processed, candidate_step, 1);
        } else {
            processed_here = ring_buffer_read(r, dst + processed, candidate_step, 1);
        }

        processed += processed_here ? candidate_step : 0;

        if (abort_ptr && (abort_value == *abort_ptr)) {
            return processed;
        }
    }

    return processed;
}

void ring_buffer_sync_init(struct ring_buffer* r) {
    __atomic_store_n(&r->state, RING_BUFFER_SYNC_PRODUCER_IDLE, __ATOMIC_SEQ_CST);
}

bool ring_buffer_producer_acquire(struct ring_buffer* r) {
    uint32_t expected_idle = RING_BUFFER_SYNC_PRODUCER_IDLE;
    bool success =
        __atomic_compare_exchange_n(&r->state, &expected_idle, RING_BUFFER_SYNC_PRODUCER_ACTIVE,
                                    false /* strong */, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return success;
}

bool ring_buffer_producer_acquire_from_hangup(struct ring_buffer* r) {
    uint32_t expected_hangup = RING_BUFFER_SYNC_CONSUMER_HUNG_UP;
    bool success =
        __atomic_compare_exchange_n(&r->state, &expected_hangup, RING_BUFFER_SYNC_PRODUCER_ACTIVE,
                                    false /* strong */, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return success;
}

void ring_buffer_producer_wait_hangup(struct ring_buffer* r) {
    while (__atomic_load_n(&r->state, __ATOMIC_SEQ_CST) != RING_BUFFER_SYNC_CONSUMER_HUNG_UP) {
        ring_buffer_yield();
    }
}

void ring_buffer_producer_idle(struct ring_buffer* r) {
    __atomic_store_n(&r->state, RING_BUFFER_SYNC_PRODUCER_IDLE, __ATOMIC_SEQ_CST);
}

bool ring_buffer_consumer_hangup(struct ring_buffer* r) {
    uint32_t expected_idle = RING_BUFFER_SYNC_PRODUCER_IDLE;
    bool success =
        __atomic_compare_exchange_n(&r->state, &expected_idle, RING_BUFFER_SYNC_CONSUMER_HANGING_UP,
                                    false /* strong */, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return success;
}

void ring_buffer_consumer_wait_producer_idle(struct ring_buffer* r) {
    while (__atomic_load_n(&r->state, __ATOMIC_SEQ_CST) != RING_BUFFER_SYNC_PRODUCER_IDLE) {
        ring_buffer_yield();
    }
}

void ring_buffer_consumer_hung_up(struct ring_buffer* r) {
    __atomic_store_n(&r->state, RING_BUFFER_SYNC_CONSUMER_HUNG_UP, __ATOMIC_SEQ_CST);
}
