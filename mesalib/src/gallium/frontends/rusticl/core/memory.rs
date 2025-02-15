use crate::api::icd::*;
use crate::api::types::*;
use crate::api::util::*;
use crate::core::context::*;
use crate::core::device::*;
use crate::core::format::*;
use crate::core::gl::*;
use crate::core::queue::*;
use crate::core::util::*;
use crate::impl_cl_type_trait;
use crate::impl_cl_type_trait_base;
use crate::perf_warning;

use mesa_rust::pipe::context::*;
use mesa_rust::pipe::resource::*;
use mesa_rust::pipe::screen::ResourceType;
use mesa_rust::pipe::transfer::*;
use mesa_rust_gen::*;
use mesa_rust_util::conversion::*;
use mesa_rust_util::properties::Properties;
use mesa_rust_util::ptr::AllocSize;
use mesa_rust_util::ptr::TrackedPointers;
use rusticl_opencl_gen::*;

use std::alloc;
use std::alloc::Layout;
use std::cmp;
use std::collections::btree_map::Entry;
use std::collections::HashMap;
use std::convert::TryInto;
use std::mem;
use std::mem::size_of;
use std::ops::Deref;
use std::os::raw::c_void;
use std::ptr;
use std::sync::Arc;
use std::sync::Mutex;
use std::sync::MutexGuard;

struct Mapping<T> {
    layout: Layout,
    writes: bool,
    ptr: Option<MutMemoryPtr>,
    /// reference count from the API perspective. Once it reaches 0, we need to write back the
    /// mappings content to the GPU resource.
    count: u32,
    inner: T,
}

impl<T> Drop for Mapping<T> {
    fn drop(&mut self) {
        if let Some(ptr) = &self.ptr {
            unsafe {
                alloc::dealloc(ptr.as_ptr().cast(), self.layout);
            }
        }
    }
}

impl<T> AllocSize<usize> for Mapping<T> {
    fn size(&self) -> usize {
        self.layout.size()
    }
}

impl<T> Deref for Mapping<T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

struct BufferMapping {
    offset: usize,
}

struct ImageMapping {
    origin: CLVec<usize>,
    region: CLVec<usize>,
}

#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct ConstMemoryPtr {
    ptr: *const c_void,
}
unsafe impl Send for ConstMemoryPtr {}
unsafe impl Sync for ConstMemoryPtr {}

impl ConstMemoryPtr {
    pub fn as_ptr(&self) -> *const c_void {
        self.ptr
    }

    /// # Safety
    ///
    /// Users need to ensure that `ptr` is only accessed in a thread-safe manner sufficient for
    /// [Send] and [Sync]
    pub unsafe fn from_ptr(ptr: *const c_void) -> Self {
        Self { ptr: ptr }
    }
}

impl From<MutMemoryPtr> for ConstMemoryPtr {
    fn from(value: MutMemoryPtr) -> Self {
        Self {
            ptr: value.ptr.cast(),
        }
    }
}

#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct MutMemoryPtr {
    ptr: *mut c_void,
}
unsafe impl Send for MutMemoryPtr {}
unsafe impl Sync for MutMemoryPtr {}

impl MutMemoryPtr {
    pub fn as_ptr(&self) -> *mut c_void {
        self.ptr
    }

    /// # Safety
    ///
    /// Users need to ensure that `ptr` is only accessed in a thread-safe manner sufficient for
    /// [Send] and [Sync]
    pub unsafe fn from_ptr(ptr: *mut c_void) -> Self {
        Self { ptr: ptr }
    }
}

#[derive(Copy, Clone, PartialEq)]
pub enum ResourceValidityEntity {
    Host,
    Device(&'static Device),
}

/// Allocation with real GPU backing storage. Tracks on which device the content is valid on.
pub struct ResourceAllocation {
    pub res: HashMap<&'static Device, Arc<PipeResource>>,
    valid_on: Mutex<Vec<ResourceValidityEntity>>,
    // it's a bit hacky, but storing the pointer as `usize` gives us `Send` and `Sync`. The
    // application is required to ensure no data races exist on the memory anyway.
    host_ptr: usize,
    hostptr_devs: Vec<ResourceValidityEntity>,
    // this might be non zero for dma-buf imported resources
    offset: usize,
}

impl ResourceAllocation {
    /// # Panics
    ///
    /// valid_on needs to be a Vec with at least one element, will panic otherwise.
    fn get_best_valid_entity_for_transfer(
        valid_on: &MutexGuard<Vec<ResourceValidityEntity>>,
    ) -> ResourceValidityEntity {
        // We want to avoid having to copy over the PCIe bus, so we prefer an entity which is either
        // the host itself or a device using host memory.
        let res = valid_on.iter().min_by_key(|entity| match entity {
            ResourceValidityEntity::Host => 0,
            ResourceValidityEntity::Device(dev) => {
                if dev.unified_memory() {
                    1
                } else {
                    2
                }
            }
        });

        *res.unwrap()
    }

    /// Small helper function to indicate when transparent migration is never required, e.g. if it's
    /// a single device allocation with no hostptr.
    fn can_skip_migration(&self) -> bool {
        match self.hostptr_devs.len() {
            // If storage isn't shared between devices, we only need to migrate when there is more
            // than one device.
            0 => self.res.len() == 1,

            // If all devices use a host_ptr allocation, the content is automatically synchronized
            // as they share the same storage. The - 1 is required as the Host is also part of
            // `hostptr_devs`.
            len => len - 1 == self.res.len(),
        }
    }

    /// Returns the GPU resource for the device `ctx` is associated with. It will transparently
    /// migrate the data to the GPU.
    /// TODO: add a map function to return a mapping to the resource of one device the data is valid
    ///       on instead of migrating if the user would simply map the resource anyway.
    fn get_res_for_access(&self, ctx: &QueueContext, rw: RWFlags) -> CLResult<&Arc<PipeResource>> {
        let dev = ctx.dev;
        let dev_entity = ResourceValidityEntity::Device(dev);
        let to_res = self.res.get(dev).ok_or(CL_OUT_OF_HOST_MEMORY)?;

        // in most cases we can skip most of the work below.
        if self.can_skip_migration() {
            return Ok(to_res);
        }

        let Ok(mut valid_on) = self.valid_on.lock() else {
            return Err(CL_OUT_OF_HOST_MEMORY);
        };

        // If the content isn't valid on dev we need to migrate it to it.
        if matches!(rw, RWFlags::RD | RWFlags::RW) && !valid_on.contains(&dev_entity) {
            // valid_on is a vec with at least one element, so this call won't panic.
            let entity = Self::get_best_valid_entity_for_transfer(&valid_on);

            let helper_ctx;
            let map;
            let flush;

            if to_res.is_buffer() {
                let ptr;
                match entity {
                    ResourceValidityEntity::Host => {
                        flush = false;
                        ptr = self.host_ptr as *mut c_void;
                    }
                    ResourceValidityEntity::Device(dev) => {
                        flush = true;

                        let from_res = &self.res[dev];
                        helper_ctx = dev.helper_ctx();

                        // update the resource and wait for the operation to finish. We also map the resources
                        // unsynchronized as we can't block or flush any other contexts here as this might cause
                        // deadlocks.
                        map = helper_ctx
                            .map_buffer_unsynchronized(
                                from_res,
                                0,
                                from_res.width() as i32,
                                RWFlags::RD,
                            )
                            .ok_or(CL_OUT_OF_HOST_MEMORY)?;

                        ptr = map.ptr();
                    }
                }

                ctx.buffer_subdata(to_res, 0, ptr, to_res.width());
            } else {
                let ResourceValidityEntity::Device(dev) = entity else {
                    // we don't support migrating from host_ptr for images yet. It's also not needed
                    // because the Image struct has a more optimized way of doing things there.
                    unimplemented!();
                };

                flush = true;
                let from_res = &self.res[dev];
                helper_ctx = dev.helper_ctx();

                // update the resource and wait for the operation to finish. We also map the resources
                // unsynchronized as we can't block or flush any other contexts here as this might cause
                // deadlocks.
                let bx = pipe_box {
                    width: from_res.width() as i32,
                    height: from_res.height() as i32,
                    depth: from_res.depth() as i16,
                    ..Default::default()
                };

                map = helper_ctx
                    .map_texture_unsynchronized(from_res, &bx, RWFlags::RD)
                    .ok_or(CL_OUT_OF_HOST_MEMORY)?;

                let row_pitch: u32 = map.row_pitch();
                let slice_pitch: usize = map.slice_pitch();

                let bx = pipe_box {
                    width: to_res.width() as i32,
                    height: to_res.height() as i32,
                    depth: to_res.depth() as i16,
                    ..Default::default()
                };

                ctx.texture_subdata(to_res, &bx, map.ptr(), row_pitch, slice_pitch);
            }

            // TODO: we really kinda need to figure out how we can make the compiler scream, that
            //       temporarily mapped memory might be accessed at some random point in the future
            //       by a GPU unless it's queues are flushed and processed.
            if flush {
                ctx.flush().wait();
            }
        }

        if matches!(rw, RWFlags::WR | RWFlags::RW) {
            // If the user writes to it it's not valid on any other device anymore.
            valid_on.clear();
        }

        if !valid_on.contains(&dev_entity) {
            // if we update one hostptr resource, we update them all.
            if self.hostptr_devs.contains(&dev_entity) {
                valid_on.extend_from_slice(&self.hostptr_devs);
            } else {
                valid_on.push(ResourceValidityEntity::Device(dev));
            }
        }

        Ok(to_res)
    }

    pub fn migrate_to_hostptr(&self, ctx: &QueueContext, rw: RWFlags) -> CLResult<()> {
        let host_entity = ResourceValidityEntity::Host;
        let host_ptr = self.host_ptr as *mut c_void;

        // in most cases we can skip most of the work below.
        if self.can_skip_migration() || host_ptr.is_null() {
            return Ok(());
        }

        let Ok(mut valid_on) = self.valid_on.lock() else {
            return Err(CL_OUT_OF_HOST_MEMORY);
        };

        // If the content isn't valid on the host we need to migrate it to it.
        if matches!(rw, RWFlags::RD | RWFlags::RW) && !valid_on.contains(&host_entity) {
            let ctx_dev_entity = ResourceValidityEntity::Device(ctx.dev);
            let mut entity = ctx_dev_entity;

            if !valid_on.contains(&entity) {
                // valid_on is a vec with at least one element, so this call won't panic.
                entity = Self::get_best_valid_entity_for_transfer(&valid_on);
            }

            debug_assert!(entity != ResourceValidityEntity::Host);

            let ResourceValidityEntity::Device(from_dev) = entity else {
                // we check if `valid_on` contains a host entity above, so this should never happen.
                unreachable!();
            };

            let helper_ctx;
            let map;
            let from_res = &self.res[from_dev];

            assert!(
                from_res.is_buffer(),
                "Transparent resource migration only supported on buffers."
            );

            if from_dev == ctx.dev {
                map = ctx
                    .buffer_map(from_res, 0, from_res.width() as i32, RWFlags::RD)
                    .ok_or(CL_OUT_OF_HOST_MEMORY)?;
            } else {
                helper_ctx = from_dev.helper_ctx();
                // update the resource and wait for the operation to finish. We also map the resources
                // unsynchronized as we can't block or flush any other contexts here as this might cause
                // deadlocks.
                map = helper_ctx
                    .map_buffer_unsynchronized(from_res, 0, from_res.width() as i32, RWFlags::RD)
                    .ok_or(CL_OUT_OF_HOST_MEMORY)?;
            }

            let ptr = map.ptr();
            // SAFETY: The application promises, that host_ptr is big enough to hold the entire
            //         content of the buffer, also `ptr` is the mapped resource containing at least
            //         `from_res.width()` bytes. Also both pointers do not overlap.
            unsafe {
                ptr::copy_nonoverlapping(ptr, host_ptr, from_res.width() as usize);
            }
        }

        if matches!(rw, RWFlags::WR | RWFlags::RW) {
            // If the user writes to it it's not valid on any other device anymore.
            valid_on.clear();
        }

        if !valid_on.contains(&host_entity) {
            // if we update the hostptr, we update all devices having a hostptr allocation.
            valid_on.extend_from_slice(&self.hostptr_devs);
        }

        Ok(())
    }
}

pub struct SubAllocation {
    mem: Mem,
    // offset relative to the actual resource, not relative to `mem`. This saves us a few
    // calculations and we only need the total amount anyway.
    offset: usize,
}

/// Abstraction over the memory allocation. It might be a real GPU backing storage or simply a sub
/// allocation over an existing memory object.
enum Allocation {
    Resource(ResourceAllocation),
    SubAlloc(SubAllocation),
}

// TODO: - Once it's used for more stuff might make sense to split it into an Image and Buffer
//         variant.
//       - Instead of doing full migration every time, it could also do it for only parts of the
//         allocation.
impl Allocation {
    /// Creates a new allocation object assuming the initial data is valid on every device.
    pub fn new(
        res: HashMap<&'static Device, Arc<PipeResource>>,
        offset: usize,
        host_ptr: *mut c_void,
    ) -> Self {
        let hostptr_devs = if !host_ptr.is_null() {
            res.iter()
                // we only add devices we actually have a host ptr resource for
                .filter_map(|(&dev, res)| {
                    res.is_user().then_some(ResourceValidityEntity::Device(dev))
                })
                // and the host itself
                .chain([ResourceValidityEntity::Host])
                .collect()
        } else {
            Vec::new()
        };

        let mut valid_on: Vec<_> = res
            .keys()
            .copied()
            .map(ResourceValidityEntity::Device)
            .collect();
        if !host_ptr.is_null() {
            valid_on.push(ResourceValidityEntity::Host);
        }

        Self::Resource(ResourceAllocation {
            valid_on: Mutex::new(valid_on),
            res: res,
            host_ptr: host_ptr as usize,
            hostptr_devs: hostptr_devs,
            offset: offset,
        })
    }

    fn new_sub(mem: Mem, offset: usize) -> Self {
        Self::SubAlloc(SubAllocation {
            // we precalculate the entire offset here.
            offset: offset + mem.alloc.offset(),
            mem: mem,
        })
    }

    /// Returns true if the backing storage of the two objects is equal.
    fn backing_resource_eq(&self, other: &Self) -> bool {
        ptr::eq(self.get_real_resource(), other.get_real_resource())
    }

    /// Follows the sub-allocation chain until it hits a real GPU allocation.
    fn get_real_resource(&self) -> &ResourceAllocation {
        match self {
            Allocation::SubAlloc(sub) => sub.mem.alloc.get_real_resource(),
            Allocation::Resource(res) => res,
        }
    }

    /// Returns the resource associated with `dev` without any data migration.
    fn get_res_of_dev(&self, dev: &Device) -> CLResult<&Arc<PipeResource>> {
        self.get_real_resource()
            .res
            .get(dev)
            .ok_or(CL_OUT_OF_HOST_MEMORY)
    }

    /// Returns the resource associated with `ctx.dev` and transparently migrate the data.
    fn get_res_for_access(&self, ctx: &QueueContext, rw: RWFlags) -> CLResult<&Arc<PipeResource>> {
        self.get_real_resource().get_res_for_access(ctx, rw)
    }

    /// Migrates the content to the host. Fails if there is no host ptr.
    pub fn _migrate_to_hostptr(&self, ctx: &QueueContext, rw: RWFlags) -> CLResult<()> {
        self.get_real_resource().migrate_to_hostptr(ctx, rw)
    }

    pub fn host_ptr(&self) -> *mut c_void {
        let mut host_ptr = self.get_real_resource().host_ptr;

        // we can only apply the offset as long the host_ptr isn't null.
        if host_ptr != 0 {
            host_ptr += self.offset();
        }

        host_ptr as _
    }

    fn is_user_alloc_for_dev(&self, dev: &Device) -> CLResult<bool> {
        Ok(self.get_res_of_dev(dev)?.is_user())
    }

    fn offset(&self) -> usize {
        match self {
            Allocation::Resource(res) => res.offset,
            Allocation::SubAlloc(sub) => sub.offset,
        }
    }
}

pub enum Mem {
    Buffer(Arc<Buffer>),
    Image(Arc<Image>),
}

impl Deref for Mem {
    type Target = MemBase;

    fn deref(&self) -> &Self::Target {
        match self {
            Self::Buffer(b) => &b.base,
            Self::Image(i) => &i.base,
        }
    }
}

impl Mem {
    pub fn is_mapped_ptr(&self, ptr: *mut c_void) -> bool {
        match self {
            Self::Buffer(b) => b.is_mapped_ptr(ptr),
            Self::Image(i) => i.is_mapped_ptr(ptr),
        }
    }

    pub fn sync_unmap(&self, ctx: &QueueContext, ptr: MutMemoryPtr) -> CLResult<()> {
        match self {
            Self::Buffer(b) => b.sync_unmap(ctx, ptr),
            Self::Image(i) => i.sync_unmap(ctx, ptr),
        }
    }

    pub fn unmap(&self, ptr: MutMemoryPtr) -> CLResult<bool> {
        match self {
            Self::Buffer(b) => b.unmap(ptr),
            Self::Image(i) => i.unmap(ptr),
        }
    }
}

/// # Mapping memory
///
/// Maps the queue associated device's resource.
///
/// Mapping resources could have been quite straightforward if OpenCL wouldn't allow for so called
/// non blocking maps. Non blocking maps shall return a valid pointer to the mapped region
/// immediately, but should not synchronize data (in case of shadow buffers) until after the map
/// event is reached in the queue. This makes it not possible to simply use pipe_transfers as those
/// can't be explicitly synced by the frontend.
///
/// In order to have a compliant implementation of the mapping API we have to consider the following
/// cases:
///   1. Mapping a cl_mem object with CL_MEM_USE_HOST_PTR: We simply return the host_ptr.
///      Synchronization of shadowed host ptrs are done in `sync_shadow` on demand.
///   2. Mapping linear resources on UMA systems: We simply create the pipe_transfer with
///      `PIPE_MAP_DIRECTLY` and `PIPE_MAP_UNSYNCHRONIZED` and return the attached pointer.
///   3. On non UMA systems or when 2. fails (e.g. due to the resource being tiled) we
///      - create a shadow pipe_resource with `PIPE_USAGE_STAGING`,
///        `PIPE_RESOURCE_FLAG_MAP_PERSISTENT` and `PIPE_RESOURCE_FLAG_MAP_COHERENT`
///      - create a pipe_transfer with `PIPE_MAP_COHERENT`, `PIPE_MAP_PERSISTENT` and
///        `PIPE_MAP_UNSYNCHRONIZED`
///      - sync the shadow buffer like a host_ptr shadow buffer in 1.
///
/// Taking this approach we guarentee that we only copy when actually needed while making sure the
/// content behind the returned pointer is valid until unmapped.
pub struct MemBase {
    pub base: CLObjectBase<CL_INVALID_MEM_OBJECT>,
    pub context: Arc<Context>,
    pub mem_type: cl_mem_object_type,
    pub flags: cl_mem_flags,
    pub size: usize,
    pub props: Properties<cl_mem_properties>,
    pub cbs: Mutex<Vec<MemCB>>,
    pub gl_obj: Option<GLObject>,
    alloc: Allocation,
}

pub struct Buffer {
    base: MemBase,
    maps: Mutex<TrackedPointers<usize, Mapping<BufferMapping>>>,
}

pub struct Image {
    base: MemBase,
    pub image_format: cl_image_format,
    pub pipe_format: pipe_format,
    pub image_desc: cl_image_desc,
    pub image_elem_size: u8,
    maps: Mutex<TrackedPointers<usize, Mapping<ImageMapping>>>,
}

impl Deref for Buffer {
    type Target = MemBase;

    fn deref(&self) -> &Self::Target {
        &self.base
    }
}

impl Deref for Image {
    type Target = MemBase;

    fn deref(&self) -> &Self::Target {
        &self.base
    }
}

impl_cl_type_trait_base!(cl_mem, MemBase, [Buffer, Image], CL_INVALID_MEM_OBJECT);
impl_cl_type_trait!(cl_mem, Buffer, CL_INVALID_MEM_OBJECT, base.base);
impl_cl_type_trait!(cl_mem, Image, CL_INVALID_MEM_OBJECT, base.base);

pub trait CLImageDescInfo {
    fn type_info(&self) -> (u8, bool);
    fn pixels(&self) -> usize;
    fn bx(&self) -> CLResult<pipe_box>;
    fn row_pitch(&self) -> CLResult<u32>;
    fn slice_pitch(&self) -> usize;
    fn width(&self) -> CLResult<u32>;
    fn height(&self) -> CLResult<u32>;
    fn size(&self) -> CLVec<usize>;

    fn dims(&self) -> u8 {
        self.type_info().0
    }

    fn dims_with_array(&self) -> u8 {
        let array: u8 = self.is_array().into();
        self.dims() + array
    }

    fn has_slice(&self) -> bool {
        self.dims() == 3 || self.is_array()
    }

    fn is_array(&self) -> bool {
        self.type_info().1
    }
}

impl CLImageDescInfo for cl_image_desc {
    fn type_info(&self) -> (u8, bool) {
        match self.image_type {
            CL_MEM_OBJECT_IMAGE1D | CL_MEM_OBJECT_IMAGE1D_BUFFER => (1, false),
            CL_MEM_OBJECT_IMAGE1D_ARRAY => (1, true),
            CL_MEM_OBJECT_IMAGE2D => (2, false),
            CL_MEM_OBJECT_IMAGE2D_ARRAY => (2, true),
            CL_MEM_OBJECT_IMAGE3D => (3, false),
            _ => panic!("unknown image_type {:x}", self.image_type),
        }
    }

    fn pixels(&self) -> usize {
        let mut res = self.image_width;
        let dims = self.dims();

        if dims > 1 {
            res *= self.image_height;
        }

        if dims > 2 {
            res *= self.image_depth;
        }

        if self.is_array() {
            res *= self.image_array_size;
        }

        res
    }

    fn size(&self) -> CLVec<usize> {
        let mut height = cmp::max(self.image_height, 1);
        let mut depth = cmp::max(self.image_depth, 1);

        match self.image_type {
            CL_MEM_OBJECT_IMAGE1D_ARRAY => height = self.image_array_size,
            CL_MEM_OBJECT_IMAGE2D_ARRAY => depth = self.image_array_size,
            _ => {}
        }

        CLVec::new([self.image_width, height, depth])
    }

    fn bx(&self) -> CLResult<pipe_box> {
        create_pipe_box(CLVec::default(), self.size(), self.image_type)
    }

    fn row_pitch(&self) -> CLResult<u32> {
        self.image_row_pitch
            .try_into_with_err(CL_OUT_OF_HOST_MEMORY)
    }

    fn slice_pitch(&self) -> usize {
        self.image_slice_pitch
    }

    fn width(&self) -> CLResult<u32> {
        self.image_width.try_into_with_err(CL_OUT_OF_HOST_MEMORY)
    }

    fn height(&self) -> CLResult<u32> {
        self.image_height.try_into_with_err(CL_OUT_OF_HOST_MEMORY)
    }
}

fn sw_copy(
    src: *const c_void,
    dst: *mut c_void,
    region: &CLVec<usize>,
    src_origin: &CLVec<usize>,
    src_row_pitch: usize,
    src_slice_pitch: usize,
    dst_origin: &CLVec<usize>,
    dst_row_pitch: usize,
    dst_slice_pitch: usize,
    pixel_size: u8,
) {
    let pixel_size = pixel_size as usize;
    for z in 0..region[2] {
        if src_row_pitch == dst_row_pitch && region[1] * pixel_size == src_row_pitch {
            unsafe {
                ptr::copy(
                    src.byte_add(
                        (*src_origin + [0, 0, z]) * [pixel_size, src_row_pitch, src_slice_pitch],
                    ),
                    dst.byte_add(
                        (*dst_origin + [0, 0, z]) * [pixel_size, dst_row_pitch, dst_slice_pitch],
                    ),
                    region[0] * region[1] * pixel_size,
                )
            }
        } else {
            for y in 0..region[1] {
                unsafe {
                    ptr::copy(
                        src.byte_add(
                            (*src_origin + [0, y, z])
                                * [pixel_size, src_row_pitch, src_slice_pitch],
                        ),
                        dst.byte_add(
                            (*dst_origin + [0, y, z])
                                * [pixel_size, dst_row_pitch, dst_slice_pitch],
                        ),
                        region[0] * pixel_size,
                    )
                };
            }
        }
    }
}

impl MemBase {
    pub fn new_buffer(
        context: Arc<Context>,
        flags: cl_mem_flags,
        size: usize,
        mut host_ptr: *mut c_void,
        props: Properties<cl_mem_properties>,
    ) -> CLResult<Arc<Buffer>> {
        let res_type = if bit_check(flags, CL_MEM_ALLOC_HOST_PTR) {
            ResourceType::Staging
        } else {
            ResourceType::Normal
        };

        let buffer = context.create_buffer(
            size,
            host_ptr,
            bit_check(flags, CL_MEM_COPY_HOST_PTR),
            res_type,
        )?;

        // We can only keep the host_ptr when `CL_MEM_USE_HOST_PTR` is set.
        if !bit_check(flags, CL_MEM_USE_HOST_PTR) {
            host_ptr = ptr::null_mut()
        }

        let alloc = Allocation::new(buffer, 0, host_ptr);
        Ok(Arc::new(Buffer {
            base: Self {
                base: CLObjectBase::new(RusticlTypes::Buffer),
                context: context,
                mem_type: CL_MEM_OBJECT_BUFFER,
                flags: flags,
                size: size,
                props: props,
                gl_obj: None,
                cbs: Mutex::new(Vec::new()),
                alloc: alloc,
            },
            maps: Mutex::new(TrackedPointers::new()),
        }))
    }

    pub fn new_sub_buffer(
        parent: Arc<Buffer>,
        flags: cl_mem_flags,
        offset: usize,
        size: usize,
    ) -> Arc<Buffer> {
        Arc::new(Buffer {
            base: Self {
                base: CLObjectBase::new(RusticlTypes::Buffer),
                context: Arc::clone(&parent.context),
                mem_type: CL_MEM_OBJECT_BUFFER,
                flags: flags,
                size: size,
                props: Properties::default(),
                gl_obj: None,
                cbs: Mutex::new(Vec::new()),
                alloc: Allocation::new_sub(Mem::Buffer(parent), offset),
            },
            maps: Mutex::new(TrackedPointers::new()),
        })
    }

    pub fn new_image(
        context: Arc<Context>,
        parent: Option<Mem>,
        flags: cl_mem_flags,
        image_format: &cl_image_format,
        mut image_desc: cl_image_desc,
        image_elem_size: u8,
        mut host_ptr: *mut c_void,
        props: Properties<cl_mem_properties>,
    ) -> CLResult<Arc<Image>> {
        // we have to sanitize the image_desc a little for internal use
        let api_image_desc = image_desc;
        let dims = image_desc.dims();
        let is_array = image_desc.is_array();
        if dims < 3 {
            image_desc.image_depth = 1;
        }
        if dims < 2 {
            image_desc.image_height = 1;
        }
        if !is_array {
            image_desc.image_array_size = 1;
        }

        let res_type = if bit_check(flags, CL_MEM_ALLOC_HOST_PTR) {
            ResourceType::Staging
        } else {
            ResourceType::Normal
        };

        let alloc = if let Some(parent) = parent {
            Allocation::new_sub(parent, 0)
        } else {
            let mut texture = context.create_texture(
                &image_desc,
                image_format,
                host_ptr,
                bit_check(flags, CL_MEM_COPY_HOST_PTR),
                res_type,
            );

            // if we error allocating a Staging resource, just try with normal as
            // `CL_MEM_ALLOC_HOST_PTR` is just a performance hint.
            if res_type == ResourceType::Staging && texture.is_err() {
                texture = context.create_texture(
                    &image_desc,
                    image_format,
                    host_ptr,
                    bit_check(flags, CL_MEM_COPY_HOST_PTR),
                    ResourceType::Normal,
                )
            }

            // We can only keep the host_ptr when `CL_MEM_USE_HOST_PTR` is set.
            if !bit_check(flags, CL_MEM_USE_HOST_PTR) {
                host_ptr = ptr::null_mut()
            }

            Allocation::new(texture?, 0, host_ptr)
        };

        let pipe_format = image_format.to_pipe_format().unwrap();
        Ok(Arc::new(Image {
            base: Self {
                base: CLObjectBase::new(RusticlTypes::Image),
                context: context,
                mem_type: image_desc.image_type,
                flags: flags,
                size: image_desc.pixels() * image_format.pixel_size().unwrap() as usize,
                props: props,
                gl_obj: None,
                cbs: Mutex::new(Vec::new()),
                alloc: alloc,
            },
            image_format: *image_format,
            pipe_format: pipe_format,
            image_desc: api_image_desc,
            image_elem_size: image_elem_size,
            maps: Mutex::new(TrackedPointers::new()),
        }))
    }

    pub fn arc_from_raw(ptr: cl_mem) -> CLResult<Mem> {
        let mem = Self::ref_from_raw(ptr)?;
        match mem.base.get_type()? {
            RusticlTypes::Buffer => Ok(Mem::Buffer(Buffer::arc_from_raw(ptr)?)),
            RusticlTypes::Image => Ok(Mem::Image(Image::arc_from_raw(ptr)?)),
            _ => Err(CL_INVALID_MEM_OBJECT),
        }
    }

    pub fn arcs_from_arr(objs: *const cl_mem, count: u32) -> CLResult<Vec<Mem>> {
        let count = count as usize;
        let mut res = Vec::with_capacity(count);
        for i in 0..count {
            res.push(Self::arc_from_raw(unsafe { *objs.add(i) })?);
        }
        Ok(res)
    }

    pub fn from_gl(
        context: Arc<Context>,
        flags: cl_mem_flags,
        gl_export_manager: &GLExportManager,
    ) -> CLResult<cl_mem> {
        let export_in = &gl_export_manager.export_in;
        let export_out = &gl_export_manager.export_out;

        let (mem_type, gl_object_type) = target_from_gl(export_in.target)?;
        let gl_mem_props = gl_export_manager.get_gl_mem_props()?;

        // Handle Buffers
        let (image_format, pipe_format, rusticl_type) = if gl_export_manager.is_gl_buffer() {
            (
                cl_image_format::default(),
                pipe_format::PIPE_FORMAT_NONE,
                RusticlTypes::Buffer,
            )
        } else {
            let image_format =
                format_from_gl(export_out.internal_format).ok_or(CL_OUT_OF_HOST_MEMORY)?;
            (
                image_format,
                image_format.to_pipe_format().unwrap(),
                RusticlTypes::Image,
            )
        };

        let imported_gl_tex = context.import_gl_buffer(
            export_out.dmabuf_fd as u32,
            export_out.modifier,
            mem_type,
            export_in.target,
            image_format,
            gl_mem_props.clone(),
        )?;

        // Cube maps faces are not linear in memory, so copy all contents
        // of desired face into a 2D image and copy it back after gl release.
        let (shadow_map, texture) = if is_cube_map_face(export_in.target) {
            let shadow = create_shadow_slice(&imported_gl_tex, image_format)?;

            let mut res_map = HashMap::new();
            shadow
                .iter()
                .map(|(k, v)| {
                    let gl_res = Arc::clone(imported_gl_tex.get(k).unwrap());
                    res_map.insert(Arc::clone(v), gl_res);
                })
                .for_each(drop);

            (Some(res_map), shadow)
        } else {
            (None, imported_gl_tex)
        };

        // it's kinda not supported, but we want to know if anything actually hits this as it's
        // certainly not tested by the CL CTS.
        if mem_type != CL_MEM_OBJECT_BUFFER {
            assert_eq!(gl_mem_props.offset, 0);
        }

        let base = Self {
            base: CLObjectBase::new(rusticl_type),
            context: context,
            mem_type: mem_type,
            flags: flags,
            size: gl_mem_props.size(),
            props: Properties::default(),
            gl_obj: Some(GLObject {
                gl_object_target: gl_export_manager.export_in.target,
                gl_object_type: gl_object_type,
                gl_object_name: export_in.obj,
                shadow_map: shadow_map,
            }),
            cbs: Mutex::new(Vec::new()),
            alloc: Allocation::new(texture, gl_mem_props.offset as usize, ptr::null_mut()),
        };

        Ok(if rusticl_type == RusticlTypes::Buffer {
            Arc::new(Buffer {
                base: base,
                maps: Mutex::new(TrackedPointers::new()),
            })
            .into_cl()
        } else {
            Arc::new(Image {
                base: base,
                image_format: image_format,
                pipe_format: pipe_format,
                image_desc: cl_image_desc {
                    image_type: mem_type,
                    image_width: gl_mem_props.width as usize,
                    image_height: gl_mem_props.height as usize,
                    image_depth: gl_mem_props.depth as usize,
                    image_array_size: gl_mem_props.array_size as usize,
                    image_row_pitch: 0,
                    image_slice_pitch: 0,
                    num_mip_levels: 1,
                    num_samples: 1,
                    ..Default::default()
                },
                image_elem_size: gl_mem_props.pixel_size,
                maps: Mutex::new(TrackedPointers::new()),
            })
            .into_cl()
        })
    }

    pub fn is_buffer(&self) -> bool {
        self.mem_type == CL_MEM_OBJECT_BUFFER
    }

    /// Checks if the backing memory is actually the same memory object.
    pub fn backing_memory_eq(&self, other: &Self) -> bool {
        self.alloc.backing_resource_eq(&other.alloc)
    }

    // this is kinda bogus, because that won't work with system SVM, but the spec wants us to
    // implement this.
    pub fn is_svm(&self) -> bool {
        self.context
            .find_svm_alloc(self.host_ptr() as usize)
            .is_some()
            && bit_check(self.flags, CL_MEM_USE_HOST_PTR)
    }

    pub fn get_res_for_access(
        &self,
        ctx: &QueueContext,
        rw: RWFlags,
    ) -> CLResult<&Arc<PipeResource>> {
        self.alloc.get_res_for_access(ctx, rw)
    }

    /// Returns the parent memory object or None if self isn't a sub allocated memory object.
    pub fn parent(&self) -> Option<&Mem> {
        match &self.alloc {
            Allocation::SubAlloc(sub) => Some(&sub.mem),
            Allocation::Resource(_) => None,
        }
    }

    pub fn host_ptr(&self) -> *mut c_void {
        self.alloc.host_ptr()
    }

    fn is_pure_user_memory(&self, d: &Device) -> CLResult<bool> {
        // 1Dbuffer objects are weird. The parent memory object can be a host_ptr thing, but we are
        // not allowed to actually return a pointer based on the host_ptr when mapping.
        Ok(self.alloc.is_user_alloc_for_dev(d)? && !self.host_ptr().is_null())
    }

    fn map<T>(
        &self,
        offset: usize,
        layout: Layout,
        writes: bool,
        maps: &Mutex<TrackedPointers<usize, Mapping<T>>>,
        inner: T,
    ) -> CLResult<MutMemoryPtr> {
        let host_ptr = self.host_ptr();
        let ptr = unsafe {
            let ptr = if !host_ptr.is_null() {
                host_ptr.byte_add(offset)
            } else {
                alloc::alloc(layout).cast()
            };

            MutMemoryPtr::from_ptr(ptr)
        };

        match maps.lock().unwrap().entry(ptr.as_ptr() as usize) {
            Entry::Occupied(mut e) => {
                debug_assert!(!host_ptr.is_null());
                e.get_mut().count += 1;
            }
            Entry::Vacant(e) => {
                e.insert(Mapping {
                    layout: layout,
                    writes: writes,
                    ptr: host_ptr.is_null().then_some(ptr),
                    count: 1,
                    inner: inner,
                });
            }
        }

        Ok(ptr)
    }
}

impl Drop for MemBase {
    fn drop(&mut self) {
        let cbs = mem::take(self.cbs.get_mut().unwrap());
        for cb in cbs.into_iter().rev() {
            cb.call(self);
        }
    }
}

impl Buffer {
    fn apply_offset(&self, offset: usize) -> CLResult<usize> {
        self.offset()
            .checked_add(offset)
            .ok_or(CL_OUT_OF_HOST_MEMORY)
    }

    pub fn copy_rect(
        &self,
        dst: &Self,
        ctx: &QueueContext,
        region: &CLVec<usize>,
        src_origin: &CLVec<usize>,
        src_row_pitch: usize,
        src_slice_pitch: usize,
        dst_origin: &CLVec<usize>,
        dst_row_pitch: usize,
        dst_slice_pitch: usize,
    ) -> CLResult<()> {
        let src_offset = CLVec::calc_offset(src_origin, [1, src_row_pitch, src_slice_pitch]);
        let mut src_offset = self.apply_offset(src_offset)?;
        let src_res = self.get_res_for_access(ctx, RWFlags::RD)?;

        let dst_offset = CLVec::calc_offset(dst_origin, [1, dst_row_pitch, dst_slice_pitch]);
        let mut dst_offset = self.apply_offset(dst_offset)?;
        let dst_res = dst.get_res_for_access(ctx, RWFlags::WR)?;

        if src_row_pitch == dst_row_pitch && region[1] == src_row_pitch {
            let slice_size = (region[0] * region[1]).try_into_with_err(CL_OUT_OF_RESOURCES)?;
            for _ in 0..region[2] {
                ctx.resource_copy_buffer(
                    src_res,
                    src_offset as i32,
                    dst_res,
                    dst_offset as u32,
                    slice_size,
                );

                src_offset += src_slice_pitch;
                dst_offset += dst_slice_pitch;
            }
        } else {
            let row_size = region[0].try_into_with_err(CL_OUT_OF_RESOURCES)?;
            for _ in 0..region[2] {
                for _ in 0..region[1] {
                    ctx.resource_copy_buffer(
                        src_res,
                        src_offset as i32,
                        dst_res,
                        dst_offset as u32,
                        row_size,
                    );

                    src_offset += src_row_pitch;
                    dst_offset += dst_row_pitch;
                }

                src_offset += src_slice_pitch - (src_row_pitch * region[1]);
                dst_offset += dst_slice_pitch - (dst_row_pitch * region[1]);
            }
        }

        Ok(())
    }

    pub fn copy_to_buffer(
        &self,
        ctx: &QueueContext,
        dst: &Buffer,
        src_offset: usize,
        dst_offset: usize,
        size: usize,
    ) -> CLResult<()> {
        let src_res = self.get_res_for_access(ctx, RWFlags::RD)?;
        let dst_res = dst.get_res_for_access(ctx, RWFlags::WR)?;
        let size = size.try_into().map_err(|_| CL_OUT_OF_HOST_MEMORY)?;
        let src_offset = self
            .apply_offset(src_offset)?
            .try_into_with_err(CL_OUT_OF_HOST_MEMORY)?;
        let dst_offset = dst
            .apply_offset(dst_offset)?
            .try_into_with_err(CL_OUT_OF_HOST_MEMORY)?;

        ctx.resource_copy_buffer(src_res, src_offset, dst_res, dst_offset, size);
        Ok(())
    }

    pub fn copy_to_image(
        &self,
        ctx: &QueueContext,
        dst: &Image,
        src_offset: usize,
        mut dst_origin: CLVec<usize>,
        region: &CLVec<usize>,
    ) -> CLResult<()> {
        let bpp = dst.image_format.pixel_size().unwrap().into();
        let src_pitch = [bpp, bpp * region[0], bpp * region[0] * region[1]];

        // If image is created from a buffer do a simple rect copy.
        if let Some(Mem::Buffer(buffer)) = dst.parent() {
            // need to update the dst origin to account for the pixel size.
            dst_origin[0] *= bpp;
            return self.copy_rect(
                buffer,
                ctx,
                region,
                &CLVec::new([src_offset, 0, 0]),
                src_pitch[1],
                src_pitch[2],
                &dst_origin,
                dst.image_desc.row_pitch()? as usize,
                dst.image_desc.slice_pitch(),
            );
        }

        let size = CLVec::calc_size(region, src_pitch);
        let src_offset = self.apply_offset(src_offset)?;
        let tx_src = self.tx(ctx, src_offset, size, RWFlags::RD)?;
        let tx_dst = dst.tx_image(
            ctx,
            &create_pipe_box(dst_origin, *region, dst.mem_type)?,
            RWFlags::WR,
        )?;

        let dst_pitch = [1, tx_dst.row_pitch() as usize, tx_dst.slice_pitch()];

        // Those pitch values cannot have 0 value in its coordinates
        debug_assert!(src_pitch[0] != 0 && src_pitch[1] != 0 && src_pitch[2] != 0);
        debug_assert!(dst_pitch[0] != 0 && dst_pitch[1] != 0 && dst_pitch[2] != 0);

        perf_warning!("clEnqueueCopyBufferToImage stalls the GPU");

        sw_copy(
            tx_src.ptr(),
            tx_dst.ptr(),
            region,
            &CLVec::default(),
            src_pitch[1],
            src_pitch[2],
            &CLVec::default(),
            dst_pitch[1],
            dst_pitch[2],
            bpp as u8,
        );
        Ok(())
    }

    pub fn fill(
        &self,
        ctx: &QueueContext,
        pattern: &[u8],
        offset: usize,
        size: usize,
    ) -> CLResult<()> {
        let offset = self.apply_offset(offset)?;
        let res = self.get_res_for_access(ctx, RWFlags::WR)?;
        ctx.clear_buffer(
            res,
            pattern,
            offset.try_into_with_err(CL_OUT_OF_HOST_MEMORY)?,
            size.try_into_with_err(CL_OUT_OF_HOST_MEMORY)?,
        );
        Ok(())
    }

    fn is_mapped_ptr(&self, ptr: *mut c_void) -> bool {
        let mut maps = self.maps.lock().unwrap();
        let entry = maps.entry(ptr as usize);
        matches!(entry, Entry::Occupied(entry) if entry.get().count > 0)
    }

    pub fn map(&self, size: usize, offset: usize, writes: bool) -> CLResult<MutMemoryPtr> {
        let layout =
            unsafe { Layout::from_size_align_unchecked(size, size_of::<[cl_ulong; 16]>()) };
        self.base.map(
            offset,
            layout,
            writes,
            &self.maps,
            BufferMapping { offset: offset },
        )
    }

    pub fn offset(&self) -> usize {
        self.alloc.offset()
    }

    pub fn read(
        &self,
        ctx: &QueueContext,
        offset: usize,
        ptr: MutMemoryPtr,
        size: usize,
    ) -> CLResult<()> {
        let ptr = ptr.as_ptr();
        let tx = self.tx(ctx, offset, size, RWFlags::RD)?;

        perf_warning!("clEnqueueReadBuffer and clEnqueueMapBuffer stall the GPU");

        unsafe {
            ptr::copy(tx.ptr(), ptr, size);
        }

        Ok(())
    }

    pub fn read_rect(
        &self,
        dst: MutMemoryPtr,
        ctx: &QueueContext,
        region: &CLVec<usize>,
        src_origin: &CLVec<usize>,
        src_row_pitch: usize,
        src_slice_pitch: usize,
        dst_origin: &CLVec<usize>,
        dst_row_pitch: usize,
        dst_slice_pitch: usize,
    ) -> CLResult<()> {
        let dst = dst.as_ptr();
        let (offset, size) =
            CLVec::calc_offset_size(src_origin, region, [1, src_row_pitch, src_slice_pitch]);
        let tx = self.tx(ctx, offset, size, RWFlags::RD)?;

        perf_warning!("clEnqueueReadBufferRect stalls the GPU");

        sw_copy(
            tx.ptr(),
            dst,
            region,
            &CLVec::default(),
            src_row_pitch,
            src_slice_pitch,
            dst_origin,
            dst_row_pitch,
            dst_slice_pitch,
            1,
        );

        Ok(())
    }

    pub fn sync_map(&self, ctx: &QueueContext, ptr: MutMemoryPtr) -> CLResult<()> {
        let maps = self.maps.lock().unwrap();
        let Some(mapping) = maps.find_alloc_precise(ptr.as_ptr() as usize) else {
            return Err(CL_INVALID_VALUE);
        };

        // in this case we only need to migrate to the device if the data is located on a device not
        // having a userptr allocation.
        if self.is_pure_user_memory(ctx.dev)? {
            let rw = if mapping.writes {
                RWFlags::RW
            } else {
                RWFlags::RD
            };

            let _ = self.get_res_for_access(ctx, rw)?;
            return Ok(());
        }

        self.read(ctx, mapping.offset, ptr, mapping.size())
    }

    pub fn sync_unmap(&self, ctx: &QueueContext, ptr: MutMemoryPtr) -> CLResult<()> {
        // no need to update
        if self.is_pure_user_memory(ctx.dev)? {
            return Ok(());
        }

        match self.maps.lock().unwrap().entry(ptr.as_ptr() as usize) {
            Entry::Vacant(_) => Err(CL_INVALID_VALUE),
            Entry::Occupied(entry) => {
                let mapping = entry.get();

                if mapping.writes {
                    self.write(ctx, mapping.offset, ptr.into(), mapping.size())?;
                }

                // only remove if the mapping wasn't reused in the meantime
                if mapping.count == 0 {
                    entry.remove();
                }

                Ok(())
            }
        }
    }

    fn tx<'a>(
        &self,
        ctx: &'a QueueContext,
        offset: usize,
        size: usize,
        rw: RWFlags,
    ) -> CLResult<PipeTransfer<'a>> {
        let offset = self.apply_offset(offset)?;
        let r = self.get_res_for_access(ctx, rw)?;

        ctx.buffer_map(
            r,
            offset.try_into_with_err(CL_OUT_OF_HOST_MEMORY)?,
            size.try_into_with_err(CL_OUT_OF_HOST_MEMORY)?,
            rw,
        )
        .ok_or(CL_OUT_OF_RESOURCES)
    }

    pub fn unmap(&self, ptr: MutMemoryPtr) -> CLResult<bool> {
        match self.maps.lock().unwrap().entry(ptr.as_ptr() as usize) {
            Entry::Vacant(_) => Err(CL_INVALID_VALUE),
            Entry::Occupied(mut entry) => {
                let entry = entry.get_mut();
                debug_assert!(entry.count > 0);
                entry.count -= 1;
                Ok(entry.count == 0)
            }
        }
    }

    pub fn write(
        &self,
        ctx: &QueueContext,
        offset: usize,
        ptr: ConstMemoryPtr,
        size: usize,
    ) -> CLResult<()> {
        let ptr = ptr.as_ptr();
        let offset = self.apply_offset(offset)?;
        let r = self.get_res_for_access(ctx, RWFlags::WR)?;

        perf_warning!("clEnqueueWriteBuffer and clEnqueueUnmapMemObject might stall the GPU");

        ctx.buffer_subdata(
            r,
            offset.try_into_with_err(CL_OUT_OF_HOST_MEMORY)?,
            ptr,
            size.try_into_with_err(CL_OUT_OF_HOST_MEMORY)?,
        );
        Ok(())
    }

    pub fn write_rect(
        &self,
        src: ConstMemoryPtr,
        ctx: &QueueContext,
        region: &CLVec<usize>,
        src_origin: &CLVec<usize>,
        src_row_pitch: usize,
        src_slice_pitch: usize,
        dst_origin: &CLVec<usize>,
        dst_row_pitch: usize,
        dst_slice_pitch: usize,
    ) -> CLResult<()> {
        let mut src = src.as_ptr();
        let src_offset = CLVec::calc_offset(src_origin, [1, src_row_pitch, src_slice_pitch]);
        src = unsafe { src.byte_add(src_offset) };

        let dst_offset = CLVec::calc_offset(dst_origin, [1, dst_row_pitch, dst_slice_pitch]);
        let mut dst_offset = self.apply_offset(dst_offset)?;
        let dst_res = self.get_res_for_access(ctx, RWFlags::WR)?;

        if src_row_pitch == dst_row_pitch && region[1] == src_row_pitch {
            let slice_size = (region[0] * region[1]).try_into_with_err(CL_OUT_OF_RESOURCES)?;
            for _ in 0..region[2] {
                ctx.buffer_subdata(dst_res, dst_offset as u32, src, slice_size);

                src = unsafe { src.byte_add(src_slice_pitch) };
                dst_offset += dst_slice_pitch;
            }
        } else {
            let row_size = region[0].try_into_with_err(CL_OUT_OF_RESOURCES)?;
            for _ in 0..region[2] {
                for _ in 0..region[1] {
                    ctx.buffer_subdata(dst_res, dst_offset as u32, src, row_size);

                    src = unsafe { src.byte_add(src_row_pitch) };
                    dst_offset += dst_row_pitch;
                }

                src = unsafe { src.byte_add(src_slice_pitch - (src_row_pitch * region[1])) };
                dst_offset += dst_slice_pitch - (dst_row_pitch * region[1]);
            }
        }

        Ok(())
    }
}

impl Image {
    pub fn copy_to_buffer(
        &self,
        ctx: &QueueContext,
        dst: &Buffer,
        mut src_origin: CLVec<usize>,
        dst_offset: usize,
        region: &CLVec<usize>,
    ) -> CLResult<()> {
        let bpp = self.image_format.pixel_size().unwrap().into();

        // the result is linear without any gaps because it's a plain buffer.
        let dst_pitch = [bpp, bpp * region[0], bpp * region[0] * region[1]];
        let mut dst_origin: CLVec<usize> = [dst_offset, 0, 0].into();

        // if the parent object of this image is a buffer, we can simply do a rect copy between
        // buffers here while taking the bpp into account.
        if let Some(Mem::Buffer(buffer)) = self.parent() {
            let mut region = *region;

            region[0] *= bpp;
            src_origin[0] *= bpp;
            dst_origin[0] *= bpp;

            return buffer.copy_rect(
                dst,
                ctx,
                &region,
                &src_origin,
                self.image_desc.row_pitch()? as usize,
                self.image_desc.slice_pitch(),
                &dst_origin,
                dst_pitch[1],
                dst_pitch[2],
            );
        }

        let tx_src = self.tx_image(
            ctx,
            &create_pipe_box(src_origin, *region, self.mem_type)?,
            RWFlags::RD,
        )?;

        let src_pitch = [1, tx_src.row_pitch() as usize, tx_src.slice_pitch()];
        let (offset, size) = CLVec::calc_offset_size(dst_origin, region, dst_pitch);
        let tx_dst = dst.tx(ctx, offset, size, RWFlags::WR)?;

        // Those pitch values cannot have 0 value in its coordinates
        debug_assert!(src_pitch[0] != 0 && src_pitch[1] != 0 && src_pitch[2] != 0);
        debug_assert!(dst_pitch[0] != 0 && dst_pitch[1] != 0 && dst_pitch[2] != 0);

        perf_warning!("clEnqueueCopyImageToBuffer stalls the GPU");

        sw_copy(
            tx_src.ptr(),
            tx_dst.ptr(),
            region,
            &CLVec::default(),
            src_pitch[1],
            src_pitch[2],
            &CLVec::default(),
            dst_pitch[1],
            dst_pitch[2],
            bpp as u8,
        );
        Ok(())
    }

    pub fn copy_to_image(
        &self,
        ctx: &QueueContext,
        dst: &Image,
        mut src_origin: CLVec<usize>,
        mut dst_origin: CLVec<usize>,
        region: &CLVec<usize>,
    ) -> CLResult<()> {
        let bpp = self.image_format.pixel_size().unwrap().into();
        let src_parent = self.parent();
        let dst_parent = dst.parent();

        let src_pitch = [
            bpp,
            self.image_desc.row_pitch()? as usize,
            self.image_desc.slice_pitch(),
        ];

        let dst_pitch = [
            bpp,
            dst.image_desc.row_pitch()? as usize,
            dst.image_desc.slice_pitch(),
        ];

        // We lower this operation depending on the parent object. Only if both the src and dst are
        // images we use the resource_copy_texture path.
        match (src_parent, dst_parent) {
            (Some(Mem::Buffer(src_buffer)), Some(Mem::Buffer(dst_buffer))) => {
                let mut region = *region;

                region[0] *= bpp;
                src_origin[0] *= bpp;
                dst_origin[0] *= bpp;

                src_buffer.copy_rect(
                    dst_buffer,
                    ctx,
                    &region,
                    &src_origin,
                    src_pitch[1],
                    src_pitch[2],
                    &dst_origin,
                    dst_pitch[1],
                    dst_pitch[2],
                )
            }
            (Some(Mem::Buffer(src)), _) => src.copy_to_image(
                ctx,
                dst,
                CLVec::calc_offset(src_origin, src_pitch),
                dst_origin,
                region,
            ),
            (_, Some(Mem::Buffer(dst))) => self.copy_to_buffer(
                ctx,
                dst,
                src_origin,
                CLVec::calc_offset(dst_origin, dst_pitch),
                region,
            ),
            _ => {
                let src_res = self.get_res_for_access(ctx, RWFlags::RD)?;
                let dst_res = dst.get_res_for_access(ctx, RWFlags::WR)?;
                let bx = create_pipe_box(src_origin, *region, self.mem_type)?;
                let mut dst_origin: [u32; 3] = dst_origin.try_into()?;

                if self.mem_type == CL_MEM_OBJECT_IMAGE1D_ARRAY {
                    (dst_origin[1], dst_origin[2]) = (dst_origin[2], dst_origin[1]);
                }

                ctx.resource_copy_texture(src_res, dst_res, &dst_origin, &bx);
                Ok(())
            }
        }
    }

    pub fn fill(
        &self,
        ctx: &QueueContext,
        pattern: [u32; 4],
        origin: &CLVec<usize>,
        region: &CLVec<usize>,
    ) -> CLResult<()> {
        let res = self.get_res_for_access(ctx, RWFlags::WR)?;

        // make sure we allocate multiples of 4 bytes so drivers don't read out of bounds or
        // unaligned.
        let pixel_size: usize = self.image_format.pixel_size().unwrap().into();
        let mut new_pattern: Vec<u32> = vec![0; pixel_size.div_ceil(size_of::<u32>())];

        // SAFETY: pointers have to be valid for read/writes of exactly one pixel of their
        // respective format.
        // `new_pattern` has the correct size due to the `size` above.
        // `pattern` is validated through the CL API and allows undefined behavior if not followed
        // by CL API rules.
        unsafe {
            util_format_pack_rgba(
                self.pipe_format,
                new_pattern.as_mut_ptr().cast(),
                pattern.as_ptr().cast(),
                1,
            );
        }

        // If image is created from a buffer, use clear_image_buffer instead
        if self.is_parent_buffer() {
            let strides = (
                self.image_desc.row_pitch()? as usize,
                self.image_desc.slice_pitch(),
            );
            ctx.clear_image_buffer(res, &new_pattern, origin, region, strides, pixel_size);
        } else {
            let bx = create_pipe_box(*origin, *region, self.mem_type)?;
            ctx.clear_texture(res, &new_pattern, &bx);
        }

        Ok(())
    }

    fn is_mapped_ptr(&self, ptr: *mut c_void) -> bool {
        let mut maps = self.maps.lock().unwrap();
        let entry = maps.entry(ptr as usize);
        matches!(entry, Entry::Occupied(entry) if entry.get().count > 0)
    }

    pub fn is_parent_buffer(&self) -> bool {
        matches!(self.parent(), Some(Mem::Buffer(_)))
    }

    pub fn map(
        &self,
        origin: CLVec<usize>,
        region: CLVec<usize>,
        row_pitch: &mut usize,
        slice_pitch: &mut usize,
        writes: bool,
    ) -> CLResult<MutMemoryPtr> {
        let pixel_size = self.image_format.pixel_size().unwrap() as usize;

        *row_pitch = self.image_desc.row_pitch()? as usize;
        *slice_pitch = self.image_desc.slice_pitch();

        let offset = CLVec::calc_offset(origin, [pixel_size, *row_pitch, *slice_pitch]);

        // From the CL Spec:
        //
        //   The pointer returned maps a 1D, 2D or 3D region starting at origin and is at least
        //   region[0] pixels in size for a 1D image, 1D image buffer or 1D image array,
        //   (image_row_pitch × region[1]) pixels in size for a 2D image or 2D image array, and
        //   (image_slice_pitch × region[2]) pixels in size for a 3D image. The result of a memory
        //   access outside this region is undefined.
        //
        // It's not guaranteed that the row_pitch is taken into account for 1D images, but the CL
        // CTS relies on this behavior.
        //
        // Also note, that the spec wording is wrong in regards to arrays, which need to take the
        // image_slice_pitch into account.
        let size = if self.image_desc.is_array() || self.image_desc.dims() == 3 {
            debug_assert_ne!(*slice_pitch, 0);
            // the slice count is in region[1] for 1D array images
            if self.mem_type == CL_MEM_OBJECT_IMAGE1D_ARRAY {
                region[1] * *slice_pitch
            } else {
                region[2] * *slice_pitch
            }
        } else {
            debug_assert_ne!(*row_pitch, 0);
            region[1] * *row_pitch
        };

        let layout;
        unsafe {
            layout = Layout::from_size_align_unchecked(size, size_of::<[u32; 4]>());
        }

        self.base.map(
            offset,
            layout,
            writes,
            &self.maps,
            ImageMapping {
                origin: origin,
                region: region,
            },
        )
    }

    fn pipe_image_host_access(&self) -> u16 {
        // those flags are all mutually exclusive
        (if bit_check(self.flags, CL_MEM_HOST_READ_ONLY) {
            PIPE_IMAGE_ACCESS_READ
        } else if bit_check(self.flags, CL_MEM_HOST_WRITE_ONLY) {
            PIPE_IMAGE_ACCESS_WRITE
        } else if bit_check(self.flags, CL_MEM_HOST_NO_ACCESS) {
            0
        } else {
            PIPE_IMAGE_ACCESS_READ_WRITE
        }) as u16
    }

    pub fn read(
        &self,
        dst: MutMemoryPtr,
        ctx: &QueueContext,
        region: &CLVec<usize>,
        src_origin: &CLVec<usize>,
        dst_row_pitch: usize,
        dst_slice_pitch: usize,
    ) -> CLResult<()> {
        let dst = dst.as_ptr();
        let pixel_size = self.image_format.pixel_size().unwrap();

        let tx;
        let src_row_pitch;
        let src_slice_pitch;
        if let Some(Mem::Buffer(buffer)) = self.parent() {
            src_row_pitch = self.image_desc.image_row_pitch;
            src_slice_pitch = self.image_desc.image_slice_pitch;

            let (offset, size) = CLVec::calc_offset_size(
                src_origin,
                region,
                [pixel_size.into(), src_row_pitch, src_slice_pitch],
            );

            tx = buffer.tx(ctx, offset, size, RWFlags::RD)?;
        } else {
            let bx = create_pipe_box(*src_origin, *region, self.mem_type)?;
            tx = self.tx_image(ctx, &bx, RWFlags::RD)?;
            src_row_pitch = tx.row_pitch() as usize;
            src_slice_pitch = tx.slice_pitch();
        };

        perf_warning!("clEnqueueReadImage and clEnqueueMapImage stall the GPU");

        sw_copy(
            tx.ptr(),
            dst,
            region,
            &CLVec::default(),
            src_row_pitch,
            src_slice_pitch,
            &CLVec::default(),
            dst_row_pitch,
            dst_slice_pitch,
            pixel_size,
        );

        Ok(())
    }

    pub fn sync_map(&self, ctx: &QueueContext, ptr: MutMemoryPtr) -> CLResult<()> {
        let maps = self.maps.lock().unwrap();
        let Some(mapping) = maps.find_alloc_precise(ptr.as_ptr() as usize) else {
            return Err(CL_INVALID_VALUE);
        };

        // in this case we only need to migrate to the device if the data is located on a device not
        // having a userptr allocation.
        if self.is_pure_user_memory(ctx.dev)? {
            let rw = if mapping.writes {
                RWFlags::RW
            } else {
                RWFlags::RD
            };

            let _ = self.get_res_for_access(ctx, rw)?;
            return Ok(());
        }

        let row_pitch = self.image_desc.row_pitch()? as usize;
        let slice_pitch = self.image_desc.slice_pitch();

        self.read(
            ptr,
            ctx,
            &mapping.region,
            &mapping.origin,
            row_pitch,
            slice_pitch,
        )
    }

    pub fn sync_unmap(&self, ctx: &QueueContext, ptr: MutMemoryPtr) -> CLResult<()> {
        // no need to update
        if self.is_pure_user_memory(ctx.dev)? {
            return Ok(());
        }

        match self.maps.lock().unwrap().entry(ptr.as_ptr() as usize) {
            Entry::Vacant(_) => Err(CL_INVALID_VALUE),
            Entry::Occupied(entry) => {
                let mapping = entry.get();
                let row_pitch = self.image_desc.row_pitch()? as usize;
                let slice_pitch = self.image_desc.slice_pitch();

                if mapping.writes {
                    self.write(
                        ptr.into(),
                        ctx,
                        &mapping.region,
                        row_pitch,
                        slice_pitch,
                        &mapping.origin,
                    )?;
                }

                // only remove if the mapping wasn't reused in the meantime
                if mapping.count == 0 {
                    entry.remove();
                }

                Ok(())
            }
        }
    }

    fn tx_image<'a>(
        &self,
        ctx: &'a QueueContext,
        bx: &pipe_box,
        rw: RWFlags,
    ) -> CLResult<PipeTransfer<'a>> {
        let r = self.get_res_for_access(ctx, rw)?;
        ctx.texture_map(r, bx, rw).ok_or(CL_OUT_OF_RESOURCES)
    }

    pub fn unmap(&self, ptr: MutMemoryPtr) -> CLResult<bool> {
        match self.maps.lock().unwrap().entry(ptr.as_ptr() as usize) {
            Entry::Vacant(_) => Err(CL_INVALID_VALUE),
            Entry::Occupied(mut entry) => {
                let entry = entry.get_mut();
                debug_assert!(entry.count > 0);
                entry.count -= 1;
                Ok(entry.count == 0)
            }
        }
    }

    pub fn write(
        &self,
        src: ConstMemoryPtr,
        ctx: &QueueContext,
        region: &CLVec<usize>,
        src_row_pitch: usize,
        mut src_slice_pitch: usize,
        dst_origin: &CLVec<usize>,
    ) -> CLResult<()> {
        // texture_subdata most likely maps the resource anyway
        perf_warning!("clEnqueueWriteImage and clEnqueueUnmapMemObject stall the GPU");

        if let Some(Mem::Buffer(buffer)) = self.parent() {
            let bpp: usize = self.image_format.pixel_size().unwrap().into();

            let mut region = *region;
            let mut dst_origin = *dst_origin;

            dst_origin[0] *= bpp;
            region[0] *= bpp;

            buffer.write_rect(
                src,
                ctx,
                &region,
                &CLVec::default(),
                src_row_pitch,
                src_slice_pitch,
                &dst_origin,
                self.image_desc.image_row_pitch,
                self.image_desc.image_slice_pitch,
            )
        } else {
            let src = src.as_ptr();
            let res = self.get_res_for_access(ctx, RWFlags::WR)?;
            let bx = create_pipe_box(*dst_origin, *region, self.mem_type)?;

            if self.mem_type == CL_MEM_OBJECT_IMAGE1D_ARRAY {
                src_slice_pitch = src_row_pitch;
            }

            ctx.texture_subdata(
                res,
                &bx,
                src,
                src_row_pitch.try_into_with_err(CL_OUT_OF_HOST_MEMORY)?,
                src_slice_pitch,
            );
            Ok(())
        }
    }

    /// Creates metadata when an 2D image or sampler view is created over a buffer resource.
    fn buffer_2d_info(&self) -> CLResult<AppImgInfo> {
        Ok(AppImgInfo::new(
            self.image_desc.row_pitch()? / self.image_elem_size as u32,
            self.image_desc.width()?,
            self.image_desc.height()?,
        ))
    }

    pub fn sampler_view<'c>(&self, ctx: &'c QueueContext) -> CLResult<PipeSamplerView<'c, '_>> {
        let res = self.get_res_for_access(ctx, RWFlags::RD)?;

        let template = if res.is_buffer() && self.mem_type == CL_MEM_OBJECT_IMAGE2D {
            res.pipe_sampler_view_template_2d_buffer(self.pipe_format, &self.buffer_2d_info()?)
        } else if res.is_buffer() {
            // we need to pass in the size of the buffer, not the width.
            let size = self.size.try_into_with_err(CL_OUT_OF_RESOURCES)?;
            res.pipe_sampler_view_template_1d_buffer(self.pipe_format, size)
        } else {
            res.pipe_sampler_view_template()
        };

        PipeSamplerView::new(ctx, res, &template).ok_or(CL_OUT_OF_HOST_MEMORY)
    }

    pub fn image_view(&self, ctx: &QueueContext, read_write: bool) -> CLResult<PipeImageView> {
        let rw = if read_write { RWFlags::RW } else { RWFlags::WR };

        let res = self.get_res_for_access(ctx, rw)?;
        if res.is_buffer() && self.mem_type == CL_MEM_OBJECT_IMAGE2D {
            Ok(res.pipe_image_view_2d_buffer(
                self.pipe_format,
                read_write,
                self.pipe_image_host_access(),
                &self.buffer_2d_info()?,
            ))
        } else if res.is_buffer() {
            let size = self.size.try_into_with_err(CL_OUT_OF_RESOURCES)?;
            Ok(res.pipe_image_view_1d_buffer(
                self.pipe_format,
                read_write,
                self.pipe_image_host_access(),
                size,
            ))
        } else {
            Ok(res.pipe_image_view(read_write, self.pipe_image_host_access()))
        }
    }
}

pub struct Sampler {
    pub base: CLObjectBase<CL_INVALID_SAMPLER>,
    pub context: Arc<Context>,
    pub normalized_coords: bool,
    pub addressing_mode: cl_addressing_mode,
    pub filter_mode: cl_filter_mode,
    pub props: Properties<cl_sampler_properties>,
}

impl_cl_type_trait!(cl_sampler, Sampler, CL_INVALID_SAMPLER);

impl Sampler {
    pub fn new(
        context: Arc<Context>,
        normalized_coords: bool,
        addressing_mode: cl_addressing_mode,
        filter_mode: cl_filter_mode,
        props: Properties<cl_sampler_properties>,
    ) -> Arc<Sampler> {
        Arc::new(Self {
            base: CLObjectBase::new(RusticlTypes::Sampler),
            context: context,
            normalized_coords: normalized_coords,
            addressing_mode: addressing_mode,
            filter_mode: filter_mode,
            props: props,
        })
    }

    pub fn nir_to_cl(
        addressing_mode: u32,
        filter_mode: u32,
        normalized_coords: u32,
    ) -> (cl_addressing_mode, cl_filter_mode, bool) {
        let addr_mode = match addressing_mode {
            cl_sampler_addressing_mode::SAMPLER_ADDRESSING_MODE_NONE => CL_ADDRESS_NONE,
            cl_sampler_addressing_mode::SAMPLER_ADDRESSING_MODE_CLAMP_TO_EDGE => {
                CL_ADDRESS_CLAMP_TO_EDGE
            }
            cl_sampler_addressing_mode::SAMPLER_ADDRESSING_MODE_CLAMP => CL_ADDRESS_CLAMP,
            cl_sampler_addressing_mode::SAMPLER_ADDRESSING_MODE_REPEAT => CL_ADDRESS_REPEAT,
            cl_sampler_addressing_mode::SAMPLER_ADDRESSING_MODE_REPEAT_MIRRORED => {
                CL_ADDRESS_MIRRORED_REPEAT
            }
            _ => panic!("unknown addressing_mode"),
        };

        let filter = match filter_mode {
            cl_sampler_filter_mode::SAMPLER_FILTER_MODE_NEAREST => CL_FILTER_NEAREST,
            cl_sampler_filter_mode::SAMPLER_FILTER_MODE_LINEAR => CL_FILTER_LINEAR,
            _ => panic!("unknown filter_mode"),
        };

        (addr_mode, filter, normalized_coords != 0)
    }

    pub fn cl_to_pipe(
        (addressing_mode, filter_mode, normalized_coords): (
            cl_addressing_mode,
            cl_filter_mode,
            bool,
        ),
    ) -> pipe_sampler_state {
        let mut res = pipe_sampler_state::default();

        let wrap = match addressing_mode {
            CL_ADDRESS_CLAMP_TO_EDGE => pipe_tex_wrap::PIPE_TEX_WRAP_CLAMP_TO_EDGE,
            CL_ADDRESS_CLAMP => pipe_tex_wrap::PIPE_TEX_WRAP_CLAMP_TO_BORDER,
            CL_ADDRESS_REPEAT => pipe_tex_wrap::PIPE_TEX_WRAP_REPEAT,
            CL_ADDRESS_MIRRORED_REPEAT => pipe_tex_wrap::PIPE_TEX_WRAP_MIRROR_REPEAT,
            // TODO: what's a reasonable default?
            _ => pipe_tex_wrap::PIPE_TEX_WRAP_CLAMP_TO_EDGE,
        };

        let img_filter = match filter_mode {
            CL_FILTER_NEAREST => pipe_tex_filter::PIPE_TEX_FILTER_NEAREST,
            CL_FILTER_LINEAR => pipe_tex_filter::PIPE_TEX_FILTER_LINEAR,
            _ => panic!("unknown filter_mode"),
        };

        res.set_min_img_filter(img_filter);
        res.set_mag_img_filter(img_filter);
        res.set_unnormalized_coords((!normalized_coords).into());
        res.set_wrap_r(wrap);
        res.set_wrap_s(wrap);
        res.set_wrap_t(wrap);

        res
    }

    pub fn pipe(&self) -> pipe_sampler_state {
        Self::cl_to_pipe((
            self.addressing_mode,
            self.filter_mode,
            self.normalized_coords,
        ))
    }
}
