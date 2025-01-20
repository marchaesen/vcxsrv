use crate::api::icd::{ArcedCLObject, CLResult};
use crate::api::types::*;
use crate::core::event::*;
use crate::core::queue::*;

use mesa_rust_util::properties::Properties;
use rusticl_opencl_gen::*;

use std::convert::TryInto;
use std::ffi::{c_void, CStr};
use std::iter::zip;
use std::mem::MaybeUninit;
use std::ops::BitAnd;
use std::sync::Arc;
use std::{cmp, mem};

// TODO: use MaybeUninit::copy_from_slice once stable
pub fn maybe_uninit_copy_from_slice<T>(this: &mut [MaybeUninit<T>], src: &[T])
where
    T: Copy,
{
    // Assert this so we stick as close as possible to MaybeUninit::copy_from_slices behavior.
    debug_assert_eq!(this.len(), src.len());

    for (dest, val) in zip(this, src) {
        *dest = MaybeUninit::new(*val);
    }
}

// TODO: use MaybeUninit::slice_assume_init_ref once stable
pub const unsafe fn slice_assume_init_ref<T>(slice: &[MaybeUninit<T>]) -> &[T] {
    // SAFETY: casting `slice` to a `*const [T]` is safe since the caller guarantees that
    // `slice` is initialized, and `MaybeUninit` is guaranteed to have the same layout as `T`.
    // The pointer obtained is valid since it refers to memory owned by `slice` which is a
    // reference and thus guaranteed to be valid for reads.
    unsafe { &*(slice as *const [_] as *const [T]) }
}

/// Token to make sure that ClInfoValue::write is called
pub struct CLInfoRes {
    _private: (),
}

/// A helper class to simplify implementing so called "info" APIs in OpenCL, e.g. `clGetDeviceInfo`.
///
/// Those APIs generally operate on opaque memory buffers and needs to be interpreted according to
/// the specific query being made. The generic parameter to the input and write functions should
/// always be explicitly specified.
pub struct CLInfoValue<'a> {
    param_value: Option<&'a mut [MaybeUninit<u8>]>,
    param_value_size_ret: Option<&'a mut MaybeUninit<usize>>,
}

impl CLInfoValue<'_> {
    /// # Safety
    /// `param_value` and `param_value_size_ret` need to be valid memory allocations or null.
    /// If `param_value` is not null it needs to point to an allocation of at least
    /// `param_value_size` bytes.
    unsafe fn new(
        param_value_size: usize,
        param_value: *mut c_void,
        param_value_size_ret: *mut usize,
    ) -> CLResult<Self> {
        let param_value_size_ret: *mut MaybeUninit<usize> = param_value_size_ret.cast();
        let param_value = if !param_value.is_null() {
            Some(unsafe { cl_slice::from_raw_parts_mut(param_value.cast(), param_value_size)? })
        } else {
            None
        };

        Ok(Self {
            param_value: param_value,
            param_value_size_ret: unsafe { param_value_size_ret.as_mut() },
        })
    }

    fn finish() -> CLInfoRes {
        CLInfoRes { _private: () }
    }

    /// Used to read from the application provided data.
    pub fn input<T>(&self) -> CLResult<&[MaybeUninit<T>]> {
        if let Some(param_value) = &self.param_value {
            let count = param_value.len() / mem::size_of::<T>();
            unsafe { cl_slice::from_raw_parts(param_value.as_ptr().cast(), count) }
        } else {
            Ok(&[])
        }
    }

    /// Writes the passed in value according to the generic. It is important to pass in the same
    /// or compatible type as stated in the OpenCL specification.
    ///
    /// It also verifies that if a buffer was provided by the application it's big enough to hold
    /// `t` and returns `Err(CL_INVALID_VALUE)` otherwise.
    ///
    /// Type specific details:
    ///  - Compatible with C arrays are `T` (if only one element is to be returned), `Vec<T>` or `&[T]`
    ///    types.
    ///  - Compatible with C strings are all basic Rust string types.
    ///  - `bool`s are automatically converted to `cl_bool`.
    ///  - For queries which can return no data, `Option<T>` can be used.
    ///  - For C property arrays (0-terminated arrays of `T`) the
    ///    [mesa_rust_util::properties::Properties] type can be used.
    ///
    /// All types implementing [CLProp] are supported.
    pub fn write<T: CLProp>(self, t: T) -> CLResult<CLInfoRes> {
        let count = t.count();
        let bytes = count * mem::size_of::<T::Output>();

        // param_value is a pointer to memory where the appropriate result being queried is
        // returned. If param_value is NULL, it is ignored.
        if let Some(param_value) = self.param_value {
            // CL_INVALID_VALUE [...] if size in bytes specified by param_value_size is < size of
            // return type as specified in the Context Attributes table and param_value is not a
            // NULL value.
            if param_value.len() < bytes {
                return Err(CL_INVALID_VALUE);
            }

            // SAFETY: Casting between types wrapped with MaybeUninit is fine, because it's up to
            //         the caller to decide if it sound to read from it. Also the count passed in is
            //         just the type adjusted size of the `param_value` slice.
            let out =
                unsafe { cl_slice::from_raw_parts_mut(param_value.as_mut_ptr().cast(), count)? };

            t.write_to(out);
        }

        // param_value_size_ret returns the actual size in bytes of data being queried by
        // param_name. If param_value_size_ret is NULL, it is ignored.
        if let Some(param_value_size_ret) = self.param_value_size_ret {
            param_value_size_ret.write(bytes);
        }

        Ok(Self::finish())
    }

    /// Returns the size of the buffer to the application it needs to provide in order to
    /// successfully execute the query.
    ///
    /// Some queries simply provide pointers where the implementation needs to write data to, e.g.
    /// `CL_PROGRAM_BINARIES`. In that case it's meaningless to write back the same pointers. This
    /// function can be used to skip those writes.
    pub fn write_len_only<T: CLProp>(self, len: usize) -> CLResult<CLInfoRes> {
        let bytes = len * mem::size_of::<T::Output>();

        // param_value_size_ret returns the actual size in bytes of data being queried by
        // param_name. If param_value_size_ret is NULL, it is ignored.
        if let Some(param_value_size_ret) = self.param_value_size_ret {
            param_value_size_ret.write(bytes);
        }

        Ok(Self::finish())
    }

    /// Similar to `write` with the only difference that instead of a precomputed value, this can
    /// take an iterator instead.
    ///
    /// This is useful when the data to be returned isn't cheap to compute or not already available.
    ///
    /// When the application only asks for the size of the result, the iterator isn't advanced at
    /// all, only its length is used.
    pub fn write_iter<T: CLProp<Output = T> + Copy>(
        self,
        iter: impl ExactSizeIterator<Item = T>,
    ) -> CLResult<CLInfoRes> {
        let count = iter.len();
        let bytes = count * mem::size_of::<T::Output>();

        // param_value is a pointer to memory where the appropriate result being queried is
        // returned. If param_value is NULL, it is ignored.
        if let Some(param_value) = self.param_value {
            // CL_INVALID_VALUE [...] if size in bytes specified by param_value_size is < size of
            // return type as specified in the Context Attributes table and param_value is not a
            // NULL value.
            if param_value.len() < bytes {
                return Err(CL_INVALID_VALUE);
            }

            let out =
                unsafe { cl_slice::from_raw_parts_mut(param_value.as_mut_ptr().cast(), count)? };

            for (item, out) in zip(iter, out.iter_mut()) {
                *out = item;
            }
        }

        // param_value_size_ret returns the actual size in bytes of data being queried by
        // param_name. If param_value_size_ret is NULL, it is ignored.
        if let Some(param_value_size_ret) = self.param_value_size_ret {
            param_value_size_ret.write(bytes);
        }

        Ok(Self::finish())
    }
}

/// # Safety
///
/// This trait helps implementing various OpenCL Query APIs, however care have to be taken that the
/// `query` implementation implements the corresponding query according to the OpenCL specification.
///
/// Queries which don't have a size known at compile time are expected to be called twice:
///  1. To ask the implementation of how much data will be returned. The application uses this
///     to allocate enough memory to be passed into the next call.
///  2. To actually execute the query.
///
/// This trait abstracts this pattern properly away to make it easier to implement it.
///
/// [CLInfoValue] contains helper functions to read and write data behind opaque buffers, the
/// applications using those OpenCL queries expect the implementation to behave accordingly.
///
/// It is advised to explicitly specify the types of [CLInfoValue::input] and the various write
/// helpers.
pub unsafe trait CLInfo<I> {
    fn query(&self, q: I, v: CLInfoValue) -> CLResult<CLInfoRes>;

    /// # Safety
    ///
    /// Same requirements from [CLInfoValue::new] apply.
    unsafe fn get_info(
        &self,
        param_name: I,
        param_value_size: usize,
        param_value: *mut ::std::os::raw::c_void,
        param_value_size_ret: *mut usize,
    ) -> CLResult<()> {
        // SAFETY: It's up to the caller as this function is marked unsafe.
        let value =
            unsafe { CLInfoValue::new(param_value_size, param_value, param_value_size_ret)? };
        self.query(param_name, value)?;
        Ok(())
    }
}

/// # Safety
///
/// See [CLInfo]
pub unsafe trait CLInfoObj<I, O> {
    fn query(&self, o: O, q: I, v: CLInfoValue) -> CLResult<CLInfoRes>;

    /// # Safety
    ///
    /// Same requirements from [CLInfoValue::new] apply.
    unsafe fn get_info_obj(
        &self,
        obj: O,
        param_name: I,
        param_value_size: usize,
        param_value: *mut ::std::os::raw::c_void,
        param_value_size_ret: *mut usize,
    ) -> CLResult<()> {
        // SAFETY: It's up to the caller as this function is marked unsafe.
        let value =
            unsafe { CLInfoValue::new(param_value_size, param_value, param_value_size_ret)? };
        self.query(obj, param_name, value)?;
        Ok(())
    }
}

/// Trait to be implemented for the [CLInfo] and [CLInfoObj].
pub trait CLProp {
    type Output: CLProp + Copy;

    /// Returns the amount of `Self::Output` returned.
    fn count(&self) -> usize;

    /// Called to write the value into the `out` buffer.
    fn write_to(&self, out: &mut [MaybeUninit<Self::Output>]);
}

macro_rules! cl_prop_for_type {
    ($ty: ty) => {
        impl CLProp for $ty {
            type Output = Self;

            fn count(&self) -> usize {
                1
            }

            fn write_to(&self, out: &mut [MaybeUninit<Self>]) {
                out[0].write(*self);
            }
        }
    };
}

cl_prop_for_type!(cl_char);
cl_prop_for_type!(cl_uchar);
cl_prop_for_type!(cl_ushort);
cl_prop_for_type!(cl_int);
cl_prop_for_type!(cl_uint);
cl_prop_for_type!(cl_ulong);
cl_prop_for_type!(isize);
cl_prop_for_type!(usize);

cl_prop_for_type!(cl_device_integer_dot_product_acceleration_properties_khr);
cl_prop_for_type!(cl_device_pci_bus_info_khr);
cl_prop_for_type!(cl_image_format);
cl_prop_for_type!(cl_name_version);

impl CLProp for bool {
    type Output = cl_bool;

    fn count(&self) -> usize {
        1
    }

    fn write_to(&self, out: &mut [MaybeUninit<cl_bool>]) {
        if *self { CL_TRUE } else { CL_FALSE }.write_to(out);
    }
}

impl CLProp for &str {
    type Output = u8;

    fn count(&self) -> usize {
        // we need one additional byte for the nul terminator
        self.len() + 1
    }

    fn write_to(&self, out: &mut [MaybeUninit<u8>]) {
        let bytes = self.as_bytes();

        maybe_uninit_copy_from_slice(&mut out[0..bytes.len()], bytes);
        out[bytes.len()].write(b'\0');
    }
}

impl CLProp for &CStr {
    type Output = u8;

    fn count(&self) -> usize {
        self.to_bytes_with_nul().len()
    }

    fn write_to(&self, out: &mut [MaybeUninit<u8>]) {
        self.to_bytes_with_nul().write_to(out);
    }
}

impl<T> CLProp for Vec<T>
where
    T: CLProp + Copy,
{
    type Output = T;

    fn count(&self) -> usize {
        self.len()
    }

    fn write_to(&self, out: &mut [MaybeUninit<T>]) {
        self.as_slice().write_to(out);
    }
}

impl<T> CLProp for &[T]
where
    T: CLProp + Copy,
{
    type Output = T;

    fn count(&self) -> usize {
        self.len()
    }

    fn write_to(&self, out: &mut [MaybeUninit<T>]) {
        maybe_uninit_copy_from_slice(&mut out[0..self.len()], self);
    }
}

impl<T, const I: usize> CLProp for [T; I]
where
    T: CLProp + Copy,
{
    type Output = Self;

    fn count(&self) -> usize {
        1
    }

    fn write_to(&self, out: &mut [MaybeUninit<Self>]) {
        out[0].write(*self);
    }
}

impl<T> CLProp for *mut T {
    type Output = Self;

    fn count(&self) -> usize {
        1
    }

    fn write_to(&self, out: &mut [MaybeUninit<Self>]) {
        out[0].write(*self);
    }
}

impl<T> CLProp for &Properties<T>
where
    T: CLProp + Copy + Default,
{
    type Output = T;

    fn count(&self) -> usize {
        self.raw_data().count()
    }

    fn write_to(&self, out: &mut [MaybeUninit<T>]) {
        self.raw_data().write_to(out);
    }
}

impl<T> CLProp for Option<T>
where
    T: CLProp + Copy,
{
    type Output = T::Output;

    fn count(&self) -> usize {
        match self {
            Some(val) => val.count(),
            None => 0,
        }
    }

    fn write_to(&self, out: &mut [MaybeUninit<T::Output>]) {
        if let Some(val) = self {
            val.write_to(out);
        };
    }
}

const CL_DEVICE_TYPES: u32 = CL_DEVICE_TYPE_ACCELERATOR
    | CL_DEVICE_TYPE_CPU
    | CL_DEVICE_TYPE_GPU
    | CL_DEVICE_TYPE_CUSTOM
    | CL_DEVICE_TYPE_DEFAULT;

pub fn check_cl_device_type(val: cl_device_type) -> CLResult<()> {
    let v: u32 = val.try_into().or(Err(CL_INVALID_DEVICE_TYPE))?;
    if v == CL_DEVICE_TYPE_ALL || v & CL_DEVICE_TYPES == v {
        return Ok(());
    }
    Err(CL_INVALID_DEVICE_TYPE)
}

pub const CL_IMAGE_TYPES: [cl_mem_object_type; 6] = [
    CL_MEM_OBJECT_IMAGE1D,
    CL_MEM_OBJECT_IMAGE2D,
    CL_MEM_OBJECT_IMAGE3D,
    CL_MEM_OBJECT_IMAGE1D_ARRAY,
    CL_MEM_OBJECT_IMAGE2D_ARRAY,
    CL_MEM_OBJECT_IMAGE1D_BUFFER,
];

pub fn check_cl_bool<T: PartialEq + TryInto<cl_uint>>(val: T) -> Option<bool> {
    let c: u32 = val.try_into().ok()?;
    if c != CL_TRUE && c != CL_FALSE {
        return None;
    }
    Some(c == CL_TRUE)
}

pub fn event_list_from_cl(
    q: &Arc<Queue>,
    num_events_in_wait_list: cl_uint,
    event_wait_list: *const cl_event,
) -> CLResult<Vec<Arc<Event>>> {
    // CL_INVALID_EVENT_WAIT_LIST if event_wait_list is NULL and num_events_in_wait_list > 0, or
    // event_wait_list is not NULL and num_events_in_wait_list is 0, or if event objects in
    // event_wait_list are not valid events.
    let res = Event::arcs_from_arr(event_wait_list, num_events_in_wait_list)
        .map_err(|_| CL_INVALID_EVENT_WAIT_LIST)?;

    // CL_INVALID_CONTEXT if context associated with command_queue and events in event_list are not
    // the same.
    if res.iter().any(|e| e.context != q.context) {
        return Err(CL_INVALID_CONTEXT);
    }

    Ok(res)
}

pub fn checked_compare(a: usize, o: cmp::Ordering, b: u64) -> bool {
    if usize::BITS > u64::BITS {
        a.cmp(&(b as usize)) == o
    } else {
        (a as u64).cmp(&b) == o
    }
}

pub fn is_alligned<T>(ptr: *const T, alignment: usize) -> bool {
    ptr as usize & (alignment - 1) == 0
}

pub fn bit_check<A: BitAnd<Output = A> + PartialEq + Default, B: Into<A>>(a: A, b: B) -> bool {
    a & b.into() != A::default()
}

// Taken from "Appendix D: Checking for Memory Copy Overlap"
// src_offset and dst_offset are additions to support sub-buffers
pub fn check_copy_overlap(
    src_origin: &CLVec<usize>,
    src_offset: usize,
    dst_origin: &CLVec<usize>,
    dst_offset: usize,
    region: &CLVec<usize>,
    row_pitch: usize,
    slice_pitch: usize,
) -> bool {
    let slice_size = (region[1] - 1) * row_pitch + region[0];
    let block_size = (region[2] - 1) * slice_pitch + slice_size;
    let src_start =
        src_origin[2] * slice_pitch + src_origin[1] * row_pitch + src_origin[0] + src_offset;
    let src_end = src_start + block_size;
    let dst_start =
        dst_origin[2] * slice_pitch + dst_origin[1] * row_pitch + dst_origin[0] + dst_offset;
    let dst_end = dst_start + block_size;

    /* No overlap if dst ends before src starts or if src ends
     * before dst starts.
     */
    if (dst_end <= src_start) || (src_end <= dst_start) {
        return false;
    }

    /* No overlap if region[0] for dst or src fits in the gap
     * between region[0] and row_pitch.
     */
    {
        let src_dx = (src_origin[0] + src_offset) % row_pitch;
        let dst_dx = (dst_origin[0] + dst_offset) % row_pitch;
        if ((dst_dx >= src_dx + region[0]) && (dst_dx + region[0] <= src_dx + row_pitch))
            || ((src_dx >= dst_dx + region[0]) && (src_dx + region[0] <= dst_dx + row_pitch))
        {
            return false;
        }
    }

    /* No overlap if region[1] for dst or src fits in the gap
     * between region[1] and slice_pitch.
     */
    {
        let src_dy = (src_origin[1] * row_pitch + src_origin[0] + src_offset) % slice_pitch;
        let dst_dy = (dst_origin[1] * row_pitch + dst_origin[0] + dst_offset) % slice_pitch;
        if ((dst_dy >= src_dy + slice_size) && (dst_dy + slice_size <= src_dy + slice_pitch))
            || ((src_dy >= dst_dy + slice_size) && (src_dy + slice_size <= dst_dy + slice_pitch))
        {
            return false;
        }
    }

    /* Otherwise src and dst overlap. */
    true
}

pub mod cl_slice {
    use crate::api::util::CLResult;
    use mesa_rust_util::ptr::addr;
    use rusticl_opencl_gen::CL_INVALID_VALUE;
    use std::mem;
    use std::slice;

    /// Wrapper around [`std::slice::from_raw_parts`] that returns `Err(CL_INVALID_VALUE)` if any of these conditions is met:
    /// - `data` is null
    /// - `data` is not correctly aligned for `T`
    /// - `len * std::mem::size_of::<T>()` is larger than `isize::MAX`
    /// - `data` + `len * std::mem::size_of::<T>()` wraps around the address space
    ///
    /// # Safety
    /// The behavior is undefined if any of the other requirements imposed by
    /// [`std::slice::from_raw_parts`] is violated.
    #[inline]
    pub unsafe fn from_raw_parts<'a, T>(data: *const T, len: usize) -> CLResult<&'a [T]> {
        if allocation_obviously_invalid(data, len) {
            return Err(CL_INVALID_VALUE);
        }

        // SAFETY: We've checked that `data` is not null and properly aligned. We've also checked
        // that the total size in bytes does not exceed `isize::MAX` and that adding that size to
        // `data` does not wrap around the address space.
        //
        // The caller has to uphold the other safety requirements imposed by [`std::slice::from_raw_parts`].
        unsafe { Ok(slice::from_raw_parts(data, len)) }
    }

    /// Wrapper around [`std::slice::from_raw_parts_mut`] that returns `Err(CL_INVALID_VALUE)` if any of these conditions is met:
    /// - `data` is null
    /// - `data` is not correctly aligned for `T`
    /// - `len * std::mem::size_of::<T>()` is larger than `isize::MAX`
    /// - `data` + `len * std::mem::size_of::<T>()` wraps around the address space
    ///
    /// # Safety
    /// The behavior is undefined if any of the other requirements imposed by
    /// [`std::slice::from_raw_parts_mut`] is violated.
    #[inline]
    pub unsafe fn from_raw_parts_mut<'a, T>(data: *mut T, len: usize) -> CLResult<&'a mut [T]> {
        if allocation_obviously_invalid(data, len) {
            return Err(CL_INVALID_VALUE);
        }

        // SAFETY: We've checked that `data` is not null and properly aligned. We've also checked
        // that the total size in bytes does not exceed `isize::MAX` and that adding that size to
        // `data` does not wrap around the address space.
        //
        // The caller has to uphold the other safety requirements imposed by [`std::slice::from_raw_parts_mut`].
        unsafe { Ok(slice::from_raw_parts_mut(data, len)) }
    }

    #[must_use]
    fn allocation_obviously_invalid<T>(data: *const T, len: usize) -> bool {
        let Some(total_size) = mem::size_of::<T>().checked_mul(len) else {
            return true;
        };
        data.is_null()
            || !mesa_rust_util::ptr::is_aligned(data)
            || total_size > isize::MAX as usize
            || addr(data).checked_add(total_size).is_none()
    }
}
