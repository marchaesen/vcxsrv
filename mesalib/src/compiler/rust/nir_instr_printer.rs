// Copyright © 2024 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use std::io;

use crate::bindings;
use crate::bindings::nir_instr;
use crate::memstream::MemStream;

/// A memstream that holds the printed NIR instructions.
pub struct NirInstrPrinter {
    stream: MemStream,
}

impl NirInstrPrinter {
    pub fn new() -> io::Result<Self> {
        Ok(Self {
            stream: MemStream::new()?,
        })
    }

    /// Prints the given NIR instruction.
    pub fn instr_to_string(&mut self, instr: &nir_instr) -> io::Result<String> {
        unsafe { bindings::nir_print_instr(instr, self.stream.c_file()) };
        self.stream.take_utf8_string_lossy()
    }
}
