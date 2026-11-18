# geditor

Windows 専用の、起動の速いテキストエディターです。既定のキーバインドは Emacs で、キーバインド・フォント・配色は設定ファイル（JSONC）で変えられます。C++20 と Win32 API・Direct2D・DirectWrite で書いていて、GUI フレームワークは使っていません。

計画した段階 M0〜M6 をすべて実装しました。要件・設計・段階計画は [docs/PLAN.md](docs/PLAN.md) にあります。

## できること

- Emacs のキーバインド（kill/yank は Windows のクリップボードと連携し、続けた kill はつなげてクリップボードに入る）。Windows の Ctrl+英字の操作は Ctrl+Shift+英字（`C-S-z` で undo、`C-S-s` で保存など）
- Shift+矢印などで選択（Emacs の shift-select と同じ）
- M-x（コマンド名を Tab で補完）、前置引数 `M-0`〜`M-9`、undo / redo、キーボードマクロ（`C-x (` / `C-x )` / `C-x e`、`<f3>` / `<f4>`、`C-(` / `C-)` / `C-S-e`）
- インクリメンタル検索（`C-s` / `C-r`、正規表現は `C-M-s` / `C-M-r`）、問い合わせ置換（`M-%` / `C-M-%`）、一致の強調表示、行番号を指定して移動（`M-g g`）。正規表現は PCRE2
- 文字コードの自動判定（UTF-8 / UTF-16LE / UTF-16BE / Shift_JIS、BOM の有無）と、EUC-JP・ISO-2022-JP を含む 9 種を指定して開く・開き直す・保存する
- 改行コード（LF / CRLF / CR）を行ごとに保持し、行末に記号で表示
- 一時ファイルを使った安全な保存と、Emacs 風のバックアップ `file~`
- 100MB のファイルの編集、書記素クラスタ単位のカーソル移動、カラー絵文字、日本語 IME
- 行番号・折り返し（なし / ウィンドウ幅 / 固定桁 / 最大幅）・空白の可視化・文字の拡大縮小
- メニューバー（File / Edit / View）・右クリックメニュー・マウス
- 設定ファイルは保存すると自動で読み直し、誤りは行・桁付きで表示

## ビルド

必要なもの: Windows 10 以降、LLVM（clang-cl / lld-link）、CMake 3.25 以降、Ninja、Visual Studio の C++ ビルドツールと Windows SDK（clang-cl が自動で見つけるので、開発者プロンプトは不要）。

```powershell
cmake --preset release
cmake --build --preset release
ctest --preset release
```

`build/release/geditor.exe` ができます。

## 使い方

```powershell
geditor.exe [--encoding=NAME] [--settings=PATH] [+LINE] [file]
```

- 1 ウィンドウで 1 ファイルを開きます。別のファイルは別のウィンドウ（プロセス）で開きます。`C-x C-f` やドラッグ＆ドロップで別のファイルを開くときは、このウィンドウで開き直すか新しいウィンドウで開くかを尋ねます
- 存在しないファイル名を渡すと、新しいファイルとして開きます（保存したときに作られます）
- `+LINE` で指定の行へ移ります
- `--encoding=NAME` は判定の代わりに文字コードを指定します（`utf-8` / `utf-8-bom` / `utf-16le` / `utf-16le-bom` / `utf-16be` / `utf-16be-bom` / `shift_jis` / `euc-jp` / `iso-2022-jp`）。ファイルを渡さないときは、新しいバッファをその文字コードで保存します。ほかの名前はエコー領域（いちばん下の行）に `Unknown encoding` と出します
- `--settings=PATH` は設定ファイルの場所を変えます

### キーの書き方

この文書のキーは Emacs の書き方です。`C-` は Ctrl、`M-` は Alt（または Esc を押してから次のキー）、`S-` は Shift です。`C-x C-s` は「Ctrl+X を押して離し、続けて Ctrl+S」です。`RET` は Enter、`DEL` は Backspace、`SPC` は Space、`<delete>` `<home>` `<prior>` `<next>` は Delete / Home / PageUp / PageDown です。Shift を押して打つ記号は、その記号で書きます（`S-` は付けません）。`C-(` は Ctrl を押しながら `(` を打つキーで、日本語配列のキーボードでは Ctrl+Shift+8 です（`C-S-8` とは書けません）。どのキーが `(` になるかはキーボードの配列で決まります。

### よく使う操作

- 開く `C-x C-f`、保存 `C-x C-s`（または `C-S-s`）、別名で保存 `C-x C-w`、終了 `C-x C-c`（未保存なら保存するか尋ねます）
- 取り消し `C-/`（または `C-S-z`）、やり直し `C-?`（または `C-S-y`）
- 検索 `C-s`（後ろ向きは `C-r`、正規表現は `C-M-s` / `C-M-r`）、置換 `M-%`、行へ移動 `M-g g`
- `C-h` は `DEL`（Backspace）と同じく前の 1 文字を消します。`C-u` はカーソルから行の先頭までを kill します（Bash と同じ）。isearch の中の `C-u` は検索語を消し、問い合わせ置換の中の `C-u` は何もしません
- 前置引数（繰り返しの回数）は `M-3` のように Alt+数字で打ちます。続けて数字も打てます（`M-1 2` は 12）
- コマンドを名前で実行 `M-x`（Tab で補完）
- メニューは F10 かマウスで開きます

### 選択と kill

- `C-SPC` で今の位置に印（マーク）を置き、カーソルを動かすと、その間が選択範囲（リージョン）になります。`C-w` で切り取り、`M-w` でコピー、`C-y` で貼り付けます。Windows のクリップボードを使います
- Shift を押しながら移動キー（`S-<left>`、`C-S-<right>`、`S-<end>`、`S-<next>` など、下のキー表の「Windows の編集キー」の移動）を押すと、移動しながら選択します。Shift を離して移動すると選択は消えます。`C-SPC` で選択していたときは、Shift 付きの移動はその選択を広げます。`C-S-b` のように、Shift を付けても割り当ての無い Emacs の移動キーも同じく選択します（`C-S-a` / `C-S-f` などの Windows 互換キーと、マクロを実行する `C-S-e` は除く。行末までの選択は `S-<end>`）。設定で `S-` 付きのキーに別のコマンドを割り当てると、そちらが動きます
- `C-k`（行の残りを kill）、`C-u`（行の先頭までを kill）、`M-d`（単語を kill）、`M-DEL`（前の単語を kill）、`C-w` を続けて使うと、kill した文字列をつなげてクリップボードに入れます（`C-u` と `M-DEL` は前に足します）。間にほかのコマンド（`M-2` などの前置引数を除く）やマウスのクリックがあると、新しく入れ直します。続けている間に別のアプリがクリップボードを書き換えても気づかず、つなげた文字列で上書きします
- Emacs の kill ring と `M-y` はありません。クリップボードの中身は 1 つです

### キーボードマクロ

- `C-x (`・`<f3>`・`C-(` のどれかで記録を始め、`C-x )`・`<f4>`・`C-)` のどれかで終えます。記録中はモードラインに `Def` が出ます
- `C-x e`・`<f4>`・`C-S-e` のどれかで最後に記録したマクロを実行します。続けて `e` を押すと、もう 1 回実行します（ほかのキー・クリック・ホイール・スクロールバー・メニュー・ファイルのドロップで終わります。実行の後に検索やミニバッファが開いたままのときは、`e` はその入力になります）。`M-3 C-x e` は 3 回実行します
- `C-(` / `C-)` / `C-S-e` は 1 回の押下で済むキーで、働きは `C-x (` / `C-x )` / `C-x e` と同じです（検索・置換・ミニバッファの中でも同じ。ただし設定の `isearch_keys` で `C-e` に割り当てがあると、検索の中の `C-S-e` はその割り当てを動かします）。`C-(` と `C-)` は Ctrl を押しながら `(` と `)` を打ちます（日本語配列のキーボードでは Ctrl+Shift+8 と Ctrl+Shift+9）。`C-S-e` は Ctrl+Shift+E です。このキーに割り当てがあるので、Shift を押したままの `C-e` は行末までの選択になりません（`S-<end>` を使います）。設定で `"C-S-e": null` と書くと `C-S-e` は何もしないキーになり、行末までの選択には戻りません
- 実行中に `C-g` を押すと止まります。そこまでの分は 1 回の `C-/` で戻ります。実行中に `C-g` より前に打ったキーとマウスの操作は `C-g` と一緒に捨て、`C-g` より後のものは実行が止まった後に働きます。`C-g` を押さなければ、実行中に打ったキーとマウスの操作は実行が終わった後に来た順に働きます。そのうちの 1 つが確認やメニューを開いた（右クリックなど）ときは、それより後のものは捨てます。キー、マウスの動き・クリック・ホイール（スクロールバーやメニューバーの上での動きを含む）、タッチとペンの操作より後ろの `C-g` は効きます。実行中のスクロールバー・メニューバー・タイトルバー・枠のクリックは捨て（実行の後にも働きません）、その後ろの `C-g` も効きます。それ以外の待っているもの（ファイルのドロップ、メニューの項目の選択、ウィンドウを閉じる操作など）や、日本語入力（IME）が受け取ったキーより後ろの `C-g` は、実行が終わるまで効きません
- 記録中に `C-x e`（`<f4>`・`C-S-e` も同じ）を押すと、記録を終えるだけで実行はしません
- 記録するのはキー（ミニバッファ・検索・置換の中で打ったキーと IME で確定した文字列を含む）で、マウスとメニューの操作は記録しません
- 1 回の実行（`M-3` の 3 回分を含む）は 1 回の `C-/` で元に戻ります
- 実行の途中でコマンドが失敗する（バッファの端で止まる、検索で見つからない、`Buffer is read-only` など）か、確認に No や Cancel で答えると、そこで止まり、残りの回数も実行しません
- マクロの中で続けた kill は、つなげた文字列を実行の終わり（または kill でないコマンドの前）にまとめてクリップボードに入れます
- 記録中に `C-g` を押すと記録を取りやめ、前のマクロが残ります
- マクロはウィンドウを閉じると消えます。設定ファイルには保存しません

### Windows 互換キーと Shift

Windows の Ctrl+英字の操作は Ctrl+Shift+英字に割り当てています（`C-S-x` が切り取り、`C-S-s` が保存など、下の表）。このため Shift を押したまま `C-x C-s` と打つと、`C-S-x`（切り取り）と `C-S-s`（保存）の 2 つの操作になります。Emacs のキー列は Shift を離して打ってください。

### 制限

- 1 ウィンドウに 1 ファイル。画面の分割・タブ・複数バッファはありません
- 検索は 1 行の中だけです（行をまたぐ正規表現は当たりません）
- 後ろ向きの検索（`C-r` / `C-M-r`）は、探す範囲の終わりをテキストの終わりとして照合します。範囲の終わりは、一致がまだ無いときは探し始めた位置、`C-r` を重ねたときは今の一致の先頭です（一致があるときに検索語を伸ばすと、終わりはテキストの終わりです）。重ねた `C-r` が探す次の一致は、今の一致の先頭（UTF-16 の単位で数えた位置）より前から始まります。一致はその位置を越えて伸びません（Emacs の後ろ向きの正規表現検索と同じです）。このため前向きの検索や一致の強調と違うことがあります: `.*` などの一致は範囲の終わりで切れ、同じ場所の強調より短くなることがあります。`$` はその位置では当たりません（本当の行末では当たります）が、`\z` / `\Z`・先読み（`(?=...)` など）・`\b` はその位置をテキストの終わりとして扱います
- 重ねた `C-s` / `C-r`（isearch の中の `C-M-s` / `C-M-r` も同じ）は、カーソルが動く一致だけに移ります。強調とカーソルは書記素クラスタの境目に置くので、前向きは表示する一致の終わり（カーソルの位置）が今の一致の終わりより後ろの一致、後ろ向きは表示する一致の先頭（カーソルの位置）が今の一致の先頭より前の一致です。そうでない一致（複数の単位からなるクラスタ（結合文字・ZWJ でつないだ絵文字・国旗など）の中で終わる・始まるもの）は飛ばし、残っていなければ `Failing` になります。たとえば `a`・ZWJ でつないだ家族の絵文字・`b` の行で `C-M-s .` の後に `C-s` を押すと、カーソルは絵文字の後ろ、`b` の後ろと移り、3 回目で `Failing` になります（後ろ向きも同じく 3 回目で `Failing`）。飛ばした一致がいくつあっても、`DEL` は 1 回の押下を取り消します
- EUC-JP と ISO-2022-JP は自動では判定しません（`--encoding` か `C-x RET c` で指定します）

## 設定

設定ファイルは `%USERPROFILE%\.geditor.json`（`--settings` で変更可）です。`M-x find-user-init-file`（File > Settings）で開くと、ファイルが無いときは書き方の例が入った新しいバッファになります。保存すると自動で読み直します。書き方の誤りはエコー領域に行と桁を付けて出し、構文の誤りならファイル全体、そうでなければその項目だけを使いません。例:

```jsonc
{
  "font": { "family": "Consolas", "size": 12, "fallback": ["BIZ UDGothic", "Segoe UI Emoji"] },
  "tab_width": 4,
  "line_numbers": true,
  "wrap": { "mode": "window" },                  // none | window | fixed | max
  "keys": {
    "C-c d": ["insert-text", "2026-11-10"],       // command with arguments
    "C-z": null                                   // unbind
  }
}
```

### 設定項目

書かなかった項目は既定値のままです。

| 項目 | 既定値 | 内容 |
|---|---|---|
| `font.family` | `"Consolas"` | 文字のフォント名。入っていないフォントは `Font not found` と出して Consolas を使います |
| `font.size` | `12` | 文字の大きさ（ポイント、1〜200） |
| `font.fallback` | なし | 主フォントに無い文字（日本語・絵文字など）に使うフォント名の配列。書かなければ Windows の代替フォントを使います |
| `colors.background` | `"#1e1e1e"` | 背景の色（色はどれも `#rrggbb`） |
| `colors.foreground` | `"#d4d4d4"` | 文字の色 |
| `colors.cursor` | `"#ffffff"` | カーソルの色 |
| `colors.selection` | `"#264f78"` | 選択範囲の背景 |
| `colors.line_number` | `"#858585"` | 行番号の色 |
| `colors.line_number_current` | `"#c6c6c6"` | カーソルのある行の行番号の色 |
| `colors.search_match` | `"#613214"` | 検索で一致した所の背景 |
| `colors.search_current` | `"#9e6a03"` | 検索の今の一致の背景 |
| `colors.whitespace` | `"#404040"` | 空白の記号の色 |
| `colors.eol` | `"#505050"` | 改行の記号の色 |
| `colors.eol_other` | `"#d7ba7d"` | ファイルの既定と違う改行の記号の色 |
| `colors.mode_line_background` | `"#007acc"` | モードライン（下から 2 行目）の背景 |
| `colors.mode_line_foreground` | `"#ffffff"` | モードラインの文字の色 |
| `tab_width` | `4` | タブの幅（1〜32） |
| `line_numbers` | `true` | 行番号を出す |
| `menu_bar` | `true` | メニューバーを出す。`false` でも右クリックのメニューは出ます |
| `wrap.mode` | `"max"` | 折り返し。`none`（しない）、`window`（ウィンドウの幅）、`fixed`（`wrap.column` の桁）、`max`（ウィンドウの幅と `wrap.column` の狭い方） |
| `wrap.column` | `80` | `fixed` と `max` の桁数（1〜10000。全角は 2 桁） |
| `whitespace.space` | `false` | 半角空白を `·` で示す |
| `whitespace.tab` | `true` | タブを `→` で示す |
| `whitespace.fullwidth_space` | `true` | 全角空白を `□` で示す |
| `whitespace.trailing` | `true` | 行末の空白に背景を付ける |
| `eol_marks.show` | `true` | 行末に改行の記号を出す |
| `eol_marks.lf` | `"↓"` | LF の記号（1 文字） |
| `eol_marks.crlf` | `"↵"` | CRLF の記号（1 文字） |
| `eol_marks.cr` | `"←"` | CR の記号（1 文字） |
| `new_file.encoding` | `"utf-8"` | 新しいファイルの文字コード（`--encoding` の 9 種） |
| `new_file.eol` | `"lf"` | 新しいファイルと改行の無いファイルの改行（`lf` / `crlf` / `cr`） |
| `backup` | `true` | そのウィンドウで初めて保存するとき、元のファイルを `file~` として残す |
| `search.case_fold` | `"smart"` | 検索の大文字小文字。`smart`（検索語に大文字があれば区別する）、`always`（区別しない）、`never`（区別する） |
| `keys` | `{}` | 通常のキーの割り当て（下） |
| `minibuffer_keys` | `{}` | ミニバッファの中のキーの割り当て |
| `isearch_keys` | `{}` | isearch（`C-s` / `C-r`）の中のキーの割り当て |
| `query_replace_keys` | `{}` | 問い合わせ置換の中のキーの割り当て |

### キーの割り当て

`keys` などには、キーと値の組を書きます。値は次の 4 つの形です。

- コマンド名: `"C-f": "forward-char"`
- 引数付きのコマンド: `"C-c d": ["insert-text", "2026-11-10"]`
- コマンドの列（1 回の `C-/` でまとめて戻ります）: `"C-c l": [["beginning-of-line"], ["insert-text", "// "], ["next-line"]]`
- `null`: そのキーを何もしないキーにします（既定の割り当ても消えます）

設定のキーは、Windows 互換キーと Emacs のキーより優先します。既定でコマンドを割り当てたキーをプレフィックスにする割り当て（`"C-h x"` など）は、`"C-h x": "C-h" runs a command; binding ignored` と出して使いません。その前に `"C-h": null` を書くと使えます（後に書いたのでは間に合いません）。

## コマンド一覧

`M-x` の後にコマンド名を入れると実行できます（Tab で補完します）。引数の要るコマンドは、ミニバッファで引数を尋ねます。キーは下の「既定のキー」の表にあります。

| コマンド | 内容 |
|---|---|
| `backward-char` | 1 文字戻る |
| `backward-kill-line` | カーソルから行の先頭までを kill する（Bash の `C-u`。続けると前につなげる。行の先頭では何もしない） |
| `backward-kill-word` | 前の単語を kill する（続けると前につなげる） |
| `backward-word` | 前の単語の先頭へ |
| `beginning-of-buffer` | バッファの先頭へ（元の位置にマークを置く） |
| `beginning-of-line` | 行の先頭へ |
| `capitalize-word` | 次の単語の先頭を大文字、残りを小文字にする |
| `copy` | 選択範囲をクリップボードへコピーする（`kill-ring-save` と同じ） |
| `cut` | 選択範囲を切り取る（`kill-region` と同じ） |
| `delete-backward-char` | 前の 1 文字を消す（選択中は選択範囲を消す） |
| `delete-char` | 次の 1 文字を消す（選択中は選択範囲を消す） |
| `digit-argument` | 前置引数の数字（`M-0`〜`M-9`） |
| `downcase-word` | 次の単語を小文字にする |
| `end-of-buffer` | バッファの末尾へ（元の位置にマークを置く） |
| `end-of-line` | 行の末尾へ |
| `exchange-point-and-mark` | カーソルとマークを入れ替え、選択範囲を出す |
| `execute-extended-command` | コマンドを名前で実行する（`M-x`） |
| `exit-minibuffer` | ミニバッファの入力を確定する |
| `find-file` | ファイルを開く |
| `find-file-with-encoding` | 文字コードを指定してファイルを開く（ファイル名、文字コード名の順に尋ねる） |
| `find-user-init-file` | 設定ファイルを開く |
| `forward-char` | 1 文字進む |
| `forward-word` | 次の単語の末尾へ |
| `goto-line` | 指定の行へ移る（前置引数があればその行へ） |
| `ignore` | 何もしない（問い合わせ置換の中の `C-u`） |
| `insert-tab` | タブ文字を入れる |
| `insert-text` | 引数の文字列を入れる |
| `isearch-abort` | isearch をやめ、始めた位置へ戻る（isearch の中の `C-g`） |
| `isearch-backward` | 後ろ向きの isearch（前置引数があれば正規表現） |
| `isearch-backward-regexp` | 後ろ向きの正規表現の isearch |
| `isearch-delete-char` | isearch の最後の入力を取り消す |
| `isearch-delete-pattern` | isearch の検索語を消し、カーソルを isearch を始めた位置へ戻す（isearch は続く。`DEL` で戻せる） |
| `isearch-exit` | 今の一致で isearch を終える |
| `isearch-forward` | 前向きの isearch（前置引数があれば正規表現） |
| `isearch-forward-regexp` | 前向きの正規表現の isearch |
| `isearch-query-replace` | isearch の検索語で問い合わせ置換を始める |
| `isearch-repeat-backward` | 前の一致へ |
| `isearch-repeat-forward` | 次の一致へ |
| `isearch-toggle-case-fold` | 大文字小文字を区別するかを切り替える |
| `isearch-toggle-regexp` | 正規表現かどうかを切り替える |
| `keyboard-escape-quit` | ミニバッファを抜け、選択範囲を消す |
| `keyboard-quit` | 入力中の操作をやめ、選択範囲を消す |
| `kill-line` | 行の残りを kill する（行末では改行を。続けるとつなげる） |
| `kill-region` | 選択範囲を kill する（続けるとつなげる） |
| `kill-ring-save` | 選択範囲をクリップボードへコピーする |
| `kill-word` | 次の単語を kill する（続けるとつなげる） |
| `kmacro-end-and-call-macro` | 最後のキーボードマクロを実行する（記録中なら記録を終える） |
| `kmacro-end-macro` | キーボードマクロの記録を終える |
| `kmacro-start-macro` | キーボードマクロの記録を始める |
| `mark-whole-buffer` | バッファ全体を選択する（カーソルは先頭） |
| `menu-bar-open` | メニューを開く |
| `minibuffer-complete` | ミニバッファの入力を補完する |
| `negative-argument` | 前置引数を負にする |
| `newline` | 改行を入れる |
| `next-history-element` | ミニバッファの履歴の次へ |
| `next-line` | 次の行（折り返した画面の行）へ |
| `open-line` | カーソルの後ろに改行を入れる |
| `paste` | クリップボードから貼り付ける（`yank` と同じ） |
| `previous-history-element` | ミニバッファの履歴の前へ |
| `previous-line` | 前の行（折り返した画面の行）へ |
| `query-replace` | 問い合わせ置換（置換する文字列と置換後の文字列を尋ねる） |
| `query-replace-act` | 今の一致を置換して次へ |
| `query-replace-act-and-exit` | 今の一致を置換して終える |
| `query-replace-all` | 残りをすべて置換する |
| `query-replace-exit` | 問い合わせ置換を終える |
| `query-replace-regexp` | 正規表現の問い合わせ置換（置換後の文字列では `$1` が使える） |
| `query-replace-skip` | 今の一致を飛ばして次へ |
| `quit` | ウィンドウを閉じる（未保存なら保存するか尋ねる） |
| `quoted-insert` | 次のキーの文字をそのまま入れる |
| `read-only-mode` | 読み取り専用を切り替える |
| `recenter-top-bottom` | カーソルの行を画面の中央・上端・下端に順に置く |
| `redo` | 取り消した操作をやり直す |
| `revert-buffer-with-coding-system` | ファイルを指定の文字コードで読み直す |
| `save-buffer` | 保存する |
| `scroll-down-command` | 1 画面戻る |
| `scroll-up-command` | 1 画面進む |
| `select-all` | バッファ全体を選択する（カーソルは末尾） |
| `self-insert-command` | 打った文字を入れる（文字キー） |
| `set-buffer-file-coding-system` | 次に保存するときの文字コードを変える |
| `set-eol` | すべての行の改行を `lf` / `crlf` / `cr` にそろえる |
| `set-mark-command` | 今の位置にマークを置く |
| `set-wrap-mode` | このウィンドウの折り返しを変える（`none` / `window` / `fixed` / `max`） |
| `text-scale-decrease` | 文字を小さくする |
| `text-scale-increase` | 文字を大きくする |
| `text-scale-reset` | 文字の大きさを元に戻す |
| `toggle-eol-marks` | このウィンドウの改行の記号を切り替える |
| `toggle-line-numbers` | このウィンドウの行番号を切り替える |
| `toggle-whitespace-marks` | このウィンドウの空白の記号を切り替える |
| `transpose-chars` | カーソルの前後の文字を入れ替える |
| `undo` | 操作を取り消す |
| `universal-argument` | 前置引数（Emacs の `C-u`。1 回ごとに 4 倍、続けて数字や `-` も打てる）。既定のキーは無く、使うときは設定で `"C-u": "universal-argument"` のように割り当てる。`keys` にこう書いても、isearch と問い合わせ置換の中の `C-u` は `isearch-delete-pattern` と `ignore` のまま |
| `upcase-word` | 次の単語を大文字にする |
| `write-file` | 別の名前で保存する |
| `yank` | クリップボードから貼り付ける |

## 既定のキー

設定ファイルの `keys` などで変えられます。Emacs のキーに加えて、Windows の編集キー（矢印・Home・End・PageUp・PageDown・Delete と、その Ctrl 付き）を同じコマンドに割り当てています。

### 通常のキー

| キー | コマンド |
|---|---|
| `C-f` / `<right>` | `forward-char` |
| `C-b` / `<left>` | `backward-char` |
| `C-n` / `<down>` | `next-line` |
| `C-p` / `<up>` | `previous-line` |
| `C-a` / `<home>` | `beginning-of-line` |
| `C-e` / `<end>` | `end-of-line` |
| `M-f` / `C-<right>` | `forward-word` |
| `M-b` / `C-<left>` | `backward-word` |
| `M-<` / `C-<home>` | `beginning-of-buffer` |
| `M->` / `C-<end>` | `end-of-buffer` |
| `C-v` / `<next>` | `scroll-up-command` |
| `M-v` / `<prior>` | `scroll-down-command` |
| `C-l` | `recenter-top-bottom` |
| `M-g g` / `M-g M-g` | `goto-line` |
| `C-SPC` | `set-mark-command` |
| `C-x C-x` | `exchange-point-and-mark` |
| `C-x h` | `mark-whole-buffer` |
| `C-g` | `keyboard-quit` |
| `ESC ESC ESC` | `keyboard-escape-quit` |
| `C-d` / `<delete>` | `delete-char` |
| `DEL` / `C-h` | `delete-backward-char` |
| `C-k` | `kill-line` |
| `C-u` | `backward-kill-line` |
| `M-d` | `kill-word` |
| `M-DEL` | `backward-kill-word` |
| `C-w` | `kill-region` |
| `M-w` | `kill-ring-save` |
| `C-y` | `yank` |
| `RET` / `C-j` | `newline` |
| `C-o` | `open-line` |
| `TAB` | `insert-tab` |
| `C-q` | `quoted-insert` |
| `C-t` | `transpose-chars` |
| `M-u` | `upcase-word` |
| `M-l` | `downcase-word` |
| `M-c` | `capitalize-word` |
| `C-/` / `C-_` / `C-x u` | `undo` |
| `C-?` / `C-M-_` | `redo` |
| `C-x C-f` | `find-file` |
| `C-x C-s` | `save-buffer` |
| `C-x C-w` | `write-file` |
| `C-x C-q` | `read-only-mode` |
| `C-x C-c` | `quit` |
| `C-s` | `isearch-forward` |
| `C-r` | `isearch-backward` |
| `C-M-s` | `isearch-forward-regexp` |
| `C-M-r` | `isearch-backward-regexp` |
| `M-%` | `query-replace` |
| `C-M-%` | `query-replace-regexp` |
| `C-x RET f` | `set-buffer-file-coding-system` |
| `C-x RET r` | `revert-buffer-with-coding-system` |
| `C-x RET c` | `find-file-with-encoding` |
| `C-x RET e` | `set-eol` |
| `C-x C-+` / `C-x C-=` | `text-scale-increase` |
| `C-x C--` | `text-scale-decrease` |
| `C-x C-0` | `text-scale-reset` |
| `<f10>` | `menu-bar-open` |
| `M-0` / `M-1` / `M-2` / `M-3` / `M-4` / `M-5` / `M-6` / `M-7` / `M-8` / `M-9` | `digit-argument` |
| `M--` | `negative-argument` |
| `C-x (` / `<f3>` / `C-(` | `kmacro-start-macro` |
| `C-x )` / `C-)` | `kmacro-end-macro` |
| `C-x e` / `<f4>` / `C-S-e` | `kmacro-end-and-call-macro` |
| `M-x` | `execute-extended-command` |

Shift を足した移動キー（`S-<left>`、`C-S-<end>` など）は、同じ移動で選択します（「選択と kill」）。

### Windows 互換キー

通常のキーより優先します。ミニバッファ・isearch・問い合わせ置換の中では働きません（その中では `C-S-g` が `C-g` として働くなど、Shift の無いキーとして扱います）。

| キー | コマンド |
|---|---|
| `C-S-a` | `select-all` |
| `C-S-c` | `copy` |
| `C-S-x` | `cut` |
| `C-S-v` | `paste` |
| `C-S-z` | `undo` |
| `C-S-y` | `redo` |
| `C-S-s` | `save-buffer` |
| `C-S-w` | `quit` |
| `C-S-f` | `isearch-forward` |
| `C-S-h` / `C-S-r` | `query-replace` |
| `C-S-g` | `goto-line` |

### ミニバッファの中

ほかのキーは通常のキー（Windows 互換キーを除く）として働きます（`DEL` / `C-h` は前の 1 文字を、`C-u` は入力の先頭までを消します）。`C-g` と `ESC ESC ESC` でミニバッファを抜けます。

| キー | コマンド |
|---|---|
| `RET` / `C-j` | `exit-minibuffer` |
| `TAB` | `minibuffer-complete` |
| `M-p` | `previous-history-element` |
| `M-n` | `next-history-element` |

### isearch の中

文字キーは検索語に足します。ほかのキーは今の一致で isearch を終えてから、通常のキー（Windows 互換キーを除く）として働きます。`C-u` は検索語を消して、カーソルを isearch を始めた位置に戻します（isearch は続き、次に打った文字はそこから探します。クリップボードには入れません）。

| キー | コマンド |
|---|---|
| `C-s` / `C-M-s` | `isearch-repeat-forward` |
| `C-r` / `C-M-r` | `isearch-repeat-backward` |
| `DEL` / `C-h` | `isearch-delete-char` |
| `C-u` | `isearch-delete-pattern` |
| `RET` | `isearch-exit` |
| `C-g` | `isearch-abort` |
| `M-r` | `isearch-toggle-regexp` |
| `M-c` | `isearch-toggle-case-fold` |
| `M-%` | `isearch-query-replace` |

### 問い合わせ置換の中

ほかのキーは通常のキー（Windows 互換キーを除く）として働きます。割り当ての無い文字キーは文字を入れず、`<key> is undefined` と出します。`C-u` は何もしません（問い合わせ置換は続きます。途中で開いたミニバッファの中の `C-u` は入力の先頭までを消します）。

| キー | コマンド |
|---|---|
| `y` / `SPC` | `query-replace-act` |
| `n` / `DEL` / `C-h` | `query-replace-skip` |
| `!` | `query-replace-all` |
| `.` | `query-replace-act-and-exit` |
| `q` / `RET` | `query-replace-exit` |
| `C-u` | `ignore` |

## ライセンス

MIT License（[LICENSE](LICENSE)）。同梱の PCRE2（`third_party/pcre2/`）は BSD-3-Clause WITH PCRE2-exception で、ライセンスは [third_party/pcre2/LICENCE.md](third_party/pcre2/LICENCE.md) にあります。
