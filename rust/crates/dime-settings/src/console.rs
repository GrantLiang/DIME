//! The CLI's output streams (phase 5b-15f, Windows only).
//!
//! `wWinMain` pointed the CRT's `stdout` / `stderr` at the inherited handles
//! (`_open_osfhandle(h, _O_WRONLY | _O_TEXT)` + `_dup2`) or at `CONOUT$`
//! (`freopen_s`), and `CLI.cpp`'s `WriteWide` printed with
//! `fwprintf_s(f, L"%.*s", n, s)` in pieces of at most 1,048,576 units. The
//! process never calls `setlocale`, so the streams are narrow text-mode
//! streams in the "C" locale. What the UCRT then writes (probed with a
//! `/MT` program, to a file and to a console):
//!
//! - each `fwprintf_s` call stops at the first NUL of its piece (`%.*s`);
//! - a unit up to U+00FF is one byte of the same value, anything else
//!   (CJK, `€`, either half of a surrogate pair, a lone surrogate) is `?`;
//! - `\n` becomes `\r\n` when the bytes reach the handle (`\r\n` in the
//!   text becomes `\r\r\n`); nothing else is translated;
//! - a console gets the same bytes (they show in its code page);
//! - `stdout` on a pipe or file keeps 4,096 untranslated bytes and writes
//!   them when the buffer is full and at the final `fflush`; on a character
//!   device (a console) and for `stderr` every call is written at once. That
//!   decides the order when both handles are the same pipe (`2>&1`).

#![allow(clippy::not_unsafe_ptr_arg_deref)]

use std::ptr::null_mut;

type Handle = *mut std::ffi::c_void;

windows_link::link!("kernel32.dll" "system" fn WriteFile(h: Handle, buf: *const u8, len: u32, written: *mut u32, ov: *mut std::ffi::c_void) -> i32);
windows_link::link!("kernel32.dll" "system" fn GetFileType(h: Handle) -> u32);

const FILE_TYPE_CHAR: u32 = 2;
/// `WriteWide`'s piece size.
const PIECE: usize = 1_048_576;
/// The UCRT stream buffer (`_INTERNAL_BUFSIZ`).
const STREAM_BUFFER: usize = 4096;

/// What one `fwprintf_s(f, L"%.*s", n, s)` hands the stream: the narrow
/// bytes before text-mode translation.
pub fn narrow_piece(piece: &[u16]) -> Vec<u8> {
    piece
        .iter()
        .take_while(|&&c| c != 0)
        .map(|&c| if c <= 0xFF { c as u8 } else { b'?' })
        .collect()
}

/// `WriteWide`: the stream bytes for `text`, piece by piece.
pub fn narrow_text(text: &[u16]) -> Vec<u8> {
    text.chunks(PIECE).flat_map(narrow_piece).collect()
}

/// The text-mode translation `_write` applies: `\n` → `\r\n`.
pub fn translate(bytes: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(bytes.len() + bytes.len() / 8);
    for &b in bytes {
        if b == b'\n' {
            out.push(b'\r');
        }
        out.push(b);
    }
    out
}

/// One CRT output stream on a Win32 handle. A null or invalid handle drops
/// everything (a GUI process's unset `stderr`).
pub struct Stream {
    handle: Handle,
    /// `None`: every call goes out at once.
    buffer: Option<Vec<u8>>,
}

impl Stream {
    /// `stdout` after `_dup2`: buffered unless the handle is a character
    /// device.
    pub fn stdout(handle: Handle) -> Self {
        let buffered = valid(handle) && unsafe { GetFileType(handle) } != FILE_TYPE_CHAR;
        Stream { handle, buffer: buffered.then(Vec::new) }
    }

    /// `stderr`, or any stream written at once.
    pub fn unbuffered(handle: Handle) -> Self {
        Stream { handle, buffer: None }
    }

    /// `WriteWide(f, text, len)`.
    pub fn write_wide(&mut self, text: &[u16]) {
        if text.is_empty() {
            return;
        }
        for piece in text.chunks(PIECE) {
            let bytes = narrow_piece(piece);
            self.put(&bytes);
        }
    }

    fn put(&mut self, bytes: &[u8]) {
        match &mut self.buffer {
            None => self.raw(bytes),
            Some(buf) => {
                // `_fputc_nolock` / `_flsbuf`: a full buffer goes out whole
                // before the next byte is stored.
                let mut full: Vec<Vec<u8>> = Vec::new();
                for &b in bytes {
                    if buf.len() == STREAM_BUFFER {
                        full.push(std::mem::take(buf));
                    }
                    buf.push(b);
                }
                for chunk in full {
                    self.raw(&chunk);
                }
            }
        }
    }

    /// `fflush`.
    pub fn flush(&mut self) {
        if let Some(buf) = &mut self.buffer {
            let bytes = std::mem::take(buf);
            if !bytes.is_empty() {
                self.raw(&bytes);
            }
        }
    }

    fn raw(&self, bytes: &[u8]) {
        if !valid(self.handle) {
            return;
        }
        let out = translate(bytes);
        let mut off = 0;
        while off < out.len() {
            let n = (out.len() - off).min(u32::MAX as usize) as u32;
            let mut written = 0u32;
            // SAFETY: `out` outlives the call; the handle is the caller's.
            let ok = unsafe { WriteFile(self.handle, out[off..].as_ptr(), n, &mut written, null_mut()) };
            if ok == 0 || written == 0 {
                return;
            }
            off += written as usize;
        }
    }
}

impl Drop for Stream {
    fn drop(&mut self) {
        self.flush();
    }
}

fn valid(h: Handle) -> bool {
    !h.is_null() && h as isize != -1
}

#[cfg(test)]
mod tests {
    use super::*;

    fn w(s: &str) -> Vec<u16> {
        s.encode_utf16().collect()
    }

    #[test]
    fn narrow_matches_the_probe() {
        assert_eq!(narrow_text(&w("a\nb\r\n")), b"a\nb\r\n");
        assert_eq!(narrow_text(&w("x\u{e9}y")), b"x\xe9y");
        assert_eq!(narrow_text(&w("p\u{4e2d}q")), b"p?q");
        assert_eq!(narrow_text(&w("a\u{1F600}b")), b"a??b");
        assert_eq!(narrow_text(&[0x63, 0x85, 0x1a, 0x7f, 0xff]), b"c\x85\x1a\x7f\xff");
        assert_eq!(narrow_text(&[0x65, 0, 0x66]), b"e");
        assert_eq!(narrow_text(&[0xdc00, 0x67, 0xd800]), b"?g?");
        assert_eq!(translate(b"a\nb\r\n"), b"a\r\nb\r\r\n");
    }

    #[test]
    fn nul_stops_only_its_piece() {
        let mut text = vec![0x41u16; PIECE + 2];
        text[1] = 0;
        let out = narrow_text(&text);
        assert_eq!(out.len(), 1 + 2);
    }
}
