// Copyright © 2024 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use std::io;
use std::marker::PhantomPinned;
use std::pin::Pin;

use crate::bindings;

struct MemStreamImpl {
    stream: bindings::u_memstream,
    buffer: *mut u8,
    buffer_size: usize,
    _pin: PhantomPinned,
}

/// A Rust memstream abstraction. Useful when interacting with C code that
/// expects a FILE* pointer.
///
/// The size of the buffer is managed by the C code automatically.
pub struct MemStream(Pin<Box<MemStreamImpl>>);

impl MemStream {
    pub fn new() -> io::Result<Self> {
        let mut stream_impl = Box::pin(MemStreamImpl {
            stream: unsafe { std::mem::zeroed() },
            buffer: std::ptr::null_mut(),
            buffer_size: 0,
            _pin: PhantomPinned,
        });

        unsafe {
            let stream_impl = stream_impl.as_mut().get_unchecked_mut();
            if !bindings::u_memstream_open(
                &mut stream_impl.stream,
                (&mut stream_impl.buffer as *mut *mut u8).cast(),
                &mut stream_impl.buffer_size,
            ) {
                return Err(io::Error::last_os_error());
            }
            if bindings::u_memstream_flush(&mut stream_impl.stream) != 0 {
                return Err(io::Error::last_os_error());
            }
        }

        Ok(Self(stream_impl))
    }

    // Safety: caller must ensure that inner is not moved through the returned
    // reference.
    unsafe fn inner_mut(&mut self) -> &mut MemStreamImpl {
        unsafe { self.0.as_mut().get_unchecked_mut() }
    }

    /// Flushes the stream so written data appears in the stream
    pub fn flush(&mut self) -> io::Result<()> {
        unsafe {
            let stream = self.inner_mut();
            if bindings::u_memstream_flush(&mut stream.stream) != 0 {
                return Err(io::Error::last_os_error());
            }
        }

        Ok(())
    }

    /// Resets the MemStream
    pub fn reset(&mut self) -> io::Result<()> {
        *self = Self::new()?;
        Ok(())
    }

    /// Resets the MemStream and returns its contents
    pub fn take(&mut self) -> io::Result<Vec<u8>> {
        let mut vec = Vec::new();
        vec.extend_from_slice(self.as_slice()?);
        self.reset()?;
        Ok(vec)
    }

    /// Resets the MemStream and returns its contents as a UTF-8 string
    pub fn take_utf8_string_lossy(&mut self) -> io::Result<String> {
        let string = String::from_utf8_lossy(self.as_slice()?).into_owned();
        self.reset()?;
        Ok(string)
    }

    /// Returns the current position in the stream.
    pub fn position(&self) -> usize {
        unsafe { bindings::compiler_rs_ftell(self.c_file()) as usize }
    }

    /// Seek to a position relative to the start of the stream.
    pub fn seek(&mut self, offset: u64) -> io::Result<()> {
        let offset = offset.try_into().map_err(|_| {
            io::Error::new(io::ErrorKind::InvalidInput, "offset too large")
        })?;

        unsafe {
            if bindings::compiler_rs_fseek(self.c_file(), offset, 0) != 0 {
                Err(io::Error::last_os_error())
            } else {
                Ok(())
            }
        }
    }

    /// Returns the underlying C file.
    ///
    /// # Safety
    ///
    /// The memstream abstraction assumes that the file is valid throughout its
    /// lifetime.
    pub unsafe fn c_file(&self) -> *mut bindings::FILE {
        self.0.stream.f
    }

    /// Returns a slice view into the memstream
    ///
    /// This is only safe with respect to other safe Rust methods.  Even though
    /// this takes a reference to the stream there is nothing preventing you
    /// from modifying the stream through the FILE with unsafe C code.
    ///
    /// This is conceptually the same as `AsRef`, but it flushes the stream
    /// first, which means it takes &mut self as a receiver.
    fn as_slice(&mut self) -> io::Result<&[u8]> {
        // Make sure we have the most up-to-date data before returning a slice.
        self.flush()?;
        let pos = self.position();

        if pos == 0 {
            Ok(&[])
        } else {
            // SAFETY: this does not move the stream and we know that
            // self.position() cannot exceed the stream size as per the
            // open_memstream() API.
            Ok(unsafe { std::slice::from_raw_parts(self.0.buffer, pos) })
        }
    }
}

impl Drop for MemStream {
    fn drop(&mut self) {
        // SAFETY: this does not move the stream.
        unsafe {
            bindings::u_memstream_close(&mut self.inner_mut().stream);
            bindings::compiler_rs_free(self.0.buffer as *mut std::ffi::c_void);
        }
    }
}

#[test]
fn test_memstream() {
    use std::ffi::CString;

    let mut s = MemStream::new().unwrap();
    let test_str = "Test string";
    let test_c_str = CString::new(test_str).unwrap();
    let test_bytes = test_c_str.as_bytes();

    unsafe {
        bindings::compiler_rs_fwrite(
            test_bytes.as_ptr().cast(),
            1,
            test_bytes.len(),
            s.c_file(),
        );
    }
    let res = s.take_utf8_string_lossy().unwrap();
    assert_eq!(res, test_str);
}
