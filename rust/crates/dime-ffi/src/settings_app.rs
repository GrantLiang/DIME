//! The settings app's configuration in Rust (phase 5b-15a, Windows only).
//!
//! `NativeApp` answers the `DimeSettingsAppVtbl` (`SettingsApp::Host`) the
//! settings window uses, with what `SettingsApp::ConfigHost`
//! (`src/DIMESettings/SettingsApp.cpp`) does through `CConfig`, `Global` and
//! `SettingsModel`: loading and writing the per-mode INI file, the three
//! colour palettes and the six live colours, the reverse-conversion
//! providers and selection, the snapshot, the paths, the dpi and the dark
//! theme. The values are the process-wide settings object that `CConfig`
//! also reads (`DIME_USE_RUST_CONFIG`), so the parts still in C++ (message
//! box) see the same values; those calls go to the C++ host the app was
//! made with. Table parsing (`CConfig::parseCINFile`) is
//! `appenv::parse_cin_file` since phase 5b-15b, the engine
//! `settings_engine.rs` since 5b-15c.
//!
//! State `CConfig` keeps in its own statics lives here: the IME mode and its
//! INI path, the INI time stamp and the mode it was loaded for, `colors[6]`
//! and the reverse-conversion list.

use std::cell::RefCell;
use std::ptr::null_mut;

use dime_config::settings_model::{self, SettingsSnapshot};
use dime_config::values::{color_mode, colors};
use dime_config::{ini, ConfigValues, Env, ImeMode};
use dime_core::{DictionaryType, TableDictionaryEngine, WinTextOps};
use dime_settings::appenv::{self, ReverseProvider};
use dime_settings::window::App;

use crate::settings_engine::SettingsEngine;
use crate::settings_window::{AppVtbl, Ctx, VtblApp};
use crate::{global_settings, guard, DimeSettings};

type Handle = *mut std::ffi::c_void;

windows_link::link!("kernel32.dll" "system" fn GetFileAttributesW(name: *const u16) -> u32);
windows_link::link!("kernel32.dll" "system" fn CreateDirectoryW(name: *const u16, sa: *const std::ffi::c_void) -> i32);
windows_link::link!("user32.dll" "system" fn GetSysColor(index: i32) -> u32);

const INVALID_FILE_ATTRIBUTES: u32 = 0xFFFF_FFFF;
const COLOR_3DHIGHLIGHT: i32 = 20;
/// `MAKELCID(1028, SORT_DEFAULT)`, the locale `LoadConfigFromFile` parses with.
const LCID_CHT: u32 = 1028;
/// `CConfig::EnumerateReverseConversionProviders(1028)`.
const LANGID_CHT: u16 = 1028;

/// The DIME profile GUIDs (`Global::DIME*GuidProfile`) in memory order.
fn profile_guid(mode: i32) -> [u8; 16] {
    let (d1, d2, d3, d4): (u32, u16, u16, [u8; 8]) = match mode {
        0 => (0x3685_1834, 0x92ad, 0x4397, [0x9f, 0x50, 0x80, 0x03, 0x84, 0xd5, 0xc2, 0x4c]),
        1 => (0x5dfc_1743, 0x638c, 0x4f61, [0x9e, 0x76, 0xfc, 0xfa, 0x97, 0x07, 0xf4, 0x50]),
        2 => (0x2689_2981, 0x14e3, 0x447b, [0xaf, 0x2c, 0x90, 0x67, 0xcd, 0x4a, 0x4a, 0x8a]),
        3 => (0x061b_eea7, 0xfff9, 0x420c, [0xb3, 0xf6, 0xa9, 0x04, 0x7a, 0xfd, 0x08, 0x77]),
        _ => return [0; 16],
    };
    let mut out = [0u8; 16];
    out[..4].copy_from_slice(&d1.to_le_bytes());
    out[4..6].copy_from_slice(&d2.to_le_bytes());
    out[6..8].copy_from_slice(&d3.to_le_bytes());
    out[8..].copy_from_slice(&d4);
    out
}

fn w(s: &str) -> Vec<u16> {
    s.encode_utf16().collect()
}

fn z(s: &[u16]) -> Vec<u16> {
    s.iter().copied().take_while(|&c| c != 0).chain(std::iter::once(0)).collect()
}

fn exists(path: &[u16]) -> bool {
    let p = z(path);
    // SAFETY: NUL-terminated path.
    unsafe { GetFileAttributesW(p.as_ptr()) != INVALID_FILE_ATTRIBUTES }
}

fn create_dir(path: &[u16]) -> bool {
    let p = z(path);
    // SAFETY: NUL-terminated path.
    unsafe { CreateDirectoryW(p.as_ptr(), std::ptr::null()) != 0 }
}

/// The process-wide settings, initialised for this machine when nobody has
/// done so yet (`CConfig`'s static initialisers normally do).
pub(crate) fn global() -> &'static mut DimeSettings {
    // SAFETY: the global object lives for the process; like the C++ statics
    // it replaces, the host serialises access (one UI thread).
    let g = unsafe { &mut *global_settings() };
    if g.settings.v == ConfigValues::ZERO {
        let env = Env {
            // SAFETY: plain user32 call.
            sys_3d_highlight: unsafe { GetSysColor(COLOR_3DHIGHLIGHT) },
            windows_1809_or_later: i32::from(appenv::windows_build() >= 17763),
        };
        g.settings = dime_config::Settings::initial(&env);
        g.env = env;
        g.refresh();
    }
    g
}

/// `CConfig`'s own statics for the settings app.
pub struct ConfigStore {
    /// `Global::testAppDataOverride` for this store (no NUL); `None`: `%APPDATA%`.
    app_data_override: Option<Vec<u16>>,
    ime_mode: i32,
    profile_dir: Vec<u16>,
    ini_path: Vec<u16>,
    init_mtime: i64,
    config_mode: i32,
    colors: [u32; 6],
    reverse: Vec<ReverseProvider>,
}

impl ConfigStore {
    pub fn new(app_data_override: Option<Vec<u16>>) -> Self {
        let g = global();
        ConfigStore {
            app_data_override,
            ime_mode: ImeMode::None as i32,
            profile_dir: Vec::new(),
            ini_path: Vec::new(),
            init_mtime: 0,
            config_mode: ImeMode::None as i32,
            colors: [
                colors::ITEM,
                colors::SELECTED_ITEM,
                g.env.sys_3d_highlight,
                colors::PHRASE,
                colors::NUMBER,
                colors::SELECTED_BK,
            ],
            reverse: Vec::new(),
        }
    }

    /// `Global::GetAppDataPath(path, create)`.
    pub(crate) fn app_data(&self, create: bool) -> Vec<u16> {
        match &self.app_data_override {
            Some(p) => {
                if create {
                    create_dir(p);
                }
                p.clone()
            }
            None => appenv::app_data_path(create),
        }
    }

    /// `CConfig::SetIMEMode`: the profile folder and INI path of a new mode,
    /// and the mode's own profile dropped from the reverse-conversion list.
    fn set_ime_mode(&mut self, mode: i32) {
        if self.ime_mode == mode {
            return;
        }
        self.ime_mode = mode;
        let mut profile = self.app_data(true);
        profile.extend(w("\\DIME"));
        self.profile_dir = profile.clone();
        if !exists(&profile) && !create_dir(&profile) {
            return;
        }
        let file = match mode {
            0 => "\\DayiConfig.ini",
            1 => "\\ArrayConfig.ini",
            2 => "\\PhoneConfig.ini",
            3 => "\\GenericConfig.ini",
            _ => "\\config.ini",
        };
        let mut ini = profile;
        ini.extend(w(file));
        self.ini_path = ini;
        // Like the C++ loop, the entry after a removed one is not checked.
        let own = profile_guid(mode);
        let mut i = 0;
        while i < self.reverse.len() {
            if self.reverse[i].guid_profile == own {
                self.reverse.remove(i);
            }
            i += 1;
        }
    }

    /// `CConfig::LoadConfigFromFile`.
    fn load_from_file(&mut self, mode: i32) -> bool {
        let g = global();
        let m = ImeMode::from_i32(mode);
        g.settings.v.set_mode_defaults(m);
        let Some(text) = appenv::read_cfile(&self.ini_path) else {
            g.refresh();
            return false;
        };
        ini::begin_load(&mut g.settings, m);
        if let Some(buf) = text {
            let ops = WinTextOps { lcid: LCID_CHT };
            let mut engine = TableDictionaryEngine::new(WinTextOps { lcid: LCID_CHT }, buf, DictionaryType::Ini);
            engine.parse_config();
            for (k, v) in &engine.config().config_entries {
                ini::apply_entry(&ops, &mut g.settings, k, v);
            }
        }
        ini::finish_load(&mut g.settings, &g.env);
        g.refresh();
        true
    }

    /// `CConfig::LoadConfig`. The font `SetDefaultTextFont` creates is the
    /// IME's (`Global::defaultlFontHandle`); the settings app never uses it.
    pub fn load_config(&mut self, mode: i32) -> bool {
        self.set_ime_mode(mode);
        let mut ret = false;
        if exists(&self.ini_path) {
            let mtime = appenv::file_mtime(&self.ini_path);
            let updated = mtime.is_some_and(|t| t > self.init_mtime);
            if mtime.is_none() || updated || self.config_mode != mode {
                ret = true;
                if self.load_from_file(mode) {
                    self.init_mtime = mtime.unwrap_or(self.init_mtime);
                    self.config_mode = mode;
                }
                let g = global();
                // The settings app runs with Global::isWindows8 FALSE.
                if g.settings.v.app_permission_set == 0
                    && mode != ImeMode::None as i32
                    && appenv::grant_read_access(&self.profile_dir, false)
                {
                    g.settings.v.app_permission_set = 1;
                    self.write_config(mode);
                }
            }
        } else {
            let g = global();
            ini::load_missing(&mut g.settings, ImeMode::from_i32(mode), &g.env);
            g.refresh();
            if mode != ImeMode::None as i32 {
                self.write_config(mode);
            }
        }
        ret
    }

    /// `CConfig::WriteConfig(mode)` (no update check): the serialized INI to
    /// the current mode's file.
    pub fn write_config(&mut self, mode: i32) {
        if self.ini_path.is_empty() {
            return;
        }
        let g = global();
        let text = ini::serialize(&g.settings, ImeMode::from_i32(mode));
        let bytes: Vec<u8> = text.iter().flat_map(|c| c.to_le_bytes()).collect();
        let path = String::from_utf16_lossy(&self.ini_path);
        let _ = std::fs::write(path, bytes);
    }

    pub fn reset_all_defaults(&mut self, mode: i32) {
        let g = global();
        g.settings.reset_all_defaults(ImeMode::from_i32(mode), &g.env);
        g.refresh();
    }

    pub fn color_mode(&self) -> i32 {
        global().settings.v.color_mode
    }

    pub fn set_color_mode(&mut self, m: i32) {
        global().settings.v.color_mode = m;
    }

    /// `CConfig::LoadColorsForMode`.
    pub fn load_colors_for_mode(&mut self, m: i32) {
        let v = &global().settings.v;
        let p = match m {
            color_mode::DARK => &v.dark,
            color_mode::CUSTOM => &v.custom,
            color_mode::SYSTEM if appenv::system_dark() => &v.dark,
            _ => &v.light,
        };
        self.colors = [p.item, p.selected, p.item_bg, p.phrase, p.number, p.selected_bg];
    }

    /// `CConfig::SaveColorsForMode`: nothing in SYSTEM mode.
    pub fn save_colors_for_mode(&mut self, m: i32) {
        let v = &mut global().settings.v;
        let p = match m {
            color_mode::DARK => &mut v.dark,
            color_mode::CUSTOM => &mut v.custom,
            color_mode::SYSTEM => return,
            _ => &mut v.light,
        };
        let c = self.colors;
        p.item = c[0];
        p.selected = c[1];
        p.item_bg = c[2];
        p.phrase = c[3];
        p.number = c[4];
        p.selected_bg = c[5];
    }

    pub fn colors(&self) -> [u32; 6] {
        self.colors
    }

    pub fn set_color(&mut self, index: i32, color: u32) {
        if let Some(c) = usize::try_from(index).ok().and_then(|i| self.colors.get_mut(i)) {
            *c = color;
        }
    }

    /// `CConfig::SetReverseConversionSelection`: 0 is none, otherwise the
    /// (sel - 1)th provider. An index past the list changes only the reload
    /// flag (C++ would read past the end).
    pub fn set_reverse_selection(&mut self, sel: i32) {
        let g = global();
        if sel == 0 {
            g.settings.v.reverse_conversion_clsid = [0; 16];
            g.settings.v.reverse_conversion_guid_profile = [0; 16];
            g.settings.reverse_conversion_description = Some(w("(無)"));
        } else if let Some(p) = usize::try_from(sel - 1).ok().and_then(|i| self.reverse.get(i)) {
            g.settings.v.reverse_conversion_clsid = p.clsid;
            g.settings.v.reverse_conversion_guid_profile = p.guid_profile;
            g.settings.reverse_conversion_description = Some(p.description.clone());
        }
        g.settings.v.reload_reverse_conversion = 1;
        g.refresh();
    }

    pub fn enumerate_reverse_providers(&mut self) {
        self.reverse = appenv::reverse_providers(LANGID_CHT);
    }

    /// Replace the provider list (`CConfig::SetReverseConvervsionInfoList`).
    pub fn set_reverse_providers(&mut self, list: Vec<ReverseProvider>) {
        self.reverse = list;
    }

    pub fn reverse_count(&self) -> i32 {
        self.reverse.len() as i32
    }

    pub fn reverse_description(&self, i: i32) -> Vec<u16> {
        usize::try_from(i)
            .ok()
            .and_then(|i| self.reverse.get(i))
            .map_or_else(Vec::new, |p| p.description.clone())
    }

    pub fn reverse_is_current(&self, i: i32) -> bool {
        let g = global();
        usize::try_from(i)
            .ok()
            .and_then(|i| self.reverse.get(i))
            .is_some_and(|p| p.guid_profile == g.settings.v.reverse_conversion_guid_profile)
    }

    pub fn reverse_is_null(&self) -> bool {
        global().settings.v.reverse_conversion_guid_profile == [0; 16]
    }

    pub fn load_snapshot(&self) -> SettingsSnapshot {
        settings_model::load_snapshot(&global().settings.v, &self.colors)
    }

    pub fn apply_snapshot(&mut self, s: &SettingsSnapshot) {
        settings_model::apply_snapshot(&mut global().settings.v, s);
    }

    /// `SettingsModel::GetCustomTableTxtPath` / `GetCustomTableCINPath`.
    pub fn custom_table_path(&self, mode: i32, cin: bool) -> Vec<u16> {
        let name = match mode {
            0 => "DAYI",
            1 => "ARRAY",
            2 => "PHONETIC",
            _ => "GENERIC",
        };
        let mut p = self.app_data(true);
        p.extend(w(&format!("\\DIME\\{name}-Custom.{}", if cin { "cin" } else { "txt" })));
        p
    }

    pub fn app_data_path(&self) -> Vec<u16> {
        self.app_data(true)
    }
}

/// The settings app: [`ConfigStore`] plus the C++ host for what is still in
/// C++ (message box, table parsing, the composition engine).
pub struct NativeApp {
    fallback: VtblApp,
    store: RefCell<ConfigStore>,
    /// `ConfigHost::_engine` (phase 5b-15c).
    engine: RefCell<Option<SettingsEngine>>,
}

impl NativeApp {
    fn s(&self) -> std::cell::RefMut<'_, ConfigStore> {
        self.store.borrow_mut()
    }
}

impl App for NativeApp {
    fn dpi_for_window(&self, hwnd: Handle) -> u32 {
        appenv::dpi_for_window(hwnd)
    }
    fn system_dark(&self) -> bool {
        appenv::system_dark()
    }
    fn show_message(&self, parent: Handle, text: &[u16], caption: &[u16], mb_type: u32) -> i32 {
        self.fallback.show_message(parent, text, caption, mb_type)
    }
    fn load_config(&self, mode: i32) {
        self.s().load_config(mode);
    }
    fn write_config(&self, mode: i32) {
        self.s().write_config(mode);
    }
    fn reset_all_defaults(&self, mode: i32) {
        self.s().reset_all_defaults(mode);
    }
    fn color_mode(&self) -> i32 {
        self.s().color_mode()
    }
    fn set_color_mode(&self, m: i32) {
        self.s().set_color_mode(m);
    }
    fn load_colors_for_mode(&self, m: i32) {
        self.s().load_colors_for_mode(m);
    }
    fn save_colors_for_mode(&self, m: i32) {
        self.s().save_colors_for_mode(m);
    }
    fn get_colors(&self) -> [u32; 6] {
        self.s().colors()
    }
    fn set_color(&self, index: i32, color: u32) {
        self.s().set_color(index, color);
    }
    fn set_reverse_selection(&self, sel: i32) {
        self.s().set_reverse_selection(sel);
    }
    fn enumerate_reverse_providers(&self) {
        self.s().enumerate_reverse_providers();
    }
    fn reverse_count(&self) -> i32 {
        self.s().reverse_count()
    }
    fn reverse_description(&self, index: i32) -> Vec<u16> {
        self.s().reverse_description(index)
    }
    fn reverse_is_current(&self, index: i32) -> bool {
        self.s().reverse_is_current(index)
    }
    fn reverse_is_null(&self) -> bool {
        self.s().reverse_is_null()
    }
    fn load_snapshot(&self) -> SettingsSnapshot {
        self.s().load_snapshot()
    }
    fn apply_snapshot(&self, s: &SettingsSnapshot) {
        self.s().apply_snapshot(s);
    }
    fn parse_cin(&self, src: &[u16], dst: &[u16], custom_table: bool) -> bool {
        appenv::parse_cin_file(src, dst, custom_table, false)
    }
    fn recreate_engine(&self, mode: i32) {
        // DestroyEngine, then CreateEngine.
        *self.engine.borrow_mut() = None;
        let app_data = self.s().app_data_path();
        let engine = SettingsEngine::create(global(), mode, &app_data);
        *self.engine.borrow_mut() = Some(engine);
    }
    fn destroy_engine(&self) {
        *self.engine.borrow_mut() = None;
    }
    fn has_engine(&self) -> bool {
        self.engine.borrow().is_some()
    }
    fn engine_char_ok(&self, ch: u16) -> bool {
        self.engine.borrow().as_ref().is_some_and(|e| e.char_ok(ch))
    }
    fn custom_table_txt_path(&self, mode: i32) -> Vec<u16> {
        self.s().custom_table_path(mode, false)
    }
    fn custom_table_cin_path(&self, mode: i32) -> Vec<u16> {
        self.s().custom_table_path(mode, true)
    }
    fn app_data_path(&self) -> Vec<u16> {
        self.s().app_data_path()
    }
    fn windows_1809_or_later(&self) -> bool {
        global().env.windows_1809_or_later != 0
    }
}

// ---- C ABI ----------------------------------------------------------------

fn app<'a>(c: Ctx) -> &'a NativeApp {
    // SAFETY: `c` is a NativeApp from dime_settings_native_app_new, alive
    // until dime_settings_native_app_free.
    unsafe { &*(c as *const NativeApp) }
}

/// Copies `s` plus a NUL into `buf` (`cap` units, truncated); returns `s.len()`.
fn out(s: &[u16], buf: *mut u16, cap: usize) -> usize {
    if !buf.is_null() && cap > 0 {
        let n = s.len().min(cap - 1);
        // SAFETY: the caller's buffer holds `cap` units.
        unsafe {
            std::ptr::copy_nonoverlapping(s.as_ptr(), buf, n);
            *buf.add(n) = 0;
        }
    }
    s.len()
}

fn wstr<'a>(p: *const u16) -> &'a [u16] {
    if p.is_null() {
        return &[];
    }
    // SAFETY: NUL-terminated string from the caller.
    unsafe {
        let mut n = 0;
        while *p.add(n) != 0 {
            n += 1;
        }
        std::slice::from_raw_parts(p, n)
    }
}

unsafe extern "C" fn n_dpi(c: Ctx, h: Handle) -> u32 {
    guard(96, || app(c).dpi_for_window(h))
}
unsafe extern "C" fn n_dark(c: Ctx) -> i32 {
    guard(0, || i32::from(app(c).system_dark()))
}
unsafe extern "C" fn n_msg(c: Ctx, parent: Handle, text: *const u16, caption: *const u16, ty: u32) -> i32 {
    guard(2, || app(c).show_message(parent, wstr(text), wstr(caption), ty))
}
unsafe extern "C" fn n_load(c: Ctx, m: i32) {
    guard((), || app(c).load_config(m))
}
unsafe extern "C" fn n_write(c: Ctx, m: i32) {
    guard((), || app(c).write_config(m))
}
unsafe extern "C" fn n_reset(c: Ctx, m: i32) {
    guard((), || app(c).reset_all_defaults(m))
}
unsafe extern "C" fn n_color_mode(c: Ctx) -> i32 {
    guard(0, || app(c).color_mode())
}
unsafe extern "C" fn n_set_color_mode(c: Ctx, m: i32) {
    guard((), || app(c).set_color_mode(m))
}
unsafe extern "C" fn n_load_colors(c: Ctx, m: i32) {
    guard((), || app(c).load_colors_for_mode(m))
}
unsafe extern "C" fn n_save_colors(c: Ctx, m: i32) {
    guard((), || app(c).save_colors_for_mode(m))
}
unsafe extern "C" fn n_get_colors(c: Ctx, o: *mut u32) {
    guard((), || {
        if !o.is_null() {
            let v = app(c).get_colors();
            // SAFETY: the caller passes six COLORREFs.
            unsafe { std::ptr::copy_nonoverlapping(v.as_ptr(), o, 6) };
        }
    })
}
unsafe extern "C" fn n_set_color(c: Ctx, i: i32, color: u32) {
    guard((), || app(c).set_color(i, color))
}
unsafe extern "C" fn n_set_reverse(c: Ctx, sel: i32) {
    guard((), || app(c).set_reverse_selection(sel))
}
unsafe extern "C" fn n_enum_reverse(c: Ctx) {
    guard((), || app(c).enumerate_reverse_providers())
}
unsafe extern "C" fn n_reverse_count(c: Ctx) -> i32 {
    guard(0, || app(c).reverse_count())
}
unsafe extern "C" fn n_reverse_desc(c: Ctx, i: i32, buf: *mut u16, cap: usize) -> usize {
    guard(0, || out(&app(c).reverse_description(i), buf, cap))
}
unsafe extern "C" fn n_reverse_current(c: Ctx, i: i32) -> i32 {
    guard(0, || i32::from(app(c).reverse_is_current(i)))
}
unsafe extern "C" fn n_reverse_null(c: Ctx) -> i32 {
    guard(1, || i32::from(app(c).reverse_is_null()))
}
unsafe extern "C" fn n_load_snapshot(c: Ctx, o: *mut SettingsSnapshot) {
    guard((), || {
        if !o.is_null() {
            // SAFETY: the caller's SettingsSnapshot.
            unsafe { *o = app(c).load_snapshot() };
        }
    })
}
unsafe extern "C" fn n_apply_snapshot(c: Ctx, s: *const SettingsSnapshot) {
    guard((), || {
        // SAFETY: the caller's SettingsSnapshot, or null.
        if let Some(s) = unsafe { s.as_ref() } {
            app(c).apply_snapshot(s);
        }
    })
}
unsafe extern "C" fn n_parse_cin(c: Ctx, src: *const u16, dst: *const u16, custom: i32) -> i32 {
    guard(0, || i32::from(app(c).parse_cin(wstr(src), wstr(dst), custom != 0)))
}
unsafe extern "C" fn n_recreate_engine(c: Ctx, m: i32) {
    guard((), || app(c).recreate_engine(m))
}
unsafe extern "C" fn n_destroy_engine(c: Ctx) {
    guard((), || app(c).destroy_engine())
}
unsafe extern "C" fn n_has_engine(c: Ctx) -> i32 {
    guard(0, || i32::from(app(c).has_engine()))
}
unsafe extern "C" fn n_char_ok(c: Ctx, ch: u16) -> i32 {
    guard(0, || i32::from(app(c).engine_char_ok(ch)))
}
unsafe extern "C" fn n_txt_path(c: Ctx, m: i32, buf: *mut u16, cap: usize) -> usize {
    guard(0, || out(&app(c).custom_table_txt_path(m), buf, cap))
}
unsafe extern "C" fn n_cin_path(c: Ctx, m: i32, buf: *mut u16, cap: usize) -> usize {
    guard(0, || out(&app(c).custom_table_cin_path(m), buf, cap))
}
unsafe extern "C" fn n_app_data(c: Ctx, buf: *mut u16, cap: usize) -> usize {
    guard(0, || out(&app(c).app_data_path(), buf, cap))
}
unsafe extern "C" fn n_1809(c: Ctx) -> i32 {
    guard(0, || i32::from(app(c).windows_1809_or_later()))
}

static NATIVE_VTBL: AppVtbl = AppVtbl {
    dpi_for_window: n_dpi,
    system_dark: n_dark,
    show_message: n_msg,
    load_config: n_load,
    write_config: n_write,
    reset_all_defaults: n_reset,
    color_mode: n_color_mode,
    set_color_mode: n_set_color_mode,
    load_colors_for_mode: n_load_colors,
    save_colors_for_mode: n_save_colors,
    get_colors: n_get_colors,
    set_color: n_set_color,
    set_reverse_selection: n_set_reverse,
    enumerate_reverse_providers: n_enum_reverse,
    reverse_count: n_reverse_count,
    reverse_description: n_reverse_desc,
    reverse_is_current: n_reverse_current,
    reverse_is_null: n_reverse_null,
    load_snapshot: n_load_snapshot,
    apply_snapshot: n_apply_snapshot,
    parse_cin: n_parse_cin,
    recreate_engine: n_recreate_engine,
    destroy_engine: n_destroy_engine,
    has_engine: n_has_engine,
    engine_char_ok: n_char_ok,
    custom_table_txt_path: n_txt_path,
    custom_table_cin_path: n_cin_path,
    app_data_path: n_app_data,
    windows_1809_or_later: n_1809,
};

/// The vtable whose context is a `dime_settings_native_app_new` app.
#[no_mangle]
pub extern "C" fn dime_settings_native_app_vtbl() -> *const AppVtbl {
    &NATIVE_VTBL
}

/// A Rust settings app over `fallback` (the C++ `SettingsApp::Host`, which
/// must outlive it). `app_data`: NUL-terminated folder used instead of
/// `%APPDATA%` (tests), or null. Null for a null fallback.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_native_app_new(
    fallback: *const AppVtbl,
    fallback_ctx: Ctx,
    app_data: *const u16,
) -> Ctx {
    guard(null_mut(), || {
        if fallback.is_null() {
            return null_mut();
        }
        let override_dir = (!app_data.is_null()).then(|| wstr(app_data).to_vec());
        let a = Box::new(NativeApp {
            // SAFETY: the caller keeps the C++ host alive longer than the app.
            fallback: VtblApp { v: unsafe { &*fallback }, ctx: fallback_ctx },
            store: RefCell::new(ConfigStore::new(override_dir)),
            engine: RefCell::new(None),
        });
        Box::into_raw(a).cast()
    })
}

#[no_mangle]
pub unsafe extern "C" fn dime_settings_native_app_free(a: Ctx) {
    if !a.is_null() {
        // SAFETY: from dime_settings_native_app_new.
        guard((), || drop(unsafe { Box::from_raw(a as *mut NativeApp) }));
    }
}

/// Test hook: replace the reverse-conversion list with `count` providers
/// (`clsids` / `profiles`: 16 bytes each; `descriptions`: NUL-terminated),
/// as `CConfig::SetReverseConvervsionInfoList` does.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_native_app_set_reverse(
    a: Ctx,
    count: usize,
    clsids: *const [u8; 16],
    profiles: *const [u8; 16],
    descriptions: *const *const u16,
) {
    guard((), || {
        if a.is_null() || (count > 0 && (clsids.is_null() || profiles.is_null() || descriptions.is_null())) {
            return;
        }
        let list = (0..count)
            .map(|i| {
                // SAFETY: the caller's arrays hold `count` entries.
                unsafe {
                    ReverseProvider {
                        clsid: *clsids.add(i),
                        guid_profile: *profiles.add(i),
                        description: wstr(*descriptions.add(i)).to_vec(),
                    }
                }
            })
            .collect();
        app(a).s().set_reverse_providers(list);
    })
}

/// `CConfig::parseCINFile(src, dst, custom_table, suppress_ui)` in Rust
/// (phase 5b-15b). Paths are NUL-terminated. Returns 1 on success.
#[no_mangle]
pub unsafe extern "C" fn dime_settings_parse_cin_file(
    src: *const u16,
    dst: *const u16,
    custom_table: i32,
    suppress_ui: i32,
) -> i32 {
    guard(0, || i32::from(appenv::parse_cin_file(wstr(src), wstr(dst), custom_table != 0, suppress_ui != 0)))
}