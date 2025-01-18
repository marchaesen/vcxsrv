/*
 * Copyright 2018 Google
 * SPDX-License-Identifier: MIT
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define RING_BUFFER_SHIFT 11
#define RING_BUFFER_SIZE (1 << RING_BUFFER_SHIFT)
#define NUM_CONFIG_FIELDS 32

// Single producer/consumer ring buffer struct that can be shared
// between host and guest as-is.
struct ring_buffer {
    uint32_t host_version;
    uint32_t guest_version;
    uint32_t write_pos;    // Atomically updated for the consumer
    uint32_t unused0[13];  // Separate cache line
    uint32_t read_pos;     // Atomically updated for the producer
    uint32_t read_live_count;
    uint32_t read_yield_count;
    uint32_t read_sleep_us_count;
    uint32_t unused1[12];  // Separate cache line
    uint8_t buf[RING_BUFFER_SIZE];
    uint32_t state;  // An atomically updated variable from both
                     // producer and consumer for other forms of
                     // coordination.
    uint32_t config[NUM_CONFIG_FIELDS];
};

void ring_buffer_init(struct ring_buffer* r);

// Writes or reads step_size at a time. Sets errno=EAGAIN if full or empty.
// Returns the number of step_size steps read.
long ring_buffer_write(struct ring_buffer* r, const void* data, uint32_t step_size, uint32_t steps);
long ring_buffer_read(struct ring_buffer* r, void* data, uint32_t step_size, uint32_t steps);
// Like ring_buffer_write / ring_buffer_read, but merely advances the counters
// without reading or writing anything. Returns the number of step_size steps
// advanced.
long ring_buffer_advance_write(struct ring_buffer* r, uint32_t step_size, uint32_t steps);
long ring_buffer_advance_read(struct ring_buffer* r, uint32_t step_size, uint32_t steps);

// If we want to work with dynamically allocated buffers, a separate struct is
// needed; the host and guest are in different address spaces and thus have
// different views of the same memory, with the host and guest having different
// copies of this struct.
struct ring_buffer_view {
    uint8_t* buf;
    uint32_t size;
    uint32_t mask;
};

// Convenience struct that holds a pointer to a ring along with a view.  It's a
// common pattern for the ring and the buffer of the view to be shared between
// two entities (in this case, usually guest and host).
struct ring_buffer_with_view {
    struct ring_buffer* ring;
    struct ring_buffer_view view;
};

// Calculates the highest power of 2 so that
// (1 << shift) <= size.
uint32_t ring_buffer_calc_shift(uint32_t size);

// Initializes ring buffer with view using |buf|. If |size| is not a power of
// two, then the buffer will assume a size equal to the greater power of two
// less than |size|.
void ring_buffer_view_init(struct ring_buffer* r, struct ring_buffer_view* v, uint8_t* buf,
                           uint32_t size);

void ring_buffer_init_view_only(struct ring_buffer_view* v, uint8_t* buf, uint32_t size);

// Read/write functions with the view.
long ring_buffer_view_write(struct ring_buffer* r, struct ring_buffer_view* v, const void* data,
                            uint32_t step_size, uint32_t steps);
long ring_buffer_view_read(struct ring_buffer* r, struct ring_buffer_view* v, void* data,
                           uint32_t step_size, uint32_t steps);

// Usage of ring_buffer as a waitable object.
// These functions will back off if spinning too long.
//
// if |v| is null, it is assumed that the statically allocated ring buffer is
// used.
//
// Returns true if ring buffer became available, false if timed out.
bool ring_buffer_wait_write(const struct ring_buffer* r, const struct ring_buffer_view* v,
                            uint32_t bytes, uint64_t timeout_us);
bool ring_buffer_wait_read(const struct ring_buffer* r, const struct ring_buffer_view* v,
                           uint32_t bytes, uint64_t timeout_us);

// read/write fully, blocking if there is nothing to read/write.
void ring_buffer_write_fully(struct ring_buffer* r, struct ring_buffer_view* v, const void* data,
                             uint32_t bytes);
void ring_buffer_read_fully(struct ring_buffer* r, struct ring_buffer_view* v, void* data,
                            uint32_t bytes);

// Like read/write fully, but with an abort value. The value is read from
// |abortPtr| each time. If |abortPtr| is null, then behaves the same
// as ring_buffer_(read|write)_fully.
// Returns the actual number of bytes sent or received.
uint32_t ring_buffer_write_fully_with_abort(struct ring_buffer* r, struct ring_buffer_view* v,
                                            const void* data, uint32_t bytes, uint32_t abort_value,
                                            const volatile uint32_t* abort_ptr);
uint32_t ring_buffer_read_fully_with_abort(struct ring_buffer* r, struct ring_buffer_view* v,
                                           void* data, uint32_t bytes, uint32_t abort_value,
                                           const volatile uint32_t* abort_ptr);

uint32_t ring_buffer_view_get_ring_pos(const struct ring_buffer_view* v, uint32_t index);

bool ring_buffer_can_write(const struct ring_buffer* r, uint32_t bytes);
bool ring_buffer_can_read(const struct ring_buffer* r, uint32_t bytes);
bool ring_buffer_view_can_write(const struct ring_buffer* r, const struct ring_buffer_view* v,
                                uint32_t bytes);
bool ring_buffer_view_can_read(const struct ring_buffer* r, const struct ring_buffer_view* v,
                               uint32_t bytes);
uint32_t ring_buffer_available_read(const struct ring_buffer* r, const struct ring_buffer_view* v);
uint32_t ring_buffer_available_write(const struct ring_buffer* r, const struct ring_buffer_view* v);
// Copies out contents from the consumer side of
// ring buffer/view |r,v|.
// If there is less available read than |wanted_bytes|,
// returns -1.
// On success, returns 0.
int ring_buffer_copy_contents(const struct ring_buffer* r, const struct ring_buffer_view* v,
                              uint32_t wanted_bytes, uint8_t* res);

// Lockless synchronization where the consumer is allowed to hang up and go to
// sleep. This can be considered a sort of asymmetric lock for two threads,
// where the consumer can be more sleepy. It captures the pattern we usually use
// for emulator devices; the guest asks the host for something, and some host
// thread services the request and goes back to sleep.
enum ring_buffer_sync_state {
    RING_BUFFER_SYNC_PRODUCER_IDLE = 0,
    RING_BUFFER_SYNC_PRODUCER_ACTIVE = 1,
    RING_BUFFER_SYNC_CONSUMER_HANGING_UP = 2,
    RING_BUFFER_SYNC_CONSUMER_HUNG_UP = 3,
};

// Sync state is RING_BUFFER_SYNC_PRODUCER_IDLE.
void ring_buffer_sync_init(struct ring_buffer* r);

// Tries to acquire the channel for sending.
// Returns false if the consumer was in the middle of hanging up,
// true if the producer successfully acquired the channel
// (put it in the RING_BUFFER_SYNC_PRODUCER_ACTIVE state).
bool ring_buffer_producer_acquire(struct ring_buffer* r);
// Same as above, but acquires from RING_BUFFER_SYNC_CONSUMER_HUNG_UP.
bool ring_buffer_producer_acquire_from_hangup(struct ring_buffer* r);
// Waits until the consumer hangs up.
void ring_buffer_producer_wait_hangup(struct ring_buffer* r);
// Sets the state back to RING_BUFFER_SYNC_PRODUCER_IDLE.
void ring_buffer_producer_idle(struct ring_buffer* r);

// There is no symmetric consumer acquire because the consumer can consume with
// the ring buffer being in any state (albeit with long waiting if the producer
// does not send anything)

// Tries to acquire the channel on the consumer side for
// hanging up. Returns false if the producer is in the middle of sending,
// true if the consumer successfully hung up the channel
// (put it in the RING_BUFFER_SYNC_CONSUMER_HUNG_UP state).
bool ring_buffer_consumer_hangup(struct ring_buffer* r);
// Waits until the producer has set the state to
// RING_BUFFER_SYNC_PRODUCER_IDLE.
void ring_buffer_consumer_wait_producer_idle(struct ring_buffer* r);
// Sets the state to hung up.
void ring_buffer_consumer_hung_up(struct ring_buffer* r);

// Convenient function to reschedule thread
void ring_buffer_yield();

#ifdef __cplusplus
}
#endif
