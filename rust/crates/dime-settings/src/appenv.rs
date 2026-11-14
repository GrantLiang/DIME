//! What the settings app asks of Windows (phase 5b-15a, Windows only).
//!
//! The pieces of `CConfig` and `Global` that `SettingsApp::ConfigHost`
//! (`src/DIMESettings/SettingsApp.cpp`) relies on and that do not touch the
//! settings values: the dpi, the dark theme, the AppData folder, the INI
//! file's modification time and contents (read like `CFile`), opening the
//! profile folder for reading, and the TSF reverse-conversion providers.
//! The settings store over them is `dime-ffi`'s `settings_app.rs`.

// HWND is an opaque handle passed to Win32, never dereferenced.
#![allow(clippy::not_unsafe_ptr_arg_deref)]

use std::ffi::c_void;
use std::ptr::{null, null_mut};

use windows::core::{GUID, PCWSTR, PWSTR};
use windows::Win32::Foundation::{LocalFree, HLOCAL};
use windows::Win32::Security::Authorization::{
    GetNamedSecurityInfoW, SetEntriesInAclW, SetNamedSecurityInfoW, EXPLICIT_ACCESS_W, GRANT_ACCESS, SE_FILE_OBJECT,
    TRUSTEE_IS_NAME, TRUSTEE_IS_WELL_KNOWN_GROUP, TRUSTEE_W,
};
use windows::Win32::Security::{ACL, DACL_SECURITY_INFORMATION, PSECURITY_DESCRIPTOR, SUB_CONTAINERS_AND_OBJECTS_INHERIT};
use windows::Win32::System::Com::{CoCreateInstance, CLSCTX_INPROC_SERVER};
use windows::Win32::UI::TextServices::{
    CLSID_TF_InputProcessorProfiles, ITfInputProcessorProfiles, ITfReverseConversionMgr, TF_LANGUAGEPROFILE,
};

pub type Handle = *mut c_void;

windows_link::link!("user32.dll" "system" fn GetDC(h: Handle) -> Handle);
windows_link::link!("user32.dll" "system" fn ReleaseDC(h: Handle, dc: Handle) -> i32);
windows_link::link!("gdi32.dll" "system" fn GetDeviceCaps(dc: Handle, index: i32) -> i32);
windows_link::link!("kernel32.dll" "system" fn GetModuleHandleW(name: *const u16) -> Handle);
windows_link::link!("kernel32.dll" "system" fn GetProcAddress(module: Handle, name: *const u8) -> *const c_void);
windows_link::link!("advapi32.dll" "system" fn RegGetValueW(key: isize, sub: *const u16, value: *const u16, flags: u32, ty: *mut u32, data: *mut c_void, cb: *mut u32) -> i32);
windows_link::link!("shell32.dll" "system" fn SHGetSpecialFolderPathW(owner: Handle, path: *mut u16, csidl: i32, create: i32) -> i32);
windows_link::link!("kernel32.dll" "system" fn GetFileAttributesExW(name: *const u16, level: i32, info: *mut FileAttributeData) -> i32);
windows_link::link!("kernel32.dll" "system" fn CreateFileW(name: *const u16, access: u32, share: u32, sa: *const c_void, disposition: u32, flags: u32, template: Handle) -> Handle);
windows_link::link!("kernel32.dll" "system" fn GetFileSize(h: Handle, high: *mut u32) -> u32);
windows_link::link!("kernel32.dll" "system" fn SetFilePointer(h: Handle, dist: i32, high: *mut i32, method: u32) -> u32);
windows_link::link!("kernel32.dll" "system" fn ReadFile(h: Handle, buf: *mut u8, len: u32, read: *mut u32, ov: *mut c_void) -> i32);
windows_link::link!("kernel32.dll" "system" fn CloseHandle(h: Handle) -> i32);
windows_link::link!("advapi32.dll" "system" fn IsTextUnicode(buf: *const c_void, len: i32, result: *mut i32) -> i32);

const LOGPIXELSY: i32 = 90;
const CSIDL_APPDATA: i32 = 0x001A;
const MAX_PATH: usize = 260;
const HKEY_CURRENT_USER: isize = 0x8000_0001_u32 as i32 as isize;
const RRF_RT_REG_DWORD: u32 = 0x0000_0010;
const GENERIC_READ: u32 = 0x8000_0000;
const FILE_SHARE_READ: u32 = 1;
const FILE_SHARE_WRITE: u32 = 2;
const OPEN_EXISTING: u32 = 3;
const FILE_BEGIN: u32 = 0;
const INVALID_HANDLE_VALUE: Handle = -1isize as Handle;
const INVALID_FILE_SIZE: u32 = 0xFFFF_FFFF;
const INVALID_SET_FILE_POINTER: u32 = 0xFFFF_FFFF;
/// `Global::MAX_CIN_FILE_SIZE`.
pub const MAX_CIN_FILE_SIZE: u32 = 100 * 1024 * 1024;

#[repr(C)]
#[derive(Default)]
struct FileAttributeData {
    attributes: u32,
    /// FILETIMEs are two DWORDs (4-byte aligned), low part first.
    creation: [u32; 2],
    last_access: [u32; 2],
    last_write: [u32; 2],
    size_high: u32,
    size_low: u32,
}

const _: () = assert!(std::mem::size_of::<FileAttributeData>() == 36);

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

fn nul(s: &[u16]) -> Vec<u16> {
    s.iter().copied().take_while(|&c| c != 0).chain(std::iter::once(0)).collect()
}

/// `CConfig::GetDpiForHwnd` as the settings app runs it: `_GetDpiForMonitor`
/// is never set there, so it is always the screen's `LOGPIXELSY`.
pub fn dpi_for_window(_hwnd: Handle) -> u32 {
    // SAFETY: plain GDI calls on the screen DC.
    unsafe {
        let dc = GetDC(null_mut());
        let dpi = GetDeviceCaps(dc, LOGPIXELSY) as u32;
        ReleaseDC(null_mut(), dc);
        dpi
    }
}

/// `RtlGetVersion`'s build number, 0 when it cannot be read.
pub fn windows_build() -> u32 {
    type RtlGetVersion = unsafe extern "system" fn(*mut OsVersionInfo) -> i32;
    let name = wide("ntdll.dll");
    // SAFETY: looking up an export of a loaded system DLL; the struct is sized.
    unsafe {
        let module = GetModuleHandleW(name.as_ptr());
        if module.is_null() {
            return 0;
        }
        let f = GetProcAddress(module, c"RtlGetVersion".as_ptr().cast());
        if f.is_null() {
            return 0;
        }
        let f: RtlGetVersion = std::mem::transmute(f);
        let mut v = OsVersionInfo {
            size: std::mem::size_of::<OsVersionInfo>() as u32,
            major: 0,
            minor: 0,
            build: 0,
            platform: 0,
            csd: [0; 128],
        };
        if f(&mut v) == 0 {
            v.build
        } else {
            0
        }
    }
}

/// `CConfig::IsSystemDarkTheme`: `ShouldAppsUseDarkMode` (uxtheme ordinal
/// 132) on Windows 10 1809+, otherwise `AppsUseLightTheme == 0`.
pub fn system_dark() -> bool {
    use std::sync::OnceLock;
    static BUILD: OnceLock<u32> = OnceLock::new();
    let build = *BUILD.get_or_init(windows_build);
    if build >= 17763 {
        let name = wide("uxtheme.dll");
        // SAFETY: an export of a loaded system DLL, called with no arguments.
        unsafe {
            let module = GetModuleHandleW(name.as_ptr());
            if !module.is_null() {
                let f = GetProcAddress(module, 132usize as *const u8);
                if !f.is_null() {
                    let f: unsafe extern "system" fn() -> bool = std::mem::transmute(f);
                    return f();
                }
            }
        }
    }
    let mut light: u32 = 1;
    let mut cb: u32 = 4;
    let key = wide("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize");
    let value = wide("AppsUseLightTheme");
    // SAFETY: a DWORD read into a DWORD buffer.
    unsafe {
        RegGetValueW(
            HKEY_CURRENT_USER,
            key.as_ptr(),
            value.as_ptr(),
            RRF_RT_REG_DWORD,
            null_mut(),
            (&mut light as *mut u32).cast(),
            &mut cb,
        );
    }
    light == 0
}

/// `Global::GetAppDataPath`: `%APPDATA%` (`CSIDL_APPDATA`), created when
/// `create`. No NUL; empty on failure.
pub fn app_data_path(create: bool) -> Vec<u16> {
    let mut buf = [0u16; MAX_PATH];
    // SAFETY: MAX_PATH buffer, as the API requires.
    let ok = unsafe { SHGetSpecialFolderPathW(null_mut(), buf.as_mut_ptr(), CSIDL_APPDATA, i32::from(create)) };
    if ok == 0 {
        return Vec::new();
    }
    buf[..buf.iter().position(|&c| c == 0).unwrap_or(MAX_PATH)].to_vec()
}

/// `_wstat(path).st_mtime`: the last write time in seconds since 1970, or
/// `None` when `_wstat` fails.
pub fn file_mtime(path: &[u16]) -> Option<i64> {
    let p = nul(path);
    let mut data = FileAttributeData::default();
    // SAFETY: NUL-terminated path; the struct is WIN32_FILE_ATTRIBUTE_DATA.
    if unsafe { GetFileAttributesExW(p.as_ptr(), 0, &mut data) } == 0 {
        return None;
    }
    let ticks = (u64::from(data.last_write[1]) << 32 | u64::from(data.last_write[0])) as i64;
    Some((ticks - 116_444_736_000_000_000) / 10_000_000)
}

/// Reading an INI file the way `CFile` does: `None` when it cannot be
/// opened (or is larger than `MAX_CIN_FILE_SIZE`), `Some(None)` when it
/// opens but has no usable text, else the text after the first two bytes
/// (taken as the BOM, whatever they are). The text must pass
/// `IsTextUnicode`.
pub fn read_cfile(path: &[u16]) -> Option<Option<Vec<u16>>> {
    let p = nul(path);
    // SAFETY: plain file calls on a handle we own; closed on every path.
    unsafe {
        let h = CreateFileW(
            p.as_ptr(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            null(),
            OPEN_EXISTING,
            0,
            null_mut(),
        );
        if h == INVALID_HANDLE_VALUE {
            return None;
        }
        let size = GetFileSize(h, null_mut());
        if size == INVALID_FILE_SIZE || size > MAX_CIN_FILE_SIZE {
            CloseHandle(h);
            return None;
        }
        let text = (|| {
            if size < 2 {
                return None;
            }
            let count = (size as usize - 2) / 2;
            if count == 0 {
                return None;
            }
            if SetFilePointer(h, 2, null_mut(), FILE_BEGIN) == INVALID_SET_FILE_POINTER {
                return None;
            }
            let mut bytes = vec![0u8; size as usize - 2];
            let mut read = 0u32;
            if ReadFile(h, bytes.as_mut_ptr(), size - 2, &mut read, null_mut()) == 0
                || IsTextUnicode(bytes.as_ptr().cast(), read as i32, null_mut()) == 0
            {
                return None;
            }
            Some(
                bytes
                    .as_chunks::<2>()
                    .0
                    .iter()
                    .take(count)
                    .map(|b| u16::from_le_bytes(*b))
                    .collect::<Vec<u16>>(),
            )
        })();
        CloseHandle(h);
        Some(text)
    }
}

/// The ACL step of `CConfig::LoadConfig`: give "Everyone" (and, when
/// `windows8`, "ALL APPLICATION PACKAGES") read access to `dir`, inherited.
/// Returns false when the C++ code would `goto ErrorExit` (reading the DACL
/// or building the new ACL failed); the result of applying it is ignored, as
/// in C++. The settings app has `Global::isWindows8` FALSE.
pub fn grant_read_access(dir: &[u16], windows8: bool) -> bool {
    let p = nul(dir);
    let mut old_dacl: *mut ACL = null_mut();
    let mut sd = PSECURITY_DESCRIPTOR::default();
    let mut new_dacl: *mut ACL = null_mut();
    let mut everyone = wide("Everyone");
    let mut packages = wide("ALL APPLICATION PACKAGES");
    let make = |name: &mut Vec<u16>| EXPLICIT_ACCESS_W {
        grfAccessPermissions: 0x8000_0000, // GENERIC_READ
        grfAccessMode: GRANT_ACCESS,
        grfInheritance: SUB_CONTAINERS_AND_OBJECTS_INHERIT,
        Trustee: TRUSTEE_W {
            TrusteeForm: TRUSTEE_IS_NAME,
            TrusteeType: TRUSTEE_IS_WELL_KNOWN_GROUP,
            ptstrName: PWSTR(name.as_mut_ptr()),
            ..Default::default()
        },
    };
    // SAFETY: Win32 security calls; every allocation is freed below.
    let ok = unsafe {
        (|| {
            if GetNamedSecurityInfoW(
                PCWSTR(p.as_ptr()),
                SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION,
                None,
                None,
                Some(&mut old_dacl),
                None,
                &mut sd,
            )
            .is_err()
            {
                return false;
            }
            let ea = [make(&mut everyone)];
            if SetEntriesInAclW(Some(&ea), Some(old_dacl), &mut new_dacl).is_err() {
                return false;
            }
            if windows8 {
                let ea = [make(&mut packages)];
                let mut next: *mut ACL = null_mut();
                if SetEntriesInAclW(Some(&ea), Some(new_dacl), &mut next).is_err() {
                    return false;
                }
                LocalFree(Some(HLOCAL(new_dacl.cast())));
                new_dacl = next;
            }
            if !new_dacl.is_null() {
                let _ = SetNamedSecurityInfoW(
                    PCWSTR(p.as_ptr()),
                    SE_FILE_OBJECT,
                    DACL_SECURITY_INFORMATION,
                    None,
                    None,
                    Some(new_dacl),
                    None,
                );
            }
            true
        })()
    };
    // SAFETY: both were allocated by the calls above (or are null).
    unsafe {
        if !new_dacl.is_null() {
            LocalFree(Some(HLOCAL(new_dacl.cast())));
        }
        if !sd.0.is_null() {
            LocalFree(Some(HLOCAL(sd.0)));
        }
    }
    ok
}

/// The in-memory bytes of a GUID.
pub fn guid_bytes(g: &GUID) -> [u8; 16] {
    let mut out = [0u8; 16];
    out[..4].copy_from_slice(&g.data1.to_le_bytes());
    out[4..6].copy_from_slice(&g.data2.to_le_bytes());
    out[6..8].copy_from_slice(&g.data3.to_le_bytes());
    out[8..].copy_from_slice(&g.data4);
    out
}

/// One `LanguageProfileInfo` of `CTfInputProcessorProfile::
/// GetReverseConversionProviders`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ReverseProvider {
    /// In-memory bytes of the GUID, as in `ConfigValues`.
    pub clsid: [u8; 16],
    pub guid_profile: [u8; 16],
    /// No NUL.
    pub description: Vec<u16>,
}

/// `CConfig::EnumerateReverseConversionProviders(langid)`: the TSF
/// language profiles of `langid` that have a description (and answer
/// `IsEnabledLanguageProfile`) and whose text service gives an
/// `ITfReverseConversion`. Empty when TSF is unavailable (COM must be
/// initialised on the thread).
pub fn reverse_providers(langid: u16) -> Vec<ReverseProvider> {
    let mut out = Vec::new();
    // SAFETY: COM calls through the windows crate's checked wrappers.
    unsafe {
        let Ok(profiles) =
            CoCreateInstance::<_, ITfInputProcessorProfiles>(&CLSID_TF_InputProcessorProfiles, None, CLSCTX_INPROC_SERVER)
        else {
            return out;
        };
        let Ok(list) = profiles.EnumLanguageProfiles(langid) else {
            return out;
        };
        loop {
            let mut item = [TF_LANGUAGEPROFILE::default()];
            let mut fetched = 0u32;
            if list.Next(&mut item, &mut fetched).is_err() || fetched != 1 {
                break;
            }
            let p = &item[0];
            let desc = profiles.GetLanguageProfileDescription(&p.clsid, langid, &p.guidProfile);
            let enabled = profiles.IsEnabledLanguageProfile(&p.clsid, langid, &p.guidProfile);
            let (Ok(desc), Ok(_)) = (desc, enabled) else {
                continue;
            };
            let Ok(mgr) = CoCreateInstance::<_, ITfReverseConversionMgr>(&p.clsid, None, CLSCTX_INPROC_SERVER) else {
                continue;
            };
            if mgr.GetReverseConversion(langid, &p.guidProfile, 0).is_ok() {
                out.push(ReverseProvider {
                    clsid: guid_bytes(&p.clsid),
                    guid_profile: guid_bytes(&p.guidProfile),
                    description: desc.to_vec(),
                });
            }
        }
    }
    out
}
