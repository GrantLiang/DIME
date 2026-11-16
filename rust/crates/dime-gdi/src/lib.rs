//! GDI / GDI+ wrappers with a recorder for tests (phase 6a-3, shared with
//! dime-ui in 6b-2). Windows only; see `gdi`.

#[cfg(windows)]
mod gdi;
#[cfg(windows)]
pub use gdi::*;
