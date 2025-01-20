#[derive(Default)]
pub struct Properties<T> {
    props: Vec<T>,
}

/// This encapsulates a C property array, where the list is 0 terminated.
impl<T> Properties<T> {
    /// Creates a Properties object copying from the supplied pointer.
    ///
    /// It returns `None` if any property is found twice.
    ///
    /// If `p` is null the saved list of properties will be empty. Otherwise it will be 0
    /// terminated.
    ///
    /// # Safety
    ///
    /// Besides `p` being valid to be dereferenced, it also needs to point to a `T::default()`
    /// terminated array of `T`.
    pub unsafe fn new(mut p: *const T) -> Option<Self>
    where
        T: Copy + Default + PartialEq,
    {
        let mut res = Self::default();
        if !p.is_null() {
            unsafe {
                while *p != T::default() {
                    // Property lists are expected to be small, so no point in using HashSet or
                    // sorting.
                    if res.get(&*p).is_some() {
                        return None;
                    }

                    res.props.push(*p);
                    res.props.push(*p.add(1));

                    // Advance by two as we read through a list of pairs.
                    p = p.add(2);
                }
            }

            // terminate the array
            res.props.push(T::default());
        }

        Some(res)
    }

    /// Returns the value for the given `key` if existent.
    pub fn get(&self, key: &T) -> Option<&T>
    where
        T: PartialEq,
    {
        self.iter().find_map(|(k, v)| (k == key).then_some(v))
    }

    /// Returns true when there is no property available.
    pub fn is_empty(&self) -> bool {
        self.props.len() <= 1
    }

    pub fn iter(&self) -> impl Iterator<Item = (&T, &T)> {
        // TODO: use array_chuncks once stabilized
        self.props
            .chunks_exact(2)
            .map(|elems| (&elems[0], &elems[1]))
    }

    /// Returns the amount of key/value pairs available.
    pub fn len(&self) -> usize {
        // only valid lengths are all uneven numbers and 0, so division by 2 gives us always the
        // correct result.
        self.props.len() / 2
    }

    /// Returns a slice to the raw buffer.
    ///
    /// It will return an empty slice if `self` was created with a null pointer. A `T::default()`
    /// terminated one otherwise.
    pub fn raw_data(&self) -> &[T] {
        &self.props
    }
}
