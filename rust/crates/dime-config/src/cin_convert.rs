//! `CConfig::parseCINFile` (phase 5b-15b): a .cin table or the custom
//! table text converted to the UTF-16 file the IME loads.
//!
//! The C++ reads the source with `_wfopen_s(..., "r, ccs=UTF-8")` (custom
//! table: `ccs=UTF-16LE`), one `fgetws(line, 1024)` at a time, tries three
//! `swscanf_s` patterns for "key, separator, value", and writes with
//! `_wfopen_s(..., "w+, ccs=UTF-16LE")` (BOM, `\n` written as `\r\n`).
//! Everything here follows the CRT as it behaves, including its limits:
//! a key of 64 units or more, a separator of 64 or more and a value of
//! 960 or more do not parse; long lines are read in pieces of 1023 units;
//! escaped keys and values are cut to 63 / 959 units like `StringCchCat`.

use dime_core::text::TextOps;

/// `fgetws(line, MAX_TABLE_LINE_LENGTH)`: at most this many units per piece.
const LINE_PIECE: usize = 1023;
/// `key[MAX_KEY_LENGTH]`, `sep[MAX_KEY_LENGTH]`: longest token is one less.
const KEY_CAP: usize = 64;
/// `value[MAX_VALUE_LENGTH]`.
const VALUE_CAP: usize = 960;
/// `others[MAX_COMMIT_LENGTH]`.
const OTHERS_CAP: usize = 1024;

const TAB: u16 = b'\t' as u16;
const CR: u16 = b'\r' as u16;
const LF: u16 = b'\n' as u16;
const SPACE: u16 = b' ' as u16;
const QUOTE: u16 = b'"' as u16;
const BACKSLASH: u16 = b'\\' as u16;
const CTRL_Z: u16 = 0x1A;

fn w(s: &str) -> Vec<u16> {
    s.encode_utf16().collect()
}

/// A failed read (`EILSEQ`) in a [`Source`].
const READ_ERROR: u32 = u32::MAX;

/// The source as the CRT delivers it to `fgetws`: UTF-16 units, with
/// `READ_ERROR` where a read failed.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Source(Vec<u32>);

impl Source {
    /// Text that reads without errors.
    pub fn from_text(text: &[u16]) -> Source {
        Source(text.iter().map(|&c| u32::from(c)).collect())
    }
}

/// The source text as the CRT's Unicode text mode reads it: a UTF-8 or
/// UTF-16LE BOM decides the encoding (and is skipped), otherwise the `ccs`
/// of the open call does; `\r\n` becomes `\n`; Ctrl-Z ends the text. `utf8`
/// converts UTF-8 bytes like `MultiByteToWideChar`.
pub fn decode_source(bytes: &[u8], custom_table: bool, utf8: &dyn Fn(&[u8]) -> Vec<u16>) -> Source {
    let units: Vec<u32> = if let Some(rest) = bytes.strip_prefix(&[0xEF, 0xBB, 0xBF]) {
        crt_utf8(rest, utf8)
    } else if let Some(rest) = bytes.strip_prefix(&[0xFF, 0xFE]) {
        utf16le(rest)
    } else if custom_table {
        utf16le(bytes)
    } else {
        crt_utf8(bytes, utf8)
    };
    let units = &units[..units.iter().position(|&c| c == u32::from(CTRL_Z)).unwrap_or(units.len())];
    let mut out = Vec::with_capacity(units.len());
    let mut i = 0;
    while i < units.len() {
        if units[i] == u32::from(CR) && units.get(i + 1) == Some(&u32::from(LF)) {
            out.push(u32::from(LF));
            i += 2;
        } else {
            out.push(units[i]);
            i += 1;
        }
    }
    Source(out)
}

/// The UCRT's `_read` of a `ccs=UTF-8` text file: 2048 bytes at a time.
/// When a piece ends inside a multi-byte sequence (the last lead byte,
/// found within four bytes from the end, does not have exactly the bytes
/// it needs after it), that sequence is left for the next read, and at the
/// end of the file it is lost. When no lead byte is found that way, the
/// read fails (`EILSEQ`): the piece is lost and reading goes on after it.
/// Lead bytes are 0xC0..=0xF7. Each piece is converted like
/// `MultiByteToWideChar` (invalid bytes become U+FFFD).
fn crt_utf8(bytes: &[u8], utf8: &dyn Fn(&[u8]) -> Vec<u16>) -> Vec<u32> {
    const PIECE: usize = 2048;
    fn trail(b: u8) -> usize {
        match b {
            0xC0..=0xDF => 1,
            0xE0..=0xEF => 2,
            0xF0..=0xF7 => 3,
            _ => 0,
        }
    }
    let mut out = Vec::new();
    let mut pos = 0;
    while pos < bytes.len() {
        let c = &bytes[pos..(pos + PIECE).min(bytes.len())];
        let take = if c[c.len() - 1] & 0x80 == 0 {
            Some(c.len())
        } else {
            let mut j = c.len() - 1;
            let mut count = 1;
            let mut lead = true;
            while trail(c[j]) == 0 && count <= 4 {
                if j == 0 {
                    lead = false;
                    break;
                }
                j -= 1;
                count += 1;
            }
            let t = trail(c[j]);
            if !lead || t == 0 {
                None
            } else if t + 1 == count {
                Some(c.len())
            } else {
                Some(j)
            }
        };
        match take {
            None => {
                out.push(READ_ERROR);
                pos += c.len();
            }
            Some(0) => break,
            Some(n) => {
                out.extend(utf8(&c[..n]).into_iter().map(u32::from));
                pos += n;
            }
        }
    }
    out
}

fn utf16le(bytes: &[u8]) -> Vec<u32> {
    bytes.as_chunks::<2>().0.iter().map(|b| u32::from(u16::from_le_bytes(*b))).collect()
}

/// The lines `while (fgetws(line, 1024, f) != NULL)` gets: up to and
/// including `\n`, at most 1023 units. A failed read ends the line being
/// read; when nothing was read yet, `fgetws` returns NULL and the loop ends.
fn pieces(src: &Source) -> Vec<Vec<u16>> {
    let mut out = Vec::new();
    let mut cur: Vec<u16> = Vec::new();
    for &u in &src.0 {
        if u == READ_ERROR {
            if cur.is_empty() {
                return out;
            }
            out.push(std::mem::take(&mut cur));
            continue;
        }
        let c = u as u16;
        cur.push(c);
        if c == LF || cur.len() == LINE_PIECE {
            out.push(std::mem::take(&mut cur));
        }
    }
    if !cur.is_empty() {
        out.push(cur);
    }
    out
}
/// `%[set]` / `%[^set]` with a buffer of `cap` units: the matched run, or
/// `None` when it is empty or does not fit (`swscanf_s` then stops).
fn scan_set<'a>(s: &'a [u16], pos: &mut usize, cap: usize, inside: impl Fn(u16) -> bool) -> Option<&'a [u16]> {
    let start = *pos;
    let mut end = start;
    while end < s.len() && inside(s[end]) {
        end += 1;
    }
    if end == start || end - start >= cap {
        return None;
    }
    *pos = end;
    Some(&s[start..end])
}

/// One `swscanf_s` pattern: key set, separator, value set, and (`others`)
/// a trailing `%s`. Returns the key and value when the call returns 3.
fn try_pattern<'a>(
    ops: &dyn TextOps,
    s: &'a [u16],
    key_stops_at_space: bool,
    value_stops_at_space: bool,
    others: bool,
) -> Option<(&'a [u16], &'a [u16])> {
    let mut pos = 0;
    let key = scan_set(s, &mut pos, KEY_CAP, |c| {
        !(c == TAB || c == CR || c == LF || (key_stops_at_space && c == SPACE))
    })?;
    scan_set(s, &mut pos, KEY_CAP, |c| c == SPACE || c == TAB || c == CR || c == LF)?;
    let value = scan_set(s, &mut pos, VALUE_CAP, |c| {
        !(c == TAB || c == CR || c == LF || (value_stops_at_space && c == SPACE))
    })?;
    if others {
        // %s: skip white space, then one or more non-space units. A fourth
        // field means the call returns 4; one too long for its buffer, or
        // none at all, leaves it at 3.
        while pos < s.len() && ops.is_space(s[pos]) {
            pos += 1;
        }
        let start = pos;
        while pos < s.len() && !ops.is_space(s[pos]) {
            pos += 1;
        }
        if pos > start && pos - start < OTHERS_CAP {
            return None;
        }
    }
    Some((key, value))
}

/// `StringCchCat` into a buffer of `cap` units: as much as fits.
fn cat(dst: &mut Vec<u16>, cap: usize, src: &[u16]) {
    for &c in src {
        if dst.len() + 1 >= cap {
            return;
        }
        dst.push(c);
    }
}

fn escaped(s: &[u16], cap: usize) -> Vec<u16> {
    let mut out = Vec::new();
    cat(&mut out, cap, &[QUOTE]);
    for &c in s {
        match c {
            QUOTE => cat(&mut out, cap, &[BACKSLASH, QUOTE]),
            BACKSLASH => cat(&mut out, cap, &[BACKSLASH, BACKSLASH]),
            _ => cat(&mut out, cap, &[c]),
        }
    }
    cat(&mut out, cap, &[QUOTE]);
    out
}

fn quoted(s: &[u16]) -> bool {
    s.len() >= 2 && s[0] == QUOTE && s[s.len() - 1] == QUOTE
}

/// The converted file's text (no BOM, `\n` line ends): the body of
/// `CConfig::parseCINFile` between opening and closing the two files.
pub fn convert(ops: &dyn TextOps, source: &Source, custom_table: bool) -> Vec<u16> {
    let mut out = Vec::new();
    let mut do_escape = false;
    if custom_table {
        out.extend(w("%chardef begin\n"));
        do_escape = true;
    }
    let is = |a: &[u16], b: &str| ops.eq(a, &w(b));
    for piece in pieces(source) {
        let piece = &piece[..];
        // C string semantics from here on: the line ends at a NUL.
        let line = &piece[..piece.iter().position(|&c| c == 0).unwrap_or(piece.len())];
        let parsed = try_pattern(ops, line, false, true, true)
            .or_else(|| try_pattern(ops, line, true, true, true))
            .or_else(|| try_pattern(ops, line, true, false, false));
        let Some((key, value)) = parsed else {
            if !do_escape {
                out.extend_from_slice(line);
            }
            continue;
        };
        let section = is(key, "%keyname") || is(key, "%chardef");
        if section && is(value, "begin") {
            do_escape = true;
            out.extend_from_slice(key);
            out.push(SPACE);
            out.extend_from_slice(value);
            out.push(LF);
            continue;
        }
        if section && is(value, "end") {
            do_escape = false;
            out.extend_from_slice(key);
            out.push(SPACE);
            out.extend_from_slice(value);
            out.push(LF);
            continue;
        }
        if is(key, "%encoding") {
            out.extend_from_slice(key);
            out.extend(w("\tUTF-16LE\n"));
            continue;
        }
        if do_escape && !(quoted(key) && quoted(value)) {
            out.extend(escaped(key, KEY_CAP));
            out.push(TAB);
            out.extend(escaped(value, VALUE_CAP));
        } else {
            out.extend_from_slice(key);
            out.push(TAB);
            out.extend_from_slice(value);
        }
        out.push(LF);
    }
    if custom_table {
        out.extend(w("%chardef end\n"));
    }
    out
}

/// The bytes `_wfopen_s(..., "w+, ccs=UTF-16LE")` puts in the file for
/// `text`: a BOM, then UTF-16LE with every `\n` written as `\r\n`.
pub fn encode_output(text: &[u16]) -> Vec<u8> {
    let mut out = vec![0xFF, 0xFE];
    for &c in text {
        if c == LF {
            out.extend_from_slice(&CR.to_le_bytes());
        }
        out.extend_from_slice(&c.to_le_bytes());
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use dime_core::text::SimpleTextOps;

    fn s(text: &str) -> Vec<u16> {
        w(text)
    }

    fn run(src: &str, custom: bool) -> String {
        String::from_utf16(&convert(&SimpleTextOps, &Source::from_text(&s(src)), custom)).unwrap()
    }

    #[test]
    fn plain_table() {
        let src = "%gen_inp\n%ename Test\n%encoding UTF-8\n%chardef begin\na \u{4E2D}\nb\t\u{6587}\n%chardef end\n";
        let out = run(src, false);
        assert_eq!(
            out,
            "%gen_inp\n%ename\tTest\n%encoding\tUTF-16LE\n%chardef begin\n\"a\"\t\"\u{4E2D}\"\n\"b\"\t\"\u{6587}\"\n%chardef end\n"
        );
    }

    #[test]
    fn custom_table_wraps_and_escapes() {
        let out = run("a\"b \\x\nbad\n", true);
        assert_eq!(out, "%chardef begin\n\"a\\\"b\"\t\"\\\\x\"\n%chardef end\n");
    }

    #[test]
    fn key_with_spaces_and_value_with_spaces() {
        // Pattern 1: key up to the tab.
        assert_eq!(run("a b\tc\n", false), "a b\tc\n");
        // Pattern 3: value with spaces.
        assert_eq!(run("a b c\n", false), "a\tb c\n");
    }

    #[test]
    fn long_key_does_not_parse() {
        let key: String = "k".repeat(64);
        assert_eq!(run(&format!("{key} v\n"), false), format!("{key} v\n"));
        let key: String = "k".repeat(63);
        assert_eq!(run(&format!("{key} v\n"), false), format!("{key}\tv\n"));
    }

    #[test]
    fn output_bytes() {
        assert_eq!(encode_output(&s("a\n")), vec![0xFF, 0xFE, b'a', 0, b'\r', 0, b'\n', 0]);
    }

    #[test]
    fn decode_bom_and_crlf() {
        let utf8 = |b: &[u8]| String::from_utf8_lossy(b).encode_utf16().collect::<Vec<u16>>();
        assert_eq!(decode_source(b"\xEF\xBB\xBFa\r\nb", true, &utf8), Source::from_text(&s("a\nb")));
        assert_eq!(decode_source(b"\xFF\xFEa\0\r\0\n\0", false, &utf8), Source::from_text(&s("a\n")));
        assert_eq!(decode_source(b"a\0", true, &utf8), Source::from_text(&s("a")));
    }
}
