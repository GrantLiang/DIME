# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 專案概述

DIME 是 Windows 8/10/11 的中文輸入法：Windows TSF（Text Services Framework）的外殼是原生 C++，輸入法邏輯、碼表引擎、設定、視窗與設定程式是 Rust（見下方「Rust 架構」），唯一的外部相依是官方 `windows` crate。同一個 `DIME.dll` 提供四種輸入法模式（`IME_MODE`，定義於 `src/BaseStructure.h`）：大易 `DAYI`、行列 `ARRAY`、注音 `PHONETIC`、自訂 `GENERIC`。

## 建置

需求：Visual Studio（C++ 桌面開發 workload、Windows SDK；專案用 v145（CI 用 runner 上最新的 MSVC，並安裝 Spectre 程式庫與 ATL））。方案檔是 `src/DIME.sln`，指令要在 `src/` 下執行。

```powershell
cd src
msbuild DIME.sln /p:Configuration=Debug /p:Platform=x64                       # 整個方案（含測試）
msbuild DIME.sln /p:Configuration=Release /p:Platform=x64 /t:DIME              # 僅 IME DLL
msbuild DIME.sln /p:Configuration=Release /p:Platform=Win32 /t:DIMESettings    # 設定程式（僅 Win32）
```

- `DIME` 專案 → `DIME.dll`（Win32 / x64 / ARM64 / ARM64EC）；`DIMESettings` 專案 → `DIMESettings.exe`（僅 Win32，所有平台共用；Utility 專案，由 cargo 建置 `rust/crates/dime-settings-exe`）；`DIMETests` 專案 → 單元測試 DLL。
- 每次建置的 PreBuildEvent 會執行 `src/buildInfo.cmd`，以 git commit 數與日期重新產生 **受版控的** `src/BuildInfo.h`（版號格式 `2.0.<commit數>.<日期>`；本 fork 從 2.0 起算，比 upstream 的 1.3.x 高，MSI 從 upstream 換過來是升級）。建置後 `BuildInfo.h` 出現 diff 屬正常；除非是要發版，否則不要把它一起 commit。

## 測試

使用 Microsoft CppUnitTestFramework（`TEST_CLASS` / `TEST_METHOD`），所有測試在 `src/tests/`，編成單一 `DIMETests.dll`，輸出到 `src/bin/<Platform>/<Configuration>/`。

```powershell
cd src
msbuild DIME.sln /p:Configuration=Debug /p:Platform=x64
vstest.console.exe bin\x64\Debug\DIMETests.dll                                   # 全部
vstest.console.exe bin\x64\Debug\DIMETests.dll /Tests:ParseModeTests               # 單一測試類別或方法（名稱片段比對）
vstest.console.exe bin\x64\Debug\DIMETests.dll /TestCaseFilter:"FullyQualifiedName~CLI"
```

- CI（`.github/workflows/DIME_tests.yml`）跑 Win32/x64 × Debug/Release 矩陣。
- `src/tests/run_all_coverage.ps1` 以 OpenCppCoverage 產生覆蓋率報告。
- **按鍵流程黃金測試**（`KeyFlowTest.cpp` + `KeyFlowHarness.*`）：真實 TSF thread manager + 自製 `ITextStoreACP`，把按鍵腳本送進未修改的 `CDIME`，逐鍵記錄吃鍵與否、文件文字、組字、候選字，與 `src/tests/keyflow/*.txt` 比對。改動按鍵處理、候選字、整句等行為時，先跑這組；行為是刻意改變時設 `DIME_KEYFLOW_RECORD=1` 重新錄製並檢查 diff。測試用 `Global::testAppDataOverride` 與 `%ProgramW6432%` 指向暫存資料夾，不碰真實 `%APPDATA%\DIME`；產品程式碼取得 `%APPDATA%` 一律用 `Global::GetAppDataPath()`。
- 測試專案**直接編譯** `src/` 與 `src/DIMESettings/` 的產品 `.cpp`（不是連結 `DIME.dll`）並連結 `dime_ffi.lib`，定義 `DIME_UNIT_TESTING`。此巨集會在 `Config.h`、`DIME.h`、`File.h`、`UIPresenter.h`、`CLI.h` 等處開放測試用 hook；`TestStubs.cpp` 補上被排除的 `DllMain.cpp` 符號。新增產品 `.cpp` 而測試需要用到時，要一併加進 `src/tests/DIMETests.vcxproj`。

## 架構重點

- **TSF 文字服務（`DIME.dll`）**：`CDIME`（`DIME.cpp/.h`）是 TSF text service 主體，實作各種 sink，分散在多個檔案：`KeyEventSink.cpp`、`ThreadMgrEventSink.cpp`、`TextEditSink.cpp`、`ThreadFocusSink.cpp`、`ActiveLanguageProfileNotifySink.cpp`、`FunctionProviderSink.cpp` 等。COM 註冊在 `Register.cpp` / `Server.cpp` / `DllMain.cpp`。
- **按鍵處理流程**：`KeyEventSink`（是否吃鍵：`KeyProcesser` 的 `IsVirtualKeyNeed`，Rust `keyclass`）→ `CKeyHandlerEditSession` 在 TSF edit session 中呼叫 Rust 的 `session::handle_key`（路由與 handler），它經 `RustSessionHost` 回呼 C++ 修改 composition、開關 UI → `CompositionProcessorEngine`（字根緩衝、查字典、產生候選字的 C++ 外殼，邏輯在 Rust；初始化在 `SetupCompositionProcesseorEngine.cpp`，檔名拼錯是原樣）。
- **字典**：`.cin` 碼表由 Rust 的 `dime-core` 解析與搜尋，經 `CRustTableDictionaryEngine`（`CTableDictionaryEngine` 的子類別）接到 C++，檔案透過 `File.cpp` 的記憶體映射（`CMemoryFile`）讀取。反查走 TSF 標準介面（`ReverseConversion*.cpp`、`SearchCandidateProvider`）。
- **UI**：`UIPresenter`（TSF 的 `ITfCandidateListUIElement` 等）經 Rust 呈現器操作候選字窗、提示窗、陰影與捲軸，這些視窗都由 `dime-ui` 擁有；`LanguageBar` 為語言列按鈕。游標位置追蹤用 TSF text layout 加 `UIACaretTracker`（UIA fallback）。
- **設定**：`CConfig`（`Config.cpp`，全為 static；值存在 Rust 的全域設定物件）讀寫 `%APPDATA%\DIME\` 下每個模式各自的 INI（`DayiConfig.ini`、`ArrayConfig.ini`、`PhoneConfig.ini`、`GenericConfig.ini`），INI 的解析與輸出在 Rust `dime-config`。Store app（AppContainer）只能唯讀，`Config.cpp` 會為 "ALL APPLICATION PACKAGES" 開讀取權限。
- **`DIMESettings.exe`**：純 Rust（`dime-settings-exe`），內含設定視窗與無頭 CLI（文件見 `docs/DIME_CLI.md`；exit code：0 成功、1 參數錯誤、2 I/O 錯誤、3 該 key 不適用此模式）。`src/DIMESettings/` 剩下的 C++（控制項 ID 的 `SettingsKeys.h`、`SettingsController.h` 等）只給測試驅動 Rust 視窗與 CLI 用。修改設定項時，設定視窗、CLI、`CConfig` 三處都要一致。
- 各功能的設計文件與議題紀錄放在 `docs/*.md`（例如 `CANDI_WINDOW.md`、`CARET_TRACKING*.md`、`LIGHT_DARK_THEME.md`、`MSI_INSTALLER.md`），修改相關區域前先查閱。

## Rust 架構

`rust/` 是 Cargo workspace（`rust/Cargo.lock` 進版控，建置一律 `cargo --locked`）。輸入法的邏輯、碼表引擎、設定、視窗與設定程式都在 Rust；C++ 只剩 TSF／COM 外殼與它要呼叫的少數 Windows 介面。移植經過 6 個階段（計畫與每一步的紀錄在 `docs/RUST_MIGRATION_PLAN.md`）；最後一個保留全 C++ 實作的版本是 tag v2.0.770。

- **建置**：`DIME.vcxproj`／`DIMETests.vcxproj` 建置前自動跑 cargo 產生 `dime_ffi.lib` 並連結（`DIME.dll` 的 Release 用 `+crt-static` 配合 `/MT`，輸出到 `rust/target/crt-static/`）。`DIME.dll` 支援 Win32/x64/ARM64/ARM64EC。沒有建置開關：`/p:DimeUseRustEngine=false`／`/p:DimeTestRustEngine=false` 會報錯。連結 `dime_ffi.lib` 要加 `synchronization.lib`（標準庫用 `WaitOnAddress`）；完整清單用 `cargo rustc -p dime-ffi --lib -- --print native-static-libs` 查。MSBuild 裡呼叫 cargo 要清掉 `VSTEL_MSBuildProjectFullPath`／`VCINSTALLDIR`／`VSINSTALLDIR`／`LIB`／`LIBPATH`（`$(DimeCargoEnv)`），否則 x64 主機的 build script 會用 Win32 的 x86 `LIB` 連結。
- **crates**：
  - `dime-core`：碼表解析與搜尋（`DictionaryParser`／`DictionarySearch` 的逐行移植）、組字核心（`keyclass` 按鍵分類、`composition` 組字緩衝、`candgen` 候選字產生、`candidates` 選取與翻頁、`sentence`／`sentence_buffer` 整句、`engine_setup` 引擎設定、`langbar` 語言列）與按鍵流程 `session`（`handle_key` 是按鍵路由 + 各 handler）。
  - `dime-config`：`CConfig` 的設定值、預設值、INI 讀寫（`ini.rs` 的 `[Config]` 套用、`WriteConfig` 逐位元組相同的輸出）、CLI（`cli`、`keys.rs` 的 key 表）、設定程式的模型／版面／幾何／碼表轉換（`settings_model`、`page_layout`、`geometry`、`cin_convert`、`text_file`）。
  - `dime-ui`：候選字窗（`candwin`／`candproc`／`candpaint`／`candwnd`）、捲軸（`sbwin`／`sbproc`／`sbpaint`）、提示窗（`notifywin`／`notify`）、陰影（`shadow`）、呈現器（`presenter` 位置決策、`presenterwin` 擁有視窗與狀態）、視窗基礎設施 `win`（`docs/RUST_WINDOW_OWNERSHIP.md`）、定位計算 `layout`。依賴官方 `windows` crate（workspace 唯一的外部相依，只開需要的 `Win32_*` 功能）。
  - `dime-gdi`：Rust 視窗與設定視窗的 GDI／GDI+ 呼叫，附每執行緒的錄製器（測試用，見「黃金檔」）。`dime-ui` 經 `gdi.rs`（`windows` crate 的簽章）呼叫它，`dime-settings` 以 `gdi` 重新匯出。
  - `dime-settings`：設定視窗（`window` 擁有視窗與所有 Host；`nav`、`frame`、`driver`、`command`、`wndproc`、`children`、`controls`、`populate`、`lifecycle`、`editor`、`paint`、`owner`、`shell`、`msgboxwin`、`winhost` 等）。
  - `dime-ffi`：C ABI（`include/dime_ffi.h`），編成 `dime_ffi.lib`；也有設定程式的 `NativeApp`／`NativeCli`／`settings_main`。所有匯出函式以 `catch_unwind` 包住，panic 不得跨出邊界。
  - `dime-settings-exe`：`DIMESettings.exe`（bin `DIMESettings`），執行期沒有 C++。build.rs 自己寫 `.res`（圖示、版本取自 `src/BuildInfo.h`、manifest），不用 `rc.exe`。CLI 輸出照 CRT 在 "C" locale 文字模式（U+00FF 以上變 `?`、`\n` 變 `\r\n`、NUL 結束該次寫入、`stdout` 在管線／檔案上有 4,096 位元組緩衝）。
- **刻意保留的怪異行為**：碼表解析保留 C++ 的行為（chardef 中沒有分隔字元的行會中止搜尋、`%selkey` 只認 tab 分隔、排序平手順序），設定的數字解析模擬 MSVC 的 `_wtoi`／`wcstol`／`wcstoul`（全形數字、`0x`／八進位前綴、溢位截斷），碼表轉換照 MSVC CRT 文字模式讀寫與 `swscanf_s`（UCRT 讀 UTF-8 時每 2048 位元組一段），`MulDiv` 模擬系統行為（x86 與 x64 在 `INT_MIN` 上不同）。呼叫端與使用者的檔案依賴這些行為，改之前先看對應的黃金檔與測試。地區相關比較經 `TextOps`（Windows 上是與 C++ 相同的 `CompareStringW`／`GetStringTypeExW`）。
- **C++ 這邊的接線**：
  - 碼表引擎一律用 `CTableDictionaryEngine::Create()`（回傳 `CRustTableDictionaryEngine`，`src/RustTableDictionaryEngine.*`）。基底 `CTableDictionaryEngine` 的搜尋與解析是空的預設實作（只給只放字根表的引擎與測試替身用），保留字根表、選字鍵欄位與排序（排序仍是 C++）。碼表緩衝區由 `CFile`／`CMemoryFile` 提供（含重新載入與 Big5 過濾）；回傳字串在轉接層內 intern，指標在引擎存活期間有效。
  - 設定：`CConfig` 的設定成員（`DIME_CONFIG_VALUE`／`DIME_CONFIG_DEFINE`）是指向 Rust 全域設定物件的參考。呼叫 `dime_settings_*` 一律透過 `CRustConfigScope`。`ReverseConversionDescription` 用 `Get/SetReverseConversionDescriptionText()` 存取，不可 `delete[]`。新增設定項時 `DimeConfigValues`（308 bytes，兩邊都有 size 檢查）、`DIME_CONFIG_DEFINE` 對應欄位、`ExportValues`／`ImportValues`、`keys.rs` 的 `KEYS` 要一起改。
  - 按鍵流程：`CKeyHandlerEditSession::DoEditSession` 只呼叫 `dime_session_handle_key`；C++ 的副作用（TSF composition、edit session、UI）經 `src/RustSessionHost.*` 的 `DimeHost` vtable。Rust 擁有的狀態在 `session::SessionState`（`CDIME::_pRustSession`：整句、Shift 英數／萬用字元等待旗標、`_candidateMode`／`_isCandidateWithWildcard`，後兩者是綁到 session 欄位的 C++ reference）。各公開函式（`IsVirtualKeyNeed`、`GetCandidateList`、`SetupDictionaryFile`、語言列的 `*CompartmentUpdated` 等）呼叫對應的 `_XxxRust`。
  - UI：`CUIPresenter` 經 `_rp`（`DimePresenter`）操作 Rust 的呈現器；候選字窗與提示窗的 callback 型別在 `src/UIWindowTypes.h`。測試存取位置、slot 與候選字窗用 `_GetPlacement`／`_SetPlacement`／`_NotifySlotExists`／`_NotifySlotWnd`／`_CandWnd`；交給候選字窗的 `CCandidateRange` 要活得比視窗久（視窗保留指標）。
  - 設定程式（`src/DIMESettings/`）剩下的 C++ 只給測試用：`Settings*.cpp` 的對外函式呼叫 Rust，`SettingsApp::Host`／`ConfigHost` 與 `CLI.cpp` 是測試驅動 Rust 視窗與 CLI 的橋。修改設定項時，設定視窗（`dime-settings`）、CLI（`dime-config::cli`）與 `CConfig` 三處都要一致。
- **vtable**：C++ 與 Rust 之間的 Host vtable（`DimeHost`、`DimeCliHostVtbl`、`DimeSettingsCmdHostVtbl`、`SettingsApp` 的 `AppVtbl`、`SettingsProc`／`SettingsDriver`／`SettingsChildren` 的 Host、`CEngineSetupHost`、`CLangBarHost` 等），欄位順序在 `dime_ffi.h`、C++ 的 `kVtbl`／`s_vtbl` 與 Rust 的對應 struct 三處必須一致；thunk 不可丟例外。Host 方法不要用 `MessageBox`／`ChooseFont` 等會被 `windows.h` 巨集改名的名稱。視窗程序會重入：Rust 端 Host 只持有原始指標、每次存取只借用一下，不可跨 Host 呼叫保留 `&mut`。Rust 宣告 Win32 結構時注意 `FILETIME` 是兩個 DWORD（4 位元組對齊），`CHARFORMAT2W` 照 C++ 繼承版面（`wWeight` 在 offset 92）。
- **設定視窗的 Windows 細節**：上色一律用 TOM（不可改用 `EM_SETSEL`＋`EM_SETCHARFORMAT`，會移動游標），期間關閉 `EN_CHANGE`。下拉選單與編輯框的無障礙名稱以 Dynamic Annotation（`IAccPropServices::SetHwndPropStr`, `PROPID_ACC_NAME`）設定，滾輪子類別在 `WM_NCDESTROY` 清除（需要執行緒已初始化 COM）。切換列的勾選框（`ROLE_TOGGLE`）可見但套空的視窗區域，捲動時 `ResetControlRegion` 不碰它（判斷看類別 `Button`：`CBS_DROPDOWNLIST` 的低位元和 `BS_AUTOCHECKBOX` 相同），滾輪子類別對它略過 `WM_PAINT`／`WM_ERASEBKGND`／`WM_PRINTCLIENT`。視窗位置存在 `SOFTWARE\DIME\SettingsUI`（測試只用 `SettingsUIShellParityTest`）。手動檢查清單在 `docs/SETTINGS_MANUAL_CHECKLIST.md`。

### 黃金檔與測試

- 移植期間的「C++ 對 Rust」對拍測試都已轉成黃金檔：測試把 Rust 的輸出與 `src/tests/golden/<group>/<name>.txt` 比對（`src/tests/Golden.*`）。多數測試名稱仍叫 `*ParityTest`，有些還保留「C++ 那側」的變數或包裝，但那一側現在也是 Rust，參考值是黃金檔與測試裡的絕對斷言。行為是刻意改變時設 `DIME_GOLDEN_RECORD=1` 重錄並檢查 diff；錄製後要在 x64 與 Win32、Debug 與 Release 都通過（CI 的 Test 也跑 `build/**` 分支）。
- 寫進黃金檔的輸入一律用 `Golden::Pick`（不要用 `uniform_int_distribution`），亂數抽取的順序就是錄製時的順序，不可調動。黃金檔不可含位址、暫存路徑、機器的主題／dpi／字型量測、滑鼠位置、前景視窗、螢幕大小或 Debug CRT 的填充位元組。比對失敗時，該段的實際輸出（`Golden::Chunks` 的明細，每段上限 200,000 字元）在測試的標準輸出。
- 視窗的繪圖錄繪圖呼叫而不是像素：`Golden::GdiRecording` 期間 `dime-gdi` 記下每個 GDI 呼叫，量字（`GetTextExtentPoint32W`、`GetTextMetricsW`、`DrawTextW` 的 `DT_CALCRECT`）回傳依字型高度的合成值。要錄的視窗只在錄製期間建立與操作，並固定 96 dpi 與淺色主題（`CConfig::testDpiOverride`／`testSystemDarkOverride`）、工作區（`dime_ui_set_work_area_for_test`）、滑鼠位置（`dime_ui_set_cursor_pos_for_test`）與 `Show(TRUE)` 讀的前景視窗事實（`CUIPresenter::testLayoutFacts`）；視窗與模組 handle 在紀錄裡一律寫 `x`。
- **按鍵流程黃金測試**（`KeyFlowTest`）見上方「測試」一節。
- 視窗測試：需要真實視窗的測試呼叫 `tests/WindowClassTestHelper.h` 的 `EnsureWindowClassesRegistered()`，不要直接呼叫 `Global::RegisterWindowClass()`（重複註冊會把 atom 設為 0）；測試用的 `CRustCandidateWindow`／`CRustNotifyWindow`（`src/tests/`）以 `RustCandidateTestClasses()`／`RustNotifyTestClasses()` 的測試類別建立。收尾用 `DestroyAndDelete()`（先 `_Destroy()` 再 `delete`），否則 HWND、可見視窗與計時器會留到後面會處理訊息的 KeyFlow 測試。
- 設定視窗測試：`SettingsWindowParityTest`／`SettingsScreenshotTest`／`SettingsUiaTest`／`RichEditViewportTest` 以 `dime_settings_window_create` 建 Rust 視窗（共用 `src/tests/SettingsFakeApp.h`）。UIA 測試的視窗以 alpha 0 的分層視窗顯示，UIA 用戶端在另一條 MTA 執行緒，視窗執行緒要持續處理訊息；不要透過 UIA 展開下拉選單。RichEdit 的 Enter／Backspace 要送 `WM_KEYDOWN`。截圖用 `WM_PAINT` 本體加子控制項的 `WM_PRINT` 合成，不要改回 `PrintWindow(PW_RENDERFULLCONTENT)`；設 `DIME_SETTINGS_SCREENSHOT_DIR` 存下全部截圖。訊息框測試的驅動執行緒要等視窗可見再操作。`SettingsMainParityTest::ExecutableCli` 執行真正的 `rust\target\<triple>\release\DIMESettings.exe`（由 `DIMETests` 的 cargo 步驟建置；exe 沒有測試用的 AppData 覆寫，只能用不讀 AppData 的命令）。
- 整句測試經 `src/tests/RustSentenceBuffer.h`（以 `dime_sentence_buffer_*` 實作、介面同舊 C++ `CSentenceBuffer` 的測試用類別）測 Rust，學習用 `LearnInto(user)`。

```powershell
cargo test --manifest-path rust/Cargo.toml                 # Rust 單元測試
cargo clippy --manifest-path rust/Cargo.toml --all-targets
$env:DIME_GOLDEN_RECORD=1; vstest.console.exe src\bin\x64\Debug\DIMETests.dll /Tests:UILayoutParityTest   # 刻意改變行為後重錄
```

- 本機建置注意：若環境變數 `NoDefaultCurrentDirectoryInExePath=1`，`BuildInfo.cmd` 預建置步驟會失敗；`DIME.vcxproj` 需要 Spectre 版 MSVC 程式庫（本機沒有時可加 `/p:SpectreMitigation=false` 僅供驗證）。

### 整句輸入（`SentenceMode`，目前僅大易）

- 設定 `SentenceMode=1`（INI / CLI / 設定視窗「整句輸入」）後，空白鍵或結束鍵不再跳出候選字窗，而是把該字（連同該字碼的全部候選字）加入組字區的「整句」，由 `rust/crates/dime-core/src/sentence.rs` 的 Viterbi 依字頻（`TCFreq.cin`）與詞庫（`Array-Phrase.cin` 的詞，不用其字碼）重選整句；Enter／空白送出，←→ 移動、↓ 改選游標所在字（選定即釘住），Backspace／Delete 刪字，Esc 放棄。標點、地址鍵、Shift 英數等直接輸出的鍵會先送出整句。
- 整句的狀態與按鍵處理在 Rust session（`dime-core::session`、`sentence_buffer`）；TSF 端（`SentenceHandler.cpp`）負責寫組字區、載入字典與學習檔。`SentenceBuffer.*` 是 `CSentenceLexicon`（字頻與詞庫）與 `CSentenceUserModel`（學習資料）的 C++ 包裝。
- 學習：送出整句時，使用者手動選定（釘住）的字會被 `UserModel`（Rust）記住「該字碼偏好的字」與「它和前後字組成的雙字詞」，之後轉換加分（同一字碼要選約 3 次才會在沒有上下文時取代第一候選字；詞在上下文中一次就生效）。存在 `%APPDATA%\DIME\SentenceUser.txt`（UTF-16，刪除即重置）；多個行程共用此檔，存檔時重新讀檔再加上本行程新增的部分（`_pSentencePending`），不會互相覆蓋。Store app／低完整性行程不寫檔，只留在記憶體。

## 安裝程式與發版

- 現行安裝程式為 WiX 4（`installer/wix/`，Burn bundle `DIME-Universal.exe` 內含 `DIME-64bit.msi` / `DIME-32bit.msi`，custom action 在 `DIMEInstallerCA/`）；以 `installer\deploy-wix-installer.ps1` 建置（參數：`-ProductVersionOverride`、`-NonInteractive`、`-SkipChecksumUpdate`、`-OutputDir`）。NSIS（`DIME-Universal.nsi`、`deploy-installer.ps1`）為舊版。
- `installer/` 中的 MSI/ZIP 與 `README.md` 裡的版本、SHA-256 checksum 區塊（`<!-- ... _START -->` / `_END` 標記之間）由 CI bot 自動更新（`build_installer.yml`、`new_release.yml`），不要手動修改這些區塊或二進位檔。發版說明取自 `docs/DRAFT_RELEASE.md`。
- 隨安裝程式出貨的 `installer/*.cin` 必須是 **UTF-16 LE**；使用者透過 CLI/設定載入的 `.cin` 則是 UTF-8（README「自訂碼表格式」一節有完整格式說明）。`Tables/` 是碼表原始檔/歷史版本。
- 執行期不要直接修改 `%APPDATA%\DIME\` 下的檔案；設定與碼表變更一律透過 `DIMESettings.exe` CLI。
