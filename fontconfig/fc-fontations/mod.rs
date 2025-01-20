extern crate fc_fontations_bindgen;

use fc_fontations_bindgen::{fcint::FcPatternCreate, FcFontSet, FcFontSetAdd};

#[no_mangle]
/// Externally called in fcfontations.c as the file scanner function
/// similar to the job that FreeType performs.
///
/// # Safety
/// * At this point, the font file path is not dereferenced.
/// * In this initial sanity check mock call, only one empty pattern
///   is added to the FontSet, which is null checked, which is sound.
pub unsafe extern "C" fn add_patterns_to_fontset(
    _: *const libc::c_char,
    font_set: *mut FcFontSet,
) -> libc::c_int {
    let empty_pattern = FcPatternCreate();
    if !font_set.is_null() {
        FcFontSetAdd(
            font_set,
            empty_pattern as *mut fc_fontations_bindgen::FcPattern,
        )
    } else {
        0
    }
}

#[cfg(test)]
mod test {
    use crate::add_patterns_to_fontset;
    use fc_fontations_bindgen::{FcFontSetCreate, FcFontSetDestroy};

    #[test]
    fn basic_pattern_construction() {
        unsafe {
            let font_set = FcFontSetCreate();
            assert!(add_patterns_to_fontset(std::ptr::null(), font_set) == 1);
            FcFontSetDestroy(font_set);
        }
    }
}
