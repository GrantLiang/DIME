//! The settings app's CLI host in Rust (phase 5b-15e, Windows only).
//!
//! `dime-config::cli` has decided what every command does since phase 5a;
//! its side effects went to the C++ host in `src/DIMESettings/CLI.cpp`
//! (`DimeCliHostVtbl`). `NativeCli` does them in Rust with the pieces the
//! settings app already uses: `ConfigStore` (INI load / write / reset and
//! the custom table paths), the settings object for `--get` / `--set`,
//! `SettingsEngine` for the custom table check, `appenv::parse_cin_file`
//! for table conversion, and `children::load_text_file`. Only the two
//! output streams still go to the C++ host (its CRT `FILE*`s), until
//! `wWinMain` moves (5b-15f).

use std::ptr::null_mut;

use dime_config::cli::{self, CliHost, WriteUtf16};
use dime_config::settings_model;
use dime_config::{keys, ImeMode};
use dime_settings::{appenv, children};

use crate::cli::{collect, DimeCliHost, FfiHost};
use crate::settings_app::{global, ConfigStore};
use crate::settings_engine::SettingsEngine;
use crate::{guard, key_at};

windows_link::link!("kernel32.dll" "system" fn GetFileAttributesW(name: *const u16) -> u32);
windows_link::link!("kernel32.dll" "system" fn CopyFileW(src: *const u16, dst: *const u16, fail_if_exists: i32) -> i32);
windows_link::link!("kernel32.dll" "system" fn GetLastError() -> u32);
windows_link::link!("kernel32.dll" "system" fn CreateFileW(name: *const u16, access: u32, share: u32, sa: *const std::ffi::c_void, disposition: u32, flags: u32, template: *mut std::ffi::c_void) -> *mut std::ffi::c_void);
windows_link::link!("kernel32.dll" "system" fn WriteFile(h: *mut std::ffi::c_void, buf: *const u8, len: u32, written: *mut u32, ov: *mut std::ffi::c_void) -> i32);
windows_link::link!("kernel32.dll" "system" fn CloseHandle(h: *mut std::ffi::c_void) -> i32);

const INVALID_FILE_ATTRIBUTES: u32 = 0xFFFF_FFFF;
const GENERIC_WRITE: u32 = 0x4000_0000;
const FILE_SHARE_READ: u32 = 1;
const CREATE_ALWAYS: u32 = 2;
const FILE_ATTRIBUTE_NORMAL: u32 = 0x80;

fn z(s: &[u16]) -> Vec<u16> {
    s.iter().copied().take_while(|&c| c != 0).chain(std::iter::once(0)).collect()
}

fn w(s: &str) -> Vec<u16> {
    s.encode_utf16().collect()
}

pub struct NativeCli {
    /// The C++ host: only its `write_out` / `write_err` are used.
    streams: FfiHost,
    store: ConfigStore,
    engine: Option<SettingsEngine>,
}

impl NativeCli {
    /// `GetDimePath`: `%APPDATA%\DIME` (not created).
    fn dime_path(&self) -> Vec<u16> {
        let mut p = self.store.app_data(false);
        p.extend(w("\\DIME"));
        p
    }

    fn in_dime(&self, name: &str) -> Vec<u16> {
        let mut p = self.dime_path();
        p.extend(w(&format!("\\{name}")));
        p
    }
}

impl CliHost for NativeCli {
    fn write_out(&mut self, text: &[u16]) {
        self.streams.write_out(text);
    }
    fn write_err(&mut self, text: &[u16]) {
        self.streams.write_err(text);
    }
    fn load_config(&mut self, mode: i32) {
        self.store.load_config(mode);
    }
    fn write_config(&mut self, mode: i32) {
        self.store.write_config(mode);
    }
    fn reset_defaults(&mut self, mode: i32) {
        self.store.reset_all_defaults(mode);
    }
    fn get_value(&mut self, index: i32) -> Option<Vec<u16>> {
        key_at(index).map(|k| keys::get_value(&global().settings, k))
    }
    fn set_value(&mut self, index: i32, value: &[u16]) -> (i32, Vec<u16>) {
        let Some(k) = key_at(index) else { return (2, Vec::new()) };
        let g = global();
        let r = keys::set_value(&mut g.settings, k, value);
        g.refresh();
        match r {
            Ok(()) => (0, Vec::new()),
            Err(e) => (1, e.to_string().encode_utf16().collect()),
        }
    }
    fn file_exists(&mut self, path: &[u16]) -> bool {
        let p = z(path);
        // SAFETY: NUL-terminated path.
        unsafe { GetFileAttributesW(p.as_ptr()) != INVALID_FILE_ATTRIBUTES }
    }
    fn load_text(&mut self, path: &[u16]) -> Option<Vec<u16>> {
        children::load_text_file(&z(path))
    }
    /// `SettingsModel::CreateEngine`, then `CConfig::LoadConfig`, then the
    /// maximum codes.
    fn begin_validate(&mut self, mode: i32) -> u32 {
        self.engine = None;
        let app = self.store.app_data(true);
        self.engine = Some(SettingsEngine::create(global(), mode, &app));
        self.store.load_config(mode);
        global().settings.v.max_codes
    }
    fn validate_line(&mut self, line: &[u16], mode: i32, max_codes: u32) -> i32 {
        let errors = match &self.engine {
            Some(e) => {
                let mut ok = |c: u16| e.char_ok(c);
                settings_model::validate_line(line, ImeMode::from_i32(mode), max_codes, Some(&mut ok))
            }
            None => settings_model::validate_line(line, ImeMode::from_i32(mode), max_codes, None),
        };
        errors.first().map_or(0, |e| e.error as i32)
    }
    fn end_validate(&mut self) {
        self.engine = None;
    }
    fn custom_txt_path(&mut self, mode: i32) -> Vec<u16> {
        self.store.custom_table_path(mode, false)
    }
    fn custom_cin_path(&mut self, mode: i32) -> Vec<u16> {
        self.store.custom_table_path(mode, true)
    }
    fn write_utf16_bom(&mut self, path: &[u16], text: &[u16]) -> WriteUtf16 {
        let p = z(path);
        // SAFETY: a handle we open and close; the buffers outlive the calls.
        unsafe {
            let h = CreateFileW(p.as_ptr(), GENERIC_WRITE, FILE_SHARE_READ, std::ptr::null(), CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, null_mut());
            if h as isize == -1 {
                return WriteUtf16::OpenFailed(GetLastError());
            }
            let bom = 0xFEFFu16.to_le_bytes();
            let body: Vec<u8> = text.iter().flat_map(|c| c.to_le_bytes()).collect();
            let mut n = 0u32;
            let ok = WriteFile(h, bom.as_ptr(), 2, &mut n, null_mut()) != 0
                && WriteFile(h, body.as_ptr(), body.len() as u32, &mut n, null_mut()) != 0;
            CloseHandle(h);
            if ok {
                WriteUtf16::Ok
            } else {
                WriteUtf16::WriteFailed
            }
        }
    }
    fn parse_cin(&mut self, src: &[u16], dst: &[u16], custom: bool) -> bool {
        appenv::parse_cin_file(src, dst, custom, true)
    }
    /// `GetCustomTablePath`: `%APPDATA%\DIME\<MODE>-CUSTOM.txt`.
    fn export_src_path(&mut self, mode: i32) -> Vec<u16> {
        let prefix = match mode {
            0 => "DAYI",
            1 => "ARRAY",
            2 => "PHONETIC",
            _ => "GENERIC",
        };
        self.in_dime(&format!("{prefix}-CUSTOM.txt"))
    }
    fn copy_file(&mut self, src: &[u16], dst: &[u16]) -> Option<u32> {
        let (s, d) = (z(src), z(dst));
        // SAFETY: NUL-terminated paths.
        unsafe {
            if CopyFileW(s.as_ptr(), d.as_ptr(), 0) == 0 {
                Some(GetLastError())
            } else {
                None
            }
        }
    }
    fn main_dest(&mut self, mode: i32) -> Vec<u16> {
        match mode {
            0 => self.in_dime("Dayi.cin"),
            1 => self.in_dime("Array.cin"),
            2 => self.in_dime("Phone.cin"),
            3 => self.in_dime("Generic.cin"),
            _ => Vec::new(),
        }
    }
    fn phrase_dest(&mut self) -> Vec<u16> {
        self.in_dime("Phrase.cin")
    }
    fn array_dest(&mut self, table: i32) -> Vec<u16> {
        let name = match table {
            0 => "Array-special.cin",
            1 => "Array-shortcode.cin",
            2 => "Array-Ext-B.cin",
            3 => "Array-Ext-CD.cin",
            4 => "Array-Ext-EF.cin",
            5 => "Array40.cin",
            6 => "Array-Phrase.cin",
            _ => return Vec::new(),
        };
        self.in_dime(name)
    }
}

/// `RunCLI` with the Rust host: `argv` split like `dime_cli_run`; `streams`
/// is a C++ host whose `write_out` / `write_err` print; `app_data` is a
/// NUL-terminated folder used instead of `%APPDATA%` (tests), or null.
#[no_mangle]
pub unsafe extern "C" fn dime_cli_run_native(
    argv: *const *const u16,
    lens: *const usize,
    argc: usize,
    streams: *mut DimeCliHost,
    app_data: *const u16,
) -> i32 {
    guard(1, || {
        if streams.is_null() {
            return 1;
        }
        let args = unsafe { collect(argv, lens, argc) };
        let override_dir = (!app_data.is_null()).then(|| {
            // SAFETY: NUL-terminated string from the caller.
            unsafe {
                let mut n = 0;
                while *app_data.add(n) != 0 {
                    n += 1;
                }
                std::slice::from_raw_parts(app_data, n).to_vec()
            }
        });
        let mut host = NativeCli { streams: FfiHost { host: streams }, store: ConfigStore::new(override_dir), engine: None };
        cli::run(&args, &mut host)
    })
}
