use std::ffi::CStr;
use std::os::raw::c_char;

#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub fn c_string_to_string(cstr: *const c_char) -> String {
    if cstr.is_null() {
        return String::from("");
    }

    let res = unsafe { CStr::from_ptr(cstr).to_str() };
    assert!(res.is_ok());
    String::from(res.unwrap_or(""))
}

/// Simple wrapper around [`CStr::from_ptr`] to bind the lifetime to a slice containing the
/// nul-terminated string.
///
/// # Safety
///
/// Same as [`CStr::from_ptr`]
pub unsafe fn char_arr_to_cstr(c_str: &[c_char]) -> &CStr {
    // We make sure that at least the end of the slice is nul-terminated. We don't really care if
    // there is another nul within the slice.
    debug_assert_eq!(c_str.last().copied(), Some(b'\0' as c_char));
    unsafe { CStr::from_ptr(c_str.as_ptr()) }
}
