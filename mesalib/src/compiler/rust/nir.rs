// Copyright © 2022 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::bindings::*;

use std::ffi::{c_void, CStr};
use std::marker::PhantomData;
use std::mem::offset_of;
use std::ptr::NonNull;
use std::str;

pub struct ExecListIter<'a, T> {
    n: &'a exec_node,
    offset: usize,
    rev: bool,
    _marker: PhantomData<T>,
}

impl<'a, T> ExecListIter<'a, T> {
    fn new(l: &'a exec_list, offset: usize) -> Self {
        Self {
            n: &l.head_sentinel,
            offset: offset,
            rev: false,
            _marker: PhantomData,
        }
    }

    #[allow(dead_code)]
    fn new_rev(l: &'a exec_list, offset: usize) -> Self {
        Self {
            n: &l.tail_sentinel,
            offset: offset,
            rev: true,
            _marker: PhantomData,
        }
    }

    fn at(n: &'a exec_node, offset: usize, rev: bool) -> Self {
        Self {
            n,
            offset: offset,
            rev: rev,
            _marker: PhantomData,
        }
    }
}

impl<'a, T: 'a> Iterator for ExecListIter<'a, T> {
    type Item = &'a T;

    fn next(&mut self) -> Option<Self::Item> {
        if self.rev {
            self.n = unsafe { &*self.n.prev };
            if self.n.prev.is_null() {
                None
            } else {
                let t: *const c_void = (self.n as *const exec_node).cast();
                Some(unsafe { &*(t.sub(self.offset).cast()) })
            }
        } else {
            self.n = unsafe { &*self.n.next };
            if self.n.next.is_null() {
                None
            } else {
                let t: *const c_void = (self.n as *const exec_node).cast();
                Some(unsafe { &*(t.sub(self.offset).cast()) })
            }
        }
    }
}

impl nir_def {
    pub fn parent_instr<'a>(&'a self) -> &'a nir_instr {
        unsafe { NonNull::new(self.parent_instr).unwrap().as_ref() }
    }

    pub fn components_read(&self) -> nir_component_mask_t {
        unsafe { nir_def_components_read(self as *const _) }
    }

    pub fn all_uses_are_fsat(&self) -> bool {
        unsafe { nir_def_all_uses_are_fsat(self as *const _) }
    }
}

pub trait AsDef {
    fn as_def<'a>(&'a self) -> &'a nir_def;

    fn bit_size(&self) -> u8 {
        self.as_def().bit_size
    }

    fn num_components(&self) -> u8 {
        self.as_def().num_components
    }

    fn as_load_const<'a>(&'a self) -> Option<&'a nir_load_const_instr> {
        self.as_def().parent_instr().as_load_const()
    }

    fn is_const(&self) -> bool {
        self.as_load_const().is_some()
    }

    fn comp_as_int(&self, comp: u8) -> Option<i64> {
        if let Some(load) = self.as_load_const() {
            assert!(comp < load.def.num_components);
            Some(unsafe {
                let comp = usize::from(comp);
                match self.bit_size() {
                    8 => load.values()[comp].i8_ as i64,
                    16 => load.values()[comp].i16_ as i64,
                    32 => load.values()[comp].i32_ as i64,
                    64 => load.values()[comp].i64_,
                    _ => panic!("Invalid bit size"),
                }
            })
        } else {
            None
        }
    }

    fn comp_as_uint(&self, comp: u8) -> Option<u64> {
        if let Some(load) = self.as_load_const() {
            assert!(comp < load.def.num_components);
            Some(unsafe {
                let comp = usize::from(comp);
                match self.bit_size() {
                    8 => load.values()[comp].u8_ as u64,
                    16 => load.values()[comp].u16_ as u64,
                    32 => load.values()[comp].u32_ as u64,
                    64 => load.values()[comp].u64_,
                    _ => panic!("Invalid bit size"),
                }
            })
        } else {
            None
        }
    }

    fn as_int(&self) -> Option<i64> {
        assert!(self.num_components() == 1);
        self.comp_as_int(0)
    }

    fn as_uint(&self) -> Option<u64> {
        assert!(self.num_components() == 1);
        self.comp_as_uint(0)
    }

    fn is_zero(&self) -> bool {
        self.num_components() == 1 && self.as_uint() == Some(0)
    }
}

impl AsDef for nir_def {
    fn as_def<'a>(&'a self) -> &'a nir_def {
        self
    }
}

impl AsDef for nir_src {
    fn as_def<'a>(&'a self) -> &'a nir_def {
        unsafe { NonNull::new(self.ssa).unwrap().as_ref() }
    }
}

impl nir_alu_instr {
    pub fn info(&self) -> &'static nir_op_info {
        let info_idx: usize = self.op.try_into().unwrap();
        unsafe { &nir_op_infos[info_idx] }
    }

    pub fn src_components(&self, src_idx: u8) -> u8 {
        assert!(src_idx < self.info().num_inputs);
        unsafe {
            nir_ssa_alu_instr_src_components(self as *const _, src_idx.into())
                .try_into()
                .unwrap()
        }
    }

    pub fn srcs_as_slice<'a>(&'a self) -> &'a [nir_alu_src] {
        unsafe {
            self.src
                .as_slice(self.info().num_inputs.try_into().unwrap())
        }
    }

    pub fn get_src(&self, idx: usize) -> &nir_alu_src {
        &self.srcs_as_slice()[idx]
    }
}

impl nir_op_info {
    pub fn name(&self) -> &'static str {
        unsafe { CStr::from_ptr(self.name).to_str().expect("Invalid UTF-8") }
    }
}

impl nir_alu_src {
    pub fn bit_size(&self) -> u8 {
        self.src.bit_size()
    }

    pub fn comp_as_int(&self, comp: u8) -> Option<i64> {
        self.src.comp_as_int(self.swizzle[usize::from(comp)])
    }

    pub fn comp_as_uint(&self, comp: u8) -> Option<u64> {
        self.src.comp_as_uint(self.swizzle[usize::from(comp)])
    }
}

impl nir_tex_instr {
    pub fn srcs_as_slice<'a>(&'a self) -> &'a [nir_tex_src] {
        unsafe { std::slice::from_raw_parts(self.src, self.num_srcs as usize) }
    }

    pub fn get_src(&self, idx: usize) -> &nir_tex_src {
        &self.srcs_as_slice()[idx]
    }
}

impl nir_intrinsic_instr {
    pub fn info(&self) -> &'static nir_intrinsic_info {
        let info_idx: usize = self.intrinsic.try_into().unwrap();
        unsafe { &nir_intrinsic_infos[info_idx] }
    }

    pub fn srcs_as_slice<'a>(&'a self) -> &'a [nir_src] {
        unsafe { self.src.as_slice(self.info().num_srcs.try_into().unwrap()) }
    }

    pub fn get_src(&self, idx: usize) -> &nir_src {
        &self.srcs_as_slice()[idx]
    }

    pub fn get_const_index(&self, name: u32) -> u32 {
        let name: usize = name.try_into().unwrap();
        let idx = self.info().index_map[name];
        assert!(idx > 0);
        self.const_index[usize::from(idx - 1)] as u32
    }

    pub fn base(&self) -> i32 {
        self.get_const_index(NIR_INTRINSIC_BASE) as i32
    }

    pub fn range_base(&self) -> i32 {
        self.get_const_index(NIR_INTRINSIC_RANGE_BASE) as i32
    }

    pub fn range(&self) -> i32 {
        self.get_const_index(NIR_INTRINSIC_RANGE) as i32
    }

    pub fn write_mask(&self) -> u32 {
        self.get_const_index(NIR_INTRINSIC_WRITE_MASK)
    }

    pub fn stream_id(&self) -> u32 {
        self.get_const_index(NIR_INTRINSIC_STREAM_ID)
    }

    pub fn component(&self) -> u32 {
        self.get_const_index(NIR_INTRINSIC_COMPONENT)
    }

    pub fn interp_mode(&self) -> u32 {
        self.get_const_index(NIR_INTRINSIC_INTERP_MODE)
    }

    pub fn reduction_op(&self) -> nir_op {
        self.get_const_index(NIR_INTRINSIC_REDUCTION_OP) as nir_op
    }

    pub fn cluster_size(&self) -> u32 {
        self.get_const_index(NIR_INTRINSIC_CLUSTER_SIZE)
    }

    pub fn image_dim(&self) -> glsl_sampler_dim {
        self.get_const_index(NIR_INTRINSIC_IMAGE_DIM) as glsl_sampler_dim
    }

    pub fn image_array(&self) -> bool {
        self.get_const_index(NIR_INTRINSIC_IMAGE_ARRAY) != 0
    }

    pub fn access(&self) -> gl_access_qualifier {
        self.get_const_index(NIR_INTRINSIC_ACCESS) as gl_access_qualifier
    }

    pub fn align(&self) -> u32 {
        let mul = self.align_mul();
        let offset = self.align_offset();
        assert!(offset < mul);
        if offset > 0 {
            1 << offset.trailing_zeros()
        } else {
            mul
        }
    }

    pub fn align_mul(&self) -> u32 {
        self.get_const_index(NIR_INTRINSIC_ALIGN_MUL)
    }

    pub fn align_offset(&self) -> u32 {
        self.get_const_index(NIR_INTRINSIC_ALIGN_OFFSET)
    }

    pub fn execution_scope(&self) -> mesa_scope {
        self.get_const_index(NIR_INTRINSIC_EXECUTION_SCOPE)
    }

    pub fn memory_scope(&self) -> mesa_scope {
        self.get_const_index(NIR_INTRINSIC_MEMORY_SCOPE)
    }

    pub fn memory_semantics(&self) -> nir_memory_semantics {
        self.get_const_index(NIR_INTRINSIC_MEMORY_SEMANTICS)
    }

    pub fn memory_modes(&self) -> nir_variable_mode {
        self.get_const_index(NIR_INTRINSIC_MEMORY_MODES)
    }

    pub fn flags(&self) -> u32 {
        self.get_const_index(NIR_INTRINSIC_FLAGS)
    }

    pub fn atomic_op(&self) -> nir_atomic_op {
        self.get_const_index(NIR_INTRINSIC_ATOMIC_OP) as nir_atomic_op
    }
}

impl nir_intrinsic_info {
    pub fn name(&self) -> &'static str {
        unsafe { CStr::from_ptr(self.name).to_str().expect("Invalid UTF-8") }
    }
}

impl nir_load_const_instr {
    pub fn values<'a>(&'a self) -> &'a [nir_const_value] {
        unsafe { self.value.as_slice(self.def.num_components as usize) }
    }
}

impl nir_phi_src {
    pub fn pred<'a>(&'a self) -> &'a nir_block {
        unsafe { NonNull::new(self.pred).unwrap().as_ref() }
    }
}

impl nir_phi_instr {
    pub fn iter_srcs(&self) -> ExecListIter<nir_phi_src> {
        ExecListIter::new(&self.srcs, offset_of!(nir_phi_src, node))
    }
}

impl nir_jump_instr {
    pub fn target<'a>(&'a self) -> Option<&'a nir_block> {
        NonNull::new(self.target).map(|b| unsafe { b.as_ref() })
    }

    pub fn else_target<'a>(&'a self) -> Option<&'a nir_block> {
        NonNull::new(self.else_target).map(|b| unsafe { b.as_ref() })
    }
}

impl nir_instr {
    pub fn as_alu<'a>(&'a self) -> Option<&'a nir_alu_instr> {
        if self.type_ == nir_instr_type_alu {
            let p = self as *const nir_instr;
            Some(unsafe { &*(p as *const nir_alu_instr) })
        } else {
            None
        }
    }

    pub fn as_jump<'a>(&'a self) -> Option<&'a nir_jump_instr> {
        if self.type_ == nir_instr_type_jump {
            let p = self as *const nir_instr;
            Some(unsafe { &*(p as *const nir_jump_instr) })
        } else {
            None
        }
    }

    pub fn as_tex<'a>(&'a self) -> Option<&'a nir_tex_instr> {
        if self.type_ == nir_instr_type_tex {
            let p = self as *const nir_instr;
            Some(unsafe { &*(p as *const nir_tex_instr) })
        } else {
            None
        }
    }

    pub fn as_intrinsic<'a>(&'a self) -> Option<&'a nir_intrinsic_instr> {
        if self.type_ == nir_instr_type_intrinsic {
            let p = self as *const nir_instr;
            Some(unsafe { &*(p as *const nir_intrinsic_instr) })
        } else {
            None
        }
    }

    pub fn as_load_const<'a>(&'a self) -> Option<&'a nir_load_const_instr> {
        if self.type_ == nir_instr_type_load_const {
            let p = self as *const nir_instr;
            Some(unsafe { &*(p as *const nir_load_const_instr) })
        } else {
            None
        }
    }

    pub fn as_undef<'a>(&'a self) -> Option<&'a nir_undef_instr> {
        if self.type_ == nir_instr_type_undef {
            let p = self as *const nir_instr;
            Some(unsafe { &*(p as *const nir_undef_instr) })
        } else {
            None
        }
    }

    pub fn as_phi<'a>(&'a self) -> Option<&'a nir_phi_instr> {
        if self.type_ == nir_instr_type_phi {
            let p = self as *const nir_instr;
            Some(unsafe { &*(p as *const nir_phi_instr) })
        } else {
            None
        }
    }

    pub fn def<'a>(&'a self) -> Option<&'a nir_def> {
        unsafe {
            let def = nir_instr_def(self as *const _ as *mut _);
            NonNull::new(def).map(|d| d.as_ref())
        }
    }
}

impl nir_block {
    pub fn iter_instr_list(&self) -> ExecListIter<nir_instr> {
        ExecListIter::new(&self.instr_list, offset_of!(nir_instr, node))
    }

    pub fn successors<'a>(&'a self) -> [Option<&'a nir_block>; 2] {
        [
            NonNull::new(self.successors[0]).map(|b| unsafe { b.as_ref() }),
            NonNull::new(self.successors[1]).map(|b| unsafe { b.as_ref() }),
        ]
    }

    pub fn following_if<'a>(&'a self) -> Option<&'a nir_if> {
        let self_ptr = self as *const _ as *mut _;
        unsafe { nir_block_get_following_if(self_ptr).as_ref() }
    }

    pub fn following_loop<'a>(&'a self) -> Option<&'a nir_loop> {
        let self_ptr = self as *const _ as *mut _;
        unsafe { nir_block_get_following_loop(self_ptr).as_ref() }
    }

    pub fn parent(&self) -> &nir_cf_node {
        self.cf_node.parent().unwrap()
    }
}

impl nir_if {
    pub fn first_then_block(&self) -> &nir_block {
        self.iter_then_list().next().unwrap().as_block().unwrap()
    }

    pub fn first_else_block(&self) -> &nir_block {
        self.iter_else_list().next().unwrap().as_block().unwrap()
    }

    pub fn iter_then_list(&self) -> ExecListIter<nir_cf_node> {
        ExecListIter::new(&self.then_list, offset_of!(nir_cf_node, node))
    }

    pub fn iter_else_list(&self) -> ExecListIter<nir_cf_node> {
        ExecListIter::new(&self.else_list, offset_of!(nir_cf_node, node))
    }

    pub fn following_block(&self) -> &nir_block {
        self.cf_node.next().unwrap().as_block().unwrap()
    }
}

impl nir_loop {
    pub fn iter_body(&self) -> ExecListIter<nir_cf_node> {
        ExecListIter::new(&self.body, offset_of!(nir_cf_node, node))
    }

    pub fn first_block(&self) -> &nir_block {
        self.iter_body().next().unwrap().as_block().unwrap()
    }

    pub fn following_block(&self) -> &nir_block {
        self.cf_node.next().unwrap().as_block().unwrap()
    }
}

impl nir_cf_node {
    pub fn as_block<'a>(&'a self) -> Option<&'a nir_block> {
        if self.type_ == nir_cf_node_block {
            Some(unsafe { &*(self as *const nir_cf_node as *const nir_block) })
        } else {
            None
        }
    }

    pub fn as_if<'a>(&'a self) -> Option<&'a nir_if> {
        if self.type_ == nir_cf_node_if {
            Some(unsafe { &*(self as *const nir_cf_node as *const nir_if) })
        } else {
            None
        }
    }

    pub fn as_loop<'a>(&'a self) -> Option<&'a nir_loop> {
        if self.type_ == nir_cf_node_loop {
            Some(unsafe { &*(self as *const nir_cf_node as *const nir_loop) })
        } else {
            None
        }
    }

    pub fn next(&self) -> Option<&nir_cf_node> {
        let mut iter: ExecListIter<nir_cf_node> =
            ExecListIter::at(&self.node, offset_of!(nir_cf_node, node), false);
        iter.next()
    }

    pub fn prev(&self) -> Option<&nir_cf_node> {
        let mut iter: ExecListIter<nir_cf_node> =
            ExecListIter::at(&self.node, offset_of!(nir_cf_node, node), true);
        iter.next()
    }

    pub fn parent<'a>(&'a self) -> Option<&'a nir_cf_node> {
        NonNull::new(self.parent).map(|b| unsafe { b.as_ref() })
    }
}

impl nir_function_impl {
    pub fn iter_body(&self) -> ExecListIter<nir_cf_node> {
        ExecListIter::new(&self.body, offset_of!(nir_cf_node, node))
    }

    pub fn end_block<'a>(&'a self) -> &'a nir_block {
        unsafe { NonNull::new(self.end_block).unwrap().as_ref() }
    }

    pub fn function<'a>(&'a self) -> &'a nir_function {
        unsafe { self.function.as_ref() }.unwrap()
    }
}

impl nir_function {
    pub fn get_impl(&self) -> Option<&nir_function_impl> {
        unsafe { self.impl_.as_ref() }
    }
}

impl nir_shader {
    pub fn iter_functions(&self) -> ExecListIter<nir_function> {
        ExecListIter::new(&self.functions, offset_of!(nir_function, node))
    }

    pub fn iter_variables(&self) -> ExecListIter<nir_variable> {
        ExecListIter::new(&self.variables, offset_of!(nir_variable, node))
    }
}
