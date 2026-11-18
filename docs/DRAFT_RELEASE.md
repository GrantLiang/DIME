## v2.0.816 之後

- **自訂地址鍵符號**（大易）：設定新增「地址鍵符號」（CLI key `DayiArticleChars`）。開啟「地址鍵輸入符號」時，`'` `[` `]` `\` `-` `` ` `` 依序輸出你填的 6 個字，例如填 `，『』：。、` 就是 `'`→，　`[`→『　`]`→』　`\`→：　`-`→。　`` ` ``→、；留空用原本的 `，。？；、：`。
  `DIMESettings.exe --mode dayi --set DayiArticleMode 1 --set DayiArticleChars ，『』：。、`

## v2.0.816：整句輸入用方向鍵回頭改字

- **整句輸入打字根時也能用方向鍵回頭改字**：之前打了字根（尤其是候選字窗開著時）按 ←→ Home End 沒有反應，要先選字或按 Esc 才能移動。現在會先把這個字加入整句（如同空白鍵；在候選字窗移過反白則用反白的字），再移動游標，接著可用 ↓ 改前面的字。

## v2.0.812：整句輸入搭配選字鍵

- **整句輸入搭配選字鍵**：說明文件與設定說明補上用法——同時開啟「整句輸入」與「打字時同步組字」，打字根時就會顯示候選字窗，可用選字鍵直接選字（選中的字固定並會被學習）；大易建議搭配「大易選擇鍵」選字鍵模式。
  `DIMESettings.exe --mode dayi --set SentenceMode 1 --set AutoCompose 1 --set DayiSelkeyMode 2`

## v2.0.808：移除 C++ 實作

這一版沒有新功能，輸入與設定的行為與 v2.0.770 相同。v2.0.765／v2.0.770 以 Rust 為預設、但仍保留可切換的 C++ 實作；這一版把 C++ 的組字核心、字典引擎、設定儲存、候選字／提示視窗與設定程式全部刪除（約 22,000 行），輸入法只剩 Rust 實作，C++ 只保留 Windows TSF／COM 的外殼。

- 原本「C++ 與 Rust 結果相同」的對拍測試，改成和錄好的黃金檔比對（視窗繪圖改錄 GDI 呼叫，結果與機器的字型、主題、dpi 無關）。
- 若遇到 v2.0.770 沒有、這一版才出現的問題，請回報並附上版本號；需要暫時退回時可安裝 v2.0.770。

### 給開發者

- 建置不再有 Rust／C++ 開關，`DIMESettings.exe` 只由 cargo 建置；要建置全 C++ 版本請用 tag v2.0.770。
- 測試規則（黃金檔的錄製方式與穩定化）見 [CLAUDE.md](https://github.com/GrantLiang/DIME/blob/master/CLAUDE.md) 的「Rust 架構」一節。

---

## v2.0.765 之後的修正

- **修正設定視窗捲動後開關與下拉選單異常**：在 v2.0.765 中，拖曳捲軸或以滾輪捲動設定頁面後，開關的位置會出現白色的勾選框蓋住開關，下拉選單則會消失或無法選取（滑鼠移到捲軸上時整個不見）。現在捲動後開關與下拉選單都會正常顯示與操作。

## DIME 2.0（v2.0.765）：Rust 核心版 (本 fork，基於 upstream `master` `2788155`)

這是本 fork 第一個以 **Rust 核心** 為預設建置的版本。輸入法的組字、字典、設定與候選字／提示視窗大多已改由 Rust 實作，設定程式 `DIMESettings.exe` 則完全改為 Rust 程式。行為與外觀以對拍測試、按鍵流程黃金測試與逐像素截圖比對確認和原本的 C++ 版相同；若發現任何差異，請回報。

### 新功能

- **大易整句輸入**（設定「整句輸入」，CLI key `SentenceMode`，預設關閉）
  - 連續打字不必逐字選字：空白鍵或結束鍵把該字加入組字區的整句，依字頻（`TCFreq.cin`）與詞庫（`Array-Phrase.cin` 的詞）以上下文自動選出整句。
  - Enter／空白送出，←→ 移動、↓ 改選游標所在的字（選定後固定），Backspace／Delete 刪字，Esc 放棄；標點、地址鍵、Shift 英數等會先送出整句。
  - 會學習手動改選的字與它和前後字組成的詞，存在 `%APPDATA%\DIME\SentenceUser.txt`（刪除即重置）；Store app 與低完整性行程只記在記憶體。
- **大易選字鍵模式**（設定「選字鍵」，CLI key `DayiSelkeyMode`）：`依碼表設定`、`數字鍵 1~0`，或 `大易選擇鍵`（空白鍵選第 1 個字，`'` `[` `]` `-` `\` 選第 2～6 個字，Shift+`'` `[` `]` `-` 選第 7～10 個字）。
- **設定視窗無障礙**：下拉選單與編輯框有朗讀名稱（列標題），開關可由螢幕閱讀器與 UI 自動化切換。

### 修正

- 自動組字搭配萬用字元時的當機。
- 依 README 格式撰寫的 `Phrase.cin` 聯想詞表查不到聯想詞。
- 設定視窗標題的版號會跟著版本更新（`BuildInfo.h` 每次建置都重新產生）。

### 變更

- 版號改為 2.0 起算（`2.0.<commit 數>`）。本 fork 不併回 upstream，2.0 比 upstream 的 1.3.x 高，已安裝 upstream 版本的電腦直接安裝即為升級。
- 舊版屬性表設定對話框已移除；`DIMESettings.exe --legacy` 改開新的設定視窗。
- `DIMESettings.exe` 的版本資訊改為應用程式（`FILETYPE`、`InternalName`、`OriginalFilename`），不再沿用 `DIME.dll` 的值。

### 給開發者

- 預設建置需要 Rust 工具鏈與對應的 target（見 [HOW_TO_BUILD.md](https://github.com/GrantLiang/DIME/blob/master/docs/HOW_TO_BUILD.md)）；全 C++ 的建置（`/p:DimeUseRustEngine=false`）已移除，最後一個可以這樣建置的版本是 v2.0.770。
- 改寫計畫與進度：[RUST_MIGRATION_PLAN.md](https://github.com/GrantLiang/DIME/blob/master/docs/RUST_MIGRATION_PLAN.md)。

---
