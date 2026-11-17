//! C interface for the Android app (`libgba_recomp.a`): the phone generates the C of the game from the ROM the user picked.
//! Nothing here keeps anything of the ROM: the C goes to the file the caller names, and the caller keeps it in its private folder.
use crate::rom::Rom;
use std::ffi::{c_char, CStr};

fn put(msg: *mut c_char, len: usize, s: &str) {
    if msg.is_null() || len == 0 {
        return;
    }
    let n = s.len().min(len - 1);
    // SAFETY: the caller gives a buffer of `len` bytes
    unsafe {
        std::ptr::copy_nonoverlapping(s.as_ptr(), msg as *mut u8, n);
        *msg.add(n) = 0;
    }
}

fn run(rom: &[u8], profile: &str, roots: &str, out: &str) -> Result<String, String> {
    if rom.len() < 0xC0 {
        return Err("the file is too small for a GBA ROM".into());
    }
    let p = crate::profile::parse_with(profile, &|_| Ok(roots.to_string()))?;
    let rom = Rom { data: rom.to_vec(), overlays: p.overlays };
    let g = crate::cgen::generate(&rom, &p.roots, p.resume, true);
    std::fs::write(out, &g.text).map_err(|e| format!("{out}: {e}"))?;
    Ok(format!("functions {}, translated {}, not translated {} (callees: {})", g.functions, g.translated, g.functions - g.translated, g.missing))
}

/// Generates the C of the game into `out_path`. `profile` is the text of `games/<id>.toml` and `roots` the text of the roots file
/// it names. Returns 0 and a summary in `msg`, or 1 and the reason.
///
/// # Safety
/// The pointers must be valid: `rom` for `rom_len` bytes, the strings NUL-terminated, `msg` for `msg_len` bytes.
#[no_mangle]
pub unsafe extern "C" fn gba_gen_generate(
    rom: *const u8,
    rom_len: usize,
    profile: *const c_char,
    roots: *const c_char,
    out_path: *const c_char,
    msg: *mut c_char,
    msg_len: usize,
) -> i32 {
    let rom = std::slice::from_raw_parts(rom, rom_len);
    let (profile, roots, out) = (
        CStr::from_ptr(profile).to_string_lossy().into_owned(),
        CStr::from_ptr(roots).to_string_lossy().into_owned(),
        CStr::from_ptr(out_path).to_string_lossy().into_owned(),
    );
    match std::panic::catch_unwind(|| run(rom, &profile, &roots, &out)) {
        Ok(Ok(s)) => {
            put(msg, msg_len, &s);
            0
        }
        Ok(Err(e)) => {
            put(msg, msg_len, &e);
            1
        }
        Err(_) => {
            put(msg, msg_len, "the generator stopped unexpectedly");
            1
        }
    }
}
