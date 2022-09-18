use crate::api::event::create_and_queue;
use crate::api::icd::*;
use crate::api::util::*;
use crate::core::event::*;
use crate::core::queue::*;

use mesa_rust_util::properties::*;
use rusticl_opencl_gen::*;

use std::ptr;
use std::sync::Arc;

impl CLInfo<cl_command_queue_info> for cl_command_queue {
    fn query(&self, q: cl_command_queue_info, _: &[u8]) -> CLResult<Vec<u8>> {
        let queue = self.get_ref()?;
        Ok(match q {
            CL_QUEUE_CONTEXT => {
                // Note we use as_ptr here which doesn't increase the reference count.
                let ptr = Arc::as_ptr(&queue.context);
                cl_prop::<cl_context>(cl_context::from_ptr(ptr))
            }
            CL_QUEUE_DEVICE => {
                // Note we use as_ptr here which doesn't increase the reference count.
                let ptr = Arc::as_ptr(&queue.device);
                cl_prop::<cl_device_id>(cl_device_id::from_ptr(ptr))
            }
            CL_QUEUE_DEVICE_DEFAULT => cl_prop::<cl_command_queue>(ptr::null_mut()),
            CL_QUEUE_PROPERTIES => cl_prop::<cl_command_queue_properties>(queue.props),
            CL_QUEUE_PROPERTIES_ARRAY => {
                cl_prop::<&Option<Properties<cl_queue_properties>>>(&queue.props_v2)
            }
            CL_QUEUE_REFERENCE_COUNT => cl_prop::<cl_uint>(self.refcnt()?),
            // clGetCommandQueueInfo, passing CL_QUEUE_SIZE Returns CL_INVALID_COMMAND_QUEUE since
            // command_queue cannot be a valid device command-queue.
            CL_QUEUE_SIZE => return Err(CL_INVALID_COMMAND_QUEUE),
            // CL_INVALID_VALUE if param_name is not one of the supported values
            _ => return Err(CL_INVALID_VALUE),
        })
    }
}

fn valid_command_queue_properties(properties: cl_command_queue_properties) -> bool {
    let valid_flags =
        cl_bitfield::from(CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE | CL_QUEUE_PROFILING_ENABLE);
    properties & !valid_flags == 0
}

fn supported_command_queue_properties(properties: cl_command_queue_properties) -> bool {
    let valid_flags = cl_bitfield::from(CL_QUEUE_PROFILING_ENABLE);
    properties & !valid_flags == 0
}

pub fn create_command_queue_impl(
    context: cl_context,
    device: cl_device_id,
    properties: cl_command_queue_properties,
    properties_v2: Option<Properties<cl_queue_properties>>,
) -> CLResult<cl_command_queue> {
    let c = context.get_arc()?;
    let d = device.get_arc()?;

    // CL_INVALID_DEVICE if device [...] is not associated with context.
    if !c.devs.contains(&d) {
        return Err(CL_INVALID_DEVICE);
    }

    // CL_INVALID_VALUE if values specified in properties are not valid.
    if !valid_command_queue_properties(properties) {
        return Err(CL_INVALID_VALUE);
    }

    // CL_INVALID_QUEUE_PROPERTIES if values specified in properties are valid but are not supported by the device.
    if !supported_command_queue_properties(properties) {
        return Err(CL_INVALID_QUEUE_PROPERTIES);
    }

    Ok(cl_command_queue::from_arc(Queue::new(
        c,
        d,
        properties,
        properties_v2,
    )?))
}

pub fn create_command_queue(
    context: cl_context,
    device: cl_device_id,
    properties: cl_command_queue_properties,
) -> CLResult<cl_command_queue> {
    create_command_queue_impl(context, device, properties, None)
}

pub fn create_command_queue_with_properties(
    context: cl_context,
    device: cl_device_id,
    properties: *const cl_queue_properties,
) -> CLResult<cl_command_queue> {
    let c = context.get_arc()?;
    let d = device.get_arc()?;

    let mut queue_properties = cl_command_queue_properties::default();
    let properties = if properties.is_null() {
        None
    } else {
        let properties = Properties::from_ptr(properties).ok_or(CL_INVALID_PROPERTY)?;

        for (k, v) in &properties.props {
            match *k as cl_uint {
                CL_QUEUE_PROPERTIES => queue_properties = *v,
                // CL_INVALID_QUEUE_PROPERTIES if values specified in properties are valid but are not
                // supported by the device.
                CL_QUEUE_SIZE => return Err(CL_INVALID_QUEUE_PROPERTIES),
                _ => return Err(CL_INVALID_PROPERTY),
            }
        }

        Some(properties)
    };

    Ok(cl_command_queue::from_arc(Queue::new(
        c,
        d,
        queue_properties,
        properties,
    )?))
}

pub fn enqueue_marker(command_queue: cl_command_queue, event: *mut cl_event) -> CLResult<()> {
    let q = command_queue.get_arc()?;

    // TODO marker makes sure previous commands did complete
    create_and_queue(
        q,
        CL_COMMAND_MARKER,
        Vec::new(),
        event,
        false,
        Box::new(|_, _| Ok(())),
    )
}

pub fn enqueue_marker_with_wait_list(
    command_queue: cl_command_queue,
    num_events_in_wait_list: cl_uint,
    event_wait_list: *const cl_event,
    event: *mut cl_event,
) -> CLResult<()> {
    let q = command_queue.get_arc()?;
    let evs = event_list_from_cl(&q, num_events_in_wait_list, event_wait_list)?;

    // TODO marker makes sure previous commands did complete
    create_and_queue(
        q,
        CL_COMMAND_MARKER,
        evs,
        event,
        false,
        Box::new(|_, _| Ok(())),
    )
}

pub fn enqueue_barrier(command_queue: cl_command_queue) -> CLResult<()> {
    let q = command_queue.get_arc()?;

    // TODO barriers make sure previous commands did complete and other commands didn't start
    let e = Event::new(&q, CL_COMMAND_BARRIER, Vec::new(), Box::new(|_, _| Ok(())));
    q.queue(e);
    Ok(())
}

pub fn enqueue_barrier_with_wait_list(
    command_queue: cl_command_queue,
    num_events_in_wait_list: cl_uint,
    event_wait_list: *const cl_event,
    event: *mut cl_event,
) -> CLResult<()> {
    let q = command_queue.get_arc()?;
    let evs = event_list_from_cl(&q, num_events_in_wait_list, event_wait_list)?;

    // TODO barriers make sure previous commands did complete and other commands didn't start
    create_and_queue(
        q,
        CL_COMMAND_BARRIER,
        evs,
        event,
        false,
        Box::new(|_, _| Ok(())),
    )
}

pub fn flush_queue(command_queue: cl_command_queue) -> CLResult<()> {
    // CL_INVALID_COMMAND_QUEUE if command_queue is not a valid host command-queue.
    command_queue.get_ref()?.flush(false)
}

pub fn finish_queue(command_queue: cl_command_queue) -> CLResult<()> {
    // CL_INVALID_COMMAND_QUEUE if command_queue is not a valid host command-queue.
    command_queue.get_ref()?.flush(true)
}

pub fn release_command_queue(command_queue: cl_command_queue) -> CLResult<()> {
    // clReleaseCommandQueue performs an implicit flush to issue any previously queued OpenCL
    // commands in command_queue.
    flush_queue(command_queue)?;
    command_queue.release()?;
    Ok(())
}
