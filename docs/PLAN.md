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
| Windows 互換キー | Windows の Ctrl+英字の操作を Ctrl+Alt+英字に割り当てる。Emacs の `C-M-` より優先 |
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
- 前置引数 C-u、キーボードマクロ
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
| CR の改行の直後の LF の改行 | 保存するとき、CR の改行のすぐ後に LF の改行が続く箇所（例: CR の行の次の LF の行を空にしたとき）は、その LF を CR として書く。続けて LF の改行が並べば、それぞれ直前が CR になるので全部 CR にする。バッファの改行コードも同じに変える（記号とモードラインがファイルと一致する）。undo の単位にはせず、保存に成功すれば未変更になる。エコー領域に件数を出す（例 `Wrote <path>; saved 1 LF line end after a CR as CR to keep the lines`）。ほかの改行は変えず、保存に失敗したらバッファも変えない | そのまま `\r` `\n` と書くと読み直したときに 1 つの CRLF になり、行が黙って 1 つ減る（M4a のレビューで発見）。2026-11-15 に決定（M4a の修正 F1。ユーザーが見直すことがある） |
| 新しく入る改行 | 入力・貼り付け・置換で入る改行は既定の改行コード。挿入するテキスト（貼り付け・IME・`insert-text` など）に含まれる `\r\n` と単独の `\r` は、先に `\n` 1 文字へ直してから入れる（M1 から）。`quoted-insert` の後の `RET` は `Cannot insert a carriage return` と表示して入れない（テキスト中の単独 `\r` は改行コードと区別できなくなるため） | 混在ファイルでも挙動が予測できる。CRLF のテキストを貼っても `\r` が混ざったり改行が二重になったりしない |
| 行の結合・分割 | 改行を消して 2 行を結合すると、消した `\n` の改行コードは消え、結合した行の末尾には後ろの行の改行コードが残る。行の途中に改行を入れて分割すると、入れた `\n`（前半の行の末尾）は既定の改行コードで、後半の行は元の改行コードを保つ。改行の無い最終行の末尾に改行を入れると既定の改行コード | 例: 既定 CRLF のファイル `a⏎b↓c⏎`（⏎=CRLF, ↓=LF）で 1 行目末の改行を消すと `ab↓c⏎`、続けて `a` と `b` の間で改行すると `a⏎b↓c⏎` |
| 既定の改行コードと Mixed 表示 | 既定の改行コードは読込時に決め、編集では変えない（`set-eol` と開き直しでだけ変わる）。既定と違う改行コードの `\n` の数を piece の集計で持ち、1 以上なら `Mixed(<既定>)`、0 になれば既定だけを表示する | 編集後もモードラインが実際の状態と一致する |
| クリップボードへの書き出し | kill / copy でクリップボードへ書くときは、`\n` を CRLF にする（Windows の慣習）。元の行ごとの改行コードは引き継がない | 他のアプリへ貼ったときに正しく改行される |
| 改行コードの変更 | `set-eol`（メニュー File > Line Endings）は全行をその改行コードにそろえ、既定も変える。1 回の undo で元の混在状態に戻る | ヒアリングで確定 |
| 指定できる文字コード | 自動判定の 7 種（`utf-8` / `utf-8-bom` / `utf-16le` / `utf-16le-bom` / `utf-16be` / `utf-16be-bom` / `shift_jis`）に、`euc-jp`（CP 20932）、`iso-2022-jp`（CP 50220）を加えた 9 種（Unicode 系と日本語系だけ）。Unicode 系以外の変換は `MultiByteToWideChar` / `WideCharToMultiByte`。読込は使えるコードページでは `MB_ERR_INVALID_CHARS` を付け、どのコードページでも読んだ結果を同じ文字コードで書き戻して元のバイト列と比べる。書出は使えるコードページでは `WC_NO_BEST_FIT_CHARS` を付けて `lpUsedDefaultChar` を見て、どのコードページでも書いた結果を読み戻して元のテキストと比べる（似た文字への黙った置き換えを防ぐ）。`dwFlags` が 0 でなければならないコードページ（50220 など）は比べる方法だけで判定する。各コードページのフラグ対応は実装時に実測で確かめる | 2026-11-11 にユーザーが確定。2026-11-14 に Unicode 系と日本語系だけに絞った（`windows-1252` / `gb18030` / `big5` / `euc-kr` は不要とユーザーが確定） |
| 文字コードを指定して開く | `find-file-with-encoding`：ミニバッファでファイル名、続けて文字コード名を Tab 補完で入力する（メニューから選んでも同じ）。以降の流れ（置き換え / 新しいウィンドウの確認）は find-file と同じ。新しいウィンドウでは、その文字コードを起動引数で渡す | 2026-11-11 にユーザーが確定（ミニバッファで指定） |
| 文字コードを指定して開き直す | `revert-buffer-with-coding-system`：今のファイルを指定の文字コードで読み直す。未保存の変更があれば「変更を捨てて開き直すか」を Windows のダイアログで確認する。undo 履歴は捨て、カーソルは同じ行番号に置く。ファイル名の無いバッファやディスクに無いファイルでは `Buffer is not visiting a file` などと表示して何もしない | 自動判定が外れたときの直し方 |
| 指定した文字コードで読めないバイト | 置換文字（U+FFFD）にして開き、ミニバッファに最初の位置（バイト位置と行）を表示する。読んだ結果を同じ文字コードで書き戻して元のバイト列と一致しない場合も同じ扱い。このバッファはモードラインの文字コードに `!` を付けて（例 `Shift_JIS!`）、保存か開き直しまで示し続ける。保存しようとすると、Unicode 系（UTF-8 / UTF-16）では「元のバイトは失われる」と警告して確認し、Unicode 系以外では U+FFFD を表せないので保存を中止する（「Unicode 系以外の文字コードで表せない文字の保存」と同じ）。読み違えたときは、別の文字コードを指定して開き直せばディスク上のファイルから読み直せる | 黙って壊さない（「判定不能なバイト列」と同じ方針） |
| メニューバー | File / Edit / View の 3 つ（4.9）。各項目は M-x と同じコマンドを呼び、右側に今のキー割り当てを出す（設定を再読込したら更新）。F10（`menu-bar-open`）で File メニューを下に開いた状態で入る（Emacs と同じ。左右キーで Edit・View へ移り、Esc で閉じる。2026-11-14 にユーザーが確定）。Alt+英字はキーマップ（`M-f` など）が優先で、メニューのアクセスキーには使わない。設定 `menu_bar` で非表示にでき、そのとき F10 は `Menu bar is hidden` と表示するだけ。ミニバッファ・isearch・問い合わせ置換の途中は全項目を灰色にする（入力途中の状態を壊さない） | 2026-11-11 にユーザーが確定 |
| 画面の言語 | メニュー・ダイアログ・プロンプト・メッセージは英語 | 2026-11-11 にユーザーが確定 |
| UTF-16 の BOM なし判定 | NUL バイトの偏りで判定 | 「BOM 有無 Unicode 対応」の要件 |
| 判定不能なバイト列 | UTF-8 として置換文字で読み（モードラインに `UTF-8!`）、保存時に警告して確認 | 黙って壊さない |
| Unicode 系以外の文字コードで表せない文字の保存 | 保存を中止し、最初の文字の位置を示して文字コードの変更を促す（Shift_JIS ほか、指定できる文字コードすべて） | 黙って `?` に化けさせない |
| カーソル単位 | 書記素クラスタ（Unicode UAX #29 の拡張書記素クラスタ。文字幅表と同じ Unicode の版の表から作る）。C-f / C-b、DEL / C-d、単語移動の端、マウスの位置、折り返し位置、選択範囲の端は、クラスタの途中に来ない。結合文字（例: か＋゛）、異体字セレクタ付きの絵文字、肌の色、ZWJ でつないだ絵文字、国旗（地域指示子 2 文字）、ハングルの字母の並びはそれぞれ 1 文字として動き、1 回で消える | 異体字セレクタ付きの絵文字が 1 桁に縮んで描かれ、Backspace が 2 回要った（手での確認）。2026-11-14 にユーザーが確定（以前は「コードポイント単位、書記素クラスタは後回し」） |
| 折り返し中の上下移動 | `next-line` / `previous-line`（C-n / C-p / ↓ / ↑）とスクロールは画面の行（折り返した行）単位で動く。目標の桁は画面の行の中の升目で数える。`beginning-of-line` / `end-of-line`（C-a / C-e）・`kill-line` はファイルの行（改行まで）単位 | Emacs の既定（`line-move-visual`）と同じ。2026-11-13 にユーザーが確定 |
| マウス | クリックはその文字の前にカーソルを置く（行末より右と右端の 1 升はその画面の行の末尾、行番号の欄は画面の行の先頭、最後の行より下はバッファの末尾、本文とモードラインの間の隙間は最後に見えている行）。ドラッグで選択し、本文の外では画面の行単位で自動スクロールする。ダブルクリックは `forward-word` と同じ定義の単語を選ぶ。Shift+クリックは選択範囲を広げる。ホイールは Windows の設定の行数だけ画面の行単位でスクロールする。クリックで入力の undo のまとまりを切る。モードライン・エコー領域とミニバッファの入力中のクリックは何もしない。中ボタン・横ホイール・トリプルクリック・Ctrl+ホイールには何も割り当てない（利用者の環境では中ボタンを終了に割り当てている） | 2026-11-14 にユーザーが確定（M3b の決定 B5） |
| 桁の数え方 | 書記素クラスタごとに数える。絵文字として表示するクラスタ（Unicode の Emoji の性質を持つ文字に U+FE0F が付いたもの、肌の色付き、ZWJ でつないだ絵文字、国旗、既定で絵文字表示の文字）は 2 桁。それ以外は先頭の文字で決め、東アジアの全角（East Asian Width F/W）を 2 桁、それ以外を 1 桁とする（U+FE0E を付けた文字もこの規則）。クラスタ内の 2 文字目以降は桁に数えない | 折り返し・カーソル位置・マウス位置の計算に使う。2026-11-14 にクラスタ単位に変更 |
| 文字の配置 | 端末と同じ升目配置。升目の幅は主フォントの `0` の送り幅で、全角は 2 升。各書記素クラスタは自分の升目の中に描く（4.7） | 「全角 2 桁」の折り返しと見た目が必ず揃い、位置計算が整数演算で済む。代わりに合字やアラビア文字などの複雑な文字組みは正しく出ない |
| 新規ファイル | UTF-8（BOM なし）+ LF。設定 `new_file.encoding` / `new_file.eol` で変更可 | ヒアリングで確定 |
| 改行の無いファイル | 改行コードは `new_file.eol` を使う。ASCII のみのファイルは UTF-8 と判定する | |
| 別ファイルを開く | C-x C-f・ドラッグ＆ドロップのたびに Windows のダイアログで「このウィンドウで開く（置き換える）」「新しいウィンドウで開く」「キャンセル」を選ぶ。置き換えるときに未保存なら、続けて保存の確認（保存する / 保存しない / キャンセル）を出す。ファイル名が無く未編集の空のウィンドウでは確認せずに置き換える。置き換えると undo 履歴は捨てる。新しいウィンドウは自分の exe をそのファイルを引数にして別プロセスで起動する | 1 ウィンドウ 1 ファイル。2026-11-11 にユーザーが確定 |
| 検索の単位 | 論理行ごと（パターンは行をまたがない） | piece tree 上で連続バッファを作らずに済む |
| 大文字小文字 | smart case（パターンに大文字が無ければ区別しない）。isearch 中 M-c で切替 | Emacs と同じ |
| kill と クリップボード | kill / copy はクリップボードへ書き、yank はクリップボードから読む。連続した kill は追記 | kill ring を持たない代わり |
| 選択 | Emacs のマーク + transient mark（リージョンを強調表示）。Shift+矢印でも選択開始 | Windows 操作との共存 |
| undo の単位 | 1 コマンド = 1 単位。連続した文字入力は 20 文字まで 1 単位。1 キーに割り当てたコマンド列・キーボードマクロ 1 回の実行も 1 単位。undo でカーソル位置も戻す | 一般的なエディターと同じ感覚 |
| 保存状態の追跡 | undo で保存時点に戻ると未変更表示に戻る | |
| undo 履歴の保持 | piece の参照で持ち、テキストを複製しない。上限なし | 大きな削除でもメモリを食わない |
| 外部での変更 | 保存時に更新日時を確かめ、変わっていれば確認 | 上書き事故を防ぐ |
| 読み取り専用属性のファイル | 開いた時点で `read-only-mode` を入れる（モードラインに `%%`。編集すると `Buffer is read-only`）。`C-x C-q` で外せば編集できるが、保存は属性のため失敗し、理由を出す | Emacs と同じ。2026-11-14 にユーザーが確定 |
| 起動引数 | `geditor.exe [--encoding=NAME] [--settings=PATH] [+LINE] [file]`。存在しないファイル名なら新規として開く。`--encoding` は指定できる文字コード名で、自動判定の代わりに使う。`--settings` は設定ファイルの場所を変える（テストと実機確認で利用者の設定に触れないため。新しいウィンドウにも引き継ぐ） | Emacs と同じ。`--encoding` は「新しいウィンドウで開く」で文字コードを引き継ぐため |
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
- `null` はそのキーを未割当にする。下位（Windows 互換キー・Emacs 既定）へは落ちない。押すと `<key> is undefined` と表示する。修飾なしの文字キーに `null` を書くと、その文字は入力されなくなる

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
| マーク | `C-SPC` set-mark-command, `C-x C-x` exchange-point-and-mark, `C-x h` mark-whole-buffer, `C-g` keyboard-quit, `ESC ESC ESC` keyboard-escape-quit（ミニバッファを抜け、選択範囲を解除する。Emacs と同じ。2026-11-14 にユーザーが確定） |
| 削除・kill | `C-d` / `<delete>` delete-char, `DEL` delete-backward-char, `C-k` kill-line, `M-d` kill-word, `M-DEL` backward-kill-word, `C-w` kill-region, `M-w` kill-ring-save, `C-y` yank |
| 入力 | `RET` / `C-j` newline, `C-o` open-line, `TAB` insert-tab, `C-q` quoted-insert, `C-t` transpose-chars, `M-u` upcase-word, `M-l` downcase-word, `M-c` capitalize-word |
| undo | `C-/` / `C-_` / `C-x u` undo, `C-?` / `C-M-_` redo |
| 検索・置換 | `C-s` isearch-forward, `C-r` isearch-backward（`C-u` 付きで正規表現）, `M-%` query-replace, `C-M-%` query-replace-regexp |
| ファイル | `C-x C-f` find-file, `C-x C-s` save-buffer, `C-x C-w` write-file, `C-x C-q` read-only-mode, `C-x C-c` quit |
| 文字コード | `C-x RET f` set-buffer-file-coding-system（保存に使う文字コード。2 章「指定できる文字コード」の 9 種）, `C-x RET r` revert-buffer-with-coding-system, `C-x RET c` find-file-with-encoding（Emacs では `C-x RET c` は次のコマンドの文字コードを指定するプレフィックス。geditor は開く操作に絞った独自の割当）, `C-x RET e` set-eol（`lf` / `crlf` / `cr`。geditor 独自の割当） |
| 表示 | `C-x C-+` / `C-x C-=` text-scale-increase, `C-x C--` text-scale-decrease, `C-x C-0` text-scale-reset（Emacs では 3 キーとも text-scale-adjust。geditor は動作ごとにコマンドを分ける）。既定のキーが無いコマンド: toggle-line-numbers, toggle-eol-marks, toggle-whitespace-marks, set-wrap-mode（`none` / `window` / `fixed` / `max`）。メニュー View と M-x から使う |
| メニュー | `<f10>` menu-bar-open |
| 前置引数 | `C-u` universal-argument, `M-0`〜`M-9` digit-argument, `M--` negative-argument |
| マクロ | `C-x (` / `<f3>` kmacro-start-macro, `C-x )` / `<f4>`（記録中）kmacro-end-macro, `C-x e` / `<f4>` kmacro-end-and-call-macro（続けて `e` で繰り返し） |
| その他 | `M-x` execute-extended-command |

isearch 中: `C-s` / `C-r` 次・前の一致、`DEL` 1 文字戻す、`RET` 確定、`C-g` 取消（開始位置へ戻る）、`M-r` 正規表現の切替、`M-c` 大文字小文字の区別の切替、`M-%` その語で問い合わせ置換へ。

問い合わせ置換中: `y` / `SPC` 置換、`n` / `DEL` 飛ばす、`!` 残りを全部置換、`.` 置換して終了、`q` / `RET` 終了。置換全体を 1 つの undo 単位にする。正規表現の置換文字列は PCRE2 の `$1` 形式。

ミニバッファ: `C-a` `C-e` `C-f` `C-b` `C-k` `DEL` `C-d` などの編集キーは global と同じ。`RET` 確定、`C-g` と `ESC ESC ESC` 取消、`TAB` 補完、`M-p` / `M-n` 履歴。

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

mode line: 変更有無（`**`）・読取専用（`%%`）・ファイル名・文字コードと BOM（読めないバイトを置換文字にしたバッファは末尾に `!`）・改行コード（`LF` / `CRLF` / `CR`、混在なら `Mixed(<既定>)`）・行と桁・折り返しモード・マクロ記録中（`Def`）。文字領域の右端に Win32 標準の縦スクロールバーを置く（図では省略）。

### 4.7 描画

- 升目配置: 各書記素クラスタの升目数（1 か 2）を桁の数え方で決め、クラスタ全体を 1 つのグリフの並びとして自分の升目に描き、折り返し位置・カーソル位置・マウス位置はすべて升目の整数計算で求める。`IDWriteTextLayout` による文字組みと折り返しは使わない
- 文字の形は `IDWriteFontFallback::MapCharacters` で使うフォントを決め、`IDWriteTextAnalyzer` でグリフに変換し、送り幅を「升目数 × 升目の幅」に置き換えた `DrawGlyphRun` で描く。升目より広いグリフは升目の幅に収まるよう横に縮める
- 代替フォントは `IDWriteFontFallbackBuilder` で組む
- 折り返しは表示範囲の論理行だけ計算する。長い論理行（数 MB の 1 行など）は、折り返し位置を 64K 文字ごとの区切りで記録して再利用し、スクロールのたびに行頭から数え直さない。スクロールバーの位置は論理行ベースの近似で出す
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
8. `ReplaceFileW` には `REPLACEFILE_IGNORE_MERGE_ERRORS` を付けない。属性や ACL を引き継げなければ保存の失敗として扱い、他の失敗と同じく元のファイルと `file~` を変えず、一時ファイルを残さない（M4a の修正 F4）
9. `file~` を作る段階の失敗の表示には `file~` のパスを入れる（例 `Cannot write <path>: making the backup <path>~: Access is denied`。M4a の修正 F5）

### 4.9 メニューバー

メニューの中身はコア（`menu.*`）にデータとして持ち、Win32 側はそれを `HMENU` に組み立てるだけにする。各項目はコマンド名（と引数）を持ち、右側に表示するキーはその時点のキーマップから引く（そのコマンドを同じ引数で呼ぶキーのうち最も短いもの 1 つ。同じ長さなら設定 > Windows 互換 > Emacs の順。2026-11-14 にユーザーが確定）。チェックとラジオの状態はメニューを開く直前（`WM_INITMENUPOPUP`）にコアへ問い合わせる。まだ実装していない段階のコマンドの項目は灰色にする。

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
  - [M2-4] 4.4 の Windows 互換キー 12 個すべてが既定で期待するコマンド名に解決され、対応する `C-M-` の Emacs 操作より優先されることをテストで確かめる（コマンドの動作は各段階で確かめる）
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
  - [M4-7] 指定して開く・開き直すのテスト: 9 種それぞれで日本語の見本を正しく読む。読めないバイトは U+FFFD になり、バイト位置と行が表示され、モードラインに `!` が付く。保存しようとすると Unicode 系では確認が出て、Unicode 系以外では中止される。書き戻して一致しない場合も同じ。開き直しで未保存の変更があると確認が出て、キャンセルなら何も変わらない。ファイル名の無いバッファでの開き直しは何もしない。読み違えた後に別の文字コードで開き直すと正しく読める。`--encoding` 付きの起動引数が新しいウィンドウに渡る。`shift_jis` などで似た文字への置き換え（best fit。例: `¥` U+00A5）が起きる文字を保存しようとすると中止される
  - [M4-8] 改行記号（手動確認）: 混在ファイルで LF / CRLF / CR の 3 記号が区別でき、既定と違う行が `eol_other` 色で出る。モードラインが `Mixed(<既定>)` になり、File > Line Endings のラジオが既定を指す
  - [M4-9] File メニューのチェック・ラジオ（Encoding for Saving / Line Endings / Read Only）の状態が、対応するコマンドの実行と開き直しで切り替わることを表駆動のテストで確かめる。M4 で実装した File メニューの項目がすべて有効になることを確かめる

### M5: 検索と置換

- isearch（通常・正規表現）、一致の強調表示、問い合わせ置換（通常・正規表現）、goto-line
- 受入条件
  - [M5-1] isearch の各キー（4.4 の「isearch 中」の一覧）に最低 1 件のテスト。`C-u C-s` / `C-u C-r` が正規表現検索になることも確かめる
  - [M5-2] 問い合わせ置換の各キーに最低 1 件のテスト。置換全体が 1 回の undo で戻る
  - [M5-3] 正規表現: サロゲートペア・全角・`$1` 置換・不正なパターンのエラー表示をテストで確かめる
  - [M5-4] smart case の 3 設定をテストで確かめる
  - [M5-5] 100MB ファイルで、末尾付近の語の検索が 1 秒以内に見つかる
  - [M5-6] Windows 互換キー `C-M-f` / `C-M-h` / `C-M-r` / `C-M-g` が検索・置換・goto-line として動くことをテストで確かめる
  - [M5-7] isearch・問い合わせ置換の途中はメニューの全項目が無効になり、終わると元に戻ることをテストで確かめる。Edit メニューの Find / Replace / Go to Line が有効になり、選ぶとそれぞれのコマンドが始まることをテストで確かめる

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
| 改行コードが 1 行おきに変わるファイルで piece が行数と同じだけでき、メモリと速度が落ちる | 許容する（そうしたファイルはまれ）。M4-5 で 100MB の交互ファイルを測る |
| 一部のコードページ（Microsoft の文書では 50220〜50229 など）は `MultiByteToWideChar` / `WideCharToMultiByte` の `dwFlags` が 0 でなければならず、不正バイトの検出や best fit の抑止ができない | どのコードページでも読込・書出の結果を逆変換して比べる（2 章「指定できる文字コード」）。9 種それぞれで使えるフラグは実装時に実測で確かめる（`IsValidCodePage` はコードページが入っているかしか分からない） |

## 7. 進め方

- 段階ごとに計画（受入条件・対象範囲）を確認してから実装し、独立した検証役の OK を得てから次へ進む
- どの段階の機能も、ヘッドレス（`--headless`）で操作して確かめられるように作る。メニュー・右クリックメニュー・マウスのように画面に出る UI にも、ヘッドレスで同じコマンドに届く経路と、状態を外から読む手段を用意する。見た目だけは `--visible` の確認で見る（2026-11-14 にユーザーが確定）
- commit はユーザーの判断で行う
