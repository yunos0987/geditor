# geditor 実装計画

Windows 専用の、起動の速いシンプルなテキストエディター。既定のキーバインドは Emacs で、キーバインドとフォントは設定ファイルで簡単に変えられる。

この文書はヒアリング（2026-11-10、追加 2026-11-11）で確定した要件と、それを実現する設計・段階計画をまとめたもの。

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
| Windows 互換キー | Windows の Ctrl+英字の操作を Ctrl+Shift+英字（`C-S-a` など 12 個）に割り当てる。その 12 個は `C-<英字>` へのシフト変換より優先（2026-11-15 にユーザーが Ctrl+Alt から変更） |
| ファイル数 | 1 ウィンドウ 1 ファイル（別ファイルは別プロセス） |
| 文字コード | UTF-8 / UTF-16 (LE/BE) / Shift_JIS(CP932) を自動判定。BOM 有無を保持・変更可。自動判定で読めないファイルのために、文字コードを指定して開く・開き直すことができる（指定できるのは 2 章「指定できる文字コード」の 9 種） |
| 改行コード | LF / CRLF / CR を行ごとに判定して保持（混在していても保存で変えない）。行末に改行コードごとに違う記号を常に表示。全行をそろえる変更コマンドとメニューあり |
| メニューバー | Win32 標準のメニューバー（File / Edit / View）。F10 で開く。設定で非表示にできる |
| 画面の言語 | 英語（メニュー・ダイアログ・ミニバッファのプロンプトとメッセージ。Emacs と同じ文言） |
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
- 前置引数（2026-11-17 から既定のキーは `M-0`〜`M-9` / `M--`。`C-u` は行の先頭までの kill）、キーボードマクロ
- 文字コード・BOM・改行コードの変更
- 文字コードを指定して開く・開き直す
- 改行コードの行末記号（LF / CRLF / CR を区別）
- メニューバー

### 初版に入れないもの（後回し）

kill ring と M-y、構文ハイライト、複数バッファ・画面分割・タブ、自動保存（`#file#`）、行間・太さ・アンチエイリアスの設定、IME の変換中文字列のインライン描画、合字・複雑な文字組み、行をまたぐ正規表現、EUC-JP / JIS などの自動判定（指定して開くことはできる）、ファイルダイアログ、印刷、スクリプト言語。

## 2. 既定で決めた事項（異論があれば変更する）

ヒアリングで聞かなかった細部を次のように決めた。

| 項目 | 既定の決定 | 理由 |
|---|---|---|
| ビルド | CMake + Ninja + clang-cl、x64、C++20、静的 CRT（`/MT`）、Release は LTO | 手元にある道具で組め、VC++ ランタイム DLL に依存しない |
| 内部の文字表現 | UTF-16（`wchar_t`） | Win32・DirectWrite・PCRE2(16bit) とそのまま受け渡せる |
| テキスト構造 | piece tree（piece table を平衡二分木で持ち、各ノードに文字数・改行数の集計を持つ） | 100MB でも挿入・削除・行番号検索が O(log n) |
| 内部の改行 | テキストは改行を `\n` 1 文字で持つ。各 piece が、中に含む `\n` の改行コード（LF / CRLF / CR）を属性として持ち、読込時は改行コードが変わる位置で piece を分ける。保存時は piece ごとに元の改行コードへ戻す | 行ごとの改行コードを保ちつつ、編集・行計算は `\n` だけを見ればよい。undo は piece の参照で戻すので改行コードも一緒に戻る |
| 改行混在ファイル | 行ごとに保持し、保存しても変えない（次の行の場合を除く）。ファイルの「既定の改行コード」は最も多いもの（同数なら CRLF > LF > CR の順）。既定はモードラインに `Mixed(CRLF)` のように出す | 2026-11-11 にユーザーが確定 |
| CR の改行の直後の LF の改行 | 保存するとき、CR の改行のすぐ後に LF の改行が続く箇所（例: CR の行の次の LF の行を空にしたとき）は、その LF を CR として書く。続けて LF の改行が並べば、それぞれ直前が CR になるので全部 CR にする。バッファの改行コードも同じに変える（記号とモードラインがファイルと一致する）。既定の改行コードも、保存したファイルを開き直したときと同じもの（最も多いもの）にする（M6 の後の保留の修正 F1）。undo の単位にはせず、保存に成功すれば未変更になる。エコー領域に件数を出す（例 `Wrote <path>; saved 1 LF line end after a CR as CR to keep the lines`）。ほかの改行は変えず、保存に失敗したらバッファも変えない | そのまま `\r` `\n` と書くと読み直したときに 1 つの CRLF になり、行が黙って 1 つ減る（M4a のレビューで発見）。2026-11-15 に決定（M4a の修正 F1。ユーザーが見直すことがある） |
| 新しく入る改行 | 入力・貼り付け・置換で入る改行は既定の改行コード。挿入するテキスト（貼り付け・IME・`insert-text` など）に含まれる `\r\n` と単独の `\r` は、先に `\n` 1 文字へ直してから入れる（M1 から）。`quoted-insert` の後の `RET` は `Cannot insert a carriage return` と表示して入れない（テキスト中の単独 `\r` は改行コードと区別できなくなるため） | 混在ファイルでも挙動が予測できる。CRLF のテキストを貼っても `\r` が混ざったり改行が二重になったりしない |
| 行の結合・分割 | 改行を消して 2 行を結合すると、消した `\n` の改行コードは消え、結合した行の末尾には後ろの行の改行コードが残る。行の途中に改行を入れて分割すると、入れた `\n`（前半の行の末尾）は既定の改行コードで、後半の行は元の改行コードを保つ。改行の無い最終行の末尾に改行を入れると既定の改行コード | 例: 既定 CRLF のファイル `a⏎b↓c⏎`（⏎=CRLF, ↓=LF）で 1 行目末の改行を消すと `ab↓c⏎`、続けて `a` と `b` の間で改行すると `a⏎b↓c⏎` |
| 既定の改行コードと Mixed 表示 | 既定の改行コードは読込時に決め、編集では変えない（`set-eol` と開き直しと、CR の改行の直後の LF の改行を CR として書いた保存（上の行）でだけ変わる）。既定と違う改行コードの `\n` の数を piece の集計で持ち、1 以上なら `Mixed(<既定>)`、0 になれば既定だけを表示する | 編集後もモードラインが実際の状態と一致する |
| クリップボードへの書き出し | kill / copy でクリップボードへ書くときは、`\n` を CRLF にする（Windows の慣習）。元の行ごとの改行コードは引き継がない | 他のアプリへ貼ったときに正しく改行される |
| 改行コードの変更 | `set-eol`（メニュー File > Line Endings）は全行をその改行コードにそろえ、既定も変える。1 回の undo で元の混在状態に戻る | ヒアリングで確定 |
| 指定できる文字コード | 自動判定の 7 種（`utf-8` / `utf-8-bom` / `utf-16le` / `utf-16le-bom` / `utf-16be` / `utf-16be-bom` / `shift_jis`）に、`euc-jp`（CP 20932）、`iso-2022-jp`（CP 50220）を加えた 9 種（Unicode 系と日本語系だけ）。Unicode 系以外の変換は `MultiByteToWideChar` / `WideCharToMultiByte`。読込は使えるコードページでは `MB_ERR_INVALID_CHARS` を付け、どのコードページでも読んだ結果を同じ文字コードで書き戻して元のバイト列と比べる。書出は使えるコードページでは `WC_NO_BEST_FIT_CHARS` を付けて `lpUsedDefaultChar` を見て、どのコードページでも書いた結果を読み戻して元のテキストと比べる（似た文字への黙った置き換えを防ぐ）。`dwFlags` が 0 でなければならないコードページ（50220 など）は比べる方法だけで判定する。各コードページのフラグ対応は 2026-11-15 に Windows 11 で実測した（M4b）: CP 932 は `MB_ERR_INVALID_CHARS`・`WC_NO_BEST_FIT_CHARS`・`lpUsedDefaultChar` とも使える（表せない文字は `?` になり `lpUsedDefaultChar` が立つ）が、`WC_NO_BEST_FIT_CHARS` では U+00A2 / A3 / AB / AC / AF / B5 / B7 / B8 / BB と U+3094 / 3099 / 309A の 12 文字のどれかがテキストの最後にあると、大きさの問い合わせは正しいバイト数を返すが、ちょうどその大きさの領域への変換が `ERROR_INSUFFICIENT_BUFFER` で失敗する（変換には余裕のある領域を渡し、失敗すれば広げて呼び直す。それでも失敗すれば 1 文字ずつ調べて最初の表せない文字を示す。M4b の修正 R1 / R2）。フラグ 0 では BMP の 85 文字が似た文字に置き換わる（例 `¥` → `\`、U+00A2 → `81 91`）。CP 20932 は 3 つとも使え、BMP に似た文字への置き換えは見つからなかった（`0x80` と `0x8F` で始まる 3 バイトの並びは `MB_ERR_INVALID_CHARS` でも読めてしまい、書き戻しの比較で見つかる）。CP 50220 はどちらのフラグも使えない（`ERROR_INVALID_FLAGS`。`lpUsedDefaultChar` を渡すと `ERROR_INVALID_PARAMETER`）。フラグ 0 では半角カナなど BMP の 63 文字を似た文字（全角カナなど）に置き換える。このため、フラグを使える CP でもすべて読込・書出の結果を逆変換して比べる。表示名は 4.6 | 2026-11-11 にユーザーが確定。2026-11-14 に Unicode 系と日本語系だけに絞った（`windows-1252` / `gb18030` / `big5` / `euc-kr` は不要とユーザーが確定） |
| 文字コードを指定して開く | `find-file-with-encoding`：ミニバッファでファイル名、続けて文字コード名を Tab 補完で入力する（メニューから選んでも同じ）。以降の流れ（置き換え / 新しいウィンドウの確認）は find-file と同じ。新しいウィンドウでは、その文字コードを起動引数で渡す。ファイル名は find-file と同じく今のフォルダーから始まり、Tab でファイル名を補完する。文字コード名は 9 種のほかを受け付けない（`[No match]`）。存在しないファイルは、その文字コードの新規ファイルになる（M4b） | 2026-11-11 にユーザーが確定（ミニバッファで指定） |
| 文字コードを指定して開き直す | `revert-buffer-with-coding-system`：今のファイルを指定の文字コードで読み直す。未保存の変更があれば「変更を捨てて開き直すか」を Windows のダイアログで確認する。undo 履歴は捨て、カーソルは同じ行番号に置く。ファイル名の無いバッファやディスクに無いファイルでは `Buffer is not visiting a file` などと表示して何もしない。M4b の実装: 確認は yes/no（`Discard the changes to <name> and read it again as <ENC>?`。No なら `Cancelled` と出してテキスト・カーソル・変更の有無・undo を変えない）。ディスクに無いファイルは `File <path> is not on disk; nothing to read again`。ファイルが短くなっていれば最後の行に置く。M6 の B3: 開き直す前に `read-only-mode` だったら（`C-x C-q` で入れたものも）開き直した後も `read-only-mode` のまま。読み取り専用属性のファイルは今までどおり `read-only-mode` になる | 自動判定が外れたときの直し方 |
| 保存に使う文字コードの変更 | `set-buffer-file-coding-system`（File > Encoding for Saving）は次の保存に使う文字コードだけを変える。テキストは変えず、バッファを変更ありにする（Emacs と同じ。undo の単位にはせず、保存に成功するか開き直すまで変更ありのまま）。同じ文字コードなら何もしない。`read-only-mode` では `set-eol` と同じく断る。BOM の有無（`utf-8` ↔ `utf-8-bom` など）もこれで変える。表せない文字は保存のときに止まる（「Unicode 系以外の文字コードで表せない文字の保存」）。BOM ありの名前で開くと BOM を外し（無くても読める）、BOM なしの名前で開くと先頭の BOM は U+FEFF の文字として残す（保存すると同じバイト列） | M4b で決定（PLAN に無かった細部） |
| 指定した文字コードで読めないバイト | 置換文字（U+FFFD）にして開き、ミニバッファに最初の位置（バイト位置と行）を表示する。読んだ結果を同じ文字コードで書き戻して元のバイト列と一致しない場合も同じ扱い。このバッファはモードラインの文字コードに `!` を付けて（例 `Shift_JIS!`）、保存か開き直しまで示し続ける（U+FFFD を消しても続く。読んだ文字のうちどれが元のバイトを失ったかは編集の後まで追わない）。保存しようとすると、どの文字コードでも「元のバイトは失われる」と警告して確認する。文言は読んだファイル（別名で保存するときもバッファのファイル。M6 の修正 R6）と、読んだときの文字コードと保存する文字コードを名指しする（例 `t.txt was read as Shift_JIS with bytes that are not valid Shift_JIS (first at byte offset 3, line 2; read as U+FFFD); their original bytes will be lost. Save as Shift_JIS anyway?`）。No なら保存しない（`Not saved`）。Yes なら普通の保存に進み、残っている U+FFFD など保存する文字コードで書けない文字があれば「Unicode 系以外の文字コードで表せない文字の保存」と同じくその位置で中止する（`!` も残る）。書けたら `!` を消す（M6 の B4 / B5、2026-11-16 にユーザーが確定。M4b では Unicode 系以外の保存を確認せずに中止していた）。読み違えたときは、別の文字コードを指定して開き直せばディスク上のファイルから読み直せる。M4b の実装: U+FFFD にする単位は、UTF-8 では不正な並びの最大の部分（Unicode 3.9 の maximal subpart）ごとに 1 つ、UTF-16 では対にならないサロゲートと奇数個目の最後のバイト、コードページでは読めない文字の先頭の 1 バイトごと。表示は `<path>: bytes that are not valid Shift_JIS were read as U+FFFD (first at byte offset N, line L)`（バイト位置は 0 から、行は 1 から）。書き戻して一致しないだけの文字（CP 932 の NEC 選定 IBM 拡張 `ED 40` は U+7E8A と読めるが `FA 5C` と書かれる、ISO-2022-JP の `ESC $ @` など）は U+FFFD にせずその文字のまま読み、最初に違うバイトの位置を `<ENC> does not give the same bytes back` と出す | 黙って壊さない（「判定不能なバイト列」と同じ方針） |
| メニューバー | File / Edit / View の 3 つ（4.9）。各項目は M-x と同じコマンドを呼び、右側に今のキー割り当てを出す（設定を再読込したら更新）。F10（`menu-bar-open`）で File メニューを下に開いた状態で入る（Emacs と同じ。左右キーで Edit・View へ移り、Esc で閉じる。2026-11-14 にユーザーが確定）。Alt+英字はキーマップ（`M-f` など）が優先で、メニューのアクセスキーには使わない。設定 `menu_bar` で非表示にでき、そのとき F10 は `Menu bar is hidden` と表示するだけ。ミニバッファ・isearch・問い合わせ置換の途中は全項目を灰色にする（入力途中の状態を壊さない） | 2026-11-11 にユーザーが確定 |
| 画面の言語 | メニュー・ダイアログ・プロンプト・メッセージは英語 | 2026-11-11 にユーザーが確定 |
| UTF-16 の BOM なし判定 | NUL バイトの偏りで判定。M4b の実装: 先頭 64KiB の NUL バイトを数え、奇数番目が 2 個以上・2 バイト単位の数の 4 分の 1 以上・偶数番目の 4 倍より多ければ UTF-16LE、偶数番目が同じ条件なら UTF-16BE（M4b の修正 R3。1 個の NUL で UTF-16 にしない。ASCII 2 文字以上の BOM なし UTF-16 はこの条件を満たし、1 文字だけのもの（`a` 00）は UTF-8 になる。漢字ばかりで ASCII が 4 分の 1 未満の BOM なし UTF-16 も UTF-16 と判定されない）。判定の順は BOM → NUL の偏り → UTF-8 として正しいか（ASCII だけなら UTF-8）→ CP 932 が `MB_ERR_INVALID_CHARS` で全バイトを読めるか（Shift_JIS）→ 判定不能。UTF-8 としても正しい Shift_JIS（半角カナ 2 文字の `ﾃｽ` = C3 BD など）は UTF-8 になる。`euc-jp` と `iso-2022-jp` は判定では選ばない | 「BOM 有無 Unicode 対応」の要件 |
| 判定不能なバイト列 | UTF-8 として置換文字で読み（モードラインに `UTF-8!`）、保存時に警告して確認。表示は `<path>: not valid UTF-8 or Shift_JIS; read as UTF-8 with U+FFFD for the bytes that are not (first at byte offset N, line L)`（M4b） | 黙って壊さない |
| Unicode 系以外の文字コードで表せない文字の保存 | 保存を中止し、最初の文字の位置を示して文字コードの変更を促す（Shift_JIS ほか、指定できる文字コードすべて）。M4b の実装: 書いた結果を読み戻して元のテキストと比べ、違う最初の文字にカーソルを移して `Cannot save as Shift_JIS: U+1F600 at line 3, column 4 cannot be written in Shift_JIS; choose another encoding with set-buffer-file-coding-system` と出す（行は 1 から、桁はモードラインの `C`）。ファイルは書かない（一時ファイルも作らない）。外部変更の確認より前に調べる | 黙って `?` に化けさせない |
| カーソル単位 | 書記素クラスタ（Unicode UAX #29 の拡張書記素クラスタ。文字幅表と同じ Unicode の版の表から作る）。C-f / C-b、DEL / C-d、単語移動の端、マウスの位置、折り返し位置、選択範囲の端は、クラスタの途中に来ない。結合文字（例: か＋゛）、異体字セレクタ付きの絵文字、肌の色、ZWJ でつないだ絵文字、国旗（地域指示子 2 文字）、ハングルの字母の並びはそれぞれ 1 文字として動き、1 回で消える | 異体字セレクタ付きの絵文字が 1 桁に縮んで描かれ、Backspace が 2 回要った（手での確認）。2026-11-14 にユーザーが確定（以前は「コードポイント単位、書記素クラスタは後回し」） |
| 折り返し中の上下移動 | `next-line` / `previous-line`（C-n / C-p / ↓ / ↑）とスクロールは画面の行（折り返した行）単位で動く。目標の桁は画面の行の中の升目で数える。`beginning-of-line` / `end-of-line`（C-a / C-e）・`kill-line` はファイルの行（改行まで）単位 | Emacs の既定（`line-move-visual`）と同じ。2026-11-13 にユーザーが確定 |
| マウス | クリックはその文字の前にカーソルを置く（行末より右と右端の 1 升はその画面の行の末尾、行番号の欄は画面の行の先頭、最後の行より下はバッファの末尾、本文とモードラインの間の隙間は最後に見えている行）。ドラッグで選択し、本文の外では画面の行単位で自動スクロールする。ダブルクリックは `forward-word` と同じ定義の単語を選ぶ。Shift+クリックは選択範囲を広げる。ホイールは Windows の設定の行数だけ画面の行単位でスクロールする。クリックで入力の undo のまとまりを切る。モードライン・エコー領域とミニバッファの入力中のクリックは何もしない。中ボタン・横ホイール・トリプルクリック・Ctrl+ホイールには何も割り当てない（利用者の環境では中ボタンを終了に割り当てている） | 2026-11-14 にユーザーが確定（M3b の決定 B5） |
| 桁の数え方 | 書記素クラスタごとに数える。絵文字として表示するクラスタ（Unicode の Emoji の性質を持つ文字に U+FE0F が付いたもの、肌の色付き、ZWJ でつないだ絵文字、国旗、既定で絵文字表示の文字）は 2 桁。それ以外は先頭の文字で決め、東アジアの全角（East Asian Width F/W）を 2 桁、それ以外を 1 桁とする（U+FE0E を付けた文字もこの規則）。クラスタ内の 2 文字目以降は桁に数えない | 折り返し・カーソル位置・マウス位置の計算に使う。2026-11-14 にクラスタ単位に変更 |
| 文字の配置 | 端末と同じ升目配置。升目の幅は主フォントの `0` の送り幅で、全角は 2 升。各書記素クラスタは自分の升目の中に描く（4.7） | 「全角 2 桁」の折り返しと見た目が必ず揃い、位置計算が整数演算で済む。代わりに合字やアラビア文字などの複雑な文字組みは正しく出ない |
| 新規ファイル | UTF-8（BOM なし）+ LF。設定 `new_file.encoding` / `new_file.eol` で変更可。文字コードを指定して開いた新規ファイル（`find-file-with-encoding`・`--encoding`）はその文字コード | ヒアリングで確定 |
| 改行の無いファイル | 改行コードは `new_file.eol` を使う。ASCII のみのファイルは UTF-8 と判定する | |
| 別ファイルを開く | C-x C-f・ドラッグ＆ドロップのたびに Windows のダイアログで「このウィンドウで開く（置き換える）」「新しいウィンドウで開く」「キャンセル」を選ぶ。置き換えるときに未保存なら、続けて保存の確認（保存する / 保存しない / キャンセル）を出す。ファイル名が無く未編集の空のウィンドウでは確認せずに置き換える。置き換えると undo 履歴は捨てる。新しいウィンドウは自分の exe をそのファイルを引数にして別プロセスで起動する | 1 ウィンドウ 1 ファイル。2026-11-11 にユーザーが確定 |
| 検索の単位 | 論理行ごと（パターンは行をまたがない） | piece tree 上で連続バッファを作らずに済む |
| 後ろ向きの検索の範囲の終わり | 後ろ向きの検索（`C-r` / `C-M-r`）は、範囲の終わり（一致がまだ無いときは探し始めた位置、`C-r` を重ねたときは今の一致の先頭）をテキストの終わりとして照合する。一致はそこを越えて伸びない（`.*` はそこで切れる）。`$` はその位置では当たらない（本当の行末では当たる）。`\z` / `\Z`・先読み・`\b` はその位置をテキストの終わりとして見る。このため後ろ向きの検索の一致は、同じ場所の前向きの検索や強調表示の一致より短いことがある。一致の始まりの規則（前向きにたどった一致のうち、始まりが範囲の中で最も後ろのもの）は変えない。2026-11-17 にユーザーが決定（S1）。重ねた `C-r` は今の一致の先頭から始まる一致を返さず、次の一致はそれより前（UTF-16 の単位で数えた位置）から始まる（[C12]、2026-11-18 にユーザーが確定（決定 3 a）。それまでは `C-M-r .*` や `C-M-r o*` の後の `C-r` で、押しても一致の先頭が動かないことがあった）。重ねた `C-s` / `C-r` はカーソルが動く一致だけを返す（2026-11-18 にユーザーが決定（決定 4 a）。下の「重ねた isearch の一致」） | Emacs の後ろ向きの正規表現検索も、探し始めた位置を越える一致を作らない（Emacs のソースの `src/search.c` の `search_buffer_re` は `re_search_2` の stop に今の位置を渡す: "Don't allow match past current point"）。行全体を照合して範囲の外で終わる一致を捨てる作りは、長い行で遅かった（10 万文字で約 7 秒） |
| 重ねた isearch の一致 | isearch の中で重ねた `C-s` / `C-r`（`C-M-s` / `C-M-r` も）は、カーソルが動く一致だけを返す。強調とカーソルは書記素クラスタの境目に置くので、前向きは表示する一致の終わり（カーソルの位置）が今の一致の表示する終わりより後ろの一致、後ろ向きは表示する一致の先頭が今の一致の表示する先頭より前の一致。そうでない一致（複数の単位からなるクラスタの中で終わる・始まるもの）は飛ばし、飛ばした一致から同じ手順で探し続ける（前向きはその一致の終わりから、後ろ向きは [C12] の手順）。残っていなければ今までどおり失敗する（`Failing I-search`、カーソルは動かず、次の押下で折り返す）。`DEL` / `C-h` は、飛ばした一致の数によらず 1 回の押下の前に戻る。例: `a`・ZWJ でつないだ家族の絵文字（8 単位）・`b` の行で `C-M-s .` の後の `C-s` は、カーソルを 1 → 9 → 10 と動かし、3 回目で失敗する（それまでは 9 に着いた後の 4 回はカーソルが 9 のままだった。家族の絵文字の残りの 4 つの符号位置の一致）。2026-11-18 にユーザーが決定（決定 4 a）。飛ばし方（飛ばした一致から同じ手順で探し続けること）は実装のときに決めた（ユーザーの決定ではない） | 失敗も折り返しもしない押下では、押すたびにカーソルが動く。1 単位で 1 クラスタの文字だけのテキストでは飛ばす一致が無い（前向きの次の一致は今の一致の終わりより後ろで終わり、後ろ向きの次の一致は今の一致の先頭より前で始まるため）。`tests/test_isearch_skip_cu.cpp` が、ASCII・かな・漢字・改行・空の行から生成したテキストと 15 のパターンの 750 の列（前向き・後ろ向きに 20 回ずつ）で、生の一致・カーソル・失敗・折り返しがそれまでの作りと同じことを確かめる |
| 大文字小文字 | smart case（パターンに大文字が無ければ区別しない）。isearch 中 M-c で切替 | Emacs と同じ |
| kill と クリップボード | kill / copy はクリップボードへ書き、yank はクリップボードから読む。連続した kill は追記。M6 の実装: kill のコマンド（`kill-line` / `kill-word` / `backward-kill-word` / `kill-region` / `cut`、2026-11-17 から `backward-kill-line` も）の直後の kill（間の前置引数は数えない）は、それまでの文字列の後ろに（後ろ向きの kill は前に）足してクリップボードに書く。続いている文字列はエディターが持ち、クリップボードを読み戻さないので、その間に別のプログラムがクリップボードを書き換えても気づかずに上書きする。ほかのコマンド（`M-w` を含む）とマウスのクリックで続きは切れる。M6 の修正 R1: キーボードマクロの実行とコマンド列の中の kill はつなげるだけで、kill でないコマンドが動く前と、実行・コマンド列の終わりに 1 回だけクリップボードに書く（入力 1 つの処理が終われば必ず書いてある）。1 つずつ打つ kill はその場で書き、書けなければ消さない。待っていた書き込みが失敗したら（もう消してある）理由を出し、`C-/` で戻せる | kill ring を持たない代わり |
| 選択 | Emacs のマーク + transient mark（リージョンを強調表示）。Shift+矢印でも選択開始 | Windows 操作との共存 |
| undo の単位 | 1 コマンド = 1 単位。連続した文字入力は 20 文字まで 1 単位。1 キーに割り当てたコマンド列・キーボードマクロ 1 回の実行も 1 単位（M6: `M-3 C-x e` の 3 回分も 1 回の実行。続けて `e` で実行したものはそれぞれ 1 単位）。undo でカーソル位置も戻す | 一般的なエディターと同じ感覚 |
| 保存状態の追跡 | undo で保存時点に戻ると未変更表示に戻る | |
| undo 履歴の保持 | piece の参照で持ち、テキストを複製しない。上限なし | 大きな削除でもメモリを食わない |
| 外部での変更 | 保存時に更新日時を確かめ、変わっていれば確認 | 上書き事故を防ぐ |
| 読み取り専用属性のファイル | 開いた時点で `read-only-mode` を入れる（モードラインに `%%`。編集すると `Buffer is read-only`）。`C-x C-q` で外せば編集できるが、保存は属性のため失敗し、理由を出す | Emacs と同じ。2026-11-14 にユーザーが確定 |
| 起動引数 | `geditor.exe [--encoding=NAME] [--settings=PATH] [+LINE] [file]`。存在しないファイル名なら新規として開く。`--encoding` は指定できる文字コード名で、自動判定の代わりに使う（ほかの名前は `Unknown encoding: <NAME> (use utf-8, ...)` とエコー領域に出して自動判定で開く。M4b）。ファイルを渡さないときは、名前の無い新しいバッファをその文字コードにする（モードラインに出る。M6 の B2）。`--settings` は設定ファイルの場所を変える（テストと実機確認で利用者の設定に触れないため。新しいウィンドウにも引き継ぐ） | Emacs と同じ。`--encoding` は「新しいウィンドウで開く」で文字コードを引き継ぐため |
| ヘッドレス（確認用） | `--headless` はテスト・計測・実機確認のための起動引数（利用者向けには案内しない）。ウィンドウを画面外に出し、前面に出ず、タスクバーと Alt+Tab にも出ない。描画は通常と同じ経路で行う。キーは PostMessage で受ける。キーの修飾（Ctrl / Shift / Alt）はキーのメッセージ自身の `lParam` の予約ビット（25〜27）で受け、キーボードの状態（`GetKeyState`）は読まない。ツールは `AttachThreadInput` / `SetKeyboardState` を使わない（M4a の修正 F2。利用者が実際に押している Ctrl が混ざって、`y` が `C-y` になったため）。クリップボードは本物を使わず、プロセス内の専用の置き場を使う。確認ダイアログは出さず、文言とボタンを外へ知らせて、押すボタンを外から受け取る（どちらも WM_COPYDATA）。新しいウィンドウにも引き継ぐ。app_check・計測ツール・検証役の実機確認はこれを既定にする。前面・本物のクリップボード・ダイアログの見た目・IME が要る確認だけを `--visible` で行う | 確認の最中に利用者の画面操作・コピー・打鍵を奪わない。2026-11-14 にユーザーが確定 |
| 高 DPI | Per-Monitor V2 | モニター間移動で滲まない |
| 設定ファイルの監視 | 初回描画の後に別スレッドで `ReadDirectoryChangesW` を開始。通知から 200ms 待って読み、共有違反なら 3 回まで再試行 | 起動時間に含めない。保存途中の中身を読まない |
| 設定の誤り | 構文の誤り（JSONC として読めない）はファイル全体を採用せず直前の設定を保つ。意味の誤り（未知のコマンド・不正なキー表記・引数の型違い・プレフィックス衝突・未知の設定項目・不正な色）はその項目だけ無視して残りを反映する。どちらも行・桁付きで全件表示する。表示は最下行のエコー領域を最大 10 行まで広げて行い（11 件目以降は `... and N more`）、次のキーで 1 行に戻す | 1 か所の誤りで全設定が消えない。表示方法は 2026-11-12 にユーザーが確定 |
| 設定ファイルを開く | `find-user-init-file`（M-x で実行。既定キーなし。M3 で File メニューにも載せる）で `.geditor.json` を開く。開き方の確認は find-file と同じ。ファイルが無ければ、書き方の例をコメントで入れた新規バッファとして開く（保存するまでディスクに作らない。例の挿入は編集として扱うので未保存の表示になる） | 標準の Emacs には組み込みが無く、利用者が自分で定義するときの慣習名に合わせた。2026-11-12 にユーザーが確定 |
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
      menu.*                 -- menu model: items, commands, check/radio state
    win/
      main.cpp               -- wWinMain, command line, startup measurement
      window.*               -- HWND, message loop, mouse, drag and drop
      input.*                -- WM_KEYDOWN/WM_CHAR -> key events
      renderer.*             -- Direct2D drawing
      text_measurer.*        -- DirectWrite measuring, font fallback
      ime.*                  -- IMM32 composition/candidate window placement
      menu_bar.*             -- builds the Win32 menu bar from the core menu model
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
- 処理済みのキーに続く `WM_CHAR` / `WM_SYSCHAR` は捨てる。Alt 単独でメニューバーに入らないよう、`lParam` が 0 の `SC_KEYMENU` だけを握りつぶす（Alt+Space のシステムメニューは残す）。Alt+英字はキーマップで処理し、メニューのアクセスキーとしては使わない。割り当てのない `M-<f4>` は既定処理（ウィンドウを閉じる）に渡す。`WM_SYSCOMMAND` のコマンドは `wParam & 0xFFF0` で取り出して判定する
- メニューバーは F10 とマウスで開く。F10 は修飾キーなしでも `WM_SYSKEYDOWN`（`VK_F10`）で届くので、そこで `<f10>` キーとしてキーマップを引く。`menu-bar-open` は自分のウィンドウ手続きの握りつぶしに掛からない経路でメニューに入り、File メニューを下に開いた状態にする（見出しを選んだだけの状態や、F10 を離したときにメニューから抜ける状態にしない。2026-11-14 の手での確認で、見出しを選ぶだけの作りは「変化なし」に見えた）。`<f10>` の割り当てを外した場合も、F10 で既定のメニュー動作は起きない
- `ESC` に続くキーは `M-` 付きとして扱う（Emacs と同じ）
- IME の変換中（`VK_PROCESSKEY`）のキーはキーマップを引かない。確定文字列は 1 回の挿入として扱う
- IME の確定文字列の前の前置引数（2026-11-20 にユーザーが決定）: 確定文字列の全体を前置引数の回数だけ入れる（`C-3` の後に `かんじ` を確定すると `かんじかんじかんじ`）。打った文字の前置引数と同じく、全体が 1 回の undo で消える。Emacs は確定した文字を 1 文字ずつのキーとして受け取るので最初の 1 文字だけが繰り返されると見られる（確かめていない）が、それとは違う。以前は確定文字列が前置引数を捨てて 1 回だけ入った。次の細部は実装のときに決めた（ユーザーの決定ではない）: 0 以下の回数と大きすぎる回数は、打った文字（`self-insert-command`）と同じく `Repeat count must be positive` / `Repeat count too large` と出して何も入れず、キーボードマクロの実行を止める。前置引数の後にキー列の途中（`C-3 C-x`）で確定したときは、今までどおりキー列と一緒に前置引数を捨てて 1 回入れる。isearch と問い合わせ置換の中は変えていない
- AltGr のある配列（独語配列など）では Ctrl+Alt+キーが文字入力と重なるが、割り当てを優先する（日本語・英語配列では影響なし）。2026-11-15 に Windows 互換キーを Ctrl+Shift に移したので、既定で Ctrl+Alt を使う割り当ては `C-M-_`（redo）と `C-M-%`（query-replace-regexp）、2026-11-17 からはそれに `C-M-s` / `C-M-r`（正規表現の isearch。isearch の中では次・前の一致）を加えた 4 つ（`src/core/keymap.cpp` の既定の表で `C-M-` を含むのはこれだけ）。ほかは設定で割り当てたもの
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
- 前置引数（`M-3` など）はコマンド列全体の繰り返し回数として渡す
- `C-x` にコマンドを割り当てつつ `C-x C-s` も割り当てる、のようにプレフィックスと衝突する設定は意味の誤り（2 章「設定の誤り」）として、その項目だけ無視する
- 割り当ての解決順: 設定ファイル > Windows 互換キー > Emacs 既定
- `null` はそのキーを未割当にする。下位（Windows 互換キー・Emacs 既定）へは落ちない。押すと `<key> is undefined` と表示する。修飾なしの文字キーに `null` を書くと、その文字は入力されなくなる

### 4.4 既定キーバインド

Windows 互換キー（Emacs より優先。2026-11-15 にユーザーが Ctrl+Alt から Ctrl+Shift に変更）:

| キー | コマンド | 置き換わる操作（シフト変換で動いていた `C-<英字>`） |
|---|---|---|
| `C-S-a` | `select-all` | `C-a`（beginning-of-line） |
| `C-S-c` | `copy` | なし（`C-c` は既定で未割当） |
| `C-S-x` | `cut` | `C-x`（プレフィックス） |
| `C-S-v` | `paste` | `C-v`（scroll-up-command） |
| `C-S-z` | `undo` | なし（`C-z` は既定で未割当） |
| `C-S-y` | `redo` | `C-y`（yank） |
| `C-S-s` | `save-buffer` | `C-s`（isearch-forward） |
| `C-S-w` | `quit` | `C-w`（kill-region） |
| `C-S-f` | `isearch-forward` | `C-f`（forward-char） |
| `C-S-h` / `C-S-r` | `query-replace` | `C-h`（delete-backward-char。2026-11-17 から。それまでは未割当） / `C-r`（isearch-backward） |
| `C-S-g` | `goto-line` | `C-g`（keyboard-quit） |

- この 12 個は割り当てがあるので、`C-<英字>` へのシフト変換（Emacs と同じく、割り当ての無い `S-` 付きのキーを `S-` なしで引き直すこと）をしない。ほかの `C-S-<英字>`（`C-S-b` など）は今までどおり `C-<英字>` として動く（`C-S-e` は 2026-11-18 から下の「マクロ」の行の割り当てを持つので、同じくシフト変換をしない）
- `C-M-a` / `C-M-c` / `C-M-x` / `C-M-v` / `C-M-z` / `C-M-y` / `C-M-w` / `C-M-f` / `C-M-h` / `C-M-g` は既定で未割当（押すと `C-M-a is undefined` など）。Emacs のこれらのキーの操作（`beginning-of-defun`・`exit-recursive-edit`・`eval-defun`・`scroll-other-window`・`append-next-kill`・`forward-sexp`・`mark-defun` など）は geditor に無い。Emacs 互換の `C-M-_`（redo）と `C-M-%`（query-replace-regexp）は残る。`C-M-s` / `C-M-r` は 2026-11-17 から Emacs と同じく `isearch-forward-regexp` / `isearch-backward-regexp`（それまでは未割当）

Emacs と衝突しない Windows の編集キー（Emacs でも同じ割当）:

| キー | コマンド |
|---|---|
| `<left>` / `<right>` | `backward-char` / `forward-char` |
| `<up>` / `<down>` | `previous-line` / `next-line` |
| `<home>` / `<end>` | `beginning-of-line` / `end-of-line` |
| `<prior>` / `<next>` | `scroll-down-command` / `scroll-up-command` |
| `C-<left>` / `C-<right>` | `backward-word` / `forward-word` |
| `C-<home>` / `C-<end>` | `beginning-of-buffer` / `end-of-buffer` |
| `<delete>` / `DEL` | `delete-char` / `delete-backward-char`（`C-h` も同じ。2026-11-17） |

Shift を足した移動キー（`S-<left>`、`C-S-<right>`、`S-<end>` など上表の移動キーすべて）は、同じ移動コマンドを選択の拡張つきで実行する（Emacs の shift-select と同じ。マークが無ければ移動前の位置に一時的なマークを置き、Shift なしの移動でそのマークを外す）。設定ファイルで `S-` 付きのキーに別のコマンドを割り当てた場合はそちらを優先する。M6 の実装: キーの解決でシフト変換が起きて移動のコマンドが動くときに、キーの振り分けの 1 か所で扱う（キーごとのコマンドは作らない）。リージョンが既に有効なら（`C-SPC` で置いたものも）それを広げる。`C-S-b` など Windows 互換キーでない `C-S-<英字>` の移動とミニバッファの中でも同じ（Emacs と同じ）。`C-S-e` は 2026-11-18 からマクロの実行（下の「マクロ」の行）なので、`C-e` の選択にはならない（行末までの選択は `S-<end>`）。

Emacs 互換（global）:

| 分類 | キーとコマンド |
|---|---|
| 移動 | `C-f` forward-char, `C-b` backward-char, `C-n` next-line, `C-p` previous-line, `C-a` beginning-of-line, `C-e` end-of-line, `M-f` / `M-<right>` forward-word, `M-b` / `M-<left>` backward-word, `M-<` beginning-of-buffer, `M->` end-of-buffer, `C-v` scroll-up-command, `M-v` scroll-down-command, `C-l` recenter-top-bottom, `M-g g` / `M-g M-g` goto-line |
| マーク | `C-SPC` / `C-@` set-mark-command, `C-x C-x` exchange-point-and-mark, `C-x h` mark-whole-buffer, `C-g` keyboard-quit, `ESC ESC ESC` keyboard-escape-quit（ミニバッファを抜け、選択範囲を解除する。Emacs と同じ。2026-11-14 にユーザーが確定） |
| 削除・kill | `C-d` / `<delete>` delete-char, `DEL` / `C-h` delete-backward-char（`C-h` は 2026-11-17 にユーザーが決定。Emacs の `C-h`（ヘルプ）とは違う）, `C-k` kill-line, `C-u` backward-kill-line（カーソルから論理行の先頭までを kill する。Bash の `C-u` と同じ。続けた kill では前に足す。行の先頭では何もせず失敗にもしない。前置引数は使わない。2026-11-17 にユーザーが決定）, `M-d` / `C-<delete>` kill-word, `M-DEL` / `C-DEL` backward-kill-word, `C-w` / `S-<delete>` kill-region, `M-w` / `C-<insert>` kill-ring-save, `C-y` / `S-<insert>` yank |
| 入力 | `RET` / `C-j` newline, `C-o` open-line, `TAB` insert-tab, `C-q` quoted-insert, `C-t` transpose-chars, `M-u` upcase-word, `M-l` downcase-word, `M-c` capitalize-word |
| undo | `C-/` / `C-_` / `C-x u` undo, `C-?` / `C-M-_` redo |
| 検索・置換 | `C-s` isearch-forward, `C-r` isearch-backward（前置引数付きで正規表現）, `C-M-s` isearch-forward-regexp, `C-M-r` isearch-backward-regexp（2026-11-17。それまでは `C-u C-s` / `C-u C-r`）, `M-%` query-replace, `C-M-%` query-replace-regexp |
| ファイル | `C-x C-f` find-file, `C-x C-s` save-buffer, `C-x C-w` write-file, `C-x C-q` read-only-mode, `C-x C-c` quit |
| 文字コード | `C-x RET f` set-buffer-file-coding-system（保存に使う文字コード。2 章「指定できる文字コード」の 9 種）, `C-x RET r` revert-buffer-with-coding-system, `C-x RET c` find-file-with-encoding（Emacs では `C-x RET c` は次のコマンドの文字コードを指定するプレフィックス。geditor は開く操作に絞った独自の割当）, `C-x RET e` set-eol（`lf` / `crlf` / `cr`。geditor 独自の割当） |
| 表示 | `C-x C-+` / `C-x C-=` text-scale-increase, `C-x C--` text-scale-decrease, `C-x C-0` text-scale-reset（Emacs では 3 キーとも text-scale-adjust。geditor は動作ごとにコマンドを分ける）。既定のキーが無いコマンド: toggle-line-numbers, toggle-eol-marks, toggle-whitespace-marks, set-wrap-mode（`none` / `window` / `fixed` / `max`）。メニュー View と M-x から使う |
| メニュー | `<f10>` menu-bar-open |
| 前置引数 | `M-0`〜`M-9` / `C-0`〜`C-9` / `C-M-0`〜`C-M-9` digit-argument, `M--` / `C--` / `C-M--` negative-argument。universal-argument（Emacs の `C-u`）は 2026-11-17 から既定のキーが無い（M-x と設定の割り当てで使える。`"C-u": "universal-argument"` で以前の `C-u` に戻る） |
| マクロ | `C-x (` / `<f3>` / `C-(` kmacro-start-macro, `C-x )` / `C-)` / `<f4>`（記録中）kmacro-end-macro, `C-x e` / `<f4>` / `C-S-e` kmacro-end-and-call-macro（続けて `e` で繰り返し。記録中は `C-x e` も `C-S-e` も記録を終えるだけ。M6）。`C-(` / `C-)` / `C-S-e` は 2026-11-18 にユーザーが決定した、1 回の押下で済むキー（下の「マクロの 1 回の押下のキー」） |
| その他 | `M-x` execute-extended-command |

マクロの 1 回の押下のキー（2026-11-18 にユーザーが決定: 記録 `C-(`、記録の終了 `C-)`、実行 `C-S-e`）: 今までのキー（`C-x (` / `<f3>` / `C-x )` / `C-x e` / `<f4>`）は残し、働きも同じ。`C-(` と `C-)` は 4.2 の表記どおり Shift 込みで出る文字で書く（Ctrl を押しながら `(` / `)` を打つ。どのキーかはキーボードの配列で決まり、日本語配列では Ctrl+Shift+8 / Ctrl+Shift+9。`C-S-8` という表記は無い）。次の細部は実装のときに決めた（ユーザーの決定ではない）:

- 3 つとも上の表（Emacs 互換の global）に置き、Windows 互換キーの 12 個には足さない。このため 4.3 の解決順では設定ファイルの次に Windows 互換キー、その次で、ミニバッファ・isearch・問い合わせ置換の途中でも `C-x e` / `<f4>` などと同じに働く（isearch は今の一致で終えてから実行する）。ただし isearch の中では isearch の表を先に引き、その中でシフト変換するので、設定の `isearch_keys` で `C-e` に割り当てがあると、isearch の中の `C-S-e` はその割り当てを動かし、マクロは実行しない（2026-11-18 にヘッドレスの実アプリで確かめた。既定の isearch の表に `C-e` は無い。2026-11-19 にユーザーが、この動きのままにすると確定した）
- `C-S-e` は割り当てを持つので `C-e` へのシフト変換をしない（Shift 選択にならない）。行末までの選択は `S-<end>`
- 設定で `"C-S-e": null` と書くと未割当（`C-S-e is undefined`）になり、シフト変換には戻らない（4.3 の `null` の規則。Windows 互換キーの `null` と同じ）

Emacs の既定のキーの追加（2026-11-19 にユーザーが決定）: Emacs の `lisp/bindings.el` が global-map に割り当てているキーのうち、geditor にあるコマンドのものを足した（上の表の 2026-11-19 の分）: `S-<insert>` yank、`C-<insert>` kill-ring-save、`S-<delete>` kill-region、`C-<delete>` kill-word、`C-DEL` backward-kill-word、`M-<left>` / `M-<right>` backward-word / forward-word（Emacs は left-word / right-word。左から右へ書く文字では同じ）、`C-@` set-mark-command、`C-0`〜`C-9` と `C-M-0`〜`C-M-9` digit-argument、`C--` と `C-M--` negative-argument。`S-<insert>` は、ユーザーが Emacs と同じ `yank` を選んだ（geditor の `paste` と `yank` は同じ関数）。`S-<delete>` は、それまで割り当てが無く `<delete>`（delete-char）へシフト変換されていたので、Shift+Delete は 1 文字の削除から `C-w` と同じ切り取りに変わる（マークがあれば有効でなくてもマークからカーソルまで、マークが無ければ `The mark is not set now, so there is no region`）。対応するコマンドが無いキー（`<insert>` overwrite-mode、`C-<up>` / `C-<down>` の段落、`C-<prior>` / `C-<next>` の横スクロール、`M-<prior>` などのほかのウィンドウ、`C-S-DEL` kill-whole-line、`C-M-` 付きの矢印などの式の単位の移動、`C-x <left>` / `C-x <right>`、`C-x C-;`）は足していない。Apps キー（Emacs の `<menu>`）は右クリックのメニューのまま。次の細部は実装のときに決めた（ユーザーの決定ではない）:

- 全部上の表（Emacs 互換の global）に置き、表の中では今までのキーより後ろに置く。このため 4.9 のメニューの Emacs 側は、既定の設定では今までどおり Cut `C-w`・Copy `M-w`・Paste `C-y`（同じ長さなら表の先のキー）。設定で `C-w` や `C-y` を別のコマンドにすると、そのコマンドのもう 1 つの Emacs のキー（`S-<delete>` / `S-<insert>`）が出る
- `S-<delete>` / `S-<insert>` などは割り当てを持つので、シフト変換をしない。設定の `null` で未割当になり、シフト変換には戻らない（`"S-<delete>": "delete-char"` と書けば以前の 1 文字の削除になる）。Shift を足した `S-M-<left>` / `S-M-<right>` は、シフト変換で `M-<left>` / `M-<right>` になり、単語の単位で選択する
- 既定の設定のテスト（`tests/test_emacs_keys.cpp`）で、各キーが今までのキー（`C-y`・`M-w`・`C-w`・`M-d`・`M-DEL`・`M-b` と `C-<left>`・`M-f`・`C-SPC`・`M-<数字>`・`M--`）と、通常のバッファ・前置引数・続けた kill・マクロ・ミニバッファ・isearch・問い合わせ置換・読み取り専用で 1 手ごとに同じ状態になることを確かめた。isearch の中では isearch の表を先に引き、その中でシフト変換するので、設定の `isearch_keys` で `<insert>` や `<delete>` に割り当てがあると、isearch の中の `S-<insert>` / `S-<delete>` はその割り当てを動かす（`C-S-e` と同じ規則。`<insert>` の場合をテストで確かめた）

isearch 中: `C-s` / `C-r`（`C-M-s` / `C-M-r` も。Emacs の `isearch-mode-map` と同じ）次・前の一致（カーソルが動く一致だけ。2 章「重ねた isearch の一致」）、`DEL` / `C-h` 1 文字戻す、`C-u` 検索語を消す（`isearch-delete-pattern`）、`RET` 確定、`C-g` 取消（開始位置へ戻る）、`M-r` 正規表現の切替、`M-c` 大文字小文字の区別の切替、`M-%` その語で問い合わせ置換へ。

isearch 中の `C-u`（2026-11-18 にユーザーが決定（決定 5 b）: 検索語を空にして isearch を続ける）: 細部は実装のときに決めた（ユーザーの決定ではない）。打った文字と同じく `DEL` で戻せる 1 つの入力で、検索語は空、一致なし、失敗中でも折り返し後でもない状態になり、カーソルはこの isearch を始めた位置に戻る（表示もカーソルに合わせて動く）。向き・正規表現かどうか・`M-c` の切り替えは変えない（プロンプトの `Regexp` などは残る）。検索語が空なら何もせず、失敗にもしない。失敗中や不正な正規表現のときも働く。消した検索語はクリップボードにも、次の空の `C-s` が使う前回の検索文字列にも入れない。isearch の外では `Not in isearch` で失敗する。

問い合わせ置換中: `y` / `SPC` 置換、`n` / `DEL` / `C-h` 飛ばす、`!` 残りを全部置換、`.` 置換して終了、`q` / `RET` 終了、`C-u` 何もしない（`ignore`。2026-11-18 にユーザーが決定（決定 5 b）。コマンド名と、表示も失敗も無いことは実装のときに決めた（ユーザーの決定ではない）。プロンプト・今の一致・強調は変わらず、キーボードマクロの実行も止めない）。置換全体を 1 つの undo 単位にする。正規表現の置換文字列は PCRE2 の `$1` 形式。

ミニバッファ・isearch・問い合わせ置換の途中のほかの層のキー（M5 で決定）: Windows 互換キー（`C-S-<英字>` の 12 個）は働かない。`C-S-<英字>` はそのモードのキーマップの中でシフト変換する（`C-S-g` は `C-g` で取消、isearch の `C-S-s` は `C-s` で次の一致）。isearch のキーマップに無いキーは、isearch を今の一致で終えてから通常どおり（Windows 互換キーを除いたグローバルのキーマップで）実行する（Emacs と同じ。isearch の `C-S-z` は `C-z` になり undo しない）。ミニバッファと問い合わせ置換は今までのミニバッファの規則のまま、自分のキーマップに無いキーを Windows 互換キーを除いたグローバルのキーマップで引く（M-x の `C-S-w` は `C-w` で、終了しない）。

ミニバッファ: `C-a` `C-e` `C-f` `C-b` `C-k` `DEL` `C-h` `C-u` `C-d` などの編集キーは global と同じ（`C-u` は入力の先頭までを kill する。プロンプトはテキストではない。問い合わせ置換の途中で開いたミニバッファでも同じ）。isearch と問い合わせ置換の `C-u` は、2026-11-18 からそれぞれの表の割り当て（上）。`RET` 確定、`C-g` と `ESC ESC ESC` 取消、`TAB` 補完、`M-p` / `M-n` 履歴。

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
    "eol": "#505050", "eol_other": "#d7ba7d",  // eol_other: line ending that differs from the default
    "mode_line_background": "#007acc", "mode_line_foreground": "#ffffff"
  },
  "tab_width": 4,
  "line_numbers": true,
  "menu_bar": true,
  "wrap": { "mode": "max", "column": 80 },       // none | window | fixed | max
  "whitespace": { "space": false, "tab": true, "fullwidth_space": true, "trailing": true },
  "eol_marks": { "show": true, "lf": "↓", "crlf": "↵", "cr": "←" },
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
- 折り返しの意味: `none` は折り返さず横スクロール、`window` はウィンドウ幅、`fixed` は `column` 桁、`max` はウィンドウ幅と `column` 桁の狭い方。表示上の折り返しで、ファイルには改行を入れない。桁は 2 章の桁の数え方（全角 2 桁）で数え、ウィンドウ幅は「文字領域の幅 ÷ 升目の幅」を切り捨てた桁数から 1 を引いた桁数とする（右端の 1 升は改行記号を描くために空けておく）
- 等幅でないフォントを指定しても升目配置で描くため、文字間が不揃いに見える
- 配色は `#rrggbb`。未知のキーは警告して無視する
- `eol_marks` の記号は 1 文字（1 升か 2 升）。それ以外は意味の誤りとして既定の記号を使う
- メニューの View で切り替えた表示（行番号・改行記号・空白記号・折り返し）はそのウィンドウだけに効き、設定ファイルには書き戻さない
- 設定を反映するとき、起動中の編集内容・undo 履歴は保つ

### 4.6 画面構成

```text
+---------------------------------------------+
| File  Edit  View                            |   <- menu bar
+---------------------------------------------+
|  1 | int main() {↵                          |
|  2 |   return 0;↵                           |
|  3 | }↓                                     |   <- LF in a CRLF file (eol_other color)
+---------------------------------------------+
| -** file.c  UTF-8 Mixed(CRLF)  L3 C1  wrap:max  Def |   <- mode line
+---------------------------------------------+
| C-x-                                         |   <- minibuffer / echo area
+---------------------------------------------+
```

mode line: 変更有無（`**`）・読取専用（`%%`）・ファイル名・文字コードと BOM（`UTF-8` / `UTF-8-BOM` / `UTF-16LE` / `UTF-16LE-BOM` / `UTF-16BE` / `UTF-16BE-BOM` / `Shift_JIS` / `EUC-JP` / `ISO-2022-JP`。読めないバイトを置換文字にしたバッファと書き戻して一致しないバッファは末尾に `!`、例 `Shift_JIS!`。M4b。M4a までの `UTF-8 BOM` は `UTF-8-BOM` に変えた）・改行コード（`LF` / `CRLF` / `CR`、混在なら `Mixed(<既定>)`）・行と桁・折り返しモード・マクロ記録中（`Def`）。文字領域の右端に Win32 標準の縦スクロールバーを置く（図では省略）。

### 4.7 描画

- 升目配置: 各書記素クラスタの升目数（1 か 2）を桁の数え方で決め、クラスタ全体を 1 つのグリフの並びとして自分の升目に描き、折り返し位置・カーソル位置・マウス位置はすべて升目の整数計算で求める。`IDWriteTextLayout` による文字組みと折り返しは使わない
- 文字の形は `IDWriteFontFallback::MapCharacters` で使うフォントを決め、`IDWriteTextAnalyzer` でグリフに変換し、送り幅を「升目数 × 升目の幅」に置き換えた `DrawGlyphRun` で描く。升目より広いグリフは升目の幅に収まるよう横に縮める
- 代替フォントは `IDWriteFontFallbackBuilder` で組む
- 折り返しは表示範囲の論理行だけ計算する。長い論理行（数 MB の 1 行など）は、折り返し位置を 64K 文字ごとの区切りで記録して再利用し、スクロールのたびに行頭から数え直さない。1024 単位以上の行は、最後に通った行の区切りと行末も記録して、打鍵や描画のたびに行頭から数え直さない（M6 の後の保留の性能 K1。それまでは 64K 未満の行で毎回行頭から数えていた）。記録から数えても答えは行頭から数えたときと同じにする。画面の行の終わりより右の桁（上下の移動の目標の桁・行の右の外のクリック）は、その行の升目を持つ最後のクラスタに置く（行が幅 0 のクラスタで終わっていても）。64K 以上の行では以前、幅 0 のクラスタに置いていたので、これは不具合の修正（K1 のレビューの再修正 1）。スクロールバーの位置は論理行ベースの近似で出す。M6 の B6: 論理行がウィンドウの行数以下で、画面の行が収まらないとき（数 MB の 1 行など）は、文字の位置で近似する（範囲は文字数、つまみは先頭の画面の行の最初の文字、ページは画面に見えている文字の数。範囲が 16 ビットに収まるよう何文字かずつを 1 つに数える）。収まるかどうかは先頭から画面の行数だけ折り返して調べ、スクロールバーのために長い行全体を折り返さない。つまみを動かすと、その位置の文字の画面の行が先頭に来る。論理行の多いファイルは今までどおり。入力の後のスクロールバーの設定は、その入力の画面を描き終えてから行う（つまみの描き直しで本文の表示を遅らせない）
- 選択範囲・検索一致（表示範囲の全一致）・カーソル・空白記号（半角空白 `·`、タブ `→`、全角空白 `□`、行末空白の背景）を重ねて描く
- 改行記号: 改行のある行の末尾に、その行の改行コードの記号（既定 LF `↓` / CRLF `↵` / CR `←`）を `eol` 色で描く。ファイルの既定と違う改行コードの行は `eol_other` 色で描く。最終行に改行が無ければ何も描かない。記号は行末の次の升目に描き、カーソル位置や折り返しの桁には数えない。行が折り返し幅いっぱいでも、右端に空けた 1 升（4.5）に描くので欠けない。カーソルは升目の左端に立つ縦棒（幅 2px 相当）で描くので、行末にカーソルがあっても記号は隠れない
- 絵文字はフォントの色で描く（カラー絵文字。Segoe UI Emoji の COLR などの色付きグリフを、IME の候補一覧と同じ色で出す）。色で描くのは、2 章の桁の数え方で絵文字として 2 桁に数えるクラスタだけ。☹ ❤ ✔ ☀ のように文字表示が既定の記号と U+FE0E を付けたもの（😀＋U+FE0E も含む）は 1 色の文字として描く（2026-11-14 にユーザーが確定）。升目に収める規則と、選択範囲の背景・カーソルの重ね方は他の文字と同じ。色付きグリフを描かない画面では起動時間に影響させない（2026-11-14 にユーザーが確定。以前は「初版では単色」）
- IME の変換ウィンドウと候補ウィンドウはカーソル位置に置き、フォントも合わせる（`ImmSetCompositionWindow` / `ImmSetCandidateWindow` / `ImmSetCompositionFont`）

### 4.8 保存

1. 同じフォルダに一時ファイルを書き、`FlushFileBuffers`
2. `ReplaceFileW` で置き換える。`backup` が ON でこのセッション初回の保存なら、第 3 引数で元ファイルを `file~` として残す
3. 新規ファイル（置き換え先が無い）は `MoveFileExW` で移す
4. 保存前に、読み込み時から更新日時が変わっていないか確かめ、変わっていれば確認する
5. 書き出すバイト列は、piece ごとに `\n` をその piece の改行コードへ戻してから文字コードへ変換する
6. リンクを保つ（2026-11-14 にユーザーが確定）: 保存先がシンボリックリンクなら、リンク先の実体を 1〜3 の手順で置き換える（リンクはそのまま）。ハードリンク（リンク数 2 以上）は置き換えずに同じファイルへ直接書き戻す。このとき `backup` が ON でセッション初回なら、書く前に `file~` を写しで作る
7. 一時ファイルの名前（`.geditor-tmp` と `.geditor-old`）は、ボリュームの名前の最大長（`GetVolumeInformationW`。読めなければ 255）に収まるよう、元のファイル名の部分を切り詰める。プロセス番号と通し番号で一意に保つ。`file~` そのものが最大長を超えるファイルは、`file~` を作らずに保存し、エコー領域にそのことと `file~` のパスを出す（保存は失敗にしない）（M4a の修正 F3。M4a の当初の名前では 226 文字ほど以上のファイル名が保存できなかった）
8. `ReplaceFileW` には `REPLACEFILE_IGNORE_MERGE_ERRORS` を付けない。属性や ACL を引き継げなければ保存の失敗として扱い、他の失敗と同じく元のファイルと `file~` を変えず、一時ファイルを残さない（M4a の修正 F4）。エラー 1175〜1177 の表示は、何が起きたか・起きうる理由（属性や暗号化などの権限を新しいファイルに引き継げない）・`write-file`（`C-x C-w`）で別の名前に保存することを書く（M6 の後の保留の修正 F2）
9. `file~` を作る段階の失敗の表示には `file~` のパスを入れる（例 `Cannot write <path>: making the backup <path>~: Access is denied`。M4a の修正 F5）

### 4.9 メニューバー

メニューの中身はコア（`menu.*`）にデータとして持ち、Win32 側はそれを `HMENU` に組み立てるだけにする。各項目はコマンド名（と引数）を持ち、右側に表示するキーはその時点のキーマップから引き、Windows 側のキー、空白 2 つ、Emacs のキーの順に出す（例: Undo は `C-S-z  C-/`、Save は `C-S-s  C-x C-s`、Cut は `C-S-x  C-w`、Copy は `C-S-c  M-w`、Paste は `C-S-v  C-y`、Select All は `C-S-a  C-x h`。2026-11-15 にユーザーが確定）。Windows 側は、設定ファイルと Windows 互換キーのうち、そのコマンドを同じ引数で呼ぶキーで最も短いもの 1 つ（同じ長さなら設定が先）。Emacs 側は、Emacs 既定のキーのうち、今のキーマップでもそのコマンドを同じ引数で呼ぶキーで最も短いもの 1 つ（設定で別のコマンドにしたキーは出さない）。Emacs 側では、次の同じ動作の表（両向き。`menu.*` の 1 か所に置く）のコマンドのキーも同じ規則で候補にする（表示だけで、割り当ては変えない。同じ長さなら項目のコマンド自身のキーが先）: `cut`↔`kill-region`（`C-w`）、`copy`↔`kill-ring-save`（`M-w`）、`paste`↔`yank`（`C-y`）、`select-all`↔`mark-whole-buffer`（`C-x h`。カーソルが先頭と末尾のどちらに来るかだけが違うが、ユーザーが表示を受け入れた）。片方しか無ければそれだけ、どちらも無ければ何も出さず、両方が同じキー列なら 1 回だけ出す。チェックとラジオの状態はメニューを開く直前（`WM_INITMENUPOPUP`）にコアへ問い合わせる。まだ実装していない段階のコマンドの項目は灰色にする。

| メニュー | 項目（コマンド） |
|---|---|
| File | Open... (`find-file`), Open with Encoding... (`find-file-with-encoding`), Reopen with Encoding... (`revert-buffer-with-coding-system`), ─, Save (`save-buffer`), Save As... (`write-file`), ─, Encoding for Saving ▶ 9 種（ラジオ, `set-buffer-file-coding-system`）, Line Endings ▶ LF / CRLF / CR（ラジオ。混在ファイルでは既定に印, `set-eol`）, ─, Read Only（チェック, `read-only-mode`）, ─, Settings (`find-user-init-file`), Exit (`quit`) |
| Edit | Undo (`undo`), Redo (`redo`), ─, Cut (`cut`), Copy (`copy`), Paste (`paste`), Select All (`select-all`), ─, Find... (`isearch-forward`), Replace... (`query-replace`), Go to Line... (`goto-line`) |
| View | Line Numbers（チェック, `toggle-line-numbers`）, Line Ending Marks（チェック, `toggle-eol-marks`）, Whitespace Marks（チェック, `toggle-whitespace-marks`）, Wrap ▶ None / Window / Fixed / Max（ラジオ, `set-wrap-mode`）, ─, Zoom In (`text-scale-increase`), Zoom Out (`text-scale-decrease`), Reset Zoom (`text-scale-reset`) |

「...」の付いた項目は、選ぶとミニバッファで続きを入力する。

本文を右クリックすると、Edit の簡易メニュー（Undo / Redo / ─ / Cut / Copy / Paste / Select All）を出す（2026-11-13 にユーザーが確定）。項目は上の Edit メニューと同じコアの定義から作り、右側に今のキー割り当てを出し、有効・無効の規則もメニューバーと同じにする。右クリックではカーソルと選択範囲を動かさない。メニューバーを非表示（`menu_bar: false`）にしても出す。

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
- mode line、ミニバッファ（Tab 補完付き）、C-x C-f / C-x C-s / C-x C-w（UTF-8 のみ）、未保存時の確認。改行は LF か CRLF にそろったファイルだけを扱い、混在・CR のみのファイルはメッセージを出して開かない（M4 で行ごとの保持に置き換える）
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
- Windows 互換キー（4.4）、保存検知での再読込、エラー表示（エコー領域を広げる）、M-x、`find-user-init-file`、`--settings` 起動引数
- 前置引数（C-u、C-u C-u、数字、負数）。キー解決の一部なのでここで作る
- 後の段階で作るコマンド（検索・置換・goto-line・マクロなど）は、名前だけ登録した「未実装」コマンドとして置き、実行すると `<command> is not implemented yet` と表示する（メニューでは灰色）
- 受入条件
  - [M2-1] JSONC パーサーがコメント・末尾カンマを受け付け、構文誤りの行・桁を正しく返す（正常・境界・異常のテスト）
  - [M2-2] 4.2 の表の全表記を解析・再出力して一致するテストがある
  - [M2-3] 4 形の割り当て、`null` の解除（下位へ落ちないこと）、プレフィックス衝突と未知コマンドの項目単位の無視、構文誤りでの全体不採用をテストで確かめる
  - [M2-4] 4.4 の Windows 互換キー（`C-S-` の 12 個）すべてが既定で期待するコマンド名に解決され、`C-<英字>` へのシフト変換より優先されることをテストで確かめる（コマンドの動作は各段階で確かめる）。2026-11-15 に Ctrl+Alt（`C-M-`）から変更した
  - [M2-5] 起動中に設定ファイルを保存すると 1 秒以内に反映され、構文誤りなら直前の設定が残る（手動確認）
  - [M2-6] 前置引数が移動・削除・挿入・コマンド列に効くことをテストで確かめる
  - [M2-7] 誤りが 1 件・10 件・11 件以上のときのエコー領域の行数と `... and N more`、次のキーで 1 行に戻ることをテストで確かめる。`find-user-init-file` が、ファイルのある場合は開く確認を経て開き、無い場合は例を入れた未保存の新規バッファになり、保存した内容がそのまま設定として読めることをテストで確かめる

### M3: 表示

- 2 つに分けて進める。M3a: 表示（下の 1 項目目と Per-Monitor V2、View のコマンドの M-x からの実行）。受入条件 M3-1〜M3-4・M3-7 を確かめる。M3b: メニューバー・右クリックの簡易メニュー・マウス。受入条件 M3-5・M3-6・M3-8 を確かめる
- フォント（名前・サイズ・代替）、配色、行番号、折り返し 4 モード、タブ幅、空白の可視化、改行記号（4.7。M3 では LF / CRLF にそろったファイルの記号まで）、text-scale
- メニューバー（4.9）と View メニューのコマンド（toggle-line-numbers / toggle-eol-marks / toggle-whitespace-marks / set-wrap-mode）、menu-bar-open
- Per-Monitor V2、マウス（クリック・ドラッグ選択・ダブルクリックで単語選択・Shift+クリック・ホイール）、右クリックの Edit 簡易メニュー。ドラッグ＆ドロップはファイルを開くもの（M1 で作成済み）だけで、選択した文字列をドラッグで移動・複写する機能は作らない（選択範囲の中からドラッグしても選択し直しになる。2026-11-13 にユーザーが確定）
- 受入条件
  - [M3-1] 折り返し 4 モードの改行位置と、位置↔升目の変換を `layout` の単体テストで確かめる（全角混じり・タブ・幅ちょうどの境界・全角が行末に 1 升だけ残る場合を含む）
  - [M3-2] 設定の各表示項目を変えると画面に反映される（手動確認。項目ごとにチェック表を作る）
  - [M3-3] 英字は Consolas、日本語は代替フォントで描かれる（手動確認）
  - [M3-4] 起動時間が M0 の基準を保つ（メニューバーを表示した状態で測る）
  - [M3-5] メニュー: 4.9 の表の全項目について、コアのメニュー定義が表どおりのコマンド名を持ち、そのコマンドが登録済みであることを表駆動のテストで確かめる。表示するキーが今のキーマップと一致し、設定でキーを変えると表示も変わることをテストで確かめる。View メニューのチェック・ラジオ（Line Numbers / Line Ending Marks / Whitespace Marks / Wrap）の状態がコマンド実行で切り替わることをテストで確かめる（File メニューのチェック・ラジオは M4-9）。未実装コマンドの項目と、ミニバッファの途中の全項目が無効になることをテストで確かめる（isearch・問い合わせ置換の途中は M5-7）
  - [M3-6] メニュー（手動確認）: F10 とマウスで開ける。Alt 単独と Alt+F ではメニューに入らず、Alt+F は `forward-word` になる。Alt+Space でシステムメニューが開く。`menu_bar: false` で消え、F10 で `Menu bar is hidden` と出る。未実装コマンドの項目は灰色
  - [M3-7] 改行記号: どの行にどの記号と色を出すかをコアの単体テストで確かめる（LF ファイル・CRLF ファイル・最終行に改行が無い場合・`show: false`）。画面では記号が行末に出て、カーソル位置と折り返しに影響せず、折り返し幅いっぱいの行でも欠けず、行末のカーソルに隠れないことを手動確認する
  - [M3-8] 右クリックの簡易メニュー: 項目・コマンド名・キー表示・有効無効がメニューバーの Edit と同じ定義から出ることをテストで確かめる。右クリックでカーソルと選択範囲が動かず、選んだ項目のコマンドが実行されることを実機で確かめる
- メニューバーを付けると、Windows がプロセスで最初のメニューバーを付ける処理で `app_ms` が約 11ms 増える。M3-4 の判定ではこれを受け入れる（2026-11-14 にユーザーが確定）

### M3c: 書記素クラスタと F10

- M3b の手での確認（2026-11-14）で見つかった 2 点を直す。カーソル単位と桁の数え方を書記素クラスタにする（2 章）。F10 で File メニューを下に開く（2 章のメニューバー、4.2）
- 受入条件
  - [M3c-1] 書記素クラスタの切れ目を、Unicode の `GraphemeBreakTest.txt`（文字幅表と同じ版）の全例で確かめる
  - [M3c-2] 桁の数え方（2 章）を、U+FE0F 付き・U+FE0E 付き・肌の色付き・ZWJ でつないだ絵文字・国旗・キーキャップ・結合文字・ハングルの字母の並びの例で単体テストする。折り返し・カーソル・マウスの位置がクラスタの途中に来ないことも確かめる
  - [M3c-3] C-f / C-b / DEL / C-d / 単語移動 / クリック / ダブルクリック / 選択がクラスタ単位で動き、1 回の DEL で 1 クラスタが消え、undo で戻ることをテストで確かめる。IME で入れた U+FE0F 付きの顔文字が 2 桁で描かれ、DEL 1 回で消えることを手で確かめる
  - [M3c-4] F10 で File メニューが下に開き、左右キーで移り、Esc で閉じる（`--visible` の自動確認と手での確認）
  - [M3c-5] M1-5 の打鍵の基準と M3-4 の起動時間を保つ
  - [M3c-6] `ESC ESC ESC`（keyboard-escape-quit）でミニバッファを抜け、選択範囲が解除される。ミニバッファの外でも選択範囲が解除される。ESC 1 回・2 回は今までどおり Meta の前置き（テストと実機確認）
  - [M3c-7] 指定できる文字コードを 9 種に絞る（2 章）。メニューの Encoding for Saving と、文字コード名の補完の候補が 9 種になることをテストで確かめる

### M3d: カラー絵文字と F10 の左右移動

- M3c の手での確認（2026-11-14）で見つかった 2 点。絵文字が単色で描かれる（ユーザーがカラーを希望。4.7）。F10 で開いた File から ← で View に移らない（2 章のメニューバー）
- 受入条件
  - [M3d-1] 絵文字（U+FE0F 付きの顔文字、肌の色付き、ZWJ でつないだ絵文字、既定で絵文字表示の文字）が色付きで描かれ、2 升に収まることを画像で確かめる。絵文字でない文字の色は設定どおりで変わらない。選択範囲の中の絵文字も色付きで、背景は選択の色
  - [M3d-2] F10 で開いた File から ← で View、View から → で File に移る（端で折り返す）。`--visible` の自動確認と手での確認
  - [M3d-3] M1-5 の打鍵の基準と M3-4 の起動時間を保つ（絵文字の無い画面で起動時間が変わらない）

### M4: 文字コードと保存

- 2 つに分けて進める（2026-11-14 にユーザーが確定）。M4a: 改行と保存（行ごとの改行コード・混在・CR・`set-eol`・`Mixed(...)` 表示・`eol_other` 色・クリップボードの CRLF・安全な保存・`file~`・外部変更の確認・読取専用・リンクの保持）。受入条件 M4-4・M4-6・M4-8 と、M4-9 の Line Endings と Read Only、M4-1 は UTF-8（BOM あり・なし）の 2 種 × 改行 4 種の 8 組。M4b: 文字コード（判定・9 種・指定して開く・開き直す・`--encoding`・100MB の読込）。受入条件 M4-1 の全 36 組・M4-2・M4-3・M4-5・M4-7 と、M4-9 の Encoding for Saving

- 判定（BOM → UTF-16 の NUL 偏り → UTF-8 の妥当性 → CP932）、変換、改行の行ごとの判定と保持（M1 の piece tree の piece に改行コード属性を足す。2 章「内部の改行」「新しく入る改行」「行の結合・分割」「既定の改行コードと Mixed 表示」「クリップボードへの書き出し」）
- 指定できる 9 種の文字コード、find-file-with-encoding、revert-buffer-with-coding-system、`--encoding` 起動引数
- 文字コード・BOM・改行の変更コマンド（File メニューの項目を有効にする）、混在ファイルの改行記号と `Mixed(...)` 表示、安全な保存、`file~` バックアップ、外部変更の確認、読取専用
- 受入条件
  - [M4-1] 9 種の文字コード × 改行 4 種（LF / CRLF / CR / LF と CRLF と CR の混在）の 36 組すべてで、その文字コードで表せる見本テキストの読込（文字コード指定）→保存がバイト単位で元と一致するテスト
  - [M4-2] 判定のテスト: 各 BOM、BOM なし UTF-16、ASCII のみ（UTF-8 と判定）、改行の無いファイル（`new_file.eol` を採用）、Shift_JIS の短い文（UTF-8 と誤判定しやすい例を含む）、壊れたバイト列
  - [M4-3] Unicode 系以外の各文字コードで表せない文字を含むと保存が止まり、ファイルが変わらないテスト
  - [M4-4] 保存中に書き込みが失敗しても元ファイルが残ること、`file~` がセッション初回だけ作られることのテスト
  - [M4-5] 100MB の UTF-8 ファイル（多数行のものと、改行の無い 1 行だけのもの）を 1 秒以内に開いて表示でき、1 行のものでも `window` 折り返しで末尾までスクロールしたときの 1 画面分の再描画が 16ms 以下。100MB で 1 行おきに LF と CRLF が交互に並ぶファイルも 2 秒以内に開ける
  - [M4-6] 混在ファイルのテスト（期待値は 2 章の各行の規則と例）: 一部の行を編集して保存しても他の行の改行コードが変わらない。入力した改行は既定の改行コード。CRLF・単独 CR を含むテキストを貼り付けると `\n` に直って既定の改行コードになる。既定の決め方（多い方、同数なら CRLF > LF > CR）。2 章「行の結合・分割」の例（`a⏎b↓c⏎` → `ab↓c⏎` → `a⏎b↓c⏎`）と、改行の無い最終行への改行の追加。既定と違う改行を全部消すと表示が `Mixed(...)` から既定だけに変わる。`set-eol` で全行がそろい、1 回の undo で元の混在に戻る。kill / copy でクリップボードへ書いたテキストの改行が CRLF
  - [M4-7] 指定して開く・開き直すのテスト: 9 種それぞれで日本語の見本を正しく読む。読めないバイトは U+FFFD になり、バイト位置と行が表示され、モードラインに `!` が付く。保存しようとすると確認が出る（M4b では Unicode 系だけで、Unicode 系以外では中止していた。M6 の B4 でどの文字コードも確認し、Yes の後に残った U+FFFD など書けない文字で中止するように変えた）。書き戻して一致しない場合も同じ。開き直しで未保存の変更があると確認が出て、キャンセルなら何も変わらない。ファイル名の無いバッファでの開き直しは何もしない。読み違えた後に別の文字コードで開き直すと正しく読める。`--encoding` 付きの起動引数が新しいウィンドウに渡る。`shift_jis` などで似た文字への置き換え（best fit。例: `¥` U+00A5）が起きる文字を保存しようとすると中止される
  - [M4-8] 改行記号（手動確認）: 混在ファイルで LF / CRLF / CR の 3 記号が区別でき、既定と違う行が `eol_other` 色で出る。モードラインが `Mixed(<既定>)` になり、File > Line Endings のラジオが既定を指す
  - [M4-9] File メニューのチェック・ラジオ（Encoding for Saving / Line Endings / Read Only）の状態が、対応するコマンドの実行と開き直しで切り替わることを表駆動のテストで確かめる。M4 で実装した File メニューの項目がすべて有効になることを確かめる
- M4b の実装（2026-11-15）: 読み書きはコアの `src/core/encoding.*`（コードページは kernel32 の変換関数を直接呼ぶ）。どの文字コードもまず `\r` と `\n` を残した UTF-16 にし、改行は UTF-16 の上で 1 つの関数が分ける。テストは `tests/test_m4b_encoding.cpp`（M4-1 の 36 組・M4-2 の判定の各分岐・M4-3・M4-7 の読めないバイト）と `tests/test_m4b_commands.cpp`（M4-7 の開き直し・指定して開く・`--encoding`、M4-9、9 種の名前）。実機は `tools/app_check.py --only-m4b`。M4-5 は `tools/measure_input.py --m4-5 --wrap window` で測る（ヘッドレスの値は参考値。16ms の基準は画面に出した値で決めたもの）

### M5: 検索と置換

- isearch（通常・正規表現）、一致の強調表示、問い合わせ置換（通常・正規表現）、goto-line
- 受入条件
  - [M5-1] isearch の各キー（4.4 の「isearch 中」の一覧）に最低 1 件のテスト。`C-u C-s` / `C-u C-r` が正規表現検索になることも確かめる（2026-11-17 から既定のキーは `C-M-s` / `C-M-r`。前置引数の形は `universal-argument` を割り当てたときに確かめる）
  - [M5-2] 問い合わせ置換の各キーに最低 1 件のテスト。置換全体が 1 回の undo で戻る
  - [M5-3] 正規表現: サロゲートペア・全角・`$1` 置換・不正なパターンのエラー表示をテストで確かめる
  - [M5-4] smart case の 3 設定をテストで確かめる
  - [M5-5] 100MB ファイルで、末尾付近の語の検索が 1 秒以内に見つかる
  - [M5-6] Windows 互換キー `C-S-f` / `C-S-h` / `C-S-r` / `C-S-g` が検索・置換・goto-line として動くことをテストで確かめる
  - [M5-7] isearch・問い合わせ置換の途中はメニューの全項目が無効になり、終わると元に戻ることをテストで確かめる。Edit メニューの Find / Replace / Go to Line が有効になり、選ぶとそれぞれのコマンドが始まることをテストで確かめる
- M5 の実装（2026-11-15）: 照合はコアの `src/core/search.*`（PCRE2 16bit・UTF・JIT。通常の検索は `PCRE2_LITERAL`、正規表現は `PCRE2_UCP`、どちらも `PCRE2_MATCH_INVALID_UTF`）だけが行い、isearch・強調表示・問い合わせ置換が共用する。論理行ごとに照合し、1 つの chunk に収まる行はその場で、またがる行だけを写す（バッファ全体は写さない）。PCRE2 は最初の検索のときに初めて呼ぶ。テストは `tests/test_m5_search.cpp`、実機は `tools/app_check.py --only-m5`、M5-5 は `geditor_perf`（100MB で末尾から 100 行目の語。2026-11-15、他のプロセスの負荷がある開発 PC で、最初のキーの全体の走査 58〜84ms、private bytes の増分の最大 0.27MB。ヘッドレスのアプリでキーから状態に出るまで 141ms）。PLAN に無かった細部は Emacs に合わせて次のように決めた（詳細は `AGENTS.md` の「検索・置換・goto-line（M5）」）
  - 見つからなければ `Failing I-search: <パターン>`、次の `C-s` で折り返して `Wrapped I-search`。プロンプトは `I-search: ` / `I-search backward: ` / `Regexp I-search: `（`M-c` で切り替えると `Case-sensitive` / `Case-insensitive` が付く）
  - `DEL` は最後の入力（文字・繰り返し・切り替え）を取り消す（Emacs の `isearch-delete-char`）。`C-g` は失敗中でも 1 回で開始位置に戻る（Emacs は失敗中の `C-g` でまず一致しない部分を消すが、4.4 の「取消（開始位置へ戻る）」に合わせた）。`RET` とほかのキーで終えると開始位置にマークを置く（`Mark saved where search started`）。空のパターンの `C-s` は前回の検索文字列を使う
  - 不正な正規表現は PCRE2 のメッセージをエコー領域に出し（`Regexp I-search: a( [missing closing parenthesis]`、問い合わせ置換は `Invalid regexp: ...`）、バッファは変えない
  - 問い合わせ置換はカーソルから末尾まで（折り返さない。リージョンでの範囲指定はしない）。大文字小文字は `search.case_fold` に従う。置換の文字の大小を一致に合わせること（Emacs の `case-replace`）はしない
  - goto-line は `kLineParams` のとおりミニバッファで行番号を読み、前置引数があればその行へ直接移る。範囲外は最後（または最初）の行に寄せ、行頭に置く。Emacs と同じく、リージョンが有効でなければ元の位置にマークを置く
  - ほかの層のキーの扱いは 4.4 の「ミニバッファ・isearch・問い合わせ置換の途中のほかの層のキー」
  - M5 のレビューでの修正（2026-11-15）: R1 問い合わせ置換の途中に開いたミニバッファ（`M-x`、goto-line）には文字と IME の文字列が入り、そこでの `C-g` はミニバッファだけを閉じる。R2 isearch で向きを変えた後にパターンを伸ばすと、今の一致から探す（`C-s fo C-s C-r o` が 8〜11 の `foo` を見つける）。R3 `!` は行ごとに行の写しの上で残りの一致を探して 1 回の編集で置き換える。16 万文字の行の 2 万個の置換が `geditor_perf` で 19 秒かかっていた（一致ごとに編集した行全体を集め直し、折り返した行をたどって表示を合わせていた）のが 3.5ms（`$1` の正規表現で 6.4ms）、50 万行の 50 万個は 46 秒から 1.7 秒

- M6 の前の保留だった性能の改善（2026-11-15、ユーザーが計画と目標を承認）: 結果（読んだテキスト・U+FFFD とその位置・`!` と表示・検索で見つかる一致・折り返しの位置・カーソルの位置と書記素クラスタの境目）と判定の順・規則は変えずに、Unicode 系以外の読込（変換の大きさの問い合わせをやめ、判定で Shift_JIS を UTF-8 として全部読まず、書き戻しで元に戻らないファイルを約 64KiB ずつまとめて読む）、長い行の後ろ向きの正規表現検索（`max_start` から後ろへ窓ごとにたどる）、長い 1 行の `<end>`（1 単位で 1 クラスタになる文字の続きを、クラスタを作らずに升目に置く）を速くした。HEAD 63a24a6 と交互に測って、100MB の Shift_JIS の判定 1497〜1523ms → 937〜941ms、読めないバイトが 1 つある 100MB の Shift_JIS（指定）1296〜1326ms → 953〜963ms、最後の `ESC ( B` が無い 10MB の 1 行の ISO-2022-JP 1954〜1970ms → 320〜321ms（読込と保存のメモリは 63a24a6 以下）、40 万文字の 1 行の末尾から `C-M-r .*`（当時は `C-u C-r .*`）約 140 秒 → 1ms 未満、10MB の 1 行の `<end>`（ヘッドレスのアプリ）80〜81ms → 29〜43ms、100MB の 1 行 773〜840ms → 254〜286ms。作りと全部の値は `AGENTS.md` の「保留だった性能の改善（M6 の前）」、新旧の結果の比較は `tests/test_encoding_stretches.cpp` / `tests/test_search_backward.cpp` / `tests/test_layout_lone.cpp`

### M6: Emacs の拡張機能と仕上げ

- キーボードマクロ、連続 kill の追記、Shift 選択の仕上げ
- README.md（使い方・設定項目・コマンド一覧・既定キー表）
- 受入条件
  - [M6-1] 連続 kill の追記と Shift 選択（4.4 の Shift 付き移動キー）をテストで確かめる
  - [M6-2] マクロの記録→実行、`C-u 3 C-x e`（2026-11-17 から `M-3 C-x e`）、記録中の C-g、マクロ内のミニバッファ入力をテストで確かめる
  - [M6-3] README のコマンド一覧と、コマンド登録表の全コマンドが一致する（テストで突き合わせる）
  - [M6-4] M0〜M5 の性能基準を保つ
- M6 の実装（2026-11-16）: テストは `tests/test_m6.cpp` と `tests/test_readme.cpp`（README のコマンド一覧と既定のキーの表を、登録表と `keymap.cpp` の既定の表に突き合わせる）、実機は `tools/app_check.py --only-m6`、マクロの速さは `geditor_perf macro`。作りの詳細は `AGENTS.md` の「M6」。PLAN に無かった細部は Emacs に合わせて次のように決めた
  - マクロはキーの入力（ミニバッファ・isearch・問い合わせ置換の中のキーと IME の確定文字列を含む）を記録し、実行はそれを本物のキーと同じ入口に流す。マウスとメニューの操作は記録しない。プロセスの中だけに持ち、設定ファイルには書かない
  - 記録中の `kmacro-start-macro` は `Already defining keyboard macro`、記録していないときの `kmacro-end-macro` は `Not defining kbd macro`、マクロが無いときの `kmacro-end-and-call-macro` は `No kbd macro has been defined` と出して何も変えない。何も記録しなかった記録は `Ignore empty macro` で前のマクロを残す
  - 記録中の `C-g` は記録をやめ、前のマクロが残る
  - 実行の途中でコマンドが失敗する（バッファの端・検索の失敗・`Buffer is read-only`・割り当ての無いキーなど）か、確認に No / Cancel で答える（`Cancelled`・`Not saved`・開き方や保存の確認の Cancel。M6 の修正 R3）と、そこで止めて残りの回数も実行しない。isearch の途中で止まったときは開始位置に戻して isearch を終える
  - 実行中の `C-g` で止まる（`Quit`。それまでの分は 1 つの undo 単位。約 50ms ごとに確かめる。M6 の修正 R2）。実行中に来たキーとマウスの入力の中のどこに `C-g` があっても止まり、`C-g` より前の入力は `C-g` と一緒に捨て（Emacs も止めるときに待っている入力を捨てる）、後の入力は止まった後に働く。`C-g` が無ければ、実行中に来た入力は実行の後に来た順に働き、ほかのメッセージ（メニューの項目・閉じる操作など）を追い越さない（保留の修正 E4、2026-11-17、レビューの後の再修正 R1）。そのうちの 1 つが確認やメニューを開いた（右クリックなど）ときは、それより後の入力は捨てる（再修正 Q2）。`C-g` で捨てる入力にボタンを離す操作があっても、ドラッグは終わる（再修正 Q3）。キー、マウスの動き・クリック・ホイール（スクロールバーやメニューバーの上での動きを含む）、タッチとペンの操作より後ろの `C-g` は効く（再修正 P1）。実行中のスクロールバー・メニューバー・タイトルバー・枠のクリック（`WM_NCLBUTTONDOWN`〜`WM_NCXBUTTONDBLCLK`、ペンとタッチの `WM_NCPOINTERDOWN` / `WM_NCPOINTERUP`）は取り出して捨て、その後ろの `C-g` も効く（2026-11-17 にユーザーが決定。それまではそこで取り出すのをやめ、後ろの `C-g` は実行が終わるまで効かなかった）。それ以外の待っているメッセージ（ファイルのドロップ、メニューの項目の選択、ウィンドウを閉じる操作など）や、画面に出したウィンドウで IME が受け取ったキーより後ろの `C-g` は、実行が終わるまで効かない（メッセージごとの扱いは `AGENTS.md` の表）
  - 1 回の呼び出しは、中のイベントが呼び出しより前の編集を undo しても 1 つの undo 単位（再修正 R2）。問い合わせ置換が開いたまま終わった呼び出しは、それだけで 1 つの単位で、その後の置換が別の単位になる（再修正 R3）
  - 続けて `e` で繰り返せるのは、実行の後に isearch・ミニバッファ・問い合わせ置換が開いていないときだけ（M6 の修正 R5）。ほかのキー・クリック・ホイール・スクロールバー・メニュー・ファイルのドロップ・IME の文字列で繰り返しは終わる（M6 の修正 R4）
  - 記録中の `kmacro-end-and-call-macro`（`C-x e` も `<f4>` も）は記録を終えるだけで実行しない（4.4 の `<f4>` に合わせた。Emacs の `C-x e` は終えてから実行する）。前置引数が 0 以下なら `Repeat count must be positive`
  - 続けた kill は、ほかのコマンド・マウスのクリックで切れる。別のプログラムがクリップボードを書き換えても気づかない（2 章「kill と クリップボード」）
  - M6-4（2026-11-16、ヘッドレス、HEAD 84cbaeb と交互）: 起動・打鍵・`<end>`・ファイルを開く時間は HEAD と同じ程度（値は `AGENTS.md` の「M6」）。入力の後のスクロールバーの設定を描画の後に回した（4.7）。キーボードマクロの速さは、1000 文字のマクロを 100 回実行する時間と同じ 10 万キーを直接打つ時間の比で確かめる（`geditor_perf macro`。3 組の比の中央値が 1.25 倍以下）。測ると 0.97〜1.01 倍で、時間はどちらも約 19〜21 秒。10 万文字まで伸びる 1 行での 1 打鍵あたりの処理（10 万文字で約 0.2ms）が占め、これは保留の課題として残す（検証の最初の基準「1 秒以内」はこれを測らずに決めた誤りで、2026-11-16 に比の基準に改めた）
  - M6 の後の保留の修正（2026-11-17。どれも振る舞いが 1 つに決まる不具合で、詳細は `AGENTS.md` の「保留の修正（M6 の後）」）: S2 問い合わせ置換の途中で始める前の編集まで undo しても、残った置換が 1 つの undo 単位で、それより前の編集を混ぜない。S3 `Replaced N occurrence(s)` の N は終わった時点で残っている置換の数。S4 書記素クラスタの途中で終わる置換の後のカーソルはクラスタの後ろ（強調はクラスタに広げ、置換は一致したところだけ）。S5 isearch の途中の設定の読み直しで `search.case_fold` の区別が変われば、その場で探し直す（`DEL` で戻った状態も、戻ったときに。再修正 R4 / Q4）。S6 問い合わせ置換の途中の `C-s` / `C-r` は `I-search cannot be started during query-replace`。F1 LF を CR として保存した後の既定の改行コードは開き直したときと同じ。F2 `ReplaceFileW` のエラー 1175〜1177 の表示に理由と別名での保存を書く。E1 つながる挿入の redo の後のカーソルは元の挿入の後と同じ。E2 フォントの作成に失敗した読み直しの `Font not found` は、そのフォントを使えた時に出す。E3 末尾の空白の判定を 1 か所にする。E4 上のマクロの `C-g`。S1（長い行の後ろ向きの正規表現検索で、どの一致も範囲の後ろで終わるときの遅さ）は、今の結果を変えずに速くする方法が見つからず、残した（2026-11-17 に、範囲の終わりをテキストの終わりとして照合する規則に変えて解消した。2 章「後ろ向きの検索の範囲の終わり」）
  - 2026-11-17 にユーザーが決めた 4 件（詳細は `AGENTS.md` の「2026-11-17 の変更」）: S1 の解消（上）。マクロの実行中の本文の外のクリックを捨て、後ろの `C-g` を効かせる（上の `C-g` の項）。`C-h` を `DEL` と同じにする（通常・isearch・問い合わせ置換の表）。`C-u` を行の先頭までの kill（`backward-kill-line`）にし、`universal-argument` の既定のキーを外し、正規表現の isearch を `C-M-s` / `C-M-r`（`isearch-forward-regexp` / `isearch-backward-regexp`）にする
  - 2026-11-18 にユーザーが決めた 3 件（詳細は `AGENTS.md` の「2026-11-18 の変更」）: 決定 3 a 重ねた `C-r` は今の一致の先頭から始まる一致を返さない（[C12] の確定。2 章「後ろ向きの検索の範囲の終わり」）。決定 4 a 重ねた `C-s` / `C-r` はカーソルが動く一致だけを返す（2 章「重ねた isearch の一致」）。決定 5 b isearch の中の `C-u` は検索語を消して isearch を続け（`isearch-delete-pattern`）、問い合わせ置換の中の `C-u` は何もしない（`ignore`）。通常のバッファとミニバッファ（問い合わせ置換の途中で開いたものも）の `C-u` は `backward-kill-line` のまま（4.4）
  - 2026-11-18 にユーザーが決めたマクロのキー（詳細は 4.4 の「マクロの 1 回の押下のキー」と `AGENTS.md` の「2026-11-18 の変更（マクロの 1 回の押下のキー）」）: 記録 `C-(`、記録の終了 `C-)`、実行 `C-S-e` を既定のキーに足した。今までのキーは残る。`C-S-e` は `C-e` の Shift 選択にならなくなった
  - 2026-11-19 にユーザーが決めた Emacs の既定のキーの追加（詳細は 4.4 の「Emacs の既定のキーの追加」）: `S-<insert>` yank、`C-<insert>`、`S-<delete>`、`C-<delete>`、`C-DEL`、`M-<left>` / `M-<right>`、`C-@`、`C-<数字>` / `C-M-<数字>`、`C--` / `C-M--`。Shift+Delete は 1 文字の削除から切り取りに変わった
  - 2026-11-20 にユーザーが決めた IME の確定文字列の前置引数（詳細は 4.2 の「IME の確定文字列の前の前置引数」）: 確定文字列の全体を前置引数の回数だけ入れる
  - 2026-11-20 にユーザーが決めた版とリリース（詳細は `AGENTS.md` の「版・リリース・CI」）: 版は `VERSION` に置き、正式なリリースは MINOR が偶数、開発中は奇数。最初の正式なリリースは 0.2.0。exe に版の情報を入れ、GitHub のリリースに exe の zip を付け、変更点は `CHANGELOG.md` に書き、CI を GitHub Actions で回す
  - 保留だった 6 件: B1 Shift を押したまま `C-x C-s` と打つと `C-S-x`（切り取り）と `C-S-s`（保存）になる。Windows 互換キーを変えずに直す方法は無い（`C-S-x` は押した時点で切り取りを実行しなければならない）ので、README に書いた。B2 ファイルなしの `--encoding`（2 章「起動引数」）。B3 開き直しで `read-only-mode` を保つ（2 章「文字コードを指定して開き直す」）。B4 / B5 `!` のバッファの保存（2 章「指定した文字コードで読めないバイト」）。B6 長い 1 行のスクロールバー（4.7）
  - M6 の後の保留だった性能（2026-11-17。結果は変えない。内訳と測った値は `AGENTS.md` の「保留だった性能の改善（M6 の後）」）: K1 長い 1 行での 1 打鍵。内訳を測ると、時間はほぼ全部が打鍵の後にカーソルを画面に収めるための折り返しの計算 3 回で、64K 単位より短い行ではどれも行頭から数えていた（10 万文字の行そのものは速く、10 万文字まで伸ばす間の 64K 未満の行が遅かった）。1024 単位以上の行にも行ごとの記録を作り（4.7）、空のバッファに 10 万文字を打つ時間は約 19.4 秒から約 0.1 秒になった。K2 Unicode 系以外の読込と保存のメモリ。Shift_JIS と EUC-JP は、書き戻しの比較と保存を改行の後で区切った約 64K 単位ずつ行い、判定で UTF-8 として試した領域を CP 932 で読む間は持たないようにした。100MB の Shift_JIS の読込の増分の最大は判定 333.2MB・指定 266.8MB から 171.8〜171.9MB（テキストそのものの大きさ）に、保存は 433.3MB から 98.5MB になり、時間は同じか短い。ISO-2022-JP（状態を持つ。保存の 990.3MB の大半は CP 50220 の変換関数そのもの）と、読めないバイトのある読込（増分はテキストそのもの）は変えていない

## 6. リスクと対策

| リスク | 対策 |
|---|---|
| DirectWrite / Direct2D の初期化で 100ms を超える | M0 で最初に測る。超えたらファクトリ生成の順序見直し、フォント列挙の回避、`DXGI` スワップチェーンへの切替を試す |
| キー変換が配列（JIS / US）で食い違う | `ToUnicodeEx` で求める方式にし、JIS と US の両配列で 4.2 の表を手動確認する |
| Windows 互換キーの Ctrl+Shift+英字が、アプリ外のショートカット（IME やユーティリティ）や、Ctrl+Shift だけで入力言語・キーボード配列を切り替える Windows の設定と重なる（2026-11-15 に Ctrl+Alt から移した） | 設定で解除・付け替えできるので既定はそのまま。重なりが見つかれば README に書く |
| 行をまたぐ検索ができない | 初版の制約として README に書く。要望があれば連続バッファのキャッシュ方式を検討 |
| 100MB で UTF-16 化するとメモリが倍（約 200MB）になる | 許容する。1GB 級は対象外 |
| 升目配置で代替フォントの全角グリフが 2 升に合わない | 升目に収まるよう縮めて描く。M3-3 の手動確認で見た目を確かめる |
| 数 MB の 1 行のファイルでスクロールが固まる | 4.7 の折り返し位置の区切り記録で対処し、M4-5 で 100MB の 1 行ファイルを測る。M4b で測った値（2026-11-15、ヘッドレス、他のプロセスで CPU 負荷 50〜80% の開発 PC）: 開く時間 `open_ms` 406〜658ms、起動から初回描画まで（`measure_startup.py` にファイルを渡す）中央値 559ms、`window` 折り返しで末尾へ移る `<end>` 1 回が 1043〜1356ms、末尾で 1 文字打つごとの 1 画面の再描画の中央値 3.2〜3.7ms。M6 の前の改善の後（2026-11-15、ヘッドレス、HEAD 63a24a6 と交互に測った）: `<end>` 1 回が 254〜286ms（63a24a6 は 773〜840ms） |
| 改行コードが 1 行おきに変わるファイルで piece が行数と同じだけでき、メモリと速度が落ちる | 許容する（そうしたファイルはまれ）。M4-5 で 100MB の交互ファイルを測る。M4b で測った値（同じ条件）: `open_ms` 677〜833ms、起動から初回描画まで中央値 1063ms |
| 一部のコードページ（Microsoft の文書では 50220〜50229 など）は `MultiByteToWideChar` / `WideCharToMultiByte` の `dwFlags` が 0 でなければならず、不正バイトの検出や best fit の抑止ができない | どのコードページでも読込・書出の結果を逆変換して比べる（2 章「指定できる文字コード」）。9 種それぞれで使えるフラグは実装時に実測で確かめる（`IsValidCodePage` はコードページが入っているかしか分からない）。M4b で実測した結果は 2 章「指定できる文字コード」: 50220 はどちらのフラグも使えず、932 は `WC_NO_BEST_FIT_CHARS` で 12 文字のどれかが最後にあると問い合わせた大きさちょうどの領域では変換が失敗するので、変換の失敗を表せない文字と分けて扱い、比べる方法も併用する |

## 7. 進め方

- 段階ごとに計画（受入条件・対象範囲）を確認してから実装し、独立した検証役の OK を得てから次へ進む
- どの段階の機能も、ヘッドレス（`--headless`）で操作して確かめられるように作る。メニュー・右クリックメニュー・マウスのように画面に出る UI にも、ヘッドレスで同じコマンドに届く経路と、状態を外から読む手段を用意する。見た目だけは `--visible` の確認で見る（2026-11-14 にユーザーが確定）
- commit はユーザーの判断で行う
