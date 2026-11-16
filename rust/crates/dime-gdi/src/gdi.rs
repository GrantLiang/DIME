//! The GDI / GDI+ calls of the Rust windows' painting, with a recorder
//! for tests (phase 6a-3, Windows only).
//!
//! Each function here has the signature of the system function it wraps and
//! calls it unchanged. While a recording is on (this thread only, started
//! and taken by `dime_settings_gdi_record_*`), every call also appends one
//! line to the log: the function, its arguments, and what it returned. A
//! handle the recording created is numbered in order of creation (`h1`,
//! `h2`, ...) until it is deleted; any other handle (a DC, font or brush the
//! caller made) is written by what it is (`dc`, `font(-16,Segoe UI)`,
//! `brush(0,f0f0f0)`), because the system reuses the values of freed handles
//! and a numbering by value would depend on that; a stock object is
//! `stock(n)` (what `DEFAULT_GUI_FONT` is depends on the system's locale).
//! So the log has no addresses.
//!
//! A recording with synthetic metrics makes the text measurements machine
//! independent: `GetTextExtentPoint32W`, `GetTextMetricsW`, `GetTextFaceW`
//! and `DrawTextW` (its result, and the rectangle of `DT_CALCRECT`) answer
//! from the selected font's requested height instead of the installed fonts
//! (a character below U+0080 is half the height wide, any other the full
//! height; lines are the height). The pixels are then not the real ones, but
//! the log is the same on every machine: the golden files of the paint tests
//! are these logs, since pixels depend on the machine's fonts, ClearType and
//! dpi.

//!
//! # Safety
//!
//! Every function has the contract of the system function of the same name:
//! valid handles, and pointers to buffers of the documented sizes.

#![allow(clippy::too_many_arguments)]
#![allow(clippy::not_unsafe_ptr_arg_deref)]
#![allow(clippy::missing_safety_doc)]
#![allow(non_snake_case)]

use std::cell::RefCell;
use std::collections::HashMap;
use std::ffi::c_void;
use std::fmt::Write as _;

pub type Handle = *mut c_void;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Rect {
    pub left: i32,
    pub top: i32,
    pub right: i32,
    pub bottom: i32,
}

impl Rect {
    pub const fn new(left: i32, top: i32, right: i32, bottom: i32) -> Rect {
        Rect {
            left,
            top,
            right,
            bottom,
        }
    }
}

#[repr(C)]
#[derive(Default)]
pub struct Size {
    pub cx: i32,
    pub cy: i32,
}

#[repr(C)]
#[derive(Default)]
pub struct TextMetric {
    pub tm_height: i32,
    pub tm_ascent: i32,
    pub tm_descent: i32,
    pub tm_internal_leading: i32,
    pub tm_external_leading: i32,
    pub tm_ave_char_width: i32,
    pub tm_max_char_width: i32,
    pub tm_weight: i32,
    pub tm_overhang: i32,
    pub tm_digitized_aspect_x: i32,
    pub tm_digitized_aspect_y: i32,
    pub tm_first_char: u16,
    pub tm_last_char: u16,
    pub tm_default_char: u16,
    pub tm_break_char: u16,
    pub tm_italic: u8,
    pub tm_underlined: u8,
    pub tm_struck_out: u8,
    pub tm_pitch_and_family: u8,
    pub tm_char_set: u8,
}

#[repr(C)]
#[derive(Default)]
pub struct BitmapInfo {
    pub bi_size: u32,
    pub bi_width: i32,
    pub bi_height: i32,
    pub bi_planes: u16,
    pub bi_bit_count: u16,
    pub bi_compression: u32,
    pub bi_size_image: u32,
    pub bi_x_pels_per_meter: i32,
    pub bi_y_pels_per_meter: i32,
    pub bi_clr_used: u32,
    pub bi_clr_important: u32,
    pub colors: u32,
}

/// `LOGBRUSH`, for describing brushes the recording did not create.
#[repr(C)]
#[derive(Default)]
struct LogBrush {
    style: u32,
    color: u32,
    hatch: usize,
}

/// `LOGFONTW`, read back from fonts created outside a recording.
#[repr(C)]
#[derive(Default)]
struct LogFontW {
    height: i32,
    width: i32,
    escapement: i32,
    orientation: i32,
    weight: i32,
    italic: u8,
    underline: u8,
    strike_out: u8,
    char_set: u8,
    out_precision: u8,
    clip_precision: u8,
    quality: u8,
    pitch_and_family: u8,
    face: [u16; 32],
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Point {
    pub x: i32,
    pub y: i32,
}

#[repr(C)]
pub struct BlendFunction {
    pub op: u8,
    pub flags: u8,
    pub constant_alpha: u8,
    pub alpha_format: u8,
}

mod sys {
    use super::*;
    windows_link::link!("gdi32.dll" "system" fn CreatePen(style: i32, width: i32, color: u32) -> Handle);
    windows_link::link!("gdi32.dll" "system" fn CreateSolidBrush(color: u32) -> Handle);
    windows_link::link!("gdi32.dll" "system" fn SelectObject(hdc: Handle, obj: Handle) -> Handle);
    windows_link::link!("gdi32.dll" "system" fn GetObjectType(obj: Handle) -> u32);
    windows_link::link!("gdi32.dll" "system" fn GetObjectW(obj: Handle, size: i32, out: *mut c_void) -> i32);
    windows_link::link!("gdi32.dll" "system" fn DeleteObject(obj: Handle) -> i32);
    windows_link::link!("gdi32.dll" "system" fn RoundRect(hdc: Handle, l: i32, t: i32, r: i32, b: i32, w: i32, h: i32) -> i32);
    windows_link::link!("gdi32.dll" "system" fn CreateRoundRectRgn(l: i32, t: i32, r: i32, b: i32, w: i32, h: i32) -> Handle);
    windows_link::link!("gdi32.dll" "system" fn CreateFontIndirectW(lf: *const c_void) -> Handle);
    windows_link::link!("gdi32.dll" "system" fn Polygon(hdc: Handle, pts: *const Point, n: i32) -> i32);
    windows_link::link!("gdi32.dll" "system" fn CreateCompatibleBitmap(hdc: Handle, cx: i32, cy: i32) -> Handle);
    windows_link::link!("gdi32.dll" "system" fn BitBlt(hdc: Handle, x: i32, y: i32, cx: i32, cy: i32, src: Handle, x1: i32, y1: i32, rop: u32) -> i32);
    windows_link::link!("user32.dll" "system" fn SetWindowRgn(wnd: Handle, rgn: Handle, redraw: i32) -> i32);
    windows_link::link!("gdi32.dll" "system" fn CreateRectRgn(l: i32, t: i32, r: i32, b: i32) -> Handle);
    windows_link::link!("gdi32.dll" "system" fn FillRgn(hdc: Handle, rgn: Handle, brush: Handle) -> i32);
    windows_link::link!("gdi32.dll" "system" fn MoveToEx(hdc: Handle, x: i32, y: i32, old: *mut c_void) -> i32);
    windows_link::link!("gdi32.dll" "system" fn LineTo(hdc: Handle, x: i32, y: i32) -> i32);
    windows_link::link!("gdi32.dll" "system" fn SetBkMode(hdc: Handle, mode: i32) -> i32);
    windows_link::link!("gdi32.dll" "system" fn SetTextColor(hdc: Handle, color: u32) -> u32);
    windows_link::link!("gdi32.dll" "system" fn SetBkColor(hdc: Handle, color: u32) -> u32);
    windows_link::link!("gdi32.dll" "system" fn CreateFontW(h: i32, w: i32, esc: i32, orient: i32, weight: i32, italic: u32, underline: u32, strike: u32, charset: u32, out_precision: u32, clip_precision: u32, quality: u32, pitch: u32, face: *const u16) -> Handle);
    windows_link::link!("gdi32.dll" "system" fn GetTextExtentPoint32W(hdc: Handle, s: *const u16, c: i32, size: *mut Size) -> i32);
    windows_link::link!("gdi32.dll" "system" fn GetTextMetricsW(hdc: Handle, tm: *mut TextMetric) -> i32);
    windows_link::link!("gdi32.dll" "system" fn ExtTextOutW(hdc: Handle, x: i32, y: i32, options: u32, rc: *const Rect, s: *const u16, c: u32, dx: *const i32) -> i32);
    windows_link::link!("gdi32.dll" "system" fn GetStockObject(i: i32) -> Handle);
    windows_link::link!("gdi32.dll" "system" fn CreateCompatibleDC(hdc: Handle) -> Handle);
    windows_link::link!("gdi32.dll" "system" fn CreateDIBSection(hdc: Handle, bmi: *const BitmapInfo, usage: u32, bits: *mut *mut c_void, section: Handle, offset: u32) -> Handle);
    windows_link::link!("gdi32.dll" "system" fn DeleteDC(hdc: Handle) -> i32);
    windows_link::link!("gdi32.dll" "system" fn GetTextFaceW(hdc: Handle, c: i32, name: *mut u16) -> i32);
    windows_link::link!("gdi32.dll" "system" fn SaveDC(hdc: Handle) -> i32);
    windows_link::link!("gdi32.dll" "system" fn RestoreDC(hdc: Handle, saved: i32) -> i32);
    windows_link::link!("gdi32.dll" "system" fn ExtSelectClipRgn(hdc: Handle, rgn: Handle, mode: i32) -> i32);
    windows_link::link!("gdi32.dll" "system" fn SetViewportOrgEx(hdc: Handle, x: i32, y: i32, old: *mut c_void) -> i32);
    windows_link::link!("user32.dll" "system" fn FillRect(hdc: Handle, rc: *const Rect, brush: Handle) -> i32);
    windows_link::link!("user32.dll" "system" fn DrawTextW(hdc: Handle, s: *const u16, c: i32, rc: *mut Rect, format: u32) -> i32);
    windows_link::link!("user32.dll" "system" fn LoadImageW(inst: Handle, name: *const u16, kind: u32, cx: i32, cy: i32, load: u32) -> Handle);
    windows_link::link!("user32.dll" "system" fn DrawIconEx(hdc: Handle, x: i32, y: i32, icon: Handle, cx: i32, cy: i32, step: u32, brush: Handle, flags: u32) -> i32);
    windows_link::link!("user32.dll" "system" fn DestroyIcon(icon: Handle) -> i32);
    windows_link::link!("user32.dll" "system" fn GetDC(wnd: Handle) -> Handle);
    windows_link::link!("user32.dll" "system" fn ReleaseDC(wnd: Handle, hdc: Handle) -> i32);
    windows_link::link!("msimg32.dll" "system" fn AlphaBlend(dst: Handle, x: i32, y: i32, w: i32, h: i32, src: Handle, sx: i32, sy: i32, sw: i32, sh: i32, blend: BlendFunction) -> i32);
    windows_link::link!("gdiplus.dll" "system" fn GdipCreateFromHDC(hdc: Handle, graphics: *mut Handle) -> i32);
    windows_link::link!("gdiplus.dll" "system" fn GdipDeleteGraphics(graphics: Handle) -> i32);
    windows_link::link!("gdiplus.dll" "system" fn GdipSetSmoothingMode(graphics: Handle, mode: i32) -> i32);
    windows_link::link!("gdiplus.dll" "system" fn GdipCreatePath(fill_mode: i32, path: *mut Handle) -> i32);
    windows_link::link!("gdiplus.dll" "system" fn GdipDeletePath(path: Handle) -> i32);
    windows_link::link!("gdiplus.dll" "system" fn GdipAddPathArc(path: Handle, x: f32, y: f32, w: f32, h: f32, start: f32, sweep: f32) -> i32);
    windows_link::link!("gdiplus.dll" "system" fn GdipClosePathFigure(path: Handle) -> i32);
    windows_link::link!("gdiplus.dll" "system" fn GdipCreateSolidFill(color: u32, brush: *mut Handle) -> i32);
    windows_link::link!("gdiplus.dll" "system" fn GdipDeleteBrush(brush: Handle) -> i32);
    windows_link::link!("gdiplus.dll" "system" fn GdipFillPath(graphics: Handle, brush: Handle, path: Handle) -> i32);
    windows_link::link!("gdiplus.dll" "system" fn GdipCreatePen1(color: u32, width: f32, unit: i32, pen: *mut Handle) -> i32);
    windows_link::link!("gdiplus.dll" "system" fn GdipDeletePen(pen: Handle) -> i32);
    windows_link::link!("gdiplus.dll" "system" fn GdipDrawPath(graphics: Handle, pen: Handle, path: Handle) -> i32);
    windows_link::link!("gdiplus.dll" "system" fn GdipFillEllipse(graphics: Handle, brush: Handle, x: f32, y: f32, w: f32, h: f32) -> i32);
}

const OBJ_PEN: u32 = 1;
const OBJ_BRUSH: u32 = 2;
const OBJ_DC: u32 = 3;
const OBJ_FONT: u32 = 6;
const OBJ_BITMAP: u32 = 7;
const OBJ_REGION: u32 = 8;
const OBJ_MEMDC: u32 = 10;
const DT_CALCRECT: u32 = 0x400;
const DT_SINGLELINE: u32 = 0x20;
const DT_WORDBREAK: u32 = 0x10;
/// The height synthetic metrics use when no font of the recording is
/// selected (a stock font).
const DEFAULT_HEIGHT: i32 = 16;

struct Recorder {
    lines: Vec<String>,
    synthetic: bool,
    /// Handles the recording created and has not seen deleted -> number.
    own: HashMap<usize, usize>,
    next: usize,
    /// Font handle -> (requested height, face).
    fonts: HashMap<usize, (i32, Vec<u16>)>,
    /// DC handle -> selected font handle.
    selected: HashMap<usize, usize>,
    /// DC handle -> the selected font at each SaveDC.
    saved: HashMap<usize, Vec<Option<usize>>>,
    /// Stock object handle -> its GetStockObject index.
    stock: HashMap<usize, i32>,
}

impl Recorder {
    /// The key the tracking maps use: the handle value.
    fn id(&mut self, h: Handle) -> usize {
        h as usize
    }

    /// A handle just created: numbered, even if the value was seen before
    /// (the system reuses freed handles).
    fn fresh(&mut self, h: Handle) -> usize {
        if h.is_null() {
            return 0;
        }
        self.next += 1;
        self.own.insert(h as usize, self.next);
        h as usize
    }

    /// A handle deleted: its value may come back as something else.
    fn forget(&mut self, h: Handle) {
        let k = h as usize;
        self.own.remove(&k);
        self.fonts.remove(&k);
        self.selected.remove(&k);
        self.saved.remove(&k);
    }

    fn h(&mut self, h: Handle) -> String {
        if h.is_null() {
            return "0".to_string();
        }
        if let Some(n) = self.own.get(&(h as usize)) {
            return format!("h{n}");
        }
        if let Some(i) = self.stock.get(&(h as usize)) {
            return format!("stock({i})");
        }
        describe(h)
    }

    fn new_h(&mut self, h: Handle) -> String {
        match self.fresh(h) {
            0 => "0".to_string(),
            _ => self.h(h),
        }
    }

    /// The selected font's height (absolute) for synthetic metrics.
    fn height(&mut self, hdc: Handle) -> i32 {
        let dc = self.id(hdc);
        self.selected
            .get(&dc)
            .and_then(|f| self.fonts.get(f))
            .map(|(h, _)| if *h == 0 { DEFAULT_HEIGHT } else { h.abs() })
            .unwrap_or(DEFAULT_HEIGHT)
    }
}

/// A handle the recording did not create, written by what it is.
fn describe(h: Handle) -> String {
    // SAFETY: queries on a handle the caller passed to a GDI function.
    unsafe {
        match sys::GetObjectType(h) {
            OBJ_FONT => {
                let (height, face) = read_font(h);
                format!("font({height},{})", String::from_utf16_lossy(&face))
            }
            OBJ_BRUSH => {
                let mut lb = LogBrush::default();
                if sys::GetObjectW(h, std::mem::size_of::<LogBrush>() as i32, (&mut lb as *mut LogBrush).cast()) > 0 {
                    format!("brush({},{:06x})", lb.style, lb.color)
                } else {
                    "brush".to_string()
                }
            }
            OBJ_PEN => "pen".to_string(),
            OBJ_DC | OBJ_MEMDC => "dc".to_string(),
            OBJ_BITMAP => "bitmap".to_string(),
            OBJ_REGION => "rgn".to_string(),
            _ => "x".to_string(),
        }
    }
}

/// A font's requested height and face (its LOGFONT, not what the font
/// mapper picked).
fn read_font(h: Handle) -> (i32, Vec<u16>) {
    let mut lf = LogFontW::default();
    // SAFETY: a LOGFONTW-sized buffer for a font handle.
    let got = unsafe { sys::GetObjectW(h, std::mem::size_of::<LogFontW>() as i32, (&mut lf as *mut LogFontW).cast()) };
    if got > 0 {
        (lf.height, lf.face.iter().copied().take_while(|&c| c != 0).collect())
    } else {
        (0, Vec::new())
    }
}

/// The process's stock objects (handle -> index).
fn stock_objects() -> HashMap<usize, i32> {
    let mut m = HashMap::new();
    for i in 0..=19 {
        // SAFETY: GetStockObject with any index; null for unused ones.
        let h = unsafe { sys::GetStockObject(i) };
        if !h.is_null() {
            m.entry(h as usize).or_insert(i);
        }
    }
    m
}

thread_local! {
    static REC: RefCell<Option<Recorder>> = const { RefCell::new(None) };
}

/// Starts a recording on this thread (replacing one in progress).
pub fn record_start(synthetic: bool) {
    REC.with(|r| {
        *r.borrow_mut() = Some(Recorder {
            lines: Vec::new(),
            synthetic,
            own: HashMap::new(),
            next: 0,
            fonts: HashMap::new(),
            selected: HashMap::new(),
            saved: HashMap::new(),
            stock: stock_objects(),
        })
    });
}

/// Ends the recording and returns its lines.
pub fn record_take() -> Vec<String> {
    REC.with(|r| r.borrow_mut().take().map(|x| x.lines).unwrap_or_default())
}

fn recording() -> bool {
    REC.with(|r| r.borrow().is_some())
}

fn synthetic() -> bool {
    REC.with(|r| r.borrow().as_ref().is_some_and(|x| x.synthetic))
}

fn with<T>(f: impl FnOnce(&mut Recorder) -> T) -> Option<T> {
    REC.with(|r| r.borrow_mut().as_mut().map(f))
}

fn rect(rc: *const Rect) -> String {
    if rc.is_null() {
        return "null".to_string();
    }
    // SAFETY: the caller's rectangle, read only while the call runs.
    let r = unsafe { *rc };
    format!("[{},{},{},{}]", r.left, r.top, r.right, r.bottom)
}

/// UTF-16 text, `count` units or up to the NUL when negative.
fn units<'a>(s: *const u16, count: i32) -> &'a [u16] {
    if s.is_null() {
        return &[];
    }
    // SAFETY: the caller's string, `count` units or NUL terminated.
    unsafe {
        let n = if count >= 0 {
            count as usize
        } else {
            let mut n = 0;
            while *s.add(n) != 0 {
                n += 1;
            }
            n
        };
        std::slice::from_raw_parts(s, n)
    }
}

fn text(u: &[u16]) -> String {
    let mut out = String::from("\"");
    for c in char::decode_utf16(u.iter().copied()) {
        match c {
            Ok('"') => out.push_str("\\\""),
            Ok('\\') => out.push_str("\\\\"),
            Ok('\n') => out.push_str("\\n"),
            Ok('\r') => out.push_str("\\r"),
            Ok('\t') => out.push_str("\\t"),
            Ok(c) if (c as u32) < 0x20 => {
                let _ = write!(out, "\\x{:02x}", c as u32);
            }
            Ok(c) => out.push(c),
            Err(e) => {
                let _ = write!(out, "\\u{:04x}", e.unpaired_surrogate());
            }
        }
    }
    out.push('"');
    out
}

/// A name argument: a resource ordinal (`MAKEINTRESOURCE`) or a string.
fn name(s: *const u16) -> String {
    if (s as usize) < 0x10000 {
        format!("#{}", s as usize)
    } else {
        text(units(s, -1))
    }
}

fn char_width(c: u16, height: i32) -> i32 {
    if c < 0x80 {
        (height + 1) / 2
    } else {
        height
    }
}

fn synthetic_width(u: &[u16], height: i32) -> i32 {
    u.iter().map(|&c| char_width(c, height)).sum()
}

/// Synthetic `DrawTextW` layout: the line widths (breaking at '\n' unless
/// single line, and anywhere a line would pass `max` with word breaks).
fn synthetic_lines(u: &[u16], height: i32, format: u32, max: i32) -> Vec<i32> {
    let mut lines = vec![0];
    let mut i = 0;
    while i < u.len() {
        let c = u[i];
        let line_end = c == u16::from(b'\n') || c == u16::from(b'\r');
        if line_end && format & DT_SINGLELINE != 0 {
            // A single line shows no line ends.
            i += 1;
            continue;
        }
        if line_end {
            if c == u16::from(b'\r') && u.get(i + 1) == Some(&u16::from(b'\n')) {
                i += 1;
            }
            lines.push(0);
            i += 1;
            continue;
        }
        let w = char_width(c, height);
        let cur = lines.last_mut().unwrap();
        if format & DT_WORDBREAK != 0 && *cur > 0 && *cur + w > max {
            lines.push(w);
        } else {
            *cur += w;
        }
        i += 1;
    }
    lines
}

// ---- Objects -------------------------------------------------------------

pub unsafe fn CreatePen(style: i32, width: i32, color: u32) -> Handle {
    let h = unsafe { sys::CreatePen(style, width, color) };
    with(|r| {
        let id = r.new_h(h);
        r.lines.push(format!("CreatePen({style}, {width}, {color:06x}) = {id}"))
    });
    h
}

pub unsafe fn CreateSolidBrush(color: u32) -> Handle {
    let h = unsafe { sys::CreateSolidBrush(color) };
    with(|r| {
        let id = r.new_h(h);
        r.lines.push(format!("CreateSolidBrush({color:06x}) = {id}"))
    });
    h
}

pub unsafe fn GetStockObject(i: i32) -> Handle {
    let h = unsafe { sys::GetStockObject(i) };
    with(|r| {
        r.stock.entry(h as usize).or_insert(i);
        let id = r.h(h);
        r.lines.push(format!("GetStockObject({i}) = {id}"))
    });
    h
}

pub unsafe fn SelectObject(hdc: Handle, obj: Handle) -> Handle {
    let old = unsafe { sys::SelectObject(hdc, obj) };
    // SAFETY: a query on the handle just selected.
    let is_font = recording() && unsafe { sys::GetObjectType(obj) } == OBJ_FONT;
    with(|r| {
        let (d, o, p) = (r.h(hdc), r.h(obj), r.h(old));
        let (dc, ob) = (r.id(hdc), r.id(obj));
        if is_font {
            // The font's requested height and face (its LOGFONT, not what
            // the font mapper picked) set the synthetic metrics, also for a
            // font created outside the recording.
            if r.stock.contains_key(&ob) {
                // A stock font depends on the system's locale: the default.
                r.fonts.insert(ob, (0, Vec::new()));
            } else if !r.own.contains_key(&ob) {
                // Not the recording's: read it each time (the value may be a
                // different font than the last time it was seen).
                r.fonts.insert(ob, read_font(obj));
            }
            r.selected.insert(dc, ob);
        }
        r.lines.push(format!("SelectObject({d}, {o}) = {p}"))
    });
    old
}

pub unsafe fn DeleteObject(obj: Handle) -> i32 {
    with(|r| {
        let o = r.h(obj);
        r.lines.push(format!("DeleteObject({o})"));
        r.forget(obj);
    });
    unsafe { sys::DeleteObject(obj) }
}

pub unsafe fn CreateFontW(
    h: i32,
    w: i32,
    esc: i32,
    orient: i32,
    weight: i32,
    italic: u32,
    underline: u32,
    strike: u32,
    charset: u32,
    out_precision: u32,
    clip_precision: u32,
    quality: u32,
    pitch: u32,
    face: *const u16,
) -> Handle {
    let font = unsafe {
        sys::CreateFontW(
            h,
            w,
            esc,
            orient,
            weight,
            italic,
            underline,
            strike,
            charset,
            out_precision,
            clip_precision,
            quality,
            pitch,
            face,
        )
    };
    with(|r| {
        let key = r.fresh(font);
        let id = r.h(font);
        let f = units(face, -1).to_vec();
        r.lines.push(format!(
            "CreateFontW({h}, {w}, {esc}, {orient}, {weight}, {italic}, {underline}, {strike}, {charset}, {out_precision}, {clip_precision}, {quality}, {pitch}, {}) = {id}",
            text(&f)
        ));
        r.fonts.insert(key, (h, f));
    });
    font
}

/// `lf` points to a LOGFONTW.
pub unsafe fn CreateFontIndirectW(lf: *const c_void) -> Handle {
    let font = unsafe { sys::CreateFontIndirectW(lf) };
    with(|r| {
        let key = r.fresh(font);
        let id = r.h(font);
        // SAFETY: the caller's LOGFONTW.
        let (h, f) = if lf.is_null() {
            (0, Vec::new())
        } else {
            let l = unsafe { &*(lf as *const LogFontW) };
            (l.height, l.face.iter().copied().take_while(|&c| c != 0).collect::<Vec<u16>>())
        };
        r.lines.push(format!("CreateFontIndirectW({h}, {}) = {id}", text(&f)));
        r.fonts.insert(key, (h, f));
    });
    font
}

/// Not recorded: a query (the caller reads a font's LOGFONT).
pub unsafe fn GetObjectW(obj: Handle, size: i32, out: *mut c_void) -> i32 {
    unsafe { sys::GetObjectW(obj, size, out) }
}

pub unsafe fn Polygon(hdc: Handle, pts: *const Point, n: i32) -> i32 {
    with(|r| {
        let d = r.h(hdc);
        let mut p = String::new();
        for i in 0..n.max(0) as usize {
            // SAFETY: the caller's `n` points.
            let q = unsafe { *pts.add(i) };
            let _ = write!(p, " {},{}", q.x, q.y);
        }
        r.lines.push(format!("Polygon({d},{p})"))
    });
    unsafe { sys::Polygon(hdc, pts, n) }
}

pub unsafe fn CreateCompatibleBitmap(hdc: Handle, cx: i32, cy: i32) -> Handle {
    let bmp = unsafe { sys::CreateCompatibleBitmap(hdc, cx, cy) };
    with(|r| {
        let a = r.h(hdc);
        let id = r.new_h(bmp);
        r.lines.push(format!("CreateCompatibleBitmap({a}, {cx}, {cy}) = {id}"))
    });
    bmp
}

pub unsafe fn BitBlt(hdc: Handle, x: i32, y: i32, cx: i32, cy: i32, src: Handle, x1: i32, y1: i32, rop: u32) -> i32 {
    with(|r| {
        let (d, s) = (r.h(hdc), r.h(src));
        r.lines.push(format!("BitBlt({d}, {x}, {y}, {cx}, {cy}, {s}, {x1}, {y1}, {rop:x})"))
    });
    unsafe { sys::BitBlt(hdc, x, y, cx, cy, src, x1, y1, rop) }
}

/// The system owns `rgn` afterwards: the recording forgets it.
pub unsafe fn SetWindowRgn(wnd: Handle, rgn: Handle, redraw: i32) -> i32 {
    with(|r| {
        let g = r.h(rgn);
        r.lines.push(format!("SetWindowRgn({g}, {redraw})"));
        r.forget(rgn);
    });
    unsafe { sys::SetWindowRgn(wnd, rgn, redraw) }
}

pub unsafe fn CreateRoundRectRgn(l: i32, t: i32, rr: i32, b: i32, w: i32, h: i32) -> Handle {
    let rgn = unsafe { sys::CreateRoundRectRgn(l, t, rr, b, w, h) };
    with(|r| {
        let id = r.new_h(rgn);
        r.lines.push(format!("CreateRoundRectRgn({l}, {t}, {rr}, {b}, {w}, {h}) = {id}"))
    });
    rgn
}

pub unsafe fn CreateRectRgn(l: i32, t: i32, rr: i32, b: i32) -> Handle {
    let rgn = unsafe { sys::CreateRectRgn(l, t, rr, b) };
    with(|r| {
        let id = r.new_h(rgn);
        r.lines.push(format!("CreateRectRgn({l}, {t}, {rr}, {b}) = {id}"))
    });
    rgn
}

pub unsafe fn CreateCompatibleDC(hdc: Handle) -> Handle {
    let dc = unsafe { sys::CreateCompatibleDC(hdc) };
    with(|r| {
        let a = r.h(hdc);
        let id = r.new_h(dc);
        r.lines.push(format!("CreateCompatibleDC({a}) = {id}"))
    });
    dc
}

pub unsafe fn CreateDIBSection(
    hdc: Handle,
    bmi: *const BitmapInfo,
    usage: u32,
    bits: *mut *mut c_void,
    section: Handle,
    offset: u32,
) -> Handle {
    let bmp = unsafe { sys::CreateDIBSection(hdc, bmi, usage, bits, section, offset) };
    with(|r| {
        let a = r.h(hdc);
        let id = r.new_h(bmp);
        // SAFETY: the caller's header, read only while the call runs.
        let (w, h, bpp) = if bmi.is_null() {
            (0, 0, 0)
        } else {
            unsafe { ((*bmi).bi_width, (*bmi).bi_height, (*bmi).bi_bit_count) }
        };
        r.lines.push(format!("CreateDIBSection({a}, {w}x{h}x{bpp}, {usage}) = {id}"))
    });
    bmp
}

pub unsafe fn DeleteDC(hdc: Handle) -> i32 {
    with(|r| {
        let a = r.h(hdc);
        r.lines.push(format!("DeleteDC({a})"));
        r.forget(hdc);
    });
    unsafe { sys::DeleteDC(hdc) }
}

pub unsafe fn GetDC(wnd: Handle) -> Handle {
    let dc = unsafe { sys::GetDC(wnd) };
    with(|r| {
        let w = r.h(wnd);
        let id = r.new_h(dc);
        r.lines.push(format!("GetDC({w}) = {id}"))
    });
    dc
}

pub unsafe fn ReleaseDC(wnd: Handle, hdc: Handle) -> i32 {
    with(|r| {
        let (w, d) = (r.h(wnd), r.h(hdc));
        r.lines.push(format!("ReleaseDC({w}, {d})"));
        r.forget(hdc);
    });
    unsafe { sys::ReleaseDC(wnd, hdc) }
}

pub unsafe fn LoadImageW(inst: Handle, nm: *const u16, kind: u32, cx: i32, cy: i32, load: u32) -> Handle {
    let img = unsafe { sys::LoadImageW(inst, nm, kind, cx, cy, load) };
    with(|r| {
        let i = r.h(inst);
        let id = r.new_h(img);
        r.lines.push(format!("LoadImageW({i}, {}, {kind}, {cx}, {cy}, {load}) = {id}", name(nm)))
    });
    img
}

pub unsafe fn DestroyIcon(icon: Handle) -> i32 {
    with(|r| {
        let i = r.h(icon);
        r.lines.push(format!("DestroyIcon({i})"));
        r.forget(icon);
    });
    unsafe { sys::DestroyIcon(icon) }
}

// ---- DC state --------------------------------------------------------------

pub unsafe fn SetBkMode(hdc: Handle, mode: i32) -> i32 {
    with(|r| {
        let d = r.h(hdc);
        r.lines.push(format!("SetBkMode({d}, {mode})"))
    });
    unsafe { sys::SetBkMode(hdc, mode) }
}

pub unsafe fn SetTextColor(hdc: Handle, color: u32) -> u32 {
    with(|r| {
        let d = r.h(hdc);
        r.lines.push(format!("SetTextColor({d}, {color:06x})"))
    });
    unsafe { sys::SetTextColor(hdc, color) }
}

pub unsafe fn SetBkColor(hdc: Handle, color: u32) -> u32 {
    with(|r| {
        let d = r.h(hdc);
        r.lines.push(format!("SetBkColor({d}, {color:06x})"))
    });
    unsafe { sys::SetBkColor(hdc, color) }
}

pub unsafe fn SaveDC(hdc: Handle) -> i32 {
    let n = unsafe { sys::SaveDC(hdc) };
    with(|r| {
        let d = r.h(hdc);
        let dc = r.id(hdc);
        let font = r.selected.get(&dc).copied();
        r.saved.entry(dc).or_default().push(font);
        r.lines.push(format!("SaveDC({d}) = {n}"))
    });
    n
}

pub unsafe fn RestoreDC(hdc: Handle, saved: i32) -> i32 {
    with(|r| {
        let d = r.h(hdc);
        let dc = r.id(hdc);
        let stack = r.saved.entry(dc).or_default();
        // -n pops n states; a positive level restores the state saved as
        // that level (SaveDC returned it) and drops the ones above it.
        let keep = if saved < 0 {
            stack.len().saturating_sub(saved.unsigned_abs() as usize)
        } else {
            (saved as usize).saturating_sub(1).min(stack.len())
        };
        let font = stack.get(keep).copied().flatten();
        stack.truncate(keep);
        match font {
            Some(f) => {
                r.selected.insert(dc, f);
            }
            None => {
                r.selected.remove(&dc);
            }
        }
        r.lines.push(format!("RestoreDC({d}, {saved})"))
    });
    unsafe { sys::RestoreDC(hdc, saved) }
}

pub unsafe fn ExtSelectClipRgn(hdc: Handle, rgn: Handle, mode: i32) -> i32 {
    with(|r| {
        let (d, g) = (r.h(hdc), r.h(rgn));
        r.lines.push(format!("ExtSelectClipRgn({d}, {g}, {mode})"))
    });
    unsafe { sys::ExtSelectClipRgn(hdc, rgn, mode) }
}

pub unsafe fn SetViewportOrgEx(hdc: Handle, x: i32, y: i32, old: *mut c_void) -> i32 {
    with(|r| {
        let d = r.h(hdc);
        r.lines.push(format!("SetViewportOrgEx({d}, {x}, {y})"))
    });
    unsafe { sys::SetViewportOrgEx(hdc, x, y, old) }
}

// ---- Drawing ---------------------------------------------------------------

pub unsafe fn RoundRect(hdc: Handle, l: i32, t: i32, rr: i32, b: i32, w: i32, h: i32) -> i32 {
    with(|r| {
        let d = r.h(hdc);
        r.lines.push(format!("RoundRect({d}, {l}, {t}, {rr}, {b}, {w}, {h})"))
    });
    unsafe { sys::RoundRect(hdc, l, t, rr, b, w, h) }
}

pub unsafe fn FillRgn(hdc: Handle, rgn: Handle, brush: Handle) -> i32 {
    with(|r| {
        let (d, g, b) = (r.h(hdc), r.h(rgn), r.h(brush));
        r.lines.push(format!("FillRgn({d}, {g}, {b})"))
    });
    unsafe { sys::FillRgn(hdc, rgn, brush) }
}

pub unsafe fn FillRect(hdc: Handle, rc: *const Rect, brush: Handle) -> i32 {
    with(|r| {
        let (d, b) = (r.h(hdc), r.h(brush));
        r.lines.push(format!("FillRect({d}, {}, {b})", rect(rc)))
    });
    unsafe { sys::FillRect(hdc, rc, brush) }
}

pub unsafe fn MoveToEx(hdc: Handle, x: i32, y: i32, old: *mut c_void) -> i32 {
    with(|r| {
        let d = r.h(hdc);
        r.lines.push(format!("MoveToEx({d}, {x}, {y})"))
    });
    unsafe { sys::MoveToEx(hdc, x, y, old) }
}

pub unsafe fn LineTo(hdc: Handle, x: i32, y: i32) -> i32 {
    with(|r| {
        let d = r.h(hdc);
        r.lines.push(format!("LineTo({d}, {x}, {y})"))
    });
    unsafe { sys::LineTo(hdc, x, y) }
}

pub unsafe fn DrawIconEx(hdc: Handle, x: i32, y: i32, icon: Handle, cx: i32, cy: i32, step: u32, brush: Handle, flags: u32) -> i32 {
    with(|r| {
        let (d, i, b) = (r.h(hdc), r.h(icon), r.h(brush));
        r.lines.push(format!("DrawIconEx({d}, {x}, {y}, {i}, {cx}, {cy}, {step}, {b}, {flags})"))
    });
    unsafe { sys::DrawIconEx(hdc, x, y, icon, cx, cy, step, brush, flags) }
}

pub unsafe fn AlphaBlend(
    dst: Handle,
    x: i32,
    y: i32,
    w: i32,
    h: i32,
    src: Handle,
    sx: i32,
    sy: i32,
    sw: i32,
    sh: i32,
    blend: BlendFunction,
) -> i32 {
    with(|r| {
        let (d, s) = (r.h(dst), r.h(src));
        r.lines.push(format!(
            "AlphaBlend({d}, {x}, {y}, {w}, {h}, {s}, {sx}, {sy}, {sw}, {sh}, [{},{},{},{}])",
            blend.op, blend.flags, blend.constant_alpha, blend.alpha_format
        ))
    });
    unsafe { sys::AlphaBlend(dst, x, y, w, h, src, sx, sy, sw, sh, blend) }
}

// ---- Text --------------------------------------------------------------------

pub unsafe fn ExtTextOutW(hdc: Handle, x: i32, y: i32, options: u32, rc: *const Rect, s: *const u16, c: u32, dx: *const i32) -> i32 {
    with(|r| {
        let d = r.h(hdc);
        let t = text(units(s, c as i32));
        r.lines.push(format!("ExtTextOutW({d}, {x}, {y}, {options}, {}, {t}{})", rect(rc), if dx.is_null() { "" } else { ", dx" }))
    });
    unsafe { sys::ExtTextOutW(hdc, x, y, options, rc, s, c, dx) }
}

pub unsafe fn GetTextExtentPoint32W(hdc: Handle, s: *const u16, c: i32, size: *mut Size) -> i32 {
    if synthetic() {
        let u = units(s, c);
        let h = with(|r| r.height(hdc)).unwrap_or(DEFAULT_HEIGHT);
        let (cx, cy) = (synthetic_width(u, h), h);
        if !size.is_null() {
            // SAFETY: the caller's output.
            unsafe { *size = Size { cx, cy } };
        }
        with(|r| {
            let d = r.h(hdc);
            r.lines.push(format!("GetTextExtentPoint32W({d}, {}) = {cx}x{cy}", text(u)))
        });
        return 1;
    }
    let ok = unsafe { sys::GetTextExtentPoint32W(hdc, s, c, size) };
    if recording() {
        // SAFETY: the caller's output, just written.
        let (cx, cy) = if size.is_null() { (0, 0) } else { unsafe { ((*size).cx, (*size).cy) } };
        with(|r| {
            let d = r.h(hdc);
            r.lines.push(format!("GetTextExtentPoint32W({d}, {}) = {cx}x{cy}", text(units(s, c))))
        });
    }
    ok
}

pub unsafe fn GetTextMetricsW(hdc: Handle, tm: *mut TextMetric) -> i32 {
    if synthetic() {
        let h = with(|r| r.height(hdc)).unwrap_or(DEFAULT_HEIGHT);
        if !tm.is_null() {
            // SAFETY: the caller's output.
            unsafe {
                *tm = TextMetric {
                    tm_height: h,
                    tm_ascent: h * 4 / 5,
                    tm_descent: h - h * 4 / 5,
                    tm_ave_char_width: (h + 1) / 2,
                    tm_max_char_width: h,
                    tm_weight: 400,
                    tm_digitized_aspect_x: 96,
                    tm_digitized_aspect_y: 96,
                    tm_last_char: 0xFFFF,
                    tm_break_char: 0x20,
                    ..TextMetric::default()
                };
            }
        }
        with(|r| {
            let d = r.h(hdc);
            r.lines.push(format!("GetTextMetricsW({d}) = height {h}"))
        });
        return 1;
    }
    let ok = unsafe { sys::GetTextMetricsW(hdc, tm) };
    if recording() && !tm.is_null() {
        // SAFETY: the caller's output, just written.
        let (h, a) = unsafe { ((*tm).tm_height, (*tm).tm_ascent) };
        with(|r| {
            let d = r.h(hdc);
            r.lines.push(format!("GetTextMetricsW({d}) = height {h} ascent {a}"))
        });
    }
    ok
}

pub unsafe fn GetTextFaceW(hdc: Handle, c: i32, out: *mut u16) -> i32 {
    if synthetic() {
        // The requested face, not the one the font mapper picked.
        let face = with(|r| {
            let dc = r.id(hdc);
            r.selected.get(&dc).and_then(|f| r.fonts.get(f)).map(|(_, f)| f.clone()).unwrap_or_default()
        })
        .unwrap_or_default();
        let n = face.len().min((c.max(1) - 1) as usize);
        if !out.is_null() && c > 0 {
            // SAFETY: the caller's buffer of `c` units.
            unsafe {
                std::ptr::copy_nonoverlapping(face.as_ptr(), out, n);
                *out.add(n) = 0;
            }
        }
        with(|r| {
            let d = r.h(hdc);
            r.lines.push(format!("GetTextFaceW({d}) = {}", text(&face[..n])))
        });
        return n as i32 + 1;
    }
    let n = unsafe { sys::GetTextFaceW(hdc, c, out) };
    if recording() {
        with(|r| {
            let d = r.h(hdc);
            r.lines.push(format!("GetTextFaceW({d})"))
        });
    }
    n
}

pub unsafe fn DrawTextW(hdc: Handle, s: *const u16, c: i32, rc: *mut Rect, format: u32) -> i32 {
    if !recording() {
        return unsafe { sys::DrawTextW(hdc, s, c, rc, format) };
    }
    let before = rect(rc);
    let u = units(s, c);
    if synthetic() {
        let h = with(|r| r.height(hdc)).unwrap_or(DEFAULT_HEIGHT);
        let max = if rc.is_null() {
            0
        } else {
            // SAFETY: the caller's rectangle.
            unsafe { (*rc).right - (*rc).left }
        };
        let lines = synthetic_lines(u, h, format, max);
        let height = h * lines.len() as i32;
        if format & DT_CALCRECT != 0 {
            if !rc.is_null() {
                let widest = lines.iter().copied().max().unwrap_or(0);
                // SAFETY: the caller's rectangle.
                unsafe {
                    (*rc).right = (*rc).left + widest;
                    (*rc).bottom = (*rc).top + height;
                }
            }
        } else {
            unsafe { sys::DrawTextW(hdc, s, c, rc, format) };
        }
        with(|r| {
            let d = r.h(hdc);
            r.lines.push(format!("DrawTextW({d}, {}, {before}, {format:x}) = {height} {}", text(u), rect(rc)))
        });
        return height;
    }
    let ret = unsafe { sys::DrawTextW(hdc, s, c, rc, format) };
    with(|r| {
        let d = r.h(hdc);
        r.lines.push(format!("DrawTextW({d}, {}, {before}, {format:x}) = {ret} {}", text(u), rect(rc)))
    });
    ret
}

// ---- GDI+ --------------------------------------------------------------------

fn out_handle(r: &mut Recorder, p: *mut Handle) -> String {
    if p.is_null() {
        return "0".to_string();
    }
    // SAFETY: the caller's output, just written.
    let h = unsafe { *p };
    r.new_h(h)
}

pub unsafe fn GdipCreateFromHDC(hdc: Handle, graphics: *mut Handle) -> i32 {
    let st = unsafe { sys::GdipCreateFromHDC(hdc, graphics) };
    with(|r| {
        let d = r.h(hdc);
        let g = out_handle(r, graphics);
        r.lines.push(format!("GdipCreateFromHDC({d}) = {g}"))
    });
    st
}

pub unsafe fn GdipDeleteGraphics(graphics: Handle) -> i32 {
    with(|r| {
        let g = r.h(graphics);
        r.lines.push(format!("GdipDeleteGraphics({g})"));
        r.forget(graphics);
    });
    unsafe { sys::GdipDeleteGraphics(graphics) }
}

pub unsafe fn GdipSetSmoothingMode(graphics: Handle, mode: i32) -> i32 {
    with(|r| {
        let g = r.h(graphics);
        r.lines.push(format!("GdipSetSmoothingMode({g}, {mode})"))
    });
    unsafe { sys::GdipSetSmoothingMode(graphics, mode) }
}

pub unsafe fn GdipCreatePath(fill_mode: i32, path: *mut Handle) -> i32 {
    let st = unsafe { sys::GdipCreatePath(fill_mode, path) };
    with(|r| {
        let p = out_handle(r, path);
        r.lines.push(format!("GdipCreatePath({fill_mode}) = {p}"))
    });
    st
}

pub unsafe fn GdipDeletePath(path: Handle) -> i32 {
    with(|r| {
        let p = r.h(path);
        r.lines.push(format!("GdipDeletePath({p})"));
        r.forget(path);
    });
    unsafe { sys::GdipDeletePath(path) }
}

pub unsafe fn GdipAddPathArc(path: Handle, x: f32, y: f32, w: f32, h: f32, start: f32, sweep: f32) -> i32 {
    with(|r| {
        let p = r.h(path);
        r.lines.push(format!("GdipAddPathArc({p}, {x:.3}, {y:.3}, {w:.3}, {h:.3}, {start:.1}, {sweep:.1})"))
    });
    unsafe { sys::GdipAddPathArc(path, x, y, w, h, start, sweep) }
}

pub unsafe fn GdipClosePathFigure(path: Handle) -> i32 {
    with(|r| {
        let p = r.h(path);
        r.lines.push(format!("GdipClosePathFigure({p})"))
    });
    unsafe { sys::GdipClosePathFigure(path) }
}

pub unsafe fn GdipCreateSolidFill(color: u32, brush: *mut Handle) -> i32 {
    let st = unsafe { sys::GdipCreateSolidFill(color, brush) };
    with(|r| {
        let b = out_handle(r, brush);
        r.lines.push(format!("GdipCreateSolidFill({color:08x}) = {b}"))
    });
    st
}

pub unsafe fn GdipDeleteBrush(brush: Handle) -> i32 {
    with(|r| {
        let b = r.h(brush);
        r.lines.push(format!("GdipDeleteBrush({b})"));
        r.forget(brush);
    });
    unsafe { sys::GdipDeleteBrush(brush) }
}

pub unsafe fn GdipFillPath(graphics: Handle, brush: Handle, path: Handle) -> i32 {
    with(|r| {
        let (g, b, p) = (r.h(graphics), r.h(brush), r.h(path));
        r.lines.push(format!("GdipFillPath({g}, {b}, {p})"))
    });
    unsafe { sys::GdipFillPath(graphics, brush, path) }
}

pub unsafe fn GdipCreatePen1(color: u32, width: f32, unit: i32, pen: *mut Handle) -> i32 {
    let st = unsafe { sys::GdipCreatePen1(color, width, unit, pen) };
    with(|r| {
        let p = out_handle(r, pen);
        r.lines.push(format!("GdipCreatePen1({color:08x}, {width:.3}, {unit}) = {p}"))
    });
    st
}

pub unsafe fn GdipDeletePen(pen: Handle) -> i32 {
    with(|r| {
        let p = r.h(pen);
        r.lines.push(format!("GdipDeletePen({p})"));
        r.forget(pen);
    });
    unsafe { sys::GdipDeletePen(pen) }
}

pub unsafe fn GdipDrawPath(graphics: Handle, pen: Handle, path: Handle) -> i32 {
    with(|r| {
        let (g, p, q) = (r.h(graphics), r.h(pen), r.h(path));
        r.lines.push(format!("GdipDrawPath({g}, {p}, {q})"))
    });
    unsafe { sys::GdipDrawPath(graphics, pen, path) }
}

pub unsafe fn GdipFillEllipse(graphics: Handle, brush: Handle, x: f32, y: f32, w: f32, h: f32) -> i32 {
    with(|r| {
        let (g, b) = (r.h(graphics), r.h(brush));
        r.lines.push(format!("GdipFillEllipse({g}, {b}, {x:.3}, {y:.3}, {w:.3}, {h:.3})"))
    });
    unsafe { sys::GdipFillEllipse(graphics, brush, x, y, w, h) }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn w(s: &str) -> Vec<u16> {
        s.encode_utf16().collect()
    }

    #[test]
    fn synthetic_layout() {
        assert_eq!(synthetic_width(&w("ab\u{4e2d}"), 16), 8 + 8 + 16);
        assert_eq!(synthetic_width(&w("x"), 15), 8);
        assert_eq!(synthetic_lines(&w("ab\r\ncd\ne"), 16, 0, 0), vec![16, 16, 8]);
        assert_eq!(synthetic_lines(&w("ab\ncd"), 16, DT_SINGLELINE, 0), vec![32]);
        // Word breaks wrap anywhere a line would pass the width.
        assert_eq!(synthetic_lines(&w("\u{4e2d}\u{4e2d}\u{4e2d}"), 16, DT_WORDBREAK, 40), vec![32, 16]);
        assert_eq!(text(&w("a\"\\\n\u{1}")), "\"a\\\"\\\\\\n\\x01\"");
    }

    #[test]
    fn handles_are_numbered() {
        record_start(true);
        let mut r = Recorder {
            lines: vec![],
            synthetic: true,
            own: HashMap::new(),
            next: 0,
            fonts: HashMap::new(),
            selected: HashMap::new(),
            saved: HashMap::new(),
            stock: stock_objects(),
        };
        let a = 0x1000 as Handle;
        let b = 0x2000 as Handle;
        // Created ones are numbered; a value created again gets a new number.
        assert_eq!(r.new_h(a), "h1");
        assert_eq!(r.new_h(b), "h2");
        assert_eq!(r.h(a), "h1");
        assert_eq!(r.new_h(a), "h3");
        // Deleted: the value is no longer the recording's.
        r.forget(a);
        assert_eq!(r.h(a), "x");
        assert_eq!(r.h(std::ptr::null_mut()), "0");
        // Stock objects by index; other brushes by what they are.
        // SAFETY: a stock object; a brush created and deleted here.
        let white = unsafe { sys::GetStockObject(0) };
        assert_eq!(r.h(white), "stock(0)");
        let red = unsafe { sys::CreateSolidBrush(0x0000ff) };
        assert_eq!(r.h(red), "brush(0,0000ff)");
        unsafe { sys::DeleteObject(red) };
        assert!(record_take().is_empty());
    }
}
