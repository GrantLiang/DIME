//! Creating a settings page's child windows (phase 5b-9c, Windows only).
//!
//! Port of `SettingsChildren::Cpp::Create` (`src/DIMESettings/
//! SettingsChildren.cpp`, formerly `SettingsWindow::CreateControls` with
//! `AddControl`, `SetupRichEditor`, `LoadRichEditor` and the child
//! `WM_MOUSEWHEEL` forwarder): one `CreateWindowExW` per
//! [`ControlSpec`](crate::controls::ControlSpec), the wheel forwarder, body
//! font and dark theme, and for the 自建詞庫 RichEdit its text limit, dark
//! colours, the saved table (read like `SettingsModel::LoadTextFileAsUtf16`),
//! the editor subclass and `EN_CHANGE`. The window keeps the control list
//! and validates the editor through [`ChildHost`].

// HWND / HFONT / HINSTANCE are opaque handles passed to Win32, never dereferenced.
#![allow(clippy::not_unsafe_ptr_arg_deref)]

use std::ffi::c_void;
use std::ptr::{null, null_mut};
use std::sync::atomic::{AtomicBool, Ordering};

use dime_config::text_file;

use crate::controls::{
    ControlSpec, CLS_COMBOBOX, CLS_EDIT, CLS_RICHEDIT, CLS_STATIC, DARK_CFD, DARK_EXPLORER, ROLE_RICH_EDITOR,
    ROLE_TOGGLE,
};
use crate::editor;

pub type Handle = *mut c_void;

type SubclassProc = unsafe extern "system" fn(Handle, u32, usize, isize, usize, usize) -> isize;

windows_link::link!("user32.dll" "system" fn CreateWindowExW(ex: u32, class: *const u16, name: *const u16, style: u32, x: i32, y: i32, w: i32, h: i32, parent: Handle, menu: Handle, inst: Handle, param: *mut c_void) -> Handle);
windows_link::link!("user32.dll" "system" fn SendMessageW(h: Handle, m: u32, w: usize, l: isize) -> isize);
windows_link::link!("user32.dll" "system" fn SetWindowTextW(h: Handle, text: *const u16) -> i32);
windows_link::link!("user32.dll" "system" fn GetParent(h: Handle) -> Handle);
windows_link::link!("comctl32.dll" "system" fn SetWindowSubclass(h: Handle, proc_: SubclassProc, id: usize, data: usize) -> i32);
windows_link::link!("comctl32.dll" "system" fn RemoveWindowSubclass(h: Handle, proc_: SubclassProc, id: usize) -> i32);
windows_link::link!("comctl32.dll" "system" fn DefSubclassProc(h: Handle, m: u32, w: usize, l: isize) -> isize);
windows_link::link!("uxtheme.dll" "system" fn SetWindowTheme(h: Handle, app: *const u16, idlist: *const u16) -> i32);
windows_link::link!("shlwapi.dll" "system" fn PathFileExistsW(path: *const u16) -> i32);
windows_link::link!("kernel32.dll" "system" fn LoadLibraryW(name: *const u16) -> Handle);
windows_link::link!("kernel32.dll" "system" fn CreateFileW(name: *const u16, access: u32, share: u32, sa: *const c_void, disposition: u32, flags: u32, template: Handle) -> Handle);
windows_link::link!("kernel32.dll" "system" fn GetFileSize(h: Handle, high: *mut u32) -> u32);
windows_link::link!("kernel32.dll" "system" fn ReadFile(h: Handle, buf: *mut u8, len: u32, read: *mut u32, ov: *mut c_void) -> i32);
windows_link::link!("kernel32.dll" "system" fn CloseHandle(h: Handle) -> i32);
windows_link::link!("user32.dll" "system" fn SetWindowRgn(h: Handle, rgn: Handle, redraw: i32) -> i32);
windows_link::link!("gdi32.dll" "system" fn CreateRectRgn(l: i32, t: i32, r: i32, b: i32) -> Handle);
windows_link::link!("user32.dll" "system" fn ValidateRect(h: Handle, rc: *const c_void) -> i32);
windows_link::link!("ole32.dll" "system" fn CoCreateInstance(clsid: *const Guid, outer: Handle, ctx: u32, iid: *const Guid, out: *mut *mut c_void) -> i32);
windows_link::link!("kernel32.dll" "system" fn MultiByteToWideChar(cp: u32, flags: u32, src: *const u8, cb: i32, dst: *mut u16, cch: i32) -> i32);

const WM_SETFONT: u32 = 0x0030;
const WM_MOUSEWHEEL: u32 = 0x020A;
const WM_PAINT: u32 = 0x000F;
const WM_ERASEBKGND: u32 = 0x0014;
const WM_PRINTCLIENT: u32 = 0x0318;
const WM_NCDESTROY: u32 = 0x0082;
const WM_USER: u32 = 0x0400;
const EM_GETLINECOUNT: u32 = 0x00BA;
const EM_EXLIMITTEXT: u32 = WM_USER + 53;
const EM_SETCHARFORMAT: u32 = WM_USER + 68;
const EM_SETEVENTMASK: u32 = WM_USER + 69;
const EM_SETBKGNDCOLOR: u32 = WM_USER + 67;
const SCF_DEFAULT: usize = 0x0000;
const SCF_ALL: usize = 0x0004;
const CFM_COLOR: u32 = 0x4000_0000;
const CFM_FACE: u32 = 0x2000_0000;
const CFM_SIZE: u32 = 0x8000_0000;
const CFM_WEIGHT: u32 = 0x0040_0000;
const ENM_CHANGE: isize = 0x0000_0001;
const FW_NORMAL: u16 = 400;
const LF_FACESIZE: usize = 32;

const GENERIC_READ: u32 = 0x8000_0000;
const FILE_SHARE_READ: u32 = 1;
const FILE_SHARE_WRITE: u32 = 2;
const OPEN_EXISTING: u32 = 3;
const FILE_FLAG_SEQUENTIAL_SCAN: u32 = 0x0800_0000;
const INVALID_HANDLE_VALUE: Handle = -1isize as Handle;
const INVALID_FILE_SIZE: u32 = 0xFFFF_FFFF;

/// The wheel forwarder's subclass id on plain controls.
pub const WHEEL_SUBCLASS_ID: usize = 1;

/// `SettingsChildren::Context` / `DimeSettingsChildContext`.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Context {
    pub parent: Handle,
    pub instance: Handle,
    pub font_body: Handle,
    pub is_dark: i32,
    pub card_bg: u32,
    pub text_primary: u32,
}

/// `SettingsChildren::Host`.
pub trait ChildHost {
    /// A control was created (`h` may be null). `add` is x, y, w, h.
    fn added(&mut self, ctrl_id: i32, h: Handle, add: [i32; 4], vis_h: i32);
    /// `GetCustomTableTxtPath` for the current mode, no NUL.
    fn editor_path(&mut self) -> Vec<u16>;
    /// The RichEdit is loaded and subclassed; `line_count` is `EM_GETLINECOUNT`.
    fn editor_ready(&mut self, hre: Handle, line_count: i32);
}

/// `CHARFORMAT2W` (116 bytes) as C++ lays it out: it derives from
/// `CHARFORMATW` (92 bytes with tail padding), so `wWeight` is at 92, not
/// right after `szFaceName` as in the C declaration.
#[repr(C)]
struct CharFormat2 {
    cb_size: u32,
    mask: u32,
    effects: u32,
    height: i32,
    offset: i32,
    text_color: u32,
    char_set: u8,
    pitch_and_family: u8,
    face_name: [u16; LF_FACESIZE],
    base_padding: u16,
    weight: u16,
    spacing: i16,
    back_color: u32,
    lcid: u32,
    cookie: u32,
    style: i16,
    kerning: u16,
    underline_type: u8,
    animation: u8,
    rev_author: u8,
    underline_color: u8,
}

const _: () = assert!(std::mem::size_of::<CharFormat2>() == 116);
const _: () = assert!(std::mem::offset_of!(CharFormat2, weight) == 92);

impl CharFormat2 {
    fn new(mask: u32) -> Self {
        CharFormat2 {
            cb_size: std::mem::size_of::<CharFormat2>() as u32,
            mask,
            effects: 0,
            height: 0,
            offset: 0,
            text_color: 0,
            char_set: 0,
            pitch_and_family: 0,
            face_name: [0; LF_FACESIZE],
            base_padding: 0,
            weight: 0,
            spacing: 0,
            back_color: 0,
            lcid: 0,
            cookie: 0,
            style: 0,
            kerning: 0,
            underline_type: 0,
            animation: 0,
            rev_author: 0,
            underline_color: 0,
        }
    }
}

fn wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

/// `SettingsControls::ClassName`.
fn class_name(control_class: i32) -> &'static str {
    match control_class {
        CLS_COMBOBOX => "COMBOBOX",
        CLS_EDIT => "EDIT",
        CLS_STATIC => "STATIC",
        CLS_RICHEDIT => "RICHEDIT50W",
        _ => "BUTTON",
    }
}

/// A COM `GUID`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Guid {
    data1: u32,
    data2: u16,
    data3: u16,
    data4: [u8; 8],
}

const CLSID_ACC_PROP_SERVICES: Guid = Guid {
    data1: 0xb5f8_350b,
    data2: 0x0548,
    data3: 0x48b1,
    data4: [0xa6, 0xee, 0x88, 0xbd, 0x00, 0xb4, 0xa5, 0xe7],
};
const IID_IACC_PROP_SERVICES: Guid = Guid {
    data1: 0x6e26_e776,
    data2: 0x04f0,
    data3: 0x495d,
    data4: [0x80, 0xe4, 0x33, 0x30, 0x35, 0x2e, 0x31, 0x69],
};
const PROPID_ACC_NAME: Guid = Guid {
    data1: 0x608d_3df8,
    data2: 0x8128,
    data3: 0x4aa7,
    data4: [0xa4, 0x28, 0xf5, 0x5e, 0x49, 0x26, 0x72, 0x91],
};
const CLSCTX_INPROC_SERVER: u32 = 1;
const OBJID_CLIENT: u32 = 0xFFFF_FFFC;
const CHILDID_SELF: u32 = 0;

/// The `IAccPropServices` methods used here, in vtable order.
#[repr(C)]
struct AccPropServicesVtbl {
    query_interface: usize,
    add_ref: usize,
    release: unsafe extern "system" fn(*mut c_void) -> u32,
    set_prop_value: usize,
    set_prop_server: usize,
    clear_props: usize,
    set_hwnd_prop: usize,
    set_hwnd_prop_str: unsafe extern "system" fn(*mut c_void, Handle, u32, u32, Guid, *const u16) -> i32,
    set_hwnd_prop_server: usize,
    clear_hwnd_props: unsafe extern "system" fn(*mut c_void, Handle, u32, u32, *const Guid, i32) -> i32,
}

/// `CoCreateInstance(CLSID_AccPropServices)`: null without COM on the thread.
fn acc_prop_services() -> *mut c_void {
    let mut svc: *mut c_void = null_mut();
    // SAFETY: valid GUIDs and out pointer.
    let hr = unsafe {
        CoCreateInstance(&CLSID_ACC_PROP_SERVICES, null_mut(), CLSCTX_INPROC_SERVER, &IID_IACC_PROP_SERVICES, &mut svc)
    };
    if hr < 0 {
        null_mut()
    } else {
        svc
    }
}

/// `SettingsChildren::Cpp::SetAccName`: the accessible name through dynamic
/// annotation (phase 5b-14). `name` has no NUL.
fn set_acc_name(h: Handle, name: &[u16]) -> bool {
    let svc = acc_prop_services();
    if svc.is_null() {
        return false;
    }
    let text: Vec<u16> = name.iter().copied().chain(std::iter::once(0)).collect();
    // SAFETY: `svc` is a live IAccPropServices; released once.
    unsafe {
        let vtbl = &**(svc as *mut *const AccPropServicesVtbl);
        let hr = (vtbl.set_hwnd_prop_str)(svc, h, OBJID_CLIENT, CHILDID_SELF, PROPID_ACC_NAME, text.as_ptr());
        (vtbl.release)(svc);
        hr >= 0
    }
}

/// `SettingsChildren::Cpp::ClearAccName`.
fn clear_acc_name(h: Handle) {
    let svc = acc_prop_services();
    if svc.is_null() {
        return;
    }
    let props = [PROPID_ACC_NAME];
    // SAFETY: as above.
    unsafe {
        let vtbl = &**(svc as *mut *const AccPropServicesVtbl);
        (vtbl.clear_hwnd_props)(svc, h, OBJID_CLIENT, CHILDID_SELF, props.as_ptr(), 1);
        (vtbl.release)(svc);
    }
}

/// Child controls forward the wheel to the content area so it scrolls.
/// Reference data 1: the control has an accessible name to clear.
unsafe extern "system" fn wheel_subclass_proc(
    h: Handle,
    msg: u32,
    w: usize,
    l: isize,
    id: usize,
    flags: usize,
) -> isize {
    // SAFETY: called by comctl32 for a subclassed child; plain user32 calls.
    unsafe {
        if msg == WM_MOUSEWHEEL {
            return SendMessageW(GetParent(h), msg, w, l);
        }
        // A toggle's check box never paints: its empty window region does
        // not stop the button from drawing itself when the content area is
        // redrawn with RDW_ALLCHILDREN (scrolling), which covered the switch.
        if flags & SUBCLASS_TOGGLE_BOX != 0 {
            match msg {
                WM_PAINT => {
                    ValidateRect(h, null());
                    return 0;
                }
                WM_ERASEBKGND => return 1,
                WM_PRINTCLIENT => return 0,
                _ => {}
            }
        }
        if msg == WM_NCDESTROY {
            if flags & SUBCLASS_NAMED != 0 {
                clear_acc_name(h);
            }
            RemoveWindowSubclass(h, wheel_subclass_proc, id);
        }
        DefSubclassProc(h, msg, w, l)
    }
}

/// `wheel_subclass_proc` reference data: the control has an accessible name
/// to clear.
const SUBCLASS_NAMED: usize = 1;
/// `wheel_subclass_proc` reference data: a toggle row's check box.
const SUBCLASS_TOGGLE_BOX: usize = 2;

static MSFTEDIT_LOADED: AtomicBool = AtomicBool::new(false);

/// `MultiByteToWideChar` for [`text_file::decode_text_bytes`]; same as the
/// `dime_settings_decode_text` bridge.
fn win_convert(cp: u32, flags: u32, bytes: &[u8]) -> Option<Vec<u16>> {
    if bytes.len() > i32::MAX as usize {
        return None;
    }
    let cb = bytes.len() as i32;
    let src = if bytes.is_empty() { null() } else { bytes.as_ptr() };
    // SAFETY: `src` is null only when `cb` is 0, which the API rejects.
    let needed = unsafe { MultiByteToWideChar(cp, flags, src, cb, null_mut(), 0) };
    if needed <= 0 {
        return None;
    }
    let mut out = vec![0u16; needed as usize];
    // SAFETY: `out` holds `needed` units.
    let wrote = unsafe { MultiByteToWideChar(cp, flags, src, cb, out.as_mut_ptr(), needed) };
    if wrote <= 0 {
        return Some(Vec::new());
    }
    out.truncate(wrote as usize);
    Some(out)
}

/// `SettingsModel::LoadTextFileAsUtf16` (the `DIME_USE_RUST_CONFIG` path):
/// `None` when the file cannot be opened or read or does not decode; an
/// empty or unsized file is `Some(empty)`. `path` is NUL-terminated.
pub fn load_text_file(path: &[u16]) -> Option<Vec<u16>> {
    // SAFETY: `path` is NUL-terminated; the handle is closed on every path.
    unsafe {
        let h = CreateFileW(
            path.as_ptr(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            null(),
            OPEN_EXISTING,
            FILE_FLAG_SEQUENTIAL_SCAN,
            null_mut(),
        );
        if h == INVALID_HANDLE_VALUE {
            return None;
        }
        let size = GetFileSize(h, null_mut());
        if size == INVALID_FILE_SIZE || size == 0 {
            CloseHandle(h);
            return Some(Vec::new());
        }
        let mut raw = vec![0u8; size as usize];
        let mut read = 0u32;
        if ReadFile(h, raw.as_mut_ptr(), size, &mut read, null_mut()) == 0 || read == 0 {
            CloseHandle(h);
            return None;
        }
        CloseHandle(h);
        raw.truncate(read as usize);
        text_file::decode_text_bytes(&raw, win_convert)
    }
}

/// Right after the RichEdit is created: text limit, font, dark colours.
fn setup_rich_editor(ctx: &Context, hre: Handle) {
    // SAFETY: plain messages to a window we created.
    unsafe {
        // Issue #130: raise the RichEdit text limit to 100 MB.
        SendMessageW(hre, EM_EXLIMITTEXT, 0, 100 * 1024 * 1024);
        SendMessageW(hre, WM_SETFONT, ctx.font_body as usize, 1);
        if ctx.is_dark != 0 {
            let theme = wide("DarkMode_Explorer");
            SetWindowTheme(hre, theme.as_ptr(), null());
            SendMessageW(hre, EM_SETBKGNDCOLOR, 0, ctx.card_bg as isize);
            let mut cf = CharFormat2::new(CFM_COLOR);
            cf.text_color = ctx.text_primary;
            SendMessageW(hre, EM_SETCHARFORMAT, SCF_ALL, &cf as *const CharFormat2 as isize);
        }
    }
}

/// The saved custom table, the editor subclass, `EN_CHANGE`, then the
/// window validates the visible lines.
fn load_rich_editor(host: &mut dyn ChildHost, hre: Handle) {
    let mut path = host.editor_path();
    path.push(0);
    // SAFETY: plain calls on a window we created; `path` is NUL-terminated.
    unsafe {
        if PathFileExistsW(path.as_ptr()) != 0 {
            let mut cf = CharFormat2::new(CFM_FACE | CFM_SIZE | CFM_WEIGHT);
            for (dst, src) in cf.face_name.iter_mut().zip("Microsoft JhengHei UI".encode_utf16()) {
                *dst = src;
            }
            cf.height = 10 * 20; // 10pt in twips; RichEdit applies dpi itself
            cf.weight = FW_NORMAL;
            SendMessageW(hre, EM_SETCHARFORMAT, SCF_DEFAULT, &cf as *const CharFormat2 as isize);
            if let Some(mut text) = load_text_file(&path) {
                text.push(0);
                SetWindowTextW(hre, text.as_ptr());
            }
        }
        SetWindowSubclass(hre, editor::subclass_proc, 0, 0);
        SendMessageW(hre, EM_SETEVENTMASK, 0, ENM_CHANGE);
        let lines = SendMessageW(hre, EM_GETLINECOUNT, 0, 0) as i32;
        host.editor_ready(hre, lines);
    }
}

/// `SettingsChildren::Create`: the windows for `specs`, in order.
pub fn create(host: &mut dyn ChildHost, ctx: &Context, specs: &[ControlSpec]) {
    for spec in specs {
        if spec.control_class == CLS_RICHEDIT && !MSFTEDIT_LOADED.load(Ordering::Relaxed) {
            let dll = wide("msftedit.dll");
            // SAFETY: NUL-terminated module name.
            if !unsafe { LoadLibraryW(dll.as_ptr()) }.is_null() {
                MSFTEDIT_LOADED.store(true, Ordering::Relaxed);
            }
        }
        let class = wide(class_name(spec.control_class));
        let caption: Option<Vec<u16>> = spec
            .text
            .as_ref()
            .map(|t| t.iter().copied().chain(std::iter::once(0)).collect());
        // SAFETY: NUL-terminated strings that outlive the call.
        let h = unsafe {
            CreateWindowExW(
                spec.ex_style,
                class.as_ptr(),
                caption.as_ref().map_or(null(), |c| c.as_ptr()),
                spec.style,
                spec.create[0],
                spec.create[1],
                spec.create[2],
                spec.create[3],
                ctx.parent,
                spec.menu_id as u32 as usize as Handle,
                ctx.instance,
                null_mut(),
            )
        };
        if spec.role == ROLE_RICH_EDITOR {
            // The editor scrolls itself: no wheel forwarder.
            if !h.is_null() {
                setup_rich_editor(ctx, h);
            }
            host.added(spec.ctrl_id, h, spec.add, spec.vis_h);
            if !h.is_null() {
                load_rich_editor(host, h);
            }
            continue;
        }
        if !h.is_null() {
            // SAFETY: plain calls on a window we created.
            unsafe {
                if spec.set_font {
                    SendMessageW(h, WM_SETFONT, ctx.font_body as usize, 1);
                }
                if ctx.is_dark != 0 && spec.dark_theme == DARK_CFD {
                    let theme = wide("DarkMode_CFD");
                    SetWindowTheme(h, theme.as_ptr(), null());
                } else if ctx.is_dark != 0 && spec.dark_theme == DARK_EXPLORER {
                    let theme = wide("DarkMode_Explorer");
                    SetWindowTheme(h, theme.as_ptr(), null());
                }
            }
        }
        // A toggle's check box takes no pixels and no clicks: the content
        // area paints the switch and handles the row click.
        if !h.is_null() && spec.role == ROLE_TOGGLE {
            // SAFETY: the system owns the region after SetWindowRgn.
            unsafe { SetWindowRgn(h, CreateRectRgn(0, 0, 0, 0), 0) };
        }
        let named = !h.is_null() && spec.acc_name.as_ref().is_some_and(|n| !n.is_empty() && set_acc_name(h, n));
        host.added(spec.ctrl_id, h, spec.add, spec.vis_h);
        if !h.is_null() {
            // SAFETY: subclassing a window we created.
            let flags = if named { SUBCLASS_NAMED } else { 0 }
                | if spec.role == ROLE_TOGGLE { SUBCLASS_TOGGLE_BOX } else { 0 };
            unsafe { SetWindowSubclass(h, wheel_subclass_proc, WHEEL_SUBCLASS_ID, flags) };
        }
    }
}
