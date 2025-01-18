// Copyright © 2024 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use std::ops::Index;

pub enum AttrList<T: 'static> {
    Array(&'static [T]),
    Uniform(T),
}

impl<T: 'static> Index<usize> for AttrList<T> {
    type Output = T;

    fn index(&self, idx: usize) -> &T {
        match self {
            AttrList::Array(arr) => &arr[idx],
            AttrList::Uniform(typ) => typ,
        }
    }
}

pub trait AsSlice<T> {
    type Attr;

    fn as_slice(&self) -> &[T];
    fn as_mut_slice(&mut self) -> &mut [T];
    fn attrs(&self) -> AttrList<Self::Attr>;
}
