pub trait TryFromWithErr<T, E>: Sized {
    fn try_from_with_err(value: T, error: E) -> Result<Self, E>;
}

impl<T, U, E> TryFromWithErr<U, E> for T
where
    T: TryFrom<U>,
{
    fn try_from_with_err(value: U, error: E) -> Result<T, E> {
        T::try_from(value).map_err(|_| error)
    }
}

pub trait TryIntoWithErr<T, E>: Sized {
    fn try_into_with_err(self, error: E) -> Result<T, E>;
}

impl<T, U, E> TryIntoWithErr<T, E> for U
where
    T: TryFromWithErr<U, E>,
{
    fn try_into_with_err(self, error: E) -> Result<T, E> {
        T::try_from_with_err(self, error)
    }
}
