use crate::api::icd::*;
use crate::api::types::*;
use crate::api::util::*;
use crate::core::event::*;
use crate::core::queue::*;

use rusticl_opencl_gen::*;

use std::collections::HashSet;
use std::ptr;
use std::sync::Arc;

impl CLInfo<cl_event_info> for cl_event {
    fn query(&self, q: cl_event_info, _: &[u8]) -> CLResult<Vec<u8>> {
        let event = self.get_ref()?;
        Ok(match *q {
            CL_EVENT_COMMAND_EXECUTION_STATUS => cl_prop::<cl_int>(event.status()),
            CL_EVENT_CONTEXT => {
                // Note we use as_ptr here which doesn't increase the reference count.
                let ptr = Arc::as_ptr(&event.context);
                cl_prop::<cl_context>(cl_context::from_ptr(ptr))
            }
            CL_EVENT_COMMAND_QUEUE => {
                let ptr = match event.queue.as_ref() {
                    // Note we use as_ptr here which doesn't increase the reference count.
                    Some(queue) => Arc::as_ptr(queue),
                    None => ptr::null_mut(),
                };
                cl_prop::<cl_command_queue>(cl_command_queue::from_ptr(ptr))
            }
            CL_EVENT_REFERENCE_COUNT => cl_prop::<cl_uint>(self.refcnt()?),
            CL_EVENT_COMMAND_TYPE => cl_prop::<cl_command_type>(event.cmd_type),
            _ => return Err(CL_INVALID_VALUE),
        })
    }
}

impl CLInfo<cl_profiling_info> for cl_event {
    fn query(&self, q: cl_profiling_info, _: &[u8]) -> CLResult<Vec<u8>> {
        let event = self.get_ref()?;
        if event.cmd_type == CL_COMMAND_USER {
            // CL_PROFILING_INFO_NOT_AVAILABLE [...] if event is a user event object.
            return Err(CL_PROFILING_INFO_NOT_AVAILABLE);
        }

        Ok(match *q {
            // TODO
            CL_PROFILING_COMMAND_QUEUED => cl_prop::<cl_ulong>(0),
            CL_PROFILING_COMMAND_SUBMIT => cl_prop::<cl_ulong>(1),
            CL_PROFILING_COMMAND_START => cl_prop::<cl_ulong>(2),
            CL_PROFILING_COMMAND_END => cl_prop::<cl_ulong>(3),
            CL_PROFILING_COMMAND_COMPLETE => cl_prop::<cl_ulong>(3),
            _ => return Err(CL_INVALID_VALUE),
        })
    }
}

pub fn create_user_event(context: cl_context) -> CLResult<cl_event> {
    let c = context.get_arc()?;
    Ok(cl_event::from_arc(Event::new_user(c)))
}

pub fn wait_for_events(num_events: cl_uint, event_list: *const cl_event) -> CLResult<()> {
    let evs = cl_event::get_arc_vec_from_arr(event_list, num_events)?;

    // CL_INVALID_VALUE if num_events is zero or event_list is NULL.
    if evs.is_empty() {
        return Err(CL_INVALID_VALUE);
    }

    // CL_INVALID_CONTEXT if events specified in event_list do not belong to the same context.
    let contexts: HashSet<_> = evs.iter().map(|e| &e.context).collect();
    if contexts.len() != 1 {
        return Err(CL_INVALID_CONTEXT);
    }

    // TODO better impl
    let mut err = false;
    for e in evs {
        if let Some(q) = &e.queue {
            q.flush(false)?;
        }

        err |= e.wait() < 0;
    }

    // CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST if the execution status of any of the events
    // in event_list is a negative integer value.
    if err {
        return Err(CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST);
    }

    Ok(())
}

pub fn set_event_callback(
    event: cl_event,
    command_exec_callback_type: cl_int,
    pfn_event_notify: Option<EventCB>,
    user_data: *mut ::std::os::raw::c_void,
) -> CLResult<()> {
    let e = event.get_ref()?;

    // CL_INVALID_VALUE if pfn_event_notify is NULL
    // or if command_exec_callback_type is not CL_SUBMITTED, CL_RUNNING, or CL_COMPLETE.
    if pfn_event_notify.is_none()
        || ![CL_SUBMITTED, CL_RUNNING, CL_COMPLETE]
            .contains(&(command_exec_callback_type as cl_uint))
    {
        return Err(CL_INVALID_VALUE);
    }

    e.add_cb(
        command_exec_callback_type,
        pfn_event_notify.unwrap(),
        user_data,
    );

    Ok(())
}

pub fn set_user_event_status(event: cl_event, execution_status: cl_int) -> CLResult<()> {
    let e = event.get_ref()?;

    // CL_INVALID_VALUE if the execution_status is not CL_COMPLETE or a negative integer value.
    if execution_status != CL_COMPLETE as cl_int && execution_status > 0 {
        return Err(CL_INVALID_VALUE);
    }

    // CL_INVALID_OPERATION if the execution_status for event has already been changed by a
    // previous call to clSetUserEventStatus.
    if e.status() != CL_SUBMITTED as cl_int {
        return Err(CL_INVALID_OPERATION);
    }

    e.set_user_status(execution_status);
    Ok(())
}

pub fn create_and_queue(
    q: Arc<Queue>,
    cmd_type: cl_command_type,
    deps: Vec<Arc<Event>>,
    event: *mut cl_event,
    block: bool,
    work: EventSig,
) -> CLResult<()> {
    let e = Event::new(&q, cmd_type, deps, work);
    cl_event::leak_ref(event, &e);
    q.queue(e);
    if block {
        q.flush(true)?;
    }
    Ok(())
}
