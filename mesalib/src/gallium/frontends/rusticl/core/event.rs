use crate::api::icd::*;
use crate::api::types::*;
use crate::core::context::*;
use crate::core::queue::*;
use crate::impl_cl_type_trait;

use mesa_rust::pipe::context::*;
use mesa_rust::pipe::fence::*;
use mesa_rust_util::static_assert;
use rusticl_opencl_gen::*;

use std::os::raw::c_void;
use std::slice;
use std::sync::Arc;
use std::sync::Condvar;
use std::sync::Mutex;
use std::sync::MutexGuard;
use std::time::Duration;

// we assert that those are a continous range of numbers so we won't have to use HashMaps
static_assert!(CL_COMPLETE == 0);
static_assert!(CL_RUNNING == 1);
static_assert!(CL_SUBMITTED == 2);
static_assert!(CL_QUEUED == 3);

pub type EventSig = Box<dyn Fn(&Arc<Queue>, &PipeContext) -> CLResult<()>>;

struct EventMutState {
    status: cl_int,
    cbs: [Vec<(EventCB, *mut c_void)>; 3],
    fence: Option<PipeFence>,
}

#[repr(C)]
pub struct Event {
    pub base: CLObjectBase<CL_INVALID_EVENT>,
    pub context: Arc<Context>,
    pub queue: Option<Arc<Queue>>,
    pub cmd_type: cl_command_type,
    pub deps: Vec<Arc<Event>>,
    work: Option<EventSig>,
    state: Mutex<EventMutState>,
    cv: Condvar,
}

impl_cl_type_trait!(cl_event, Event, CL_INVALID_EVENT);

// TODO shouldn't be needed, but... uff C pointers are annoying
unsafe impl Send for Event {}
unsafe impl Sync for Event {}

impl Event {
    pub fn new(
        queue: &Arc<Queue>,
        cmd_type: cl_command_type,
        deps: Vec<Arc<Event>>,
        work: EventSig,
    ) -> Arc<Event> {
        Arc::new(Self {
            base: CLObjectBase::new(),
            context: queue.context.clone(),
            queue: Some(queue.clone()),
            cmd_type: cmd_type,
            deps: deps,
            state: Mutex::new(EventMutState {
                status: CL_QUEUED as cl_int,
                cbs: [Vec::new(), Vec::new(), Vec::new()],
                fence: None,
            }),
            work: Some(work),
            cv: Condvar::new(),
        })
    }

    pub fn new_user(context: Arc<Context>) -> Arc<Event> {
        Arc::new(Self {
            base: CLObjectBase::new(),
            context: context,
            queue: None,
            cmd_type: CL_COMMAND_USER,
            deps: Vec::new(),
            state: Mutex::new(EventMutState {
                status: CL_SUBMITTED as cl_int,
                cbs: [Vec::new(), Vec::new(), Vec::new()],
                fence: None,
            }),
            work: None,
            cv: Condvar::new(),
        })
    }

    pub fn from_cl_arr(events: *const cl_event, num_events: u32) -> CLResult<Vec<Arc<Event>>> {
        let s = unsafe { slice::from_raw_parts(events, num_events as usize) };
        s.iter().map(|e| e.get_arc()).collect()
    }

    fn state(&self) -> MutexGuard<EventMutState> {
        self.state.lock().unwrap()
    }

    pub fn status(&self) -> cl_int {
        self.state().status
    }

    fn set_status(&self, lock: &mut MutexGuard<EventMutState>, new: cl_int) {
        lock.status = new;
        self.cv.notify_all();
        if [CL_COMPLETE, CL_RUNNING, CL_SUBMITTED].contains(&(new as u32)) {
            if let Some(cbs) = lock.cbs.get(new as usize) {
                cbs.iter()
                    .for_each(|(cb, data)| unsafe { cb(cl_event::from_ptr(self), new, *data) });
            }
        }
    }

    pub fn set_user_status(&self, status: cl_int) {
        let mut lock = self.state();
        self.set_status(&mut lock, status);
    }

    pub fn is_error(&self) -> bool {
        self.status() < 0
    }

    pub fn add_cb(&self, state: cl_int, cb: EventCB, data: *mut c_void) {
        let mut lock = self.state();
        let status = lock.status;

        // call cb if the status was already reached
        if state >= status {
            drop(lock);
            unsafe { cb(cl_event::from_ptr(self), status, data) };
        } else {
            lock.cbs.get_mut(state as usize).unwrap().push((cb, data));
        }
    }

    pub fn wait(&self) -> cl_int {
        let mut lock = self.state();
        while lock.status >= CL_SUBMITTED as cl_int {
            if lock.fence.is_some() {
                lock.fence.as_ref().unwrap().wait();
                // so we trigger all cbs
                self.set_status(&mut lock, CL_RUNNING as cl_int);
                self.set_status(&mut lock, CL_COMPLETE as cl_int);
            } else {
                lock = self
                    .cv
                    .wait_timeout(lock, Duration::from_millis(50))
                    .unwrap()
                    .0;
            }
        }
        lock.status
    }

    // We always assume that work here simply submits stuff to the hardware even if it's just doing
    // sw emulation or nothing at all.
    // If anything requets waiting, we will update the status through fencing later.
    pub fn call(&self, ctx: &PipeContext) -> cl_int {
        let mut lock = self.state();
        let status = lock.status;
        if status == CL_QUEUED as cl_int {
            let new = self.work.as_ref().map_or(
                // if there is no work
                CL_SUBMITTED as cl_int,
                |w| {
                    let res = w(self.queue.as_ref().unwrap(), ctx).err().map_or(
                        // if there is an error, negate it
                        CL_SUBMITTED as cl_int,
                        |e| e,
                    );
                    lock.fence = Some(ctx.flush());
                    res
                },
            );
            self.set_status(&mut lock, new);
            new
        } else {
            status
        }
    }
}

// TODO worker thread per device
// Condvar to wait on new events to work on
// notify condvar when flushing queue events to worker
// attach fence to flushed events on context->flush
// store "newest" event for in-order queues per queue
// reordering/graph building done in worker
