//! `wWinMain` of `DIMESettings.exe` in Rust (phase 5b-15f, Windows only).
//!
//! Port of `src/DIMESettings/DIMESettings.cpp`, run by the cargo-built
//! executable (`crates/dime-settings-exe`):
//!
//! - `SettingsLifecycle::ParseLaunch` decides (`lifecycle::parse_launch`);
//!   `--reset-ui` deletes the window placement first;
//! - a command line that is not a GUI launch runs the CLI (`settings_cli`)
//!   with the CRT's streams as `dime_settings::console` writes them: the
//!   inherited handles when `stdout` is set (pipe, file), otherwise the
//!   parent's console (or a new one) through `CONOUT$`, erasing the prompt
//!   line the shell already printed and typing a silent Enter afterwards;
//! - otherwise one instance only (the mutex; a second launch hands its
//!   command line to the first window with `WM_COPYDATA` and exits with -1),
//!   dpi awareness (Per-Monitor V2, else `SetProcessDpiAwareness`), the
//!   forced dark app mode when the system is dark, common controls, COM,
//!   the reverse-conversion providers and the first `LoadConfig`, then the
//!   window (`window::run` with `NativeApp`).
//!
//! `debugPrint` calls of the C++ version are not ported (debug output only).

use std::ptr::{null, null_mut};

use dime_config::ImeMode;
use dime_settings::console::Stream;
use dime_settings::window::{self, App};
use dime_settings::{lifecycle, shell};

use crate::settings_app::NativeApp;
use crate::settings_cli::{self, Streams};

type Handle = *mut std::ffi::c_void;

windows_link::link!("kernel32.dll" "system" fn GetCommandLineW() -> *const u16);
windows_link::link!("kernel32.dll" "system" fn GetStartupInfoW(si: *mut StartupInfo));
windows_link::link!("kernel32.dll" "system" fn GetModuleHandleW(name: *const u16) -> Handle);
windows_link::link!("kernel32.dll" "system" fn LoadLibraryW(name: *const u16) -> Handle);
windows_link::link!("kernel32.dll" "system" fn FreeLibrary(module: Handle) -> i32);
windows_link::link!("kernel32.dll" "system" fn GetProcAddress(module: Handle, name: *const u8) -> *const std::ffi::c_void);
windows_link::link!("kernel32.dll" "system" fn GetLastError() -> u32);
windows_link::link!("kernel32.dll" "system" fn CreateMutexW(sa: *const std::ffi::c_void, owner: i32, name: *const u16) -> Handle);
windows_link::link!("kernel32.dll" "system" fn ReleaseMutex(h: Handle) -> i32);
windows_link::link!("kernel32.dll" "system" fn CloseHandle(h: Handle) -> i32);
windows_link::link!("kernel32.dll" "system" fn GetStdHandle(which: u32) -> Handle);
windows_link::link!("kernel32.dll" "system" fn AttachConsole(pid: u32) -> i32);
windows_link::link!("kernel32.dll" "system" fn AllocConsole() -> i32);
windows_link::link!("kernel32.dll" "system" fn FreeConsole() -> i32);
windows_link::link!("kernel32.dll" "system" fn CreateFileW(name: *const u16, access: u32, share: u32, sa: *const std::ffi::c_void, disposition: u32, flags: u32, template: Handle) -> Handle);
windows_link::link!("kernel32.dll" "system" fn GetConsoleScreenBufferInfo(h: Handle, info: *mut ConsoleScreenBufferInfo) -> i32);
windows_link::link!("kernel32.dll" "system" fn FillConsoleOutputCharacterW(h: Handle, ch: u16, len: u32, at: u32, written: *mut u32) -> i32);
windows_link::link!("kernel32.dll" "system" fn SetConsoleCursorPosition(h: Handle, at: u32) -> i32);
windows_link::link!("kernel32.dll" "system" fn GetConsoleMode(h: Handle, mode: *mut u32) -> i32);
windows_link::link!("kernel32.dll" "system" fn SetConsoleMode(h: Handle, mode: u32) -> i32);
windows_link::link!("kernel32.dll" "system" fn WriteConsoleInputW(h: Handle, records: *const InputRecord, count: u32, written: *mut u32) -> i32);
windows_link::link!("user32.dll" "system" fn FindWindowW(class: *const u16, title: *const u16) -> Handle);
windows_link::link!("user32.dll" "system" fn SendMessageW(h: Handle, msg: u32, wp: usize, lp: isize) -> isize);
windows_link::link!("user32.dll" "system" fn SetForegroundWindow(h: Handle) -> i32);
windows_link::link!("comctl32.dll" "system" fn InitCommonControls());
windows_link::link!("ole32.dll" "system" fn CoInitialize(reserved: *const std::ffi::c_void) -> i32);
windows_link::link!("shell32.dll" "system" fn CommandLineToArgvW(cmd: *const u16, argc: *mut i32) -> *mut *mut u16);
windows_link::link!("kernel32.dll" "system" fn LocalFree(mem: Handle) -> Handle);

/// `DIME_SETTINGS_INSTANCE_MUTEX_NAME`.
pub const INSTANCE_MUTEX: &str = "{B11F1FB2-3ECC-409E-A036-4162ADCEF1A3}";
/// The legacy dialog's title, still searched for.
const LEGACY_TITLE: &str = "DIME設定";
/// `WM_SETTINGS_SWITCH_MODE` (`WM_APP + 2`).
const WM_SETTINGS_SWITCH_MODE: usize = 0x8000 + 2;
const WM_COPYDATA: u32 = 0x004A;
const ERROR_ALREADY_EXISTS: u32 = 183;
const STD_INPUT_HANDLE: u32 = -10i32 as u32;
const STD_OUTPUT_HANDLE: u32 = -11i32 as u32;
const STD_ERROR_HANDLE: u32 = -12i32 as u32;
const ATTACH_PARENT_PROCESS: u32 = u32::MAX;
const GENERIC_WRITE: u32 = 0x4000_0000;
const FILE_SHARE_READ_WRITE: u32 = 3;
const CREATE_ALWAYS: u32 = 2;
const FILE_ATTRIBUTE_NORMAL: u32 = 0x80;
const ENABLE_ECHO_INPUT: u32 = 0x0004;
const KEY_EVENT: u16 = 1;
const VK_RETURN: u16 = 0x0D;
const STARTF_USESHOWWINDOW: u32 = 1;
const SW_SHOWDEFAULT: i32 = 10;
/// `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`.
const DPI_PER_MONITOR_V2: isize = -4;
/// `IDI_DIMESETTINGS`.
pub const ICON_ID: i32 = 10;

#[repr(C)]
struct StartupInfo {
    cb: u32,
    reserved: *mut u16,
    desktop: *mut u16,
    title: *mut u16,
    x: u32,
    y: u32,
    x_size: u32,
    y_size: u32,
    x_count_chars: u32,
    y_count_chars: u32,
    fill_attribute: u32,
    flags: u32,
    show_window: u16,
    cb_reserved2: u16,
    reserved2: *mut u8,
    std_input: Handle,
    std_output: Handle,
    std_error: Handle,
}

#[repr(C)]
#[derive(Default)]
struct ConsoleScreenBufferInfo {
    size_x: i16,
    size_y: i16,
    cursor_x: i16,
    cursor_y: i16,
    attributes: u16,
    window: [i16; 4],
    max_x: i16,
    max_y: i16,
}

/// `INPUT_RECORD` holding a `KEY_EVENT_RECORD` (20 bytes).
#[repr(C)]
#[derive(Clone, Copy)]
struct InputRecord {
    event_type: u16,
    _pad: u16,
    key_down: i32,
    repeat_count: u16,
    virtual_key_code: u16,
    virtual_scan_code: u16,
    unicode_char: u16,
    control_key_state: u32,
}

#[repr(C)]
struct CopyDataStruct {
    data: usize,
    cb_data: u32,
    lp_data: *const std::ffi::c_void,
}

#[repr(C)]
struct OsVersionInfo {
    size: u32,
    major: u32,
    minor: u32,
    build: u32,
    platform: u32,
    csd: [u16; 128],
}

fn wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

fn valid(h: Handle) -> bool {
    !h.is_null() && h as isize != -1
}

/// The UCRT's `lpCmdLine` for `wWinMain`: the command line after the
/// program name (a token of characters above space, where a double quote
/// toggles quoting) and the blanks (characters up to space) after it.
pub fn skip_program_name(cmd: &[u16]) -> &[u16] {
    let mut i = 0;
    let mut quoted = false;
    while i < cmd.len() && (cmd[i] > 0x20 || (cmd[i] != 0 && quoted)) {
        if cmd[i] == u16::from(b'"') {
            quoted = !quoted;
        }
        i += 1;
    }
    while i < cmd.len() && cmd[i] != 0 && cmd[i] <= 0x20 {
        i += 1;
    }
    let rest = &cmd[i..];
    let end = rest.iter().position(|&c| c == 0).unwrap_or(rest.len());
    &rest[..end]
}

/// `IsWindowsVersionOrGreater(major, minor, build)` (`RtlGetVersion`).
fn windows_at_least(major: u32, minor: u32, build: u32) -> bool {
    type RtlGetVersion = unsafe extern "system" fn(*mut OsVersionInfo) -> i32;
    let mut v = OsVersionInfo { size: std::mem::size_of::<OsVersionInfo>() as u32, major: 0, minor: 0, build: 0, platform: 0, csd: [0; 128] };
    let ntdll = wide("ntdll.dll");
    // SAFETY: an export of a loaded system DLL with its documented signature.
    unsafe {
        let m = GetModuleHandleW(ntdll.as_ptr());
        if !m.is_null() {
            let f = GetProcAddress(m, c"RtlGetVersion".as_ptr().cast());
            if !f.is_null() {
                let f: RtlGetVersion = std::mem::transmute(f);
                if f(&mut v) != 0 {
                    v.major = 0;
                    v.minor = 0;
                    v.build = 0;
                }
            }
        }
    }
    (v.major, v.minor, v.build) >= (major, minor, build)
}

/// `RunCLI(lpCmdLine)`: split with `CommandLineToArgvW` (the first token is
/// an argument here, parsed by the program-name rules, as in C++).
fn run_cli(cmd: &[u16], streams: Streams) -> i32 {
    let z: Vec<u16> = cmd.iter().copied().chain(std::iter::once(0)).collect();
    let mut argc = 0i32;
    // SAFETY: NUL-terminated command line; the array is freed below.
    let argv = unsafe { CommandLineToArgvW(z.as_ptr(), &mut argc) };
    if argv.is_null() {
        return 1;
    }
    let mut owned: Vec<Vec<u16>> = Vec::new();
    for i in 0..argc.max(0) as usize {
        // SAFETY: `argc` NUL-terminated strings.
        unsafe {
            let p = *argv.add(i);
            let mut n = 0;
            while *p.add(n) != 0 {
                n += 1;
            }
            owned.push(std::slice::from_raw_parts(p, n).to_vec());
        }
    }
    // SAFETY: from CommandLineToArgvW.
    unsafe { LocalFree(argv.cast()) };
    let args: Vec<&[u16]> = owned.iter().map(Vec::as_slice).collect();
    settings_cli::run(&args, streams, None)
}

/// The CLI half of `wWinMain`.
fn cli_main(cmd: &[u16]) -> i32 {
    // SAFETY: plain kernel32 calls.
    unsafe {
        let out = GetStdHandle(STD_OUTPUT_HANDLE);
        if valid(out) {
            // Redirected (pipe, file): the inherited handles.
            let err = GetStdHandle(STD_ERROR_HANDLE);
            let err = if valid(err) { err } else { null_mut() };
            return run_cli(cmd, Streams::Console { out: Stream::stdout(out), err: Stream::unbuffered(err) });
        }
        let attached = AttachConsole(ATTACH_PARENT_PROCESS) != 0;
        if !attached {
            AllocConsole();
        }
        // freopen_s(&fp, "CONOUT$", "w", stdout / stderr).
        let conout = wide("CONOUT$");
        let open = || CreateFileW(conout.as_ptr(), GENERIC_WRITE, FILE_SHARE_READ_WRITE, null(), CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, null_mut());
        let (con_out, con_err) = (open(), open());
        if attached {
            // Blank the prompt the shell already printed.
            let h = GetStdHandle(STD_OUTPUT_HANDLE);
            let mut info = ConsoleScreenBufferInfo::default();
            if GetConsoleScreenBufferInfo(h, &mut info) != 0 {
                let line_start = (info.cursor_y as u16 as u32) << 16;
                let mut filled = 0u32;
                FillConsoleOutputCharacterW(h, u16::from(b' '), info.size_x as u16 as u32, line_start, &mut filled);
                SetConsoleCursorPosition(h, line_start);
            }
        }
        let rc = run_cli(cmd, Streams::Console { out: Stream::stdout(con_out), err: Stream::unbuffered(con_err) });
        if attached {
            // A silent Enter so the shell's ReadConsole returns and the
            // prompt comes back.
            let h_in = GetStdHandle(STD_INPUT_HANDLE);
            let mut saved = 0u32;
            GetConsoleMode(h_in, &mut saved);
            SetConsoleMode(h_in, saved & !ENABLE_ECHO_INPUT);
            let down = InputRecord {
                event_type: KEY_EVENT,
                _pad: 0,
                key_down: 1,
                repeat_count: 1,
                virtual_key_code: VK_RETURN,
                virtual_scan_code: 0,
                unicode_char: u16::from(b'\r'),
                control_key_state: 0,
            };
            let up = InputRecord { key_down: 0, ..down };
            let records = [down, up];
            let mut written = 0u32;
            WriteConsoleInputW(h_in, records.as_ptr(), 2, &mut written);
            SetConsoleMode(h_in, saved);
        }
        FreeConsole();
        rc
    }
}

/// `SetAppPreferredMode`: uxtheme ordinal 135 (`SetPreferredAppMode`).
fn set_app_preferred_mode(mode: i32) {
    let name = wide("uxtheme.dll");
    // SAFETY: an export of a loaded system DLL, `void (int)`.
    unsafe {
        let m = GetModuleHandleW(name.as_ptr());
        if !m.is_null() {
            let f = GetProcAddress(m, 135usize as *const u8);
            if !f.is_null() {
                let f: unsafe extern "system" fn(i32) = std::mem::transmute(f);
                f(mode);
            }
        }
    }
}

/// The dpi awareness tiers of `wWinMain`.
fn set_dpi_awareness() {
    // SAFETY: exports of system DLLs with their documented signatures.
    unsafe {
        let user32 = wide("User32.dll");
        let m = GetModuleHandleW(user32.as_ptr());
        if !m.is_null() {
            let f = GetProcAddress(m, c"SetProcessDpiAwarenessContext".as_ptr().cast());
            if !f.is_null() {
                let f: unsafe extern "system" fn(isize) -> i32 = std::mem::transmute(f);
                if f(DPI_PER_MONITOR_V2) != 0 {
                    return;
                }
            }
        }
        if windows_at_least(8, 1, 0) {
            let shcore = wide("Shcore.dll");
            let m = LoadLibraryW(shcore.as_ptr());
            if !m.is_null() {
                let f = GetProcAddress(m, c"SetProcessDpiAwareness".as_ptr().cast());
                if !f.is_null() {
                    let f: unsafe extern "system" fn(i32) -> i32 = std::mem::transmute(f);
                    if f(2) < 0 {
                        f(1);
                    }
                }
                FreeLibrary(m);
            }
        }
    }
}

/// `wWinMain(hInstance, ..., lpCmdLine, nCmdShow)`; `version` is
/// `BUILD_VER_MAJOR`, `BUILD_VER_MINOR`, `BUILD_COMMIT_COUNT` for the caption.
pub fn win_main(instance: Handle, cmd: &[u16], n_cmd_show: i32, version: [u32; 3]) -> i32 {
    let launch = lifecycle::parse_launch(cmd);
    if launch.reset_ui != 0 {
        let key = wide(window::PLACEMENT_KEY);
        let value = wide(window::PLACEMENT_VALUE);
        shell::delete_placement(key.as_ptr(), value.as_ptr());
    }
    if !cmd.is_empty() && launch.new_ui == 0 && launch.legacy == 0 {
        return cli_main(cmd);
    }

    // One instance only.
    let mutex_name = wide(INSTANCE_MUTEX);
    // SAFETY: plain kernel32 / user32 calls with NUL-terminated strings;
    // `cds` and `z` outlive the SendMessage.
    unsafe {
        let mutex = CreateMutexW(null(), 1, mutex_name.as_ptr());
        let already = GetLastError() == ERROR_ALREADY_EXISTS;
        if mutex.is_null() || already {
            if !mutex.is_null() {
                ReleaseMutex(mutex);
                CloseHandle(mutex);
            }
            let class = wide(window::MAIN_CLASS);
            let mut existing = FindWindowW(class.as_ptr(), null());
            if existing.is_null() {
                let title = wide(LEGACY_TITLE);
                existing = FindWindowW(null(), title.as_ptr());
            }
            if !existing.is_null() {
                if launch.new_ui != 0 {
                    let z: Vec<u16> = cmd.iter().copied().chain(std::iter::once(0)).collect();
                    let cds = CopyDataStruct { data: WM_SETTINGS_SWITCH_MODE, cb_data: (z.len() * 2) as u32, lp_data: z.as_ptr().cast() };
                    SendMessageW(existing, WM_COPYDATA, 0, &cds as *const CopyDataStruct as isize);
                }
                SetForegroundWindow(existing);
            }
            return -1;
        }

        set_dpi_awareness();
        // ForceDark (2) before any window: every window, the message box
        // included, is dark.
        if dime_settings::appenv::system_dark() {
            set_app_preferred_mode(2);
        }
        InitCommonControls();

        let mode = if launch.new_ui != 0 { launch.gui_mode } else { ImeMode::Dayi as i32 };
        // COM for the TSF profile enumeration.
        CoInitialize(null());
        let app = NativeApp::new(None);
        // The reverse-conversion providers before the first LoadConfig
        // (which drops this IME from the list).
        app.enumerate_reverse_providers();
        app.load_config(mode);
        let caption = format!("DIME 設定 v{}.{}.{}", version[0], version[1], version[2]);
        let caption: Vec<u16> = caption.encode_utf16().collect();
        let result = window::run(Box::new(app), instance, n_cmd_show, mode, &caption, ICON_ID);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        result
    }
}

/// The process entry: the UCRT's `wWinMain` arguments, then `win_main`.
pub fn main(version: [u32; 3]) -> i32 {
    // uxtheme.dll is a load-time import of the C++ executable; the dark
    // theme checks look it up with GetModuleHandle.
    let uxtheme = wide("uxtheme.dll");
    // SAFETY: plain kernel32 calls; the command line lives as long as the
    // process.
    unsafe {
        LoadLibraryW(uxtheme.as_ptr());
        let full = GetCommandLineW();
        let mut n = 0;
        if !full.is_null() {
            while *full.add(n) != 0 {
                n += 1;
            }
        }
        let full = if full.is_null() { &[][..] } else { std::slice::from_raw_parts(full, n) };
        let cmd = skip_program_name(full);
        let mut si: StartupInfo = std::mem::zeroed();
        si.cb = std::mem::size_of::<StartupInfo>() as u32;
        GetStartupInfoW(&mut si);
        let show = if si.flags & STARTF_USESHOWWINDOW != 0 { i32::from(si.show_window) } else { SW_SHOWDEFAULT };
        win_main(GetModuleHandleW(null()), cmd, show, version)
    }
}

/// Test hook: `WriteWide` of the Rust executable on `handle`; `buffered`
/// keeps `stdout`'s 4,096-byte buffer (a pipe or file), flushed on return.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_console_write_for_test(handle: Handle, text: *const u16, len: usize, buffered: i32) {
    crate::guard((), || {
        let mut s = if buffered != 0 { Stream::stdout(handle) } else { Stream::unbuffered(handle) };
        if !text.is_null() && len > 0 {
            // SAFETY: `len` units from the caller.
            s.write_wide(unsafe { std::slice::from_raw_parts(text, len) });
        }
    })
}

/// Test hook: `skip_program_name` into `out` (`cap` units with the NUL);
/// returns the full length.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_skip_program_name_for_test(cmd: *const u16, out: *mut u16, cap: usize) -> usize {
    crate::guard(0, || {
        if cmd.is_null() {
            return 0;
        }
        // SAFETY: NUL-terminated input; `out` holds `cap` units.
        unsafe {
            let mut n = 0;
            while *cmd.add(n) != 0 {
                n += 1;
            }
            let rest = skip_program_name(std::slice::from_raw_parts(cmd, n));
            if !out.is_null() && cap > 0 {
                let c = rest.len().min(cap - 1);
                std::ptr::copy_nonoverlapping(rest.as_ptr(), out, c);
                *out.add(c) = 0;
            }
            rest.len()
        }
    })
}

/// Test hook: -1 makes the system-dark check read the system again; 0 / 1
/// make it return that (`appenv::SYSTEM_DARK_OVERRIDE`).
#[no_mangle]
pub extern "C" fn dime_settings_set_system_dark_for_test(value: i32) {
    crate::guard((), || {
        dime_settings::appenv::SYSTEM_DARK_OVERRIDE.store(value, std::sync::atomic::Ordering::Relaxed);
    })
}

/// Test hook: starts recording the settings window's GDI / GDI+ calls on
/// this thread (`dime_settings::gdi`); `synthetic` != 0 answers text
/// measurements from the requested font heights instead of the installed
/// fonts.
#[no_mangle]
pub extern "C" fn dime_settings_gdi_record_start(synthetic: i32) {
    crate::guard((), || dime_settings::gdi::record_start(synthetic != 0))
}

thread_local! {
    /// A finished recording, until a call with a buffer takes it.
    static GDI_LOG: std::cell::RefCell<Option<Vec<u16>>> = const { std::cell::RefCell::new(None) };
}

/// Test hook: ends the recording; the calls, one per line ('\n' after each),
/// go to `out` (`cap` units with the NUL). Returns the full length. A null
/// `out` only asks for the length (the text waits for the next call); a
/// buffer takes the text.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_gdi_record_take(out: *mut u16, cap: usize) -> usize {
    crate::guard(0, || {
        GDI_LOG.with(|g| {
            let mut g = g.borrow_mut();
            let lines = dime_settings::gdi::record_take();
            if g.is_none() || !lines.is_empty() {
                let mut text = String::new();
                for line in lines {
                    text.push_str(&line);
                    text.push('\n');
                }
                *g = Some(text.encode_utf16().collect());
            }
            let len = g.as_ref().map_or(0, Vec::len);
            if !out.is_null() && cap > 0 {
                let units = g.take().unwrap_or_default();
                let n = units.len().min(cap - 1);
                // SAFETY: `out` holds `cap` units.
                unsafe {
                    std::ptr::copy_nonoverlapping(units.as_ptr(), out, n);
                    *out.add(n) = 0;
                }
            }
            len
        })
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn w(s: &str) -> Vec<u16> {
        s.encode_utf16().collect()
    }

    #[test]
    fn program_name_is_skipped_like_the_ucrt() {
        assert_eq!(skip_program_name(&w("DIMESettings.exe --help")), w("--help").as_slice());
        assert_eq!(skip_program_name(&w("\"C:\\Program Files\\DIME\\DIMESettings.exe\"  --mode dayi")), w("--mode dayi").as_slice());
        assert_eq!(skip_program_name(&w("a\"b c\"d \t x")), w("x").as_slice());
        assert_eq!(skip_program_name(&w("prog")), w("").as_slice());
        assert_eq!(skip_program_name(&w("\"unterminated rest")), w("").as_slice());
        assert_eq!(skip_program_name(&w("")), w("").as_slice());
    }

    #[test]
    fn input_record_is_twenty_bytes() {
        assert_eq!(std::mem::size_of::<InputRecord>(), 20);
        assert_eq!(std::mem::size_of::<ConsoleScreenBufferInfo>(), 22);
    }
}
