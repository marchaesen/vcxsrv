// Copyright © 2024 Igalia S.L.
// SPDX-License-Identifier: MIT

use crate::parser::*;
use crate::util::EtnaAsmResultExt;

use isa_bindings::*;
use std::ffi::CStr;
use std::os::raw::c_char;

/// Shared implementation to interact with the asm parser
///
/// # Safety
///
/// `c_str` must point to a valid nul-termintated string not longer than `isize::MAX` bytes.
/// See https://doc.rust-lang.org/std/ffi/struct.CStr.html#safety for further details.
unsafe fn isa_parse(
    c_str: *const c_char,
    dual_16_mode: bool,
    func: impl FnOnce(&str, bool, &mut etna_asm_result),
) -> *mut etna_asm_result {
    let mut result = Box::new(etna_asm_result::default());

    if c_str.is_null() {
        result.set_error("str pointer is NULL");
        return Box::into_raw(result);
    }

    // SAFETY: As per the safety section, the caller has to uphold these requirements
    let c_str_safe = unsafe { CStr::from_ptr(c_str) };

    if let Ok(str) = c_str_safe.to_str() {
        func(str, dual_16_mode, &mut result);
    } else {
        result.set_error("Failed to convert CStr to &str");
        result.success = false;
    }

    Box::into_raw(result)
}

/// # Safety
///
/// `c_str` must point to a valid nul-termintated string not longer than `isize::MAX` bytes.
/// See https://doc.rust-lang.org/std/ffi/struct.CStr.html#safety for further details.
#[no_mangle]
pub unsafe extern "C" fn isa_parse_str(
    c_str: *const c_char,
    dual_16_mode: bool,
) -> *mut etna_asm_result {
    // SAFETY: As per the safety section, the caller has to uphold these requirements
    unsafe { isa_parse(c_str, dual_16_mode, asm_process_str) }
}

/// # Safety
///
/// `c_filepath` must point to a valid nul-termintated string not longer than `isize::MAX` bytes.
/// See https://doc.rust-lang.org/std/ffi/struct.CStr.html#safety for further details.
#[no_mangle]
pub unsafe extern "C" fn isa_parse_file(
    c_filepath: *const c_char,
    dual_16_mode: bool,
) -> *mut etna_asm_result {
    // SAFETY: As per the safety section, the caller has to uphold these requirements
    unsafe { isa_parse(c_filepath, dual_16_mode, asm_process_file) }
}

/// # Safety
///
/// `result` must have previously been obtained from `isa_parse_str` or `isa_parse_file`.
#[no_mangle]
pub unsafe extern "C" fn isa_asm_result_destroy(result: *mut etna_asm_result) {
    // SAFETY: As per the safety section, the caller has to uphold these requirements
    let mut r = unsafe { Box::from_raw(result) };
    r.dealloc_instructions();
    r.dealloc_error();
}
