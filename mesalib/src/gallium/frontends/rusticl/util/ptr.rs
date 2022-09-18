use std::ptr;

pub trait CheckedPtr<T> {
    /// # Safety
    ///
    /// besides a null check the function can't make sure the pointer is valid
    /// for the entire size
    unsafe fn copy_checked(self, val: *const T, size: usize);
    fn write_checked(self, val: T);
}

impl<T> CheckedPtr<T> for *mut T {
    unsafe fn copy_checked(self, val: *const T, size: usize) {
        if !self.is_null() {
            ptr::copy(val, self, size);
        }
    }

    fn write_checked(self, val: T) {
        if !self.is_null() {
            unsafe {
                *self = val;
            }
        }
    }
}
