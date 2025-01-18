use std::ops::Rem;

pub fn gcd<T>(mut a: T, mut b: T) -> T
where
    T: Copy + Default + PartialEq,
    T: Rem<Output = T>,
{
    let mut c = a % b;
    while c != T::default() {
        a = b;
        b = c;
        c = a % b;
    }

    b
}

pub struct SetBitIndices<T> {
    val: T,
}

impl<T> SetBitIndices<T> {
    pub fn from_msb(val: T) -> Self {
        Self { val: val }
    }
}

impl Iterator for SetBitIndices<u32> {
    type Item = u32;

    fn next(&mut self) -> Option<Self::Item> {
        if self.val == 0 {
            None
        } else {
            let pos = u32::BITS - self.val.leading_zeros() - 1;
            self.val ^= 1 << pos;
            Some(pos)
        }
    }
}
