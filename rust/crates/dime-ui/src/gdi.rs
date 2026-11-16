//! The GDI calls of the Rust windows' painting and text measurement, through
//! `dime_gdi` so the golden tests can record them (phase 6b-2).
//!
//! Each function has the signature of the `windows` crate function of the
//! same name and calls `dime_gdi`, which calls the system and, while a
//! recording is on for this thread, logs the call (with synthetic text
//! metrics when the recording asks for them). Import these instead of the
//! `windows::Win32::Graphics::Gdi` functions; types and constants still come
//! from `windows`.

#![allow(non_snake_case)]
#![allow(clippy::missing_safety_doc)]
#![allow(clippy::too_many_arguments)]

use std::ffi::c_void;

use windows::core::{BOOL, PCWSTR};
use windows::Win32::Foundation::{COLORREF, HWND, POINT, RECT, SIZE};
use windows::Win32::Graphics::Gdi::{
    BACKGROUND_MODE, DRAW_TEXT_FORMAT, ETO_OPTIONS, GET_STOCK_OBJECT_FLAGS, HBITMAP, HBRUSH, HDC, HFONT, HGDIOBJ, HRGN,
    LOGFONTW, ROP_CODE, TEXTMETRICW,
};

const _: () = assert!(std::mem::size_of::<TEXTMETRICW>() == std::mem::size_of::<dime_gdi::TextMetric>());
const _: () = assert!(std::mem::size_of::<RECT>() == std::mem::size_of::<dime_gdi::Rect>());
const _: () = assert!(std::mem::size_of::<SIZE>() == std::mem::size_of::<dime_gdi::Size>());
const _: () = assert!(std::mem::size_of::<POINT>() == std::mem::size_of::<dime_gdi::Point>());

pub unsafe fn SetBkMode(hdc: HDC, mode: BACKGROUND_MODE) -> i32 {
    unsafe { dime_gdi::SetBkMode(hdc.0, mode.0 as i32) }
}

pub unsafe fn SelectObject(hdc: HDC, h: HGDIOBJ) -> HGDIOBJ {
    HGDIOBJ(unsafe { dime_gdi::SelectObject(hdc.0, h.0) })
}

pub unsafe fn FillRect(hdc: HDC, rc: *const RECT, brush: HBRUSH) -> i32 {
    unsafe { dime_gdi::FillRect(hdc.0, rc.cast(), brush.0) }
}

pub unsafe fn GetTextMetricsW(hdc: HDC, tm: *mut TEXTMETRICW) -> BOOL {
    BOOL(unsafe { dime_gdi::GetTextMetricsW(hdc.0, tm.cast()) })
}

pub unsafe fn GetTextExtentPoint32W(hdc: HDC, s: &[u16], size: *mut SIZE) -> BOOL {
    BOOL(unsafe { dime_gdi::GetTextExtentPoint32W(hdc.0, s.as_ptr(), s.len() as i32, size.cast()) })
}

pub unsafe fn CreateSolidBrush(color: COLORREF) -> HBRUSH {
    HBRUSH(unsafe { dime_gdi::CreateSolidBrush(color.0) })
}

pub unsafe fn GetStockObject(i: GET_STOCK_OBJECT_FLAGS) -> HGDIOBJ {
    HGDIOBJ(unsafe { dime_gdi::GetStockObject(i.0) })
}

pub unsafe fn RoundRect(hdc: HDC, l: i32, t: i32, r: i32, b: i32, w: i32, h: i32) -> BOOL {
    BOOL(unsafe { dime_gdi::RoundRect(hdc.0, l, t, r, b, w, h) })
}

pub unsafe fn DeleteObject(h: HGDIOBJ) -> BOOL {
    BOOL(unsafe { dime_gdi::DeleteObject(h.0) })
}

pub unsafe fn SetTextColor(hdc: HDC, color: COLORREF) -> COLORREF {
    COLORREF(unsafe { dime_gdi::SetTextColor(hdc.0, color.0) })
}

pub unsafe fn SetBkColor(hdc: HDC, color: COLORREF) -> COLORREF {
    COLORREF(unsafe { dime_gdi::SetBkColor(hdc.0, color.0) })
}

pub unsafe fn ExtTextOutW(
    hdc: HDC,
    x: i32,
    y: i32,
    options: ETO_OPTIONS,
    rc: Option<*const RECT>,
    s: PCWSTR,
    c: u32,
    dx: Option<*const i32>,
) -> BOOL {
    BOOL(unsafe {
        dime_gdi::ExtTextOutW(hdc.0, x, y, options.0, rc.unwrap_or(std::ptr::null()).cast(), s.0, c, dx.unwrap_or(std::ptr::null()))
    })
}

pub unsafe fn GetObjectW(h: HGDIOBJ, size: i32, out: Option<*mut c_void>) -> i32 {
    unsafe { dime_gdi::GetObjectW(h.0, size, out.unwrap_or(std::ptr::null_mut())) }
}

pub unsafe fn CreateFontIndirectW(lf: *const LOGFONTW) -> HFONT {
    HFONT(unsafe { dime_gdi::CreateFontIndirectW(lf.cast()) })
}

pub unsafe fn DrawTextW(hdc: HDC, s: &mut [u16], rc: *mut RECT, format: DRAW_TEXT_FORMAT) -> i32 {
    unsafe { dime_gdi::DrawTextW(hdc.0, s.as_ptr(), s.len() as i32, rc.cast(), format.0) }
}

pub unsafe fn Polygon(hdc: HDC, pts: &[POINT]) -> BOOL {
    BOOL(unsafe { dime_gdi::Polygon(hdc.0, pts.as_ptr().cast(), pts.len() as i32) })
}

pub unsafe fn CreateRoundRectRgn(l: i32, t: i32, r: i32, b: i32, w: i32, h: i32) -> HRGN {
    HRGN(unsafe { dime_gdi::CreateRoundRectRgn(l, t, r, b, w, h) })
}

pub unsafe fn SetWindowRgn(hwnd: HWND, rgn: Option<HRGN>, redraw: bool) -> i32 {
    unsafe { dime_gdi::SetWindowRgn(hwnd.0, rgn.map_or(std::ptr::null_mut(), |r| r.0), redraw as i32) }
}

pub unsafe fn GetDC(hwnd: Option<HWND>) -> HDC {
    HDC(unsafe { dime_gdi::GetDC(hwnd.map_or(std::ptr::null_mut(), |h| h.0)) })
}

pub unsafe fn ReleaseDC(hwnd: Option<HWND>, hdc: HDC) -> i32 {
    unsafe { dime_gdi::ReleaseDC(hwnd.map_or(std::ptr::null_mut(), |h| h.0), hdc.0) }
}

pub unsafe fn CreateCompatibleDC(hdc: Option<HDC>) -> HDC {
    HDC(unsafe { dime_gdi::CreateCompatibleDC(hdc.map_or(std::ptr::null_mut(), |h| h.0)) })
}

pub unsafe fn CreateCompatibleBitmap(hdc: HDC, cx: i32, cy: i32) -> HBITMAP {
    HBITMAP(unsafe { dime_gdi::CreateCompatibleBitmap(hdc.0, cx, cy) })
}

pub unsafe fn BitBlt(
    hdc: HDC,
    x: i32,
    y: i32,
    cx: i32,
    cy: i32,
    src: Option<HDC>,
    x1: i32,
    y1: i32,
    rop: ROP_CODE,
) -> windows::core::Result<()> {
    let ok = unsafe { dime_gdi::BitBlt(hdc.0, x, y, cx, cy, src.map_or(std::ptr::null_mut(), |h| h.0), x1, y1, rop.0) };
    if ok != 0 {
        Ok(())
    } else {
        Err(windows::core::Error::from_thread())
    }
}

pub unsafe fn DeleteDC(hdc: HDC) -> BOOL {
    BOOL(unsafe { dime_gdi::DeleteDC(hdc.0) })
}
