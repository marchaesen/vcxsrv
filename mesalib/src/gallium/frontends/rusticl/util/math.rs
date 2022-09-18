use std::ops::Add;
use std::ops::Rem;
use std::ops::Sub;

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

pub fn align<T>(val: T, a: T) -> T
where
    T: Add<Output = T>,
    T: Copy,
    T: Default,
    T: PartialEq,
    T: Rem<Output = T>,
    T: Sub<Output = T>,
{
    let tmp = val % a;
    if tmp == T::default() {
        val
    } else {
        val + (a - tmp)
    }
}
