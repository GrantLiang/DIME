//! The settings app's composition engine in Rust (phase 5b-15c, Windows only).
//!
//! The settings app makes a fresh `CCompositionProcessorEngine` per IME
//! (`SettingsModel::CreateEngine`: `SetupDictionaryFile`,
//! `SetupConfiguration`, `SetupKeystroke`) only to ask
//! `ValidateCompositionKeyCharFull` which characters are radical keys when
//! it checks the custom table. This does the same with `dime-core`: the
//! dictionary setup (`engine_setup::setup_dictionary`, which also copies the
//! shipped tables from Program Files to AppData like the C++) over a host
//! that opens files like `CFile` and parses them with `TableDictionaryEngine`,
//! then the configuration overrides and the keystroke table.
//!
//! Side effects on the settings values match the C++: every parsed table
//! applies its `%autoCompose` and `[Config]` entries, and
//! `SetupConfiguration` sets the per-IME overrides.

use std::collections::BTreeMap;

use dime_config::{ini, ImeMode};
use dime_core::engine_setup::{
    build_keystrokes, setup_dictionary, setup_id, Keystroke, Roots, SetupHost, FN_INPUT, KIND_MAIN, MAX_RADICAL,
};
use dime_core::{DictionaryType, TableDictionaryEngine, TextOps, WinTextOps};
use dime_settings::appenv;

use crate::DimeSettings;

windows_link::link!("kernel32.dll" "system" fn GetFileAttributesW(name: *const u16) -> u32);
windows_link::link!("kernel32.dll" "system" fn CreateDirectoryW(name: *const u16, sa: *const std::ffi::c_void) -> i32);
windows_link::link!("kernel32.dll" "system" fn CopyFileW(src: *const u16, dst: *const u16, fail_if_exists: i32) -> i32);

const INVALID_FILE_ATTRIBUTES: u32 = 0xFFFF_FFFF;
/// `MAKELCID(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL), SORT_DEFAULT)`.
const LOCALE_CHT: u32 = 1028;
/// `MAX_CAND_SELKEY`: `_pEndkey` holds at most 15 units.
const MAX_CAND_SELKEY: usize = 16;
/// `ARRAY_SCOPE::ARRAY40_BIG5`.
const ARRAY40_BIG5: i32 = 4;

fn z(s: &[u16]) -> Vec<u16> {
    s.iter().copied().take_while(|&c| c != 0).chain(std::iter::once(0)).collect()
}

/// `ResolveSetupPaths`' Program Files: `%ProgramW6432%`, else `%ProgramFiles%`.
fn program_files() -> Vec<u16> {
    use std::os::windows::ffi::OsStrExt;
    std::env::var_os("ProgramW6432")
        .filter(|v| !v.is_empty())
        .or_else(|| std::env::var_os("ProgramFiles"))
        .map(|v| v.encode_wide().collect())
        .unwrap_or_default()
}

/// One table: its file (`CFile`) and, once created, its parsed engine.
#[derive(Default)]
struct Slot {
    path: Option<Vec<u16>>,
    engine: Option<Parsed>,
}

#[derive(Clone, Default)]
struct Parsed {
    dict_type: i32,
    locale: u32,
    radicals: BTreeMap<u16, Vec<u16>>,
    endkey: Vec<u16>,
}

/// `CEngineSetupHost` for a fresh engine in the settings app.
struct Host<'a> {
    slots: BTreeMap<i32, Slot>,
    settings: &'a mut DimeSettings,
}

impl Host<'_> {
    fn slot(&mut self, id: i32) -> &mut Slot {
        self.slots.entry(id).or_default()
    }
}

impl SetupHost for Host<'_> {
    fn path_exists(&mut self, path: &[u16]) -> bool {
        let p = z(path);
        // SAFETY: NUL-terminated path.
        unsafe { GetFileAttributesW(p.as_ptr()) != INVALID_FILE_ATTRIBUTES }
    }
    fn file_mtime(&mut self, path: &[u16]) -> Option<i64> {
        appenv::file_mtime(path)
    }
    fn copy_file(&mut self, src: &[u16], dst: &[u16], fail_if_exists: bool) {
        let (s, d) = (z(src), z(dst));
        // SAFETY: NUL-terminated paths.
        unsafe { CopyFileW(s.as_ptr(), d.as_ptr(), i32::from(fail_if_exists)) };
    }
    fn create_dir(&mut self, path: &[u16]) {
        let p = z(path);
        // SAFETY: NUL-terminated path.
        unsafe { CreateDirectoryW(p.as_ptr(), std::ptr::null()) };
    }
    fn path_equal(&mut self, locale: u32, a: &[u16], b: &[u16]) -> bool {
        (a.is_empty() && b.is_empty()) || WinTextOps { lcid: locale }.eq(a, b)
    }
    fn has_file(&mut self, id: i32) -> bool {
        self.slots.get(&id).is_some_and(|s| s.path.is_some())
    }
    fn has_engine(&mut self, id: i32) -> bool {
        self.slots.get(&id).is_some_and(|s| s.engine.is_some())
    }
    fn dict_type(&mut self, id: i32) -> i32 {
        self.slots.get(&id).and_then(|s| s.engine.as_ref()).map_or(-1, |e| e.dict_type)
    }
    fn file_updated(&mut self, _id: i32) -> bool {
        // Every file of a fresh engine was just opened.
        false
    }
    fn file_name(&mut self, id: i32) -> Vec<u16> {
        self.slots.get(&id).and_then(|s| s.path.clone()).unwrap_or_default()
    }
    fn drop_file(&mut self, id: i32) {
        if let Some(s) = self.slots.get_mut(&id) {
            s.path = None;
        }
    }
    fn drop_engine(&mut self, id: i32) {
        if let Some(s) = self.slots.get_mut(&id) {
            s.engine = None;
        }
    }
    fn new_file(&mut self, id: i32) -> bool {
        // `new CFile()`: no path yet; create_file gives it one.
        self.slot(id).path = None;
        true
    }
    fn create_file(&mut self, id: i32, path: &[u16], _share_mode: u32) -> bool {
        // CFile::CreateFile: opens (OPEN_EXISTING) and checks the size.
        let ok = appenv::read_cfile(path).is_some();
        if ok {
            self.slot(id).path = Some(path.to_vec());
        }
        ok
    }
    fn create_engine(&mut self, id: i32, locale: u32, dict_type: i32) -> bool {
        if !self.has_file(id) {
            return false;
        }
        self.slot(id).engine = Some(Parsed { dict_type, locale, ..Parsed::default() });
        true
    }
    fn parse_config(&mut self, id: i32, _mode: i32) {
        let Some(slot) = self.slots.get(&id) else { return };
        let (Some(path), Some(engine)) = (slot.path.clone(), slot.engine.clone()) else { return };
        // The CFile buffer: after the first two bytes, if it is Unicode text.
        let Some(Some(buf)) = appenv::read_cfile(&path) else { return };
        let Some(dict_type) = DictionaryType::from_i32(engine.dict_type) else { return };
        let mut table = TableDictionaryEngine::new(WinTextOps { lcid: engine.locale }, buf, dict_type);
        table.parse_config();
        let cfg = table.config();
        let mut radicals = engine.radicals.clone();
        if dict_type != DictionaryType::Ini {
            radicals.clear();
        }
        radicals.extend(cfg.radical_map.iter().map(|(k, v)| (*k, v.clone())));
        let endkey = cfg.endkey[..cfg.endkey.len().min(MAX_CAND_SELKEY - 1)].to_vec();
        // CConfig::SetAutoCompose and the [Config] entries.
        if let Some(on) = cfg.auto_compose {
            self.settings.settings.v.auto_compose = i32::from(on);
        }
        let ops = WinTextOps { lcid: engine.locale };
        for (k, v) in &cfg.config_entries {
            ini::apply_entry(&ops, &mut self.settings.settings, k, v);
        }
        self.settings.refresh();
        if let Some(e) = self.slots.get_mut(&id).and_then(|s| s.engine.as_mut()) {
            e.radicals = radicals;
            e.endkey = endkey;
        }
    }
    fn alias_phrase_to_main(&mut self, slot: i32) {
        let main = self.slots.get(&setup_id(KIND_MAIN, slot)).and_then(|s| s.engine.clone());
        let phrase = setup_id(dime_core::engine_setup::KIND_PHRASE, 0);
        self.slot(phrase).engine = main;
    }
    // The settings app's Global::imeMode is IME_MODE_NONE: no Big5 engine.
    fn setup_big5(&mut self, _mode: i32, _locale: u32) {}
    // Reached only when an open table was updated; never for a fresh engine.
    fn setup_keystroke(&mut self, _mode: i32) {}
    fn load_config(&mut self, _mode: i32) {}
    fn setup_configuration(&mut self, _mode: i32) {}
    fn setup_candidate_range(&mut self, _mode: i32) {}
    fn has_text_service(&mut self) -> bool {
        false
    }
    fn array_scope(&mut self) -> i32 {
        self.settings.settings.v.array_scope
    }
    fn active_mode(&mut self) -> i32 {
        ImeMode::None as i32
    }
    fn do_han_convert(&mut self) -> bool {
        self.settings.settings.v.do_han_convert != 0
    }
}

/// What `CCompositionProcessorEngine` keeps for `ValidateCompositionKeyCharFull`.
pub struct SettingsEngine {
    keystrokes: Vec<Keystroke>,
}

impl SettingsEngine {
    /// `SettingsModel::CreateEngine(mode)`. `app_data` is
    /// `Global::GetAppDataPath(create)`.
    pub fn create(settings: &mut DimeSettings, mode: i32, app_data: &[u16]) -> SettingsEngine {
        let pf = program_files();
        let mut host = Host { slots: BTreeMap::new(), settings };
        let roots = Roots { app: app_data, pf: &pf, locale: LOCALE_CHT, mode, settings_build: true };
        setup_dictionary(&mut host, &roots);
        let main = host.slots.get(&setup_id(KIND_MAIN, mode)).and_then(|s| s.engine.clone());

        // SetupConfiguration.
        let v = &mut host.settings.settings.v;
        match ImeMode::from_i32(mode) {
            ImeMode::Dayi => v.space_as_first_cand_selkey = 1,
            ImeMode::Array => {
                v.max_codes = 5;
                v.space_as_first_cand_selkey = 0;
                if v.array_scope != ARRAY40_BIG5 {
                    v.auto_compose = 1;
                }
            }
            ImeMode::Phonetic => v.space_as_first_cand_selkey = 0,
            _ => {}
        }

        // SetupKeystroke: needs a main table with 1..=MAX_RADICAL radicals.
        let keystrokes = match main {
            Some(m) if !m.radicals.is_empty() && m.radicals.len() <= MAX_RADICAL => {
                let keys: Vec<u16> = m.radicals.keys().copied().collect();
                build_keystrokes(mode, &keys, &m.endkey).0
            }
            _ => Vec::new(),
        };
        SettingsEngine { keystrokes }
    }

    /// `ValidateCompositionKeyCharFull`: `towupper` in the "C" locale, then
    /// the key must be an input key (not one that also converts).
    pub fn char_ok(&self, ch: u16) -> bool {
        let c = if (u16::from(b'a')..=u16::from(b'z')).contains(&ch) { ch - 32 } else { ch };
        if c < 32 || usize::from(c) >= 32 + MAX_RADICAL {
            return false;
        }
        self.keystrokes.get(usize::from(c - 32)).is_some_and(|k| k.function == FN_INPUT)
    }
}
