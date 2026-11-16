//! `DIMESettings.exe` (phase 5b-15f): the settings window and the headless
//! CLI, all in Rust. `dime_ffi::settings_main` is the port of `wWinMain`
//! (the C++ `src/DIMESettings/DIMESettings.cpp`, removed in phase 6a-4);
//! `build.rs` embeds the icons, the
//! version resource and the manifest the C++ executable had.

#![cfg_attr(windows, windows_subsystem = "windows")]

#[cfg(windows)]
fn main() {
    let version = [
        env!("DIME_BUILD_VER_MAJOR").parse().unwrap_or(0),
        env!("DIME_BUILD_VER_MINOR").parse().unwrap_or(0),
        env!("DIME_BUILD_COMMIT_COUNT").parse().unwrap_or(0),
    ];
    let code = dime_ffi::settings_main::main(version);
    std::process::exit(code);
}

#[cfg(not(windows))]
fn main() {
    eprintln!("DIMESettings runs on Windows only.");
    std::process::exit(1);
}
