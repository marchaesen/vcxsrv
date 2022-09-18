use mesa_rust_gen::*;

use std::convert::TryInto;
use std::ffi::c_void;
use std::ffi::CString;
use std::marker::PhantomData;
use std::ptr;
use std::ptr::NonNull;
use std::slice;

// from https://internals.rust-lang.org/t/discussion-on-offset-of/7440/2
macro_rules! offset_of {
    ($Struct:path, $field:ident) => {{
        // Using a separate function to minimize unhygienic hazards
        // (e.g. unsafety of #[repr(packed)] field borrows).
        // Uncomment `const` when `const fn`s can juggle pointers.
        /*const*/
        fn offset() -> usize {
            let u = std::mem::MaybeUninit::<$Struct>::uninit();
            // Use pattern-matching to avoid accidentally going through Deref.
            let &$Struct { $field: ref f, .. } = unsafe { &*u.as_ptr() };
            let o = (f as *const _ as usize).wrapping_sub(&u as *const _ as usize);
            // Triple check that we are within `u` still.
            assert!((0..=std::mem::size_of_val(&u)).contains(&o));
            o
        }
        offset()
    }};
}

pub struct ExecListIter<'a, T> {
    n: &'a mut exec_node,
    offset: usize,
    _marker: PhantomData<T>,
}

impl<'a, T> ExecListIter<'a, T> {
    fn new(l: &'a mut exec_list, offset: usize) -> Self {
        Self {
            n: &mut l.head_sentinel,
            offset: offset,
            _marker: PhantomData,
        }
    }
}

impl<'a, T: 'a> Iterator for ExecListIter<'a, T> {
    type Item = &'a mut T;

    fn next(&mut self) -> Option<Self::Item> {
        self.n = unsafe { &mut *self.n.next };
        if self.n.next.is_null() {
            None
        } else {
            let t: *mut c_void = (self.n as *mut exec_node).cast();
            Some(unsafe { &mut *(t.sub(self.offset).cast()) })
        }
    }
}

pub struct NirShader {
    nir: NonNull<nir_shader>,
}

impl NirShader {
    pub fn new(nir: *mut nir_shader) -> Option<Self> {
        NonNull::new(nir).map(|nir| Self { nir: nir })
    }

    pub fn deserialize(
        input: &mut &[u8],
        len: usize,
        options: *const nir_shader_compiler_options,
    ) -> Option<Self> {
        let mut reader = blob_reader::default();

        let (bin, rest) = input.split_at(len);
        *input = rest;

        unsafe {
            blob_reader_init(&mut reader, bin.as_ptr().cast(), len);
            Self::new(nir_deserialize(ptr::null_mut(), options, &mut reader))
        }
    }

    pub fn serialize(&self) -> Vec<u8> {
        let mut blob = blob::default();
        unsafe {
            blob_init(&mut blob);
            nir_serialize(&mut blob, self.nir.as_ptr(), false);
            slice::from_raw_parts(blob.data, blob.size).to_vec()
        }
    }

    pub fn print(&self) {
        unsafe { nir_print_shader(self.nir.as_ptr(), stderr) };
    }

    pub fn get_nir(&self) -> *mut nir_shader {
        self.nir.as_ptr()
    }

    pub fn dup_for_driver(&self) -> *mut nir_shader {
        unsafe { nir_shader_clone(ptr::null_mut(), self.nir.as_ptr()) }
    }

    pub fn sweep_mem(&self) {
        unsafe { nir_sweep(self.nir.as_ptr()) }
    }

    pub fn pass0<R>(&mut self, pass: unsafe extern "C" fn(*mut nir_shader) -> R) -> R {
        unsafe { pass(self.nir.as_ptr()) }
    }

    pub fn pass1<R, A>(
        &mut self,
        pass: unsafe extern "C" fn(*mut nir_shader, a: A) -> R,
        a: A,
    ) -> R {
        unsafe { pass(self.nir.as_ptr(), a) }
    }

    pub fn pass2<R, A, B>(
        &mut self,
        pass: unsafe extern "C" fn(*mut nir_shader, a: A, b: B) -> R,
        a: A,
        b: B,
    ) -> R {
        unsafe { pass(self.nir.as_ptr(), a, b) }
    }

    pub fn pass3<R, A, B, C>(
        &mut self,
        pass: unsafe extern "C" fn(*mut nir_shader, a: A, b: B, c: C) -> R,
        a: A,
        b: B,
        c: C,
    ) -> R {
        unsafe { pass(self.nir.as_ptr(), a, b, c) }
    }

    pub fn entrypoint(&self) -> *mut nir_function_impl {
        unsafe { nir_shader_get_entrypoint(self.nir.as_ptr()) }
    }

    pub fn structurize(&mut self) {
        self.pass0(nir_lower_goto_ifs);
        self.pass0(nir_opt_dead_cf);
    }

    pub fn inline(&mut self, libclc: &NirShader) {
        self.pass1(
            nir_lower_variable_initializers,
            nir_variable_mode::nir_var_function_temp,
        );
        self.pass0(nir_lower_returns);
        self.pass1(nir_lower_libclc, libclc.nir.as_ptr());
        self.pass0(nir_inline_functions);
    }

    pub fn remove_non_entrypoints(&mut self) {
        unsafe { nir_remove_non_entrypoints(self.nir.as_ptr()) };
    }

    pub fn variables(&mut self) -> ExecListIter<nir_variable> {
        ExecListIter::new(
            &mut unsafe { self.nir.as_mut() }.variables,
            offset_of!(nir_variable, node),
        )
    }

    pub fn num_images(&self) -> u8 {
        unsafe { (*self.nir.as_ptr()).info.num_images }
    }

    pub fn reset_scratch_size(&self) {
        unsafe {
            (*self.nir.as_ptr()).scratch_size = 0;
        }
    }

    pub fn scratch_size(&self) -> u32 {
        unsafe { (*self.nir.as_ptr()).scratch_size }
    }

    pub fn shared_size(&self) -> u32 {
        unsafe { (*self.nir.as_ptr()).info.shared_size }
    }

    pub fn workgroup_size(&self) -> [u16; 3] {
        unsafe { (*self.nir.as_ptr()).info.workgroup_size }
    }

    pub fn set_workgroup_size_variable_if_zero(&self) {
        let nir = self.nir.as_ptr();
        unsafe {
            (*nir)
                .info
                .set_workgroup_size_variable((*nir).info.workgroup_size[0] == 0);
        }
    }

    pub fn variables_with_mode(
        &mut self,
        mode: nir_variable_mode,
    ) -> impl Iterator<Item = &mut nir_variable> {
        self.variables()
            .filter(move |v| v.data.mode() & mode.0 != 0)
    }

    pub fn extract_constant_initializers(&self) {
        let nir = self.nir.as_ptr();
        unsafe {
            if (*nir).constant_data_size > 0 {
                assert!((*nir).constant_data.is_null());
                (*nir).constant_data = rzalloc_size(nir.cast(), (*nir).constant_data_size as usize);
                nir_gather_explicit_io_initializers(
                    nir,
                    (*nir).constant_data,
                    (*nir).constant_data_size as usize,
                    nir_variable_mode::nir_var_mem_constant,
                );
            }
        }
    }

    pub fn has_constant(&self) -> bool {
        unsafe {
            !self.nir.as_ref().constant_data.is_null() && self.nir.as_ref().constant_data_size > 0
        }
    }

    pub fn has_printf(&self) -> bool {
        unsafe {
            !self.nir.as_ref().printf_info.is_null() && self.nir.as_ref().printf_info_count != 0
        }
    }

    pub fn printf_format(&self) -> &[u_printf_info] {
        if self.has_printf() {
            unsafe {
                let nir = self.nir.as_ref();
                slice::from_raw_parts(nir.printf_info, nir.printf_info_count as usize)
            }
        } else {
            &[]
        }
    }

    pub fn get_constant_buffer(&self) -> &[u8] {
        unsafe {
            let nir = self.nir.as_ref();
            slice::from_raw_parts(nir.constant_data.cast(), nir.constant_data_size as usize)
        }
    }

    pub fn add_var(
        &self,
        mode: nir_variable_mode,
        glsl_type: *const glsl_type,
        loc: usize,
        name: &str,
    ) -> *mut nir_variable {
        let name = CString::new(name).unwrap();
        unsafe {
            let var = nir_variable_create(self.nir.as_ptr(), mode, glsl_type, name.as_ptr());
            (*var).data.location = loc.try_into().unwrap();
            var
        }
    }
}

impl Clone for NirShader {
    fn clone(&self) -> Self {
        Self {
            nir: NonNull::new(self.dup_for_driver()).unwrap(),
        }
    }
}

impl Drop for NirShader {
    fn drop(&mut self) {
        unsafe { ralloc_free(self.nir.as_ptr().cast()) };
    }
}
