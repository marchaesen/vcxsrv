// Copyright © 2024 Igalia S.L.
// SPDX-License-Identifier: MIT

use isa_bindings::*;
use std::ffi::CString;
use std::mem::ManuallyDrop;
use std::ptr;

pub trait EtnaAsmResultExt {
    fn set_error(&mut self, error_message: &str);
    fn dealloc_error(&mut self);

    fn append_instruction(&mut self, new_inst: etna_inst);
    fn dealloc_instructions(&mut self);
}

impl EtnaAsmResultExt for etna_asm_result {
    fn set_error(&mut self, error_message: &str) {
        self.dealloc_error();

        self.error = CString::new(error_message)
            .expect("CString::new failed")
            .into_raw();
    }

    fn dealloc_error(&mut self) {
        if !self.error.is_null() {
            // SAFETY: Previously obtained from CString::into_raw in set_error
            let _ = unsafe { CString::from_raw(self.error) };
            self.error = ptr::null_mut();
        }
    }

    fn append_instruction(&mut self, new_inst: etna_inst) {
        let mut instrs = if self.instr.is_null() {
            Vec::new()
        } else {
            // SAFETY: These values come from a previous call to `append_instruction` where we turned a Vec into raw parts
            unsafe { Vec::from_raw_parts(self.instr, self.num_instr, self.capacity_instr) }
        };

        instrs.push(new_inst);

        // This is Vec::into_raw_parts, which is still unstable
        let mut md_vec = ManuallyDrop::new(instrs);
        self.num_instr = md_vec.len();
        self.capacity_instr = md_vec.capacity();
        self.instr = md_vec.as_mut_ptr();
    }

    fn dealloc_instructions(&mut self) {
        if !self.instr.is_null() {
            // SAFETY: These values come from a previous call to `append_instruction` where we turned a Vec into raw parts
            let _ = unsafe { Vec::from_raw_parts(self.instr, self.num_instr, self.capacity_instr) };
            (self.instr, self.num_instr, self.capacity_instr) = (ptr::null_mut(), 0, 0);
        }
    }
}
