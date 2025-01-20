// Copyright © 2022 Collabora, Ltd.
// SPDX-License-Identifier: MIT

//! A set of usizes, represented as a bit vector
//!
//! In addition to some basic operations like `insert()` and `remove()`, this
//! module also lets you write expressions on sets that are lazily evaluated. To
//! do so, call `.s(..)` on the set to reference the bitset in a
//! lazily-evaluated `BitSetStream`, and then use typical binary operators on
//! the `BitSetStream`s.
//! ```rust
//! let a = BitSet::new();
//! let b = BitSet::new();
//! let c = BitSet::new();
//!
//! c.assign(a.s(..) | b.s(..));
//! c ^= a.s(..);
//! ```
//! Supported binary operations are `&`, `|`, `^`, `-`. Note that there is no
//! unary negation, because that would result in an infinite result set. For
//! patterns like `a & !b`, instead use set subtraction `a - b`.

use std::cmp::{max, min};
use std::ops::{
    BitAnd, BitAndAssign, BitOr, BitOrAssign, BitXor, BitXorAssign, RangeFull,
    Sub, SubAssign,
};

#[derive(Clone)]
pub struct BitSet {
    words: Vec<u32>,
}

impl BitSet {
    pub fn new() -> BitSet {
        BitSet { words: Vec::new() }
    }

    fn reserve_words(&mut self, words: usize) {
        if self.words.len() < words {
            self.words.resize(words, 0);
        }
    }

    pub fn reserve(&mut self, bits: usize) {
        self.reserve_words(bits.div_ceil(32));
    }

    pub fn clear(&mut self) {
        for w in self.words.iter_mut() {
            *w = 0;
        }
    }

    pub fn get(&self, idx: usize) -> bool {
        let w = idx / 32;
        let b = idx % 32;
        if w < self.words.len() {
            self.words[w] & (1_u32 << b) != 0
        } else {
            false
        }
    }

    pub fn is_empty(&self) -> bool {
        for w in self.words.iter() {
            if *w != 0 {
                return false;
            }
        }
        true
    }

    pub fn iter(&self) -> impl '_ + Iterator<Item = usize> {
        BitSetIter::new(self)
    }

    pub fn next_unset(&self, start: usize) -> usize {
        if start >= self.words.len() * 32 {
            return start;
        }

        let mut w = start / 32;
        let mut mask = !(u32::MAX << (start % 32));
        while w < self.words.len() {
            let b = (self.words[w] | mask).trailing_ones();
            if b < 32 {
                return w * 32 + usize::try_from(b).unwrap();
            }
            mask = 0;
            w += 1;
        }
        self.words.len() * 32
    }

    pub fn insert(&mut self, idx: usize) -> bool {
        let w = idx / 32;
        let b = idx % 32;
        self.reserve_words(w + 1);
        let exists = self.words[w] & (1_u32 << b) != 0;
        self.words[w] |= 1_u32 << b;
        !exists
    }

    pub fn remove(&mut self, idx: usize) -> bool {
        let w = idx / 32;
        let b = idx % 32;
        self.reserve_words(w + 1);
        let exists = self.words[w] & (1_u32 << b) != 0;
        self.words[w] &= !(1_u32 << b);
        exists
    }

    /// Evaluate an expression and store its value in self
    pub fn assign<B>(&mut self, value: BitSetStream<B>)
    where
        B: BitSetStreamTrait,
    {
        let mut value = value.0;
        let len = value.len();
        self.words.clear();
        self.words.resize_with(len, || value.next());
        for _ in 0..16 {
            debug_assert_eq!(value.next(), 0);
        }
    }

    /// Calculate the union of self and an expression, and store the result in
    /// self.
    ///
    /// Returns true if the value of self changes, or false otherwise. If you
    /// don't need the return value of this function, consider using the `|=`
    /// operator instead.
    pub fn union_with<B>(&mut self, other: BitSetStream<B>) -> bool
    where
        B: BitSetStreamTrait,
    {
        let mut other = other.0;
        let mut added_bits = false;
        let other_len = other.len();
        self.reserve_words(other_len);
        for w in 0..other_len {
            let uw = self.words[w] | other.next();
            if uw != self.words[w] {
                added_bits = true;
                self.words[w] = uw;
            }
        }
        added_bits
    }

    pub fn s<'a>(
        &'a self,
        _: RangeFull,
    ) -> BitSetStream<impl 'a + BitSetStreamTrait> {
        BitSetStream(BitSetStreamFromBitSet {
            iter: self.words.iter().copied(),
        })
    }
}

impl Default for BitSet {
    fn default() -> BitSet {
        BitSet::new()
    }
}

impl FromIterator<usize> for BitSet {
    fn from_iter<T>(iter: T) -> Self
    where
        T: IntoIterator<Item = usize>,
    {
        let mut res = BitSet::new();
        for i in iter {
            res.insert(i);
        }
        res
    }
}

pub trait BitSetStreamTrait {
    /// Get the next word
    ///
    /// Guaranteed to return 0 after len() elements
    fn next(&mut self) -> u32;

    /// Get the number of output words
    fn len(&self) -> usize;
}

struct BitSetStreamFromBitSet<T>
where
    T: ExactSizeIterator<Item = u32>,
{
    iter: T,
}

impl<T> BitSetStreamTrait for BitSetStreamFromBitSet<T>
where
    T: ExactSizeIterator<Item = u32>,
{
    fn next(&mut self) -> u32 {
        self.iter.next().unwrap_or(0)
    }
    fn len(&self) -> usize {
        self.iter.len()
    }
}

pub struct BitSetStream<T>(T)
where
    T: BitSetStreamTrait;

impl<T> From<BitSetStream<T>> for BitSet
where
    T: BitSetStreamTrait,
{
    fn from(value: BitSetStream<T>) -> Self {
        let mut out = BitSet::new();
        out.assign(value);
        out
    }
}

macro_rules! binop {
    (
        $BinOp:ident,
        $bin_op:ident,
        $AssignBinOp:ident,
        $assign_bin_op:ident,
        $Struct:ident,
        |$a:ident, $b:ident| $next_impl:expr,
        |$a_len: ident, $b_len: ident| $len_impl:expr,
    ) => {
        pub struct $Struct<A, B>
        where
            A: BitSetStreamTrait,
            B: BitSetStreamTrait,
        {
            a: A,
            b: B,
        }

        impl<A, B> BitSetStreamTrait for $Struct<A, B>
        where
            A: BitSetStreamTrait,
            B: BitSetStreamTrait,
        {
            fn next(&mut self) -> u32 {
                let $a = self.a.next();
                let $b = self.b.next();
                $next_impl
            }

            fn len(&self) -> usize {
                let $a_len = self.a.len();
                let $b_len = self.b.len();
                let new_len = $len_impl;
                new_len
            }
        }

        impl<A, B> $BinOp<BitSetStream<B>> for BitSetStream<A>
        where
            A: BitSetStreamTrait,
            B: BitSetStreamTrait,
        {
            type Output = BitSetStream<$Struct<A, B>>;

            fn $bin_op(self, rhs: BitSetStream<B>) -> Self::Output {
                BitSetStream($Struct {
                    a: self.0,
                    b: rhs.0,
                })
            }
        }

        impl<B> $AssignBinOp<BitSetStream<B>> for BitSet
        where
            B: BitSetStreamTrait,
        {
            fn $assign_bin_op(&mut self, rhs: BitSetStream<B>) {
                let mut rhs = rhs.0;

                let $a_len = self.words.len();
                let $b_len = rhs.len();
                let expected_word_len = $len_impl;
                self.words.resize(expected_word_len, 0);

                for lhs in &mut self.words {
                    let $a = *lhs;
                    let $b = rhs.next();
                    *lhs = $next_impl;
                }

                for _ in 0..16 {
                    debug_assert_eq!(
                        {
                            let $a = 0;
                            let $b = rhs.next();
                            $next_impl
                        },
                        0
                    );
                }
            }
        }
    };
}

binop!(
    BitAnd,
    bitand,
    BitAndAssign,
    bitand_assign,
    BitSetStreamAnd,
    |a, b| a & b,
    |a, b| min(a, b),
);

binop!(
    BitOr,
    bitor,
    BitOrAssign,
    bitor_assign,
    BitSetStreamOr,
    |a, b| a | b,
    |a, b| max(a, b),
);

binop!(
    BitXor,
    bitxor,
    BitXorAssign,
    bitxor_assign,
    BitSetStreamXor,
    |a, b| a ^ b,
    |a, b| max(a, b),
);

binop!(
    Sub,
    sub,
    SubAssign,
    sub_assign,
    BitSetStreamSub,
    |a, b| a & !b,
    |a, _b| a,
);

struct BitSetIter<'a> {
    set: &'a BitSet,
    w: usize,
    mask: u32,
}

impl<'a> BitSetIter<'a> {
    fn new(set: &'a BitSet) -> Self {
        Self {
            set,
            w: 0,
            mask: u32::MAX,
        }
    }
}

impl<'a> Iterator for BitSetIter<'a> {
    type Item = usize;

    fn next(&mut self) -> Option<usize> {
        while self.w < self.set.words.len() {
            let b = (self.set.words[self.w] & self.mask).trailing_zeros();
            if b < 32 {
                self.mask &= !(1 << b);
                return Some(self.w * 32 + usize::try_from(b).unwrap());
            }
            self.mask = u32::MAX;
            self.w += 1;
        }
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn to_vec(set: &BitSet) -> Vec<usize> {
        set.iter().collect()
    }

    #[test]
    fn test_basic() {
        let mut set = BitSet::new();

        assert_eq!(to_vec(&set), &[]);
        assert!(set.is_empty());

        set.insert(0);

        assert_eq!(to_vec(&set), &[0]);

        set.insert(73);
        set.insert(1);

        assert_eq!(to_vec(&set), &[0, 1, 73]);
        assert!(!set.is_empty());

        assert!(set.get(73));
        assert!(!set.get(197));

        assert!(set.remove(1));
        assert!(!set.remove(7));

        let mut set2 = set.clone();
        assert_eq!(to_vec(&set), &[0, 73]);
        assert!(!set.is_empty());

        assert!(set.remove(0));
        assert!(set.remove(73));
        assert!(set.is_empty());

        set.clear();
        assert!(set.is_empty());

        set2.clear();
        assert!(set2.is_empty());
    }

    #[test]
    fn test_next_unset() {
        for test_range in
            &[0..0, 42..1337, 1337..1337, 31..32, 32..33, 63..64, 64..65]
        {
            let mut set = BitSet::new();
            for i in test_range.clone() {
                set.insert(i);
            }
            for extra_bit in [17, 34, 39] {
                assert!(test_range.end != extra_bit);
                set.insert(extra_bit);
            }
            assert_eq!(set.next_unset(test_range.start), test_range.end);
        }
    }

    #[test]
    fn test_from_iter() {
        let vec = vec![0, 1, 99];
        let set: BitSet = vec.clone().into_iter().collect();
        assert_eq!(to_vec(&set), vec);
    }

    #[test]
    fn test_or() {
        let a: BitSet = vec![9, 23, 18, 72].into_iter().collect();
        let b: BitSet = vec![7, 23, 1337].into_iter().collect();
        let expected = vec![7, 9, 18, 23, 72, 1337];

        assert_eq!(to_vec(&(a.s(..) | b.s(..)).into()), &expected[..]);
        assert_eq!(to_vec(&(b.s(..) | a.s(..)).into()), &expected[..]);

        let mut actual_1 = a.clone();
        actual_1 |= b.s(..);
        assert_eq!(to_vec(&actual_1), &expected[..]);

        let mut actual_2 = b.clone();
        actual_2 |= a.s(..);
        assert_eq!(to_vec(&actual_2), &expected[..]);

        let mut actual_3 = a.clone();
        assert_eq!(actual_3.union_with(a.s(..)), false);
        assert_eq!(actual_3.union_with(b.s(..)), true);
        assert_eq!(to_vec(&actual_3), &expected[..]);

        let mut actual_4 = b.clone();
        assert_eq!(actual_4.union_with(b.s(..)), false);
        assert_eq!(actual_4.union_with(a.s(..)), true);
        assert_eq!(to_vec(&actual_4), &expected[..]);
    }

    #[test]
    fn test_and() {
        let a: BitSet = vec![1337, 42, 7, 1].into_iter().collect();
        let b: BitSet = vec![42, 783, 2, 7].into_iter().collect();
        let expected = vec![7, 42];

        assert_eq!(to_vec(&(a.s(..) & b.s(..)).into()), &expected[..]);
        assert_eq!(to_vec(&(b.s(..) & a.s(..)).into()), &expected[..]);

        let mut actual_1 = a.clone();
        actual_1 &= b.s(..);
        assert_eq!(to_vec(&actual_1), &expected[..]);

        let mut actual_2 = b.clone();
        actual_2 &= a.s(..);
        assert_eq!(to_vec(&actual_2), &expected[..]);
    }

    #[test]
    fn test_xor() {
        let a: BitSet = vec![1337, 42, 7, 1].into_iter().collect();
        let b: BitSet = vec![42, 127, 2, 7].into_iter().collect();
        let expected = vec![1, 2, 127, 1337];

        assert_eq!(to_vec(&(a.s(..) ^ b.s(..)).into()), &expected[..]);
        assert_eq!(to_vec(&(b.s(..) ^ a.s(..)).into()), &expected[..]);

        let mut actual_1 = a.clone();
        actual_1 ^= b.s(..);
        assert_eq!(to_vec(&actual_1), &expected[..]);

        let mut actual_2 = b.clone();
        actual_2 ^= a.s(..);
        assert_eq!(to_vec(&actual_2), &expected[..]);
    }

    #[test]
    fn test_sub() {
        let a: BitSet = vec![1337, 42, 7, 1].into_iter().collect();
        let b: BitSet = vec![42, 127, 2, 7].into_iter().collect();
        let expected_1 = vec![1, 1337];
        let expected_2 = vec![2, 127];

        assert_eq!(to_vec(&(a.s(..) - b.s(..)).into()), &expected_1[..]);
        assert_eq!(to_vec(&(b.s(..) - a.s(..)).into()), &expected_2[..]);

        let mut actual_1 = a.clone();
        actual_1 -= b.s(..);
        assert_eq!(to_vec(&actual_1), &expected_1[..]);

        let mut actual_2 = b.clone();
        actual_2 -= a.s(..);
        assert_eq!(to_vec(&actual_2), &expected_2[..]);
    }

    #[test]
    fn test_compund() {
        let a: BitSet = vec![1337, 42, 7, 1].into_iter().collect();
        let b: BitSet = vec![42, 127, 2, 7].into_iter().collect();
        let mut c = BitSet::new();

        c &= a.s(..) | b.s(..);
        assert!(c.is_empty());
    }
}
