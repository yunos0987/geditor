# geditor 実装計画

Windows 専用の、起動の速いシンプルなテキストエディター。既定のキーバインドは Emacs で、キーバインドとフォントは設定ファイルで簡単に変えられる。

この文書はヒアリング（2026-11-10）で確定した要件と、それを実現する設計・段階計画をまとめたもの。

## 1. 確定した要件

| 項目 | 決定 |
|---|---|
| 対象 OS | Windows のみ（Linux 対応しない） |
| 言語 | C++ + Win32 API（GUI フレームワークは使わない） |
| 描画 | DirectWrite + Direct2D |
| 設定ファイル | `%USERPROFILE%\.geditor.json`（JSONC: `//`・`/* */` コメントと末尾カンマを許可） |
| 設定の反映 | ファイル保存を検知して即反映。誤りは行・桁付きでミニバッファに表示し、直前の設定を維持（誤りの種類ごとの扱いは 2 章「設定の誤り」） |
| 既定キーバインド | exe に内蔵した Emacs 互換。設定ファイルには差分だけ書く |
| キーバインドの柔軟さ | コマンド名・引数付きコマンド・複数コマンドの連続実行・割当解除 |
| Windows 互換キー | Windows の Ctrl+英字の操作を Ctrl+Alt+英字に割り当てる。Emacs の `C-M-` より優先 |
| ファイル数 | 1 ウィンドウ 1 ファイル（別ファイルは別プロセス） |
| 文字コード | UTF-8 / UTF-16 (LE/BE) / Shift_JIS(CP932) を自動判定。BOM 有無を保持・変更可 |
| 改行コード | LF / CRLF / CR を判定して保持。変更コマンドあり |
| アンドゥ | 直線型 undo/redo（undo 後に編集すると redo 履歴は消える） |
| 正規表現 | PCRE2 を静的リンク（UTF-16 モード、JIT 有効） |
| ファイル指定 | ミニバッファ + Tab 補完（C-x C-f / C-x C-w）。ドラッグ＆ドロップでも開ける |
| 保存の安全策 | 一時ファイル経由の安全な置き換え + Emacs 風バックアップ `file~`（設定で ON/OFF） |
| ファイルサイズ | 100MB まで快適に編集できること |
| 起動速度 | 空ファイルでウィンドウの初回描画まで 100ms 以内 |

### 初版に入れる機能

- カーソル移動、マーク/リージョン、kill/yank（Windows クリップボードと連携）、切り取り・貼り付け
- アンドゥ・リドゥ
- 開く・保存・別名保存、未保存時の確認
- 日本語 IME 入力
- ミニバッファ、M-x（コマンド名を Tab 補完して実行）
- インクリメンタル検索（C-s / C-r）、正規表現検索、問い合わせ置換（通常・正規表現）、検索語の強調表示
- 行番号指定ジャンプ
- 行番号表示
- 折り返し（なし / ウィンドウ幅 / 固定桁 / 最大幅）
- タブ幅指定
- フォント（名前・サイズ・代替フォント）、配色、空白文字の可視化
- マウス操作（クリック・ドラッグ選択・ホイール）
- 前置引数 C-u、キーボードマクロ
- 文字コード・BOM・改行コードの変更

### 初版に入れないもの（後回し）

kill ring と M-y、構文ハイライト、複数バッファ・画面分割・タブ、自動保存（`#file#`）、行間・太さ・アンチエイリアスの設定、IME の変換中文字列のインライン描画、カラー絵文字、合字・複雑な文字組み、書記素クラスタ単位のカーソル移動、行をまたぐ正規表現、EUC-JP / JIS、ファイルダイアログ、印刷、スクリプト言語。

## 2. 既定で決めた事項（異論があれば変更する）

ヒアリングで聞かなかった細部を次のように決めた。

| 項目 | 既定の決定 | 理由 |
|---|---|---|
| ビルド | CMake + Ninja + clang-cl、x64、C++20、静的 CRT（`/MT`）、Release は LTO | 手元にある道具で組め、VC++ ランタイム DLL に依存しない |
| 内部の文字表現 | UTF-16（`wchar_t`） | Win32・DirectWrite・PCRE2(16bit) とそのまま受け渡せる |
| テキスト構造 | piece tree（piece table を平衡二分木で持ち、各ノードに文字数・改行数の集計を持つ） | 100MB でも挿入・削除・行番号検索が O(log n) |
| 内部の改行 | 読込時に `\n` に正規化し、保存時に選択中の改行コードへ戻す | 編集処理を単純にする |
| 改行混在ファイル | 最も多い改行を採用し、混在していた旨をミニバッファに表示 | 保存すると統一される |
| UTF-16 の BOM なし判定 | NUL バイトの偏りで判定 | 「BOM 有無 Unicode 対応」の要件 |
| 判定不能なバイト列 | UTF-8 として置換文字で読み、保存時に警告して確認 | 黙って壊さない |
| Shift_JIS で表せない文字の保存 | 保存を中止し、文字コードの変更を促す | 黙って `?` に化けさせない |
| カーソル単位 | コードポイント（サロゲートペアは分割しない） | 書記素クラスタは後回し |
| 桁の数え方 | 東アジアの全角（East Asian Width F/W）を 2 桁、それ以外を 1 桁 | 折り返し・カーソル位置・マウス位置の計算に使う |
| 文字の配置 | 端末と同じ升目配置。升目の幅は主フォントの `0` の送り幅で、全角は 2 升。各文字は自分の升目の中に描く（4.7） | 「全角 2 桁」の折り返しと見た目が必ず揃い、位置計算が整数演算で済む。代わりに合字やアラビア文字などの複雑な文字組みは正しく出ない |
| 新規ファイル | UTF-8（BOM なし）+ LF。設定 `new_file.encoding` / `new_file.eol` で変更可 | ヒアリングで確定 |
| 改行の無いファイル | 改行コードは `new_file.eol` を使う。ASCII のみのファイルは UTF-8 と判定する | |
| 別ファイルを開く | C-x C-f・ドラッグ＆ドロップとも現在のファイルを置き換える（未保存なら確認）。undo 履歴は捨てる | 1 ウィンドウ 1 ファイル |
| 検索の単位 | 論理行ごと（パターンは行をまたがない） | piece tree 上で連続バッファを作らずに済む |
| 大文字小文字 | smart case（パターンに大文字が無ければ区別しない）。isearch 中 M-c で切替 | Emacs と同じ |
| kill と クリップボード | kill / copy はクリップボードへ書き、yank はクリップボードから読む。連続した kill は追記 | kill ring を持たない代わり |
| 選択 | Emacs のマーク + transient mark（リージョンを強調表示）。Shift+矢印でも選択開始 | Windows 操作との共存 |
| undo の単位 | 1 コマンド = 1 単位。連続した文字入力は 20 文字まで 1 単位。1 キーに割り当てたコマンド列・キーボードマクロ 1 回の実行も 1 単位。undo でカーソル位置も戻す | 一般的なエディターと同じ感覚 |
| 保存状態の追跡 | undo で保存時点に戻ると未変更表示に戻る | |
| undo 履歴の保持 | piece の参照で持ち、テキストを複製しない。上限なし | 大きな削除でもメモリを食わない |
| 外部での変更 | 保存時に更新日時を確かめ、変わっていれば確認 | 上書き事故を防ぐ |
| 起動引数 | `geditor.exe [+LINE] [file]`。存在しないファイル名なら新規として開く | Emacs と同じ |
| 高 DPI | Per-Monitor V2 | モニター間移動で滲まない |
| 設定ファイルの監視 | 初回描画の後に別スレッドで `ReadDirectoryChangesW` を開始。通知から 200ms 待って読み、共有違反なら 3 回まで再試行 | 起動時間に含めない。保存途中の中身を読まない |
| 設定の誤り | 構文の誤り（JSONC として読めない）はファイル全体を採用せず直前の設定を保つ。意味の誤り（未知のコマンド・不正なキー表記・引数の型違い・プレフィックス衝突・未知の設定項目・不正な色）はその項目だけ無視して残りを反映する。どちらも行・桁付きで全件表示 | 1 か所の誤りで全設定が消えない |
| テスト | 自作の小さなテストハーネス + CTest | 外部依存を増やさない |

## 3. 構成

コア（Win32 の UI に依存しない編集ロジック）とプラットフォーム層（ウィンドウ・描画・入力）を分ける。コアはキー列を入力してバッファ状態を検査する形で、ウィンドウなしにテストできる。

```text
geditor/
  CMakeLists.txt
  CMakePresets.json
  CLAUDE.md                  -- "@AGENTS.md" only
  AGENTS.md                  -- build/test rules for AI agents
  README.md                  -- usage and config reference (Japanese)
  docs/PLAN.md
  src/
    core/
      piece_tree.*           -- text storage, line index
      undo.*                 -- linear undo/redo groups
      encoding.*             -- detect/decode/encode, BOM, EOL
      char_width.*           -- East Asian Width table (generated)
      key.*                  -- key event, key notation parser/printer
      keymap.*               -- keymaps, prefix keys, binding values
      jsonc.*                -- JSONC parser with line/column errors
      config.*               -- config model, defaults, merge
      commands.*             -- command registry and built-in commands
      editor.*               -- point, mark, prefix arg, macro, dispatch
      minibuffer.*           -- prompt, completion, history
      search.*               -- literal/regex search, query-replace (PCRE2)
      layout.*               -- cell-grid line breaking and position <-> cell mapping
    win/
      main.cpp               -- wWinMain, command line, startup measurement
      window.*               -- HWND, message loop, mouse, drag and drop
      input.*                -- WM_KEYDOWN/WM_CHAR -> key events
      renderer.*             -- Direct2D drawing
      text_measurer.*        -- DirectWrite measuring, font fallback
      ime.*                  -- IMM32 composition/candidate window placement
      clipboard.*
      file_io.*              -- read, safe save (ReplaceFileW), backup
      config_watcher.*
  tests/
  third_party/pcre2/         -- vendored PCRE2 source (16-bit only, JIT on)
  tools/
    gen_char_width.py        -- generates char_width table from Unicode data
    measure_startup.py       -- runs --measure-startup repeatedly, prints medians
```

## 4. 設計

### 4.1 起動を速くするための方針

- `wWinMain` からウィンドウ表示までに重い処理を置かない。静的初期化を持つグローバルを作らない
- D2D と DirectWrite のファクトリはそれぞれ 1 回だけ作る（DirectWrite は `DWRITE_FACTORY_TYPE_SHARED`）。COM の初期化はしない。代替フォントの組み立ては設定に指定があるときだけ行う
- 設定ファイルの読込は同期で行う（小さいので速い）。ファイル監視・PCRE2 の初期化は後回し（遅延）
- 計測: `geditor.exe --measure-startup` はプロセス生成時刻（`GetProcessTimes`）から初回描画の `EndDraw` 完了までの時間を標準エラーに出して終了する。`tools/measure_startup.py` がこれを複数回実行して中央値を出す。各段階の節目で検証する

### 4.2 キー入力とキー表記

キー表記は Emacs 流にする。

| 表記 | 意味 |
|---|---|
| `C-` / `M-` / `S-` | Ctrl / Alt（Meta）/ Shift |
| `C-M-a` | Ctrl+Alt+A |
| `C-S-a` | Ctrl+Shift+A（英字の Shift は `S-` で書く） |
| `C-?`, `M-<`, `M-%` | 英字以外の記号は Shift 込みで出る文字で書く |
| `RET` `TAB` `SPC` `ESC` `DEL` | Enter / Tab / Space / Esc / Backspace |
| `<delete>` `<insert>` `<home>` `<end>` `<prior>` `<next>` | Delete / Insert / Home / End / PageUp / PageDown |
| `<left>` `<right>` `<up>` `<down>` `<f1>`〜`<f24>` | 矢印・ファンクションキー |
| `C-x C-s` | 空白区切りでキー列（プレフィックスキー） |

- Ctrl か Alt を伴うキーは `WM_KEYDOWN` / `WM_SYSKEYDOWN` で受け、Ctrl・Alt を外した状態で出る文字を `ToUnicodeEx`（キーボード状態を変えないフラグ付き）で求めてキーにする。JIS 配列でも `C-?` `C-_` が正しく取れる
- 修飾なしの文字は `WM_CHAR` で受け、キーマップを引いて未割当なら文字を挿入する（`q` などもキーマップで扱える）
- 処理済みのキーに続く `WM_CHAR` / `WM_SYSCHAR` は捨てる。Alt 単独でメニューが開かないよう、`lParam` が 0 の `SC_KEYMENU` だけを握りつぶす（Alt+Space のシステムメニューは残す）。割り当てのない `M-<f4>` は既定処理（ウィンドウを閉じる）に渡す
- `ESC` に続くキーは `M-` 付きとして扱う（Emacs と同じ）
- IME の変換中（`VK_PROCESSKEY`）のキーはキーマップを引かない。確定文字列は 1 回の挿入として扱う
- AltGr のある配列（独語配列など）では Ctrl+Alt+キーが文字入力と重なるが、割り当てを優先する（日本語・英語配列では影響なし）
- プレフィックスキーの途中で 1 秒止まると、入力済みのキー（例 `C-x-`）をミニバッファに表示する

### 4.3 キーマップと割り当ての書式

キーマップは `global` / `minibuffer` / `isearch` / `query_replace` の 4 つ。設定ファイルの `keys` が `global` を、`minibuffer_keys` / `isearch_keys` / `query_replace_keys` がそれぞれを上書きする。

```jsonc
"keys": {
  "C-f": "forward-char",                        // command name
  "C-c d": ["insert-text", "2026-11-10"],       // command with arguments
  "C-c l": [["beginning-of-line"],              // command sequence
            ["insert-text", "// "],
            ["next-line"]],
  "C-z": null                                   // unbind
}
```

- 値は「文字列」「先頭がコマンド名の配列」「配列の配列」「`null`」の 4 形だけ
- コマンド列は 1 つの undo 単位として実行し、途中のコマンドが失敗したらそこで止めて理由を表示する
- 前置引数（C-u）はコマンド列全体の繰り返し回数として渡す
- `C-x` にコマンドを割り当てつつ `C-x C-s` も割り当てる、のようにプレフィックスと衝突する設定は意味の誤り（2 章「設定の誤り」）として、その項目だけ無視する
- 割り当ての解決順: 設定ファイル > Windows 互換キー > Emacs 既定
- `null` はそのキーを未割当にする。下位（Windows 互換キー・Emacs 既定）へは落ちない。押すと「`<key>` は未割当」と表示する。修飾なしの文字キーに `null` を書くと、その文字は入力されなくなる

### 4.4 既定キーバインド

Windows 互換キー（Emacs より優先）:

| キー | コマンド | 置き換わる Emacs の操作 |
|---|---|---|
| `C-M-a` | `select-all` | `beginning-of-defun` |
| `C-M-c` | `copy` | `exit-recursive-edit` |
| `C-M-x` | `cut` | `eval-defun`（Emacs Lisp モードでの割当） |
| `C-M-v` | `paste` | `scroll-other-window` |
| `C-M-z` | `undo` | なし |
| `C-M-y` | `redo` | なし |
| `C-M-s` | `save-buffer` | `isearch-forward-regexp`（`C-u C-s` で代替） |
| `C-M-w` | `quit` | `append-next-kill` |
| `C-M-f` | `isearch-forward` | `forward-sexp` |
| `C-M-h` / `C-M-r` | `query-replace` | `mark-defun` / `isearch-backward-regexp`（`C-u C-r` で代替） |
| `C-M-g` | `goto-line` | なし |

Emacs と衝突しない Windows の編集キー（Emacs でも同じ割当）:

| キー | コマンド |
|---|---|
| `<left>` / `<right>` | `backward-char` / `forward-char` |
| `<up>` / `<down>` | `previous-line` / `next-line` |
| `<home>` / `<end>` | `beginning-of-line` / `end-of-line` |
| `<prior>` / `<next>` | `scroll-down-command` / `scroll-up-command` |
| `C-<left>` / `C-<right>` | `backward-word` / `forward-word` |
| `C-<home>` / `C-<end>` | `beginning-of-buffer` / `end-of-buffer` |
| `<delete>` / `DEL` | `delete-char` / `delete-backward-char` |

Shift を足した移動キー（`S-<left>`、`C-S-<right>`、`S-<end>` など上表の移動キーすべて）は、同じ移動コマンドを選択の拡張つきで実行する（Emacs の shift-select と同じ。マークが無ければ移動前の位置に一時的なマークを置き、Shift なしの移動でそのマークを外す）。設定ファイルで `S-` 付きのキーに別のコマンドを割り当てた場合はそちらを優先する。

Emacs 互換（global）:

| 分類 | キーとコマンド |
|---|---|
| 移動 | `C-f` forward-char, `C-b` backward-char, `C-n` next-line, `C-p` previous-line, `C-a` beginning-of-line, `C-e` end-of-line, `M-f` forward-word, `M-b` backward-word, `M-<` beginning-of-buffer, `M->` end-of-buffer, `C-v` scroll-up-command, `M-v` scroll-down-command, `C-l` recenter-top-bottom, `M-g g` / `M-g M-g` goto-line |
| マーク | `C-SPC` set-mark-command, `C-x C-x` exchange-point-and-mark, `C-x h` mark-whole-buffer, `C-g` keyboard-quit |
| 削除・kill | `C-d` / `<delete>` delete-char, `DEL` delete-backward-char, `C-k` kill-line, `M-d` kill-word, `M-DEL` backward-kill-word, `C-w` kill-region, `M-w` kill-ring-save, `C-y` yank |
| 入力 | `RET` / `C-j` newline, `C-o` open-line, `TAB` insert-tab, `C-q` quoted-insert, `C-t` transpose-chars, `M-u` upcase-word, `M-l` downcase-word, `M-c` capitalize-word |
| undo | `C-/` / `C-_` / `C-x u` undo, `C-?` / `C-M-_` redo |
| 検索・置換 | `C-s` isearch-forward, `C-r` isearch-backward（`C-u` 付きで正規表現）, `M-%` query-replace, `C-M-%` query-replace-regexp |
| ファイル | `C-x C-f` find-file, `C-x C-s` save-buffer, `C-x C-w` write-file, `C-x C-q` read-only-mode, `C-x C-c` quit |
| 文字コード | `C-x RET f` set-buffer-file-coding-system（`utf-8` / `utf-8-bom` / `utf-16le` / `utf-16le-bom` / `utf-16be` / `utf-16be-bom` / `shift_jis`）, `C-x RET e` set-eol（`lf` / `crlf` / `cr`。geditor 独自の割当） |
| 表示 | `C-x C-+` / `C-x C-=` text-scale-increase, `C-x C--` text-scale-decrease, `C-x C-0` text-scale-reset（Emacs では 3 キーとも text-scale-adjust。geditor は動作ごとにコマンドを分ける） |
| 前置引数 | `C-u` universal-argument, `M-0`〜`M-9` digit-argument, `M--` negative-argument |
| マクロ | `C-x (` / `<f3>` kmacro-start-macro, `C-x )` / `<f4>`（記録中）kmacro-end-macro, `C-x e` / `<f4>` kmacro-end-and-call-macro（続けて `e` で繰り返し） |
| その他 | `M-x` execute-extended-command |

isearch 中: `C-s` / `C-r` 次・前の一致、`DEL` 1 文字戻す、`RET` 確定、`C-g` 取消（開始位置へ戻る）、`M-r` 正規表現の切替、`M-c` 大文字小文字の区別の切替、`M-%` その語で問い合わせ置換へ。

問い合わせ置換中: `y` / `SPC` 置換、`n` / `DEL` 飛ばす、`!` 残りを全部置換、`.` 置換して終了、`q` / `RET` 終了。置換全体を 1 つの undo 単位にする。正規表現の置換文字列は PCRE2 の `$1` 形式。

ミニバッファ: `C-a` `C-e` `C-f` `C-b` `C-k` `DEL` `C-d` などの編集キーは global と同じ。`RET` 確定、`C-g` 取消、`TAB` 補完、`M-p` / `M-n` 履歴。

### 4.5 設定ファイル

```jsonc
// %USERPROFILE%\.geditor.json
{
  "font": {
    "family": "Consolas",
    "size": 12,                                  // points
    "fallback": ["BIZ UDGothic", "Segoe UI Emoji"]
  },
  "colors": {
    "background": "#1e1e1e", "foreground": "#d4d4d4",
    "cursor": "#ffffff", "selection": "#264f78",
    "line_number": "#858585", "line_number_current": "#c6c6c6",
    "search_match": "#613214", "search_current": "#9e6a03",
    "whitespace": "#404040",
    "mode_line_background": "#007acc", "mode_line_foreground": "#ffffff"
  },
  "tab_width": 4,
  "line_numbers": true,
  "wrap": { "mode": "max", "column": 80 },       // none | window | fixed | max
  "whitespace": { "space": false, "tab": true, "fullwidth_space": true, "trailing": true, "eol": false },
  "new_file": { "encoding": "utf-8", "eol": "lf" },
  "backup": true,
  "search": { "case_fold": "smart" },            // smart | always | never
  "keys": { },
  "minibuffer_keys": { },
  "isearch_keys": { },
  "query_replace_keys": { }
}
```

- 書かなかった項目は既定値のまま。`keys` などは既定のキーマップへの差分として重ねる
- 折り返しの意味: `none` は折り返さず横スクロール、`window` はウィンドウ幅、`fixed` は `column` 桁、`max` はウィンドウ幅と `column` 桁の狭い方。表示上の折り返しで、ファイルには改行を入れない。桁は 2 章の桁の数え方（全角 2 桁）で数え、ウィンドウ幅は「文字領域の幅 ÷ 升目の幅」を切り捨てた桁数とする
- 等幅でないフォントを指定しても升目配置で描くため、文字間が不揃いに見える
- 配色は `#rrggbb`。未知のキーは警告して無視する
- 設定を反映するとき、起動中の編集内容・undo 履歴は保つ

### 4.6 画面構成

```text
+---------------------------------------------+
|  1 | text ...                               |
|  2 | text ...                               |
| .. |                                        |
+---------------------------------------------+
| -** file.txt  UTF-8 LF  L12 C5  wrap:max  Def |   <- mode line
+---------------------------------------------+
| C-x-                                         |   <- minibuffer / echo area
+---------------------------------------------+
```

mode line: 変更有無（`**`）・読取専用（`%%`）・ファイル名・文字コードと BOM・改行コード・行と桁・折り返しモード・マクロ記録中（`Def`）。文字領域の右端に Win32 標準の縦スクロールバーを置く（図では省略）。

### 4.7 描画

- 升目配置: 各文字の升目数（1 か 2）を桁の数え方で決め、折り返し位置・カーソル位置・マウス位置はすべて升目の整数計算で求める。`IDWriteTextLayout` による文字組みと折り返しは使わない
- 文字の形は `IDWriteFontFallback::MapCharacters` で使うフォントを決め、`IDWriteTextAnalyzer` でグリフに変換し、送り幅を「升目数 × 升目の幅」に置き換えた `DrawGlyphRun` で描く。升目より広いグリフは升目の幅に収まるよう横に縮める
- 代替フォントは `IDWriteFontFallbackBuilder` で組む
- 折り返しは表示範囲の論理行だけ計算する。長い論理行（数 MB の 1 行など）は、折り返し位置を 64K 文字ごとの区切りで記録して再利用し、スクロールのたびに行頭から数え直さない。スクロールバーの位置は論理行ベースの近似で出す
- 選択範囲・検索一致（表示範囲の全一致）・カーソル・空白記号（半角空白 `·`、タブ `→`、全角空白 `□`、行末空白の背景、改行 `↵`）を重ねて描く
- カラー絵文字は初版では単色で描く
- IME の変換ウィンドウと候補ウィンドウはカーソル位置に置き、フォントも合わせる（`ImmSetCompositionWindow` / `ImmSetCandidateWindow` / `ImmSetCompositionFont`）

### 4.8 保存

1. 同じフォルダに一時ファイルを書き、`FlushFileBuffers`
2. `ReplaceFileW` で置き換える。`backup` が ON でこのセッション初回の保存なら、第 3 引数で元ファイルを `file~` として残す
3. 新規ファイル（置き換え先が無い）は `MoveFileExW` で移す
4. 保存前に、読み込み時から更新日時が変わっていないか確かめ、変わっていれば確認する

## 5. 段階計画

各段階の終わりに、その段階の受入条件を独立した検証役が確かめてから次へ進む。

### M0: 土台

- CMake / Ninja / clang-cl のビルド、`CMakePresets.json`（debug / release）、`AGENTS.md` と `CLAUDE.md`（`@AGENTS.md`）
- 空のウィンドウを開き、Direct2D で固定文字列を描く。`--measure-startup` を実装
- テストハーネスと CTest の配線、PCRE2 のソース同梱とビルド
- 受入条件
  - [M0-1] `cmake --preset release` と `cmake --build` が警告なしで通る
  - [M0-2] `ctest` が通る（ハーネス自体のテスト 1 件以上）
  - [M0-3] `--measure-startup` を 10 回測った中央値が 100ms 以下（2 回目以降。手元の PC で測る）
  - [M0-4] exe 単体で動き、VC++ ランタイム DLL に依存しない（`llvm-readobj --coff-imports` か `dumpbin /dependents` で確認）

### M1: 編集の核

- piece tree、カーソル移動、挿入・削除、undo/redo、マーク/リージョン、kill/yank とクリップボード
- 固定の Emacs キーマップ（設定ファイルはまだ読まない）とキー入力変換（4.2）
- mode line、ミニバッファ（Tab 補完付き）、C-x C-f / C-x C-s / C-x C-w（UTF-8 のみ）、未保存時の確認
- IME 入力
- 受入条件
  - [M1-1] piece tree の単体テスト（挿入・削除・行番号↔位置の変換・ランダム編集を素朴な文字列実装と突き合わせる 1 万操作）が通る
  - [M1-2] キー列を流してバッファ状態を検査するテストで、4.4 の移動・削除・kill/yank・undo/redo の各コマンドに最低 1 件のテストがある
  - [M1-3] undo の単位（連続入力 20 文字・コマンド列・カーソル位置の復元・保存時点で未変更に戻る）をテストで確かめる
  - [M1-4] 日本語 IME で入力・確定でき、変換ウィンドウがカーソル位置に出る（手動確認）
  - [M1-5] 100MB の UTF-8 ファイルで、1 文字入力から再描画完了まで 16ms 以下（計測コマンドで測る）

### M2: キーバインド設定

- JSONC パーサー、`%USERPROFILE%\.geditor.json` の読込と既定値への重ね合わせ
- キー表記のパーサー、4 形の割り当て、割当解除、プレフィックス衝突の検出、4 つのキーマップ
- Windows 互換キー（4.4）、保存検知での再読込、エラー表示、M-x
- 前置引数（C-u、C-u C-u、数字、負数）。キー解決の一部なのでここで作る
- 後の段階で作るコマンド（検索・置換・goto-line・マクロなど）は、名前だけ登録した「未実装」コマンドとして置き、実行すると未実装と表示する
- 受入条件
  - [M2-1] JSONC パーサーがコメント・末尾カンマを受け付け、構文誤りの行・桁を正しく返す（正常・境界・異常のテスト）
  - [M2-2] 4.2 の表の全表記を解析・再出力して一致するテストがある
  - [M2-3] 4 形の割り当て、`null` の解除（下位へ落ちないこと）、プレフィックス衝突と未知コマンドの項目単位の無視、構文誤りでの全体不採用をテストで確かめる
  - [M2-4] 4.4 の Windows 互換キー 12 個すべてが既定で期待するコマンド名に解決され、対応する `C-M-` の Emacs 操作より優先されることをテストで確かめる（コマンドの動作は各段階で確かめる）
  - [M2-5] 起動中に設定ファイルを保存すると 1 秒以内に反映され、構文誤りなら直前の設定が残る（手動確認）
  - [M2-6] 前置引数が移動・削除・挿入・コマンド列に効くことをテストで確かめる

### M3: 表示

- フォント（名前・サイズ・代替）、配色、行番号、折り返し 4 モード、タブ幅、空白の可視化、text-scale
- Per-Monitor V2、マウス（クリック・ドラッグ選択・ダブルクリックで単語選択・Shift+クリック・ホイール）、ドラッグ＆ドロップ
- 受入条件
  - [M3-1] 折り返し 4 モードの改行位置と、位置↔升目の変換を `layout` の単体テストで確かめる（全角混じり・タブ・幅ちょうどの境界・全角が行末に 1 升だけ残る場合を含む）
  - [M3-2] 設定の各表示項目を変えると画面に反映される（手動確認。項目ごとにチェック表を作る）
  - [M3-3] 英字は Consolas、日本語は代替フォントで描かれる（手動確認）
  - [M3-4] 起動時間が M0 の基準を保つ

### M4: 文字コードと保存

- 判定（BOM → UTF-16 の NUL 偏り → UTF-8 の妥当性 → CP932）、変換、改行の判定と変換
- 文字コード・BOM・改行の変更コマンド、安全な保存、`file~` バックアップ、外部変更の確認、読取専用
- 受入条件
  - [M4-1] 7 種の文字コード × 3 種の改行の組で、読込→保存がバイト単位で元と一致するテスト
  - [M4-2] 判定のテスト: 各 BOM、BOM なし UTF-16、ASCII のみ（UTF-8 と判定）、改行の無いファイル（`new_file.eol` を採用）、Shift_JIS の短い文（UTF-8 と誤判定しやすい例を含む）、壊れたバイト列
  - [M4-3] Shift_JIS で表せない文字を含むと保存が止まり、ファイルが変わらないテスト
  - [M4-4] 保存中に書き込みが失敗しても元ファイルが残ること、`file~` がセッション初回だけ作られることのテスト
  - [M4-5] 100MB の UTF-8 ファイル（多数行のものと、改行の無い 1 行だけのもの）を 1 秒以内に開いて表示でき、1 行のものでも `window` 折り返しで末尾までスクロールしたときの 1 画面分の再描画が 16ms 以下

### M5: 検索と置換

- isearch（通常・正規表現）、一致の強調表示、問い合わせ置換（通常・正規表現）、goto-line
- 受入条件
  - [M5-1] isearch の各キー（4.4 の「isearch 中」の一覧）に最低 1 件のテスト。`C-u C-s` / `C-u C-r` が正規表現検索になることも確かめる
  - [M5-2] 問い合わせ置換の各キーに最低 1 件のテスト。置換全体が 1 回の undo で戻る
  - [M5-3] 正規表現: サロゲートペア・全角・`$1` 置換・不正なパターンのエラー表示をテストで確かめる
  - [M5-4] smart case の 3 設定をテストで確かめる
  - [M5-5] 100MB ファイルで、末尾付近の語の検索が 1 秒以内に見つかる
  - [M5-6] Windows 互換キー `C-M-f` / `C-M-h` / `C-M-r` / `C-M-g` が検索・置換・goto-line として動くことをテストで確かめる

### M6: Emacs の拡張機能と仕上げ

- キーボードマクロ、連続 kill の追記、Shift 選択の仕上げ
- README.md（使い方・設定項目・コマンド一覧・既定キー表）
- 受入条件
  - [M6-1] 連続 kill の追記と Shift 選択（4.4 の Shift 付き移動キー）をテストで確かめる
  - [M6-2] マクロの記録→実行、`C-u 3 C-x e`、記録中の C-g、マクロ内のミニバッファ入力をテストで確かめる
  - [M6-3] README のコマンド一覧と、コマンド登録表の全コマンドが一致する（テストで突き合わせる）
  - [M6-4] M0〜M5 の性能基準を保つ

## 6. リスクと対策

| リスク | 対策 |
|---|---|
| DirectWrite / Direct2D の初期化で 100ms を超える | M0 で最初に測る。超えたらファクトリ生成の順序見直し、フォント列挙の回避、`DXGI` スワップチェーンへの切替を試す |
| キー変換が配列（JIS / US）で食い違う | `ToUnicodeEx` で求める方式にし、JIS と US の両配列で 4.2 の表を手動確認する |
| Ctrl+Alt がアプリ外のショートカット（IME やユーティリティ）と重なる | 設定で解除・付け替えできるので既定はそのまま。重なりが見つかれば README に書く |
| 行をまたぐ検索ができない | 初版の制約として README に書く。要望があれば連続バッファのキャッシュ方式を検討 |
| 100MB で UTF-16 化するとメモリが倍（約 200MB）になる | 許容する。1GB 級は対象外 |
| 升目配置で代替フォントの全角グリフが 2 升に合わない | 升目に収まるよう縮めて描く。M3-3 の手動確認で見た目を確かめる |
| 数 MB の 1 行のファイルでスクロールが固まる | 4.7 の折り返し位置の区切り記録で対処し、M4-5 で 100MB の 1 行ファイルを測る |

## 7. 進め方

- 段階ごとに計画（受入条件・対象範囲）を確認してから実装し、独立した検証役の OK を得てから次へ進む
- commit はユーザーの判断で行う
