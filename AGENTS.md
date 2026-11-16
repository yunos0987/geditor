# geditor の開発ルール（AI エージェント向け）

Windows 専用の Emacs 風テキストエディター。要件・設計・段階計画は `docs/PLAN.md` が正本。実装は段階（M0〜M6）ごとに進め、各段階の受入条件（`[M1-1]` など）を満たしてから次へ進む。

## ビルドとテスト

LLVM（clang-cl / lld-link）・CMake・Ninja を使う。Visual Studio の開発者プロンプトは要らない（clang-cl が Visual Studio と Windows SDK を自動で見つける）。

```powershell
cmake --preset release            # or: debug
cmake --build --preset release
ctest --preset release
```

起動時間の計測（受入条件 M0-3 / M3-4 / M6-4）:

```powershell
python tools/measure_startup.py build/release/geditor.exe            # judges startup_ms
python tools/measure_startup.py build/release/geditor.exe --metric app -- --settings=build/s.json
```

- 既定では `build/measure_startup/settings.json`（中身は `{}`。スクリプトが書く）を `--settings` で渡すので、計測は利用者の `%USERPROFILE%\.geditor.json` に左右されない。`--` の後に引数を書くと既定の代わりにそれをそのまま geditor に渡す（別の設定ファイルで測るときなど）
- 計測スクリプト（`measure_startup.py` / `measure_input.py`）は既定でヘッドレス（`--headless`、下の「アプリの起動と実機確認」）で geditor を起動し、最初の行に `mode=headless` と出す。描画の経路は画面に出すときと同じ。画面に出して測るときは `--visible` を付ける。`--headless` を知らない版（ヘッドレスより前の commit。たとえば M3a の比較に使う HEAD f6ba6aa）は `--visible` でしか測れない。ヘッドレスの geditor は `--measure-startup` / `--measure-input` で標準エラーに `headless=1` と出し、計測スクリプトはこれが無いと値を出さずにエラーで止まる（app_check も状態に `headless=1` が無い版は確認しない）
- 計測スクリプトは結果の行（各値・中央値・判定）のすべてに `mode=headless` か `mode=visible` を付ける。ヘッドレスと画面に出したときの値は比べられない:
  - ヘッドレスの `input_ms` に入るもの: メッセージの受け取り・キーの処理・コアの計算・Direct2D の描画・`EndDraw` の完了。入らないもの: 画面への表示の待ち（モニターの垂直同期や DWM の合成に合わせて待つ時間）。ウィンドウはどのモニターにも映らない位置にある透明でない layered ウィンドウなので、`EndDraw` がこの待ちをしない。このため画面に出したときの約 13ms に対して約 1ms になる（M3a のレビューで測定）
  - ヘッドレスの `startup_ms` に入るもの: プロセスの生成・初期化・ウィンドウの作成・設定の読込・初回描画の `EndDraw`。入らないもの・違うもの: `ShowWindow` による表示と活性化（ヘッドレスは `SetWindowPos` で活性化せずに出す）、画面への合成の待ち
  - 受入条件の基準（M1-5 の 16ms、M0-3 / M3-4 の 100ms など）は画面に出した値で決めたもの。ヘッドレスの値で判定すると黙って甘くなる。基準を判定するとき、または以前の画面に出した値（M3a 以前の報告など）と比べるときは `--visible` で測る（利用者が画面を使っている間は測らない）。基準の値そのものは変えていない

- `startup_ms`: プロセス生成から初回描画の `EndDraw` 完了まで
- `entry_ms`: プロセス生成から `wWinMain` 開始まで。開発 PC では Norton 360 が未知の exe の起動を約 2 秒止めるため、この値が大きくなる
- `app_ms`: `wWinMain` 開始から初回描画まで（プログラム自身の起動コスト）
- `geditor.exe --measure-startup` は段階ごとの時刻（`stage <name>=<ms>`）も標準エラーに出す

コアの性能計測（M1 の 100MB 基準と M5-5）。生成した 100MB の UTF-8 テキストで、読込（デコード＋構築）・1 文字挿入と行番号検索・自己挿入キーの処理時間・全削除と undo のメモリ増分を測る。M5 からは、1000 か所を 1 文字ずつ編集して piece を分けたテキストで、末尾から 100 行目に置いた語（ほかに無い `§` で始まる）を先頭から isearch で探し、最初のキー（全体を走査する）と語全体の時間（基準 1 秒）と、その間に 1ms ごとに測った private bytes の増分の最大（基準 16MB。テキスト全体を 1 つの文字列に写すと約 200MB 増える）も測る。参考として、末尾から先頭の行の語を後ろ向きに探す時間も出す。問い合わせ置換の `!` も測る（M5 の修正 R3）: 4 万文字の行の 5000 個・16 万文字の行の 2 万個（通常と `$1` の正規表現）は各 1 秒以内が基準、50 万行の 50 万個は参考値。どれも置換の数・終わったカーソルの位置・1 回の undo で元に戻ることも確かめる。基準を超えると終了コード 1 で終わる。ctest には入れていないので、Release ビルドを手で実行する:

```powershell
build/release/tests/geditor_perf.exe
build/release/tests/geditor_perf.exe decode backsearch endkey   # only these parts (core: the parts above)
build/release/tests/geditor_perf.exe macro                      # M6: a keyboard macro of 1000 characters run 100 times
```

- 引数 `core` / `decode` / `backsearch` / `endkey` / `memory` / `macro` / `kill` を付けると、その部分だけを測る（引数なしは全部）。`kill`（M6 の修正 R1）は、10 万行のファイルで `C-k C-k` のマクロを `C-u 20000 C-x e` で実行する 4 万回続けた kill の時間（基準 1 秒）と、終わった後のクリップボードがつなげた文字列（20000 行分）であることと、実行中にクリップボードに書いた回数を出す。`macro`（M6）は、空のバッファで 1000 文字を打つキーボードマクロを記録し、`C-u 100 C-x e` で 100 回実行する時間と、同じ 10 万キーをマクロなしで 1 つずつ打つ時間を、同じ実行の中で順番を入れ替えながら 3 組測り、組ごとの比（マクロ ÷ 直接）の中央値が 1.25 倍以下なら合格とする（両方の時間と比を出す。できた文字数 101000 も確かめる）。10 万文字が 1 つの論理行になり、1 打鍵あたりの処理がその長い行の分かかるため、絶対時間ではなく直接打つ時間と比べる。1000 文字の後に `RET` を足したマクロ（1000 文字の行が 100 行）の時間も参考として出す（基準なし）。`memory` は 100MB の Shift_JIS などを読むときと、Shift_JIS / EUC-JP / ISO-2022-JP で保存（`encode_text`）するときの private bytes の最大と、読んだテキストと書いたバイト列の容量を出す（基準なし）。`max-line=N` は `backsearch` の N 文字より長い行を飛ばす（HEAD 63a24a6 の `.*` は 40 万文字で約 140 秒かかるため）。後の 3 つは M6 の前に片付けた保留の性能（下の「保留だった性能の改善」）: `decode` は生成した 100MB の Shift_JIS（判定と指定）・真ん中に読めないバイト（`81 20`）が 1 つある 100MB の Shift_JIS（指定と判定）・最後の `ESC ( B` が無い 10MB の 1 行の ISO-2022-JP・`0x80` を 1 つ含む 20MB の EUC-JP の読込（デコード＋構築）と、読んだ結果の要約（文字数・ハッシュ・`!`・位置。2 つのビルドで同じになるか比べる）。基準は判定の Shift_JIS 1.5 秒・読めないバイトのある Shift_JIS（指定）2 秒・ISO-2022-JP 1 秒。`backsearch` は `abcdefg ` を並べた 10 万文字と 40 万文字の 1 行の末尾から `C-u C-r` で `.*` / `a*` / `\w+` / `zq`、`C-r` で `cdef` を打つ時間（基準は 40 万文字の `.*` で 0.1 秒）。`endkey` は `measure_input.py` と同じ語の 10MB と 100MB の 1 行（120×40 の `window` 折り返し）で `<end>` と、その後の 1 画面のコアの部分（`point_view`・モードライン・強調・40 行の `display_row`）の時間（基準は 100MB で 0.5 秒）と、比べるために `for_each_cluster` だけの時間と `WrapLayout::row_count` の時間

キー入力から再描画までの時間（受入条件 M1-5）。100MB（1 行約 80 文字、ASCII と日本語）と 10MB の 1 行だけのファイルを `build/measure_input/` に一度だけ生成し、`geditor.exe --measure-input <file>` を起動して `WM_CHAR` を 1 文字ずつ送る（打鍵と同じ経路）。アプリはメッセージを受け取ってからその文字を描いた `EndDraw` の完了までを `input_ms=` として標準エラーに出す。中央値・95 パーセンタイル・ファイルを開く時間（`open_ms`）を表示し、100MB の中央値が 16ms を超えると終了コード 1 で終わる。1 行のファイルはカーソルを行末（`<end>`）に置いて測り、基準は持たない:

```powershell
python tools/measure_input.py build/release/geditor.exe            # --samples prints every sample
python tools/measure_input.py build/release/geditor.exe --wrap window
```

- 設定ファイルは既定で `{}`（行番号あり・折り返し `max`）。`--wrap none|window|fixed|max` を付けると、その折り返しモードを書いた設定ファイルで測る
- 1 行のファイルの `end_key_ms` は、先頭から行末までのスクロール（`<end>` 1 回）にかかった時間
- `--m4-5`（受入条件 M4-5）を付けると、1 行のファイルを 100MB（`single_line_100mb.txt`）にし、1 行おきに LF と CRLF が交互に並ぶ 100MB のファイル（`lines_100mb_lf_crlf.txt`）も開く。`open_ms` が多数行と 1 行で 1000ms、交互のファイルで 2000ms を超えるか、1 行の末尾で打った文字（1 文字ごとに 1 画面を描き直す）の中央値が 16ms を超えると終了コード 1。M4-5 は `--wrap window` を付けて測る。基準は画面に出した値で決めたもの（上の注意）なので、ヘッドレスの値は参考値で、判定は `--visible` で行う

```powershell
python tools/measure_input.py build/release/geditor.exe --m4-5 --wrap window
```

- `measure_input.py` は `build/measure_input/settings.json`（中身は `{}`）を `--settings` で渡す。利用者の `%USERPROFILE%\.geditor.json` は読まない。`--measure-input` では設定ファイルの監視は始めない
- `measure_input.py` もヘッドレスが既定（`--visible` で画面に出す）。文字は今も `PostMessage` の `WM_CHAR` で送る

## アプリの起動と実機確認

```powershell
build/release/geditor.exe [--encoding=NAME] [--settings=PATH] [+LINE] [file]
```

- 存在しないファイル名なら新規として開く。`+LINE` はその行へ移る。未知のオプションはエコー領域に表示する
- `--encoding=NAME`（M4b）は、判定の代わりにその文字コードでファイルを読む（新規ファイルならその文字コードで保存する）。ファイルを渡さないときは、名前の無い新しいバッファをその文字コードにする（M6 の B2。`Editor::set_new_buffer_encoding`。モードラインに出て、設定の `new_file.encoding` を読み直しても変わらない）。NAME は下の 9 種の名前だけで、ほかの名前は `Unknown encoding: <NAME> (use utf-8, ...)` とエコー領域に出して判定で開く。`find-file-with-encoding` で「新しいウィンドウで開く」を選ぶと、新しいウィンドウにその文字コードを `--encoding` で渡す（`--settings` / `--headless` と同じく `src/win/process.*` の `editor_command_line`）
- 設定ファイルは既定で `%USERPROFILE%\.geditor.json`。`--settings=PATH` で場所を変える（相対パスは今のフォルダーから。指定したときは `%USERPROFILE%` のファイルを読みも監視もしない。「新しいウィンドウで開く」にも同じ指定を渡す）
- テスト・計測・実機確認では必ず `--settings` で `build/` の下のファイルを指定し、利用者の `%USERPROFILE%\.geditor.json` を読み書き・作成・削除しない（今は存在しない）
- 設定ファイルは起動時に同期で読み、初回描画の後に別スレッドで監視する。保存すると約 200ms 後に読み直す。構文の誤りはファイル全体を採用せず、意味の誤りはその項目だけ無視する。誤りは行・桁付きでエコー領域に出し、2 件以上ならエコー領域を最大 10 行まで広げる（ウィンドウが低いときはモードラインと本文 1 行が残る行数まで。収まらない分は最後の行の `... and N more` に数える。次のキーで 1 行に戻る）。`M-x find-user-init-file` で設定ファイルを開く（無ければ書き方の例を入れた未保存の新規バッファになる）
- M3a までに反映する項目: キー設定（`keys` / `minibuffer_keys` / `isearch_keys` / `query_replace_keys`）と表示の項目（`font` / `colors` / `tab_width` / `line_numbers` / `wrap` / `whitespace` / `eol_marks`）。`colors` の `search_match` / `search_current` は M5 から検索の一致の背景に使う（`eol_other` は M4a から既定と違う改行の記号に使う）。M5 で `search.case_fold` も反映する。M4a で `new_file.eol` と `backup` も反映する。M4b で `new_file.encoding` も反映する。M3b で `menu_bar` も反映する（読み直すとその場でメニューバーを出し入れし、先頭の行は変えない）。ほかの項目は型と値を検査して保持し、後の段階で使う
- 入っていないフォント名（`font.family` / `font.fallback`）はエコー領域に `Font not found: <name>` と出し、Consolas（とシステムの代替フォント）を使う。出すのは `font.family` / `font.fallback` の値が変わったときだけで、text-scale・DPI の変更・`font.size` だけの変更でフォントを作り直しても繰り返さない。`font.fallback` を書かなければシステムの代替フォントだけを使う
- F10（`menu-bar-open`）は File メニューを下に開いた状態でメニューに入る。F10 を押している間は待ち、離したときに開く（離す操作でメニューから抜けないため）。左右キーはメニューバーの端で折り返す（M3d）: File で ← を押すと View、View で → を押すと File に、1 回の打鍵で移る。メニューループは端でシステムメニューへ移ろうとする（`WM_NEXTMENU`）ので、geditor は既定の行き先（システムメニュー）のままにして同じキーをもう 1 回だけ送り、システムメニューから反対の端へ進ませる（途中でシステムメニューが一瞬開くことがある）。メニューバーそのものを行き先に答える作り（M3c）では File が選び直されるだけだった。送ったキーにはしるしの `lParam` を付け、メニューループが受け取らずに残ったときは捨てる。Alt+Space のシステムメニューと、そこからの左右キーは既定のまま
- View のコマンド（`toggle-line-numbers` / `toggle-eol-marks` / `toggle-whitespace-marks` / `set-wrap-mode`）と text-scale（`C-x C-+` / `C-x C--` / `C-x C-0`、1 段 1.2 倍、6〜200 ピクセル）はそのウィンドウだけに効き、設定ファイルには書かない。設定を読み直しても、その項目のファイルの値が変わるまでは切り替えた値を保つ
- メニュー（M3b、PLAN 4.9）: 中身はコアの `src/core/menu.*` にデータとして持ち、`src/win/menu_bar.*` がそれだけから `HMENU` を組み立てる。メニューバーは初回描画から出す。各項目の右には、Windows 側のキー、空白 2 つ、Emacs のキーの順に出す（例: Undo は `C-S-z  C-/`、Save は `C-S-s  C-x C-s`、Cut は `C-S-x  C-w`、Select All は `C-S-a  C-x h`、Open... は `C-x C-f` だけ）。Windows 側は、設定ファイルと Windows 互換キー（`C-S-a` などの 12 個）のうち、今のキーマップでその項目のコマンド（と引数）にちょうど割り当たっているキー列で最も短いもの（同じ長さなら設定ファイルが先）。Emacs 側は、Emacs 既定のキーのうち今のキーマップでもそのコマンドを呼ぶもので最も短いもの（設定で別のコマンドにしたキーは出さない）。Emacs 側では、`src/core/menu.cpp` の同じ動作の表（`kSameActionCommands`。両向き）にあるコマンドのキーも候補にする: `cut`↔`kill-region`（`C-w`）、`copy`↔`kill-ring-save`（`M-w`）、`paste`↔`yank`（`C-y`）、`select-all`↔`mark-whole-buffer`（`C-x h`。カーソルが先頭と末尾のどちらに来るかだけが違う）。表示だけで、割り当ては変えない（同じ長さなら項目のコマンド自身のキーが先）。片方しか無ければそれだけ、どちらも無ければ何も出さず、同じキー列なら 1 回だけ出す（`C-c &` のようにキーに `&` があれば、メニューの文字列では `&&` と書いてアクセスキーにしない）。表示・灰色・チェックとラジオは `WM_INITMENUPOPUP` でそのつどコアに問い合わせる。灰色になるのは未実装のコマンドの項目（M5 の後はメニューに無い）と、ミニバッファ・isearch・問い合わせ置換の途中の全項目だけ（Cut / Copy はリージョンが無くても選べ、コマンドが理由を表示する）。File メニューの Line Endings のラジオはバッファの既定の改行コード（混在ファイルでも既定）に、Read Only のチェックは `read-only-mode` に付く（M4a）。Encoding for Saving のラジオはバッファの文字コードに付く（M4b）。F10（`menu-bar-open`）とマウスでメニューに入る。Alt+英字はキーマップのまま（`M-f` は `forward-word`）、Alt 単独ではメニューに入らない。アクセスキー（`&Undo` など）はメニューに入ってからだけ効く。`menu_bar: false` ではメニューバーが消え、F10 は `Menu bar is hidden` と出す
- 右クリックの簡易メニュー（Undo / Redo / Cut / Copy / Paste / Select All）は Edit メニューの先頭の項目そのもの（同じ定義）から作る。マウスではその位置に、Apps キーと Shift+F10（`S-<f10>` に割り当てが無いとき）ではカーソルの位置に出す。右クリックではカーソルと選択範囲を動かさない。`menu_bar: false` でも出る
- マウス（M3b）: 左クリックはクリックした升目を含む文字の前にカーソルを置く（行末より右と予約の最後の升目はその画面行の終わり。折り返した行の途中の画面行では最後の文字の前、行番号の欄はその画面行の先頭、最後の行より下はバッファの末尾。ウィンドウの高さが行の高さの倍数でないときに最後の画面行とモードラインの間にできる余白は、最後の画面行として扱い、スクロールしない）。押す・ドラッグするとマークは外れ、ドラッグで押した位置からのリージョンになる（本文の上下の外へドラッグすると、押している間 50ms ごとに 1 行ずつスクロールする）。ダブルクリックは `forward-word` / `backward-word` と同じ定義の単語を選ぶ（単語の文字でなければその 1 文字）。Shift+クリックは有効なマークから、無ければ元のカーソル位置からリージョンを広げる。ホイールは `SPI_GETWHEELSCROLLLINES` 行ずつ（ページ指定ならページ単位）で、1 刻みに満たない量はためておき、カーソルは画面から外れるときだけ動く。クリックは入力中の undo のまとまりを区切る。モードライン・エコー領域のクリックと、ミニバッファの途中の本文のクリックは何もしない。位置は升目から `Editor::offset_at_screen`（`WrapLayout`）で求め、描画側もマウス側も折り返しを自前で計算しない。選択した文字列のドラッグによる移動・複写は作らない
- 改行コード（M4a、PLAN 2 章「内部の改行」〜「改行コードの変更」）: テキストは改行を `\n` 1 文字で持ち、piece tree の各 piece が中の `\n` の改行コード（LF / CRLF / CR）を持つ。読込は改行コードが変わる位置で piece を分け、保存は piece ごとに元の改行コードへ戻す。既定の改行コードは読込時に最も多いもの（同数なら CRLF > LF > CR、改行が無ければ `new_file.eol`）で、入力・貼り付けで入る改行はこれになる。既定と違う改行があるとモードラインは `Mixed(CRLF)` のようになり、その行の改行記号は `eol_other` 色で描く。`set-eol`（`C-x RET e`、File > Line Endings）は全行をそろえて既定も変え、1 回の undo で元に戻る（何も変わらなければ未変更のまま）。CR の改行のすぐ後に LF の改行が続く箇所（CR の行の次の LF の行を空にしたときなど）は、保存のときその LF を CR として書く（続く LF も直前が CR になるので CR。M4a の修正 F1、PLAN 2 章「CR の改行の直後の LF の改行」）。そのまま書くと読み直したときに CRLF 1 つになり、行が 1 つ減るため。バッファの改行コードも同じに変え（undo の単位にはしない。undo しても CR のまま）、エコー領域に `Wrote <path>; saved 1 LF line end after a CR as CR to keep the lines` のように件数を出す。保存に失敗したらバッファは変えない。判定はコアの `cr_kept_line_ends`（`src/core/encoding.*`）
- 保存（M4a、PLAN 4.8）: 同じフォルダに一時ファイル `.<名前>.<pid>.<番号>.geditor-tmp` を書いて `FlushFileBuffers` し、既存のファイルは `ReplaceFileW`（`REPLACEFILE_IGNORE_MERGE_ERRORS` なし。属性や ACL を引き継げなければ保存の失敗。M4a の修正 F4）、新規は `MoveFileExW` で置き換える。一時ファイルの名前（`.geditor-tmp` と古いファイルを移す `.geditor-old`）は、ボリュームの名前の最大長（`GetVolumeInformationW`、読めなければ 255）に収まるよう `<名前>` の部分を切り詰める（サロゲートペアの途中では切らない。M4a の修正 F3）。`file~` そのものが最大長を超えるときは `file~` を作らずに保存し、`Wrote <path> (no backup: <path>~ would be longer than <最大長> characters)` と出す。`backup` が true なら、このプロセスで初めて保存したときだけ元の内容を `file~` に残す（`ReplaceFileW` が古いファイルを一時的な名前に移し、全部うまくいってから `file~` に名前を変える。`file~` を作れなければ元のファイルを戻して保存を失敗にする）。シンボリックリンクはリンク先を置き換え（`file~` はリンク先の隣）、ハードリンク（リンク数 2 以上）は `file~` を写しで作ってから同じファイルに直接書く（途中で失敗すると一部だけ書かれることがあり、そのときは `file~` が元の内容）。どの段階で失敗しても、エコー領域に `Cannot write <path>: <段階>: <理由>` を出し（`file~` を作る段階は `making the backup <path>~` のように `file~` のパスを入れる。M4a の修正 F5）、元のファイルと `file~` は変えず、一時ファイルを残さず、バッファは未保存のまま。保存の前に、読込（または前回の保存）時からファイルの更新日時か大きさが変わっていれば `<name> has changed on disk since it was read; save anyway?` と確かめ、No なら書かずに `Not saved` と出す。ディスクから消えていれば確かめずに新規として保存する
- 読み取り専用（M4a）: 読み取り専用属性のファイルは `read-only-mode` を入れて開く（モードライン `%%`、編集すると `Buffer is read-only`）。`C-x C-q` で外して編集できるが、保存は `Access is denied (the file is read-only)` で失敗し、ファイルは変わらない。`read-only-mode` の間は貼り付け・`set-eol`・`set-buffer-file-coding-system`・undo を含む編集のコマンドをすべて断る
- 文字コード（M4b、PLAN 2 章「指定できる文字コード」〜「Unicode 系以外の文字コードで表せない文字の保存」）: 9 種の名前（`encoding_names()`、モードラインの表示名）は `utf-8`（`UTF-8`）・`utf-8-bom`（`UTF-8-BOM`）・`utf-16le`（`UTF-16LE`）・`utf-16le-bom`（`UTF-16LE-BOM`）・`utf-16be`（`UTF-16BE`）・`utf-16be-bom`（`UTF-16BE-BOM`）・`shift_jis`（`Shift_JIS`、CP 932）・`euc-jp`（`EUC-JP`、CP 20932）・`iso-2022-jp`（`ISO-2022-JP`、CP 50220）。コアの `src/core/encoding.*` がすべての読み書きを持ち、コードページは kernel32 の `MultiByteToWideChar` / `WideCharToMultiByte` を直接呼ぶ（UI 系のライブラリはリンクしない）。どの文字コードもまず `\r` と `\n` をそのまま残した UTF-16 にし、改行は 1 つの関数（`split_line_ends`）が UTF-16 の上で分ける
  - 判定（名前を指定しないとき）: BOM（UTF-8 / UTF-16LE / UTF-16BE）→ 先頭 64KiB の NUL バイトの偏り（奇数番目が 2 個以上・2 バイト単位の数の 4 分の 1 以上・偶数番目の 4 倍より多ければ UTF-16LE、偶数番目が同じ条件なら UTF-16BE。BOM なし。1 個の NUL では UTF-16 にしない。ASCII 1 文字だけの BOM なし UTF-16 は UTF-8 になる）→ UTF-8 として正しい（ASCII だけを含む）→ CP 932 が `MB_ERR_INVALID_CHARS` で全バイトを読めれば Shift_JIS → どれでもなければ UTF-8 として U+FFFD で読み、`not valid UTF-8 or Shift_JIS` と出す。`euc-jp` と `iso-2022-jp` は判定では選ばない。UTF-8 としても正しい Shift_JIS（半角カナ 2 文字の `ﾃｽ` = C3 BD など）は UTF-8 になるので、`revert-buffer-with-coding-system` で読み直す
  - 読めないバイト: U+FFFD にして開き（UTF-8 は不正な並びの最大の部分ごとに 1 つ、UTF-16 は対にならないサロゲートと奇数個目の最後のバイト、コードページは読めない文字の先頭の 1 バイトごと）、エコー領域に最初のバイト位置（0 から数える）と行（1 から）を出し、モードラインの文字コードに `!` を付ける（例 `Shift_JIS!`）。コードページで読めても書き戻すと元のバイト列にならないもの（CP 932 の NEC 選定 IBM 拡張 `ED 40` → U+7E8A → `FA 5C`、ISO-2022-JP の `ESC $ @` や行末の前の `ESC ( B` が無いもの）は文字をそのまま残し、最初に違うバイトの位置で同じく `!` にする。`!` は保存に成功するか開き直すまで続く（U+FFFD を消しても続く）。保存しようとすると、どの文字コードでも「元のバイトは失われる」と確かめる（M6 の B4 / B5。M4b ではコードページの保存を確かめずに断っていた）。文言は読んだファイル（別名で保存するときもバッファのファイル。M6 の修正 R6）と、読んだときの文字コードと保存する文字コードを名指しする（`t.txt was read as Shift_JIS with bytes that are not valid Shift_JIS (first at byte offset 3, line 2; read as U+FFFD); their original bytes will be lost. Save as Shift_JIS anyway?`。書き戻しで元に戻らないだけのものは `with bytes that Shift_JIS does not give back the same (...)`）。No なら `Not saved` で何も書かない。Yes なら普通の保存に進み、残っている U+FFFD など書けない文字があれば下の「表せない文字の保存」と同じくその位置で止まる（ファイルは変わらず、`!` も残る）。書ける文字だけなら保存して `!` を消す（CP 932 の `ED 40` は `FA 5C` として書く）
  - 表せない文字の保存: コードページは書いた結果を読み戻して元のテキストと比べ（書くときの `WC_NO_BEST_FIT_CHARS` / `lpUsedDefaultChar` は下の実測どおり使える CP だけ）、違えば保存を中止して、最初の文字にカーソルを移し `Cannot save as Shift_JIS: U+1F600 at line 3, column 4 cannot be written in Shift_JIS; choose another encoding with set-buffer-file-coding-system` と出す（行は 1 から、桁はモードラインの `C` と同じ）。ファイル・更新日時は変わらず、`save_file` を呼ばないので一時ファイルもできず、バッファは未保存のまま。似た文字への置き換え（best fit。CP 932 の `¥` U+00A5 → `\`、CP 50220 の半角カナ → 全角カナ）も同じく止まる。UTF-8 / UTF-16 はどの文字も書ける（対にならないサロゲートは U+FFFD）
  - 各コードページのフラグ（2026-11-15 に Windows 11 で実測。`encoding.h` の冒頭にも記録）: CP 932 は `MB_ERR_INVALID_CHARS`・`WC_NO_BEST_FIT_CHARS`・`lpUsedDefaultChar` とも使える（表せない文字は `?` になり `lpUsedDefaultChar` が立つ）。ただし `WC_NO_BEST_FIT_CHARS` では、12 文字（U+00A2 / A3 / AB / AC / AF / B5 / B7 / B8 / BB、U+3094 / 3099 / 309A。フラグ 0 では `81 91` などの似た 2 バイト文字に書かれる）のどれかがテキストの最後にあると、大きさの問い合わせは正しいバイト数を返すが、ちょうどその大きさの領域へ変換すると `ERROR_INSUFFICIENT_BUFFER` で失敗する（1 バイト多い領域なら同じバイト数を書き、その文字は `?` で `lpUsedDefaultChar` が立つ）。このため変換には余裕のある領域を渡し、失敗したら広げて呼び直す。それでも変換が失敗したときは、1 文字ずつ書いて読み戻し、最初に戻らない文字の位置を示す（M4b の修正 R1 / R2。最初の実測は自分の探り方の誤りで「`0x00` と書かれる」と記録していた）。フラグ 0 では BMP の 85 文字が似た文字に置き換わる。CP 20932 は 3 つとも使える（`0x80` は U+0080、`0x8F` で始まる 3 バイトの補助漢字は U+008F ＋ 2 バイトの文字として読まれ、読み戻しの比較で `!` になる）。BMP に best fit は見つからなかった（表せない文字は `?`）。CP 50220 はどちらのフラグも使えない（`ERROR_INVALID_FLAGS`、`lpUsedDefaultChar` を渡すと `ERROR_INVALID_PARAMETER`）。フラグ 0 で半角カナなど 63 文字が似た文字に置き換わる。どのコードページでも読み戻しの比較をする
  - BOM: BOM ありの名前で開くと BOM を外し（無くても読める。保存では付ける）、BOM なしの名前で開くと先頭の BOM は U+FEFF の文字として残す（保存すると同じバイト列に戻る）
  - コマンド: `set-buffer-file-coding-system`（`C-x RET f`、File > Encoding for Saving）は次の保存に使う文字コードだけを変え、テキストは変えず、バッファを変更ありにする（Emacs と同じ。undo の単位にはせず、保存に成功するか開き直すまで変更ありのまま）。同じ文字コードなら `The encoding for saving is already UTF-8` と出して何もしない。`utf-8` ↔ `utf-8-bom` もこれで切り替える。`revert-buffer-with-coding-system`（`C-x RET r`、File > Reopen with Encoding...）はディスクのファイルを指定の文字コードで読み直す。変更があれば `Discard the changes to <name> and read it again as <ENC>?` と確かめ（No なら `Cancelled` で何も変えない）、undo 履歴は捨て、カーソルは同じ行番号（ファイルが短くなれば最後の行）に置く。読み直す前に `read-only-mode` だったら読み直した後も `read-only-mode` のまま（M6 の B3。読み取り専用属性のファイルは今までどおり `read-only-mode` になる）。ファイル名が無ければ `Buffer is not visiting a file`、ディスクに無ければ `File <path> is not on disk; nothing to read again` と出して何もしない。`find-file-with-encoding`（`C-x RET c`、File > Open with Encoding...）はミニバッファでファイル名（find-file と同じく今のフォルダーから始まり、Tab でファイル名を補完）、続けて文字コード名（Tab で 9 種を補完。ほかの名前は `[No match]`）を読み、以降は find-file と同じ流れで開く
  - 新規ファイルは `new_file.encoding` の文字コード（M4b から反映）。`--encoding` や `find-file-with-encoding` で名前を指定した新規ファイル、`set-buffer-file-coding-system` を使ったバッファは、設定を読み直しても変えない
  - File メニューの Encoding for Saving のラジオは、バッファの文字コード（判定・指定して開く・開き直す・`set-buffer-file-coding-system` で変わる）に付く。M4 で作った File の項目（Open with Encoding... / Reopen with Encoding... / Encoding for Saving / Line Endings / Read Only）はすべて有効
- Norton 360 は `build/` の外に置いた新しい exe を約 2 秒止めるので、計測や確認は `build/` の exe で行う

ヘッドレス（`--headless`）はテスト・計測・実機確認のための起動引数で、利用者向けには案内しない（付けても未知のオプションの表示は出ない）。確認の最中に利用者の画面・打鍵・クリップボードを奪わないためのもので、app_check と計測スクリプトはこれを既定にする:

- ウィンドウは仮想画面の右の外に置く（どのモニターにも映らない）。ツールウィンドウなのでタスクバーと Alt+Tab に出ず、`WS_EX_NOACTIVATE` で前面にもならない。画面の外の普通のウィンドウには `WM_PAINT` が来ず初回描画が終わらなかった（Windows 11 で実測）ので、不透明のレイヤードウィンドウにしてある。描画は通常と同じ経路（`EndDraw` まで）で、`--measure-startup` / `--measure-input` / `GCAP` の画像はそのまま使える
- キーは `PostMessage` で受ける。修飾（Ctrl / Shift / Alt）はそのキーの `WM_KEYDOWN` / `WM_SYSKEYDOWN` の `lParam` のビット（25 が Ctrl、26 が Shift、27 が Alt。Windows が 0 にしている予約のビット。`src/win/headless.h`）だけから読み、`GetKeyState` は読まない。Ctrl と Alt の無い文字のキーは、`TranslateMessage`（キーボードの状態を読む）に渡さず、キー配列と Shift のビットから文字にする。利用者が実際に押している Ctrl などが混ざらないため（M4a の修正 F2。以前は `y` が `C-y` として届いた）。画面に出すとき（`--visible`、`--input post` を含む）は今までどおり `GetKeyState` と `TranslateMessage`。geditor 自身も前面を取らない（ドロップ時の `SetForegroundWindow` もしない）
- クリップボードは本物を開かず、プロセス内の専用の置き場を使う（中身は本物と同じ CRLF）。読み書きの失敗は `GFLT` の `clipboard-read` / `clipboard-write` で 1 回だけ起こせる
- 確認（開き方・保存・はい/いいえ・ログオフ時の保存）はダイアログを出さずに待つ。待っている間はキー・マウス・ドロップを受け付けず、`WM_CLOSE` とログオフの問い合わせも答えが出るまで断る（`WM_COPYDATA`・描画・画像の保存は続く）。答えが来るまで待ち続けるので、ツールの側で時間切れを決める。起動時の Direct2D 初期化の失敗はメッセージボックスの代わりに標準エラーへ `headless_error=init ...` と出して終了コード 1 で終わる（`GEDITOR_HEADLESS_FAULT=init` を環境変数に入れるとヘッドレスのときだけわざと失敗させられる）
- 確認の文言とボタン名は `src/win/question.h` の 1 か所で作り、TaskDialog とヘッドレスの両方が使う
- 「新しいウィンドウで開く」にも `--headless` を渡す（`--settings` と同じ）
- メニューは画面に出さない（M3b）。F10（`menu-bar-open`）と `WM_CONTEXTMENU` は、メニューを開く代わりに確認と同じ形で待つ（`GSTA` の `question=menu_bar` / `context_menu`、組み立てた `HMENU` から読み戻した全項目の `menu_item=<経路>\t<キー>\t<状態>`、位置の `menu_x` / `menu_y`）。`GANS` で項目の経路（`Edit > Undo`、右クリックでは `Undo`）か `Cancel` を答えると、本物のメニューと同じ `WM_COMMAND` が送られる。無い項目と灰色の項目は断り、メニューは待ったまま。`SC_KEYMENU` / `SC_MOUSEMENU`（Alt+Space のシステムメニューを含む）は握りつぶして、回数と最後の値を `GSTA` の `keymenu_count` / `last_keymenu` に出す。メニューループに入った回数は `menu_loops`
- マウスは `PostMessage` の `WM_LBUTTONDOWN` / `WM_MOUSEMOVE` / `WM_LBUTTONUP` / `WM_LBUTTONDBLCLK` / `WM_MOUSEWHEEL` / `WM_RBUTTONUP`（クライアント座標と `MK_*`）で受ける。どの升目がどこにあるかは `GSTA` の `line_height` / `cell_width` / `text_left` / `mode_line_top` / `text_rows`（物理ピクセル）から求める（`App.cell_point`）
- 設定ファイルを書き換える確認（app_check の `apply_settings` / `write_settings`）は、決まった時間だけ待つのではなく、その設定ファイルで起動した geditor の `GSTA` の `settings_reloads`（監視スレッドからの読み直しを反映した回数）が増え、`reloads_drawn`（描き終えた画面に反映済みの読み直しの回数）が追いつくまで待ち、さらに 0.3 秒その回数が変わらないことを確かめる（1 回の保存で 2 回読み直すことがあるため）。`frames` は描き終えた画面の数
- ツールとのやり取りは `WM_COPYDATA`（`src/win/headless.h` に一覧）。`GSTA`（状態をファイルに書く: ヘッドレスか・設定ファイル・タイトル・エコー領域のメッセージ・エコー領域の行（`echo`。ミニバッファの途中はプロンプトと入力）・文字コード（`encoding` は名前、`encoding_text` はモードラインの部分。M4b）・変更の有無・文字数・クリップボードの中身・カーソルとマークと先頭の行・本文（64K 文字まで）・升目の位置・メニューバーの有無・色付きグリフの描き方（`color_support`）・断った要求の数と理由・待っている確認やメニューの種類・文言・ボタン名・検索（M5: `isearch` / `query_replace` が 1 か 0、`isearch_pattern`、画面の行で強調する一致 `search_matches`（`開始-終了` をカンマ区切り）と今の一致 `search_current`（無ければ `none`）、これまでにコンパイルしたパターンの数 `search_compiles`））、`GCBW`（クリップボードに入れる）、`GANS`（ボタン名を指定して答える。無いボタン名は断り、確認は待ったまま）。`GFLT` の `menu` / `popup` で次のメニューの作成と右クリックメニューの表示を 1 回だけ失敗させられる（エコー領域に理由が出る）。`GFLT` の `color` は次の色付きグリフの変換を失敗させる（以後は単色）。`GRDW`（本文は枚数 1〜1000）はその枚数の画面をその場で続けて描き、1 枚ずつの時間（最初の 1 枚と、残りの中央値・95 パーセンタイル・最大）を `GSTA` の `redraw_*` に出す。デバッグビルドでは残りの枚数の間に C ランタイムが確保したバイト数も出す（リリースでは -1）。分からない要求は 0 を返して数え、`OutputDebugString` にも出す

実機確認（受入条件 M1 の画面・キー・ダイアログ・ファイル・クリップボード）は `tools/app_check.py` で行う。テスト用ファイルを `build/app_check/<日時>/files/` に作って geditor を起動し、キーを送り、画面を `shots/*.png` に保存し、ファイルのバイト列・クリップボード・タイトルを確かめて、項目ごとに PASS / FAIL / SKIP と証拠を表示する。画像は目でも確かめる:

```powershell
python tools/app_check.py build/release/geditor.exe            # headless; --strict: SKIP also fails
python tools/app_check.py build/release/geditor.exe --only-retry1
python tools/app_check.py build/release/geditor.exe --only-m2
python tools/app_check.py build/release/geditor.exe --only-m3a
python tools/app_check.py build/release/geditor.exe --only-m3b
python tools/app_check.py build/release/geditor.exe --only-m3c
python tools/app_check.py build/release/geditor.exe --visible --only-m3c   # F10: needs the unlocked desktop
python tools/app_check.py build/release/geditor.exe --only-m3d
python tools/app_check.py build/release/geditor.exe --visible --only-m3d   # F10 wrap-around: needs the unlocked desktop
python tools/app_check.py build/release/geditor.exe --only-m4a
python tools/app_check.py build/release/geditor.exe --only-m4b
python tools/app_check.py build/release/geditor.exe --only-m5       # headless only
python tools/app_check.py build/release/geditor.exe --only-m6       # headless only
python tools/app_check.py build/release/geditor.exe --only-headless
python tools/app_check.py build/release/geditor.exe --visible --only-visible
python tools/app_check.py build/release/geditor.exe --visible --only-m3b
```

- 既定はヘッドレスで、最初の行に `mode headless` と出る。`--visible` を付けると従来どおり画面に出し、`SendInput` と前面（画面がロックされていなければ）・本物のクリップボード・本物の TaskDialog を使う（`mode visible`）
- ヘッドレスの実行中は、別プロセスの見張りが 20ms ごとに前面のウィンドウと見えている `#32770`（ダイアログ）と `#32768`（メニューとポップアップメニュー）のウィンドウを調べ、geditor（同じ exe）のものが 1 度でもあれば FAIL にする。前面の見張りの間隔が 1 度でも 50ms を超えたときも FAIL にする。同じ見張りは 20ms ごとに、ヘッドレスの geditor のウィンドウ（`geditor.main` のツールウィンドウ）がどのモニターとも重ならず、最小化もされていないことを調べ、外れたら FAIL にする（最小化したツールウィンドウは主モニターにタイトルバーだけ置かれるため、ヘッドレスでは本当には最小化しない。`[S2]` は `WM_SIZE` の `SIZE_MINIMIZED` と元の大きさの `SIZE_RESTORED` を送って確かめる）。新しいウィンドウを探すときは、この実行が起動した geditor とその子プロセスのウィンドウだけを見る（ほかの geditor が開いていても数えない）。本物のクリップボードは開かず、`GetClipboardSequenceNumber` が前後で同じことを確かめる。利用者が実行中にコピーすることがあるので、同じ見張りが番号の変化ごとに `GetClipboardOwner` の持ち主のプロセスと、そのとき開いていた geditor を記録し、持ち主が geditor のとき（または持ち主が分からず、この実行の geditor が開いていたとき）だけ FAIL にする。ほかのプログラムが書いたときは持ち主のプログラム名を INFO に出す（判定の部分は作り物の持ち主で `[R3]` が確かめる）。`app_driver.py` はヘッドレスのとき `SendInput`・`keybd_event`・`SetForegroundWindow`・`BringWindowToTop`・`SetActiveWindow`・`SetFocus`・クリップボードの関数・`MoveWindow`・`SW_HIDE` / `SW_SHOWNOACTIVATE` / `SW_SHOWNA` 以外の `ShowWindow`（最小化も含む）・移動する `SetWindowPos`・`AttachThreadInput`・`SetKeyboardState`（M4a の修正 F2）を呼ぶと例外にする
- `--only-headless` はヘッドレスそのものの確認（ウィンドウの位置と種類・打鍵後の画像・見張り・ツール側の禁止・キーの修飾（`[M4a fix F2]`: ツールのスレッドを geditor の入力につないで共有のキーボードの状態で Ctrl を押したままにし、`y` が `y` と入り、`lParam` のビットで Ctrl を付けた `C-y` が貼り付けになることを状態の本文で確かめる）・クリップボードの置き場と失敗・確認の種類とボタンごとの答え・待っている間のキー・新しいウィンドウ・おかしな要求・起動の失敗・計測スクリプト）だけを行う。全体の実行にも含まれる
- 画面が要る確認はヘッドレスでは SKIP（`needs --visible`）になり、`--strict` でも失敗に数えない。今は次の 11 個（`app_check.py` の `needs_visible` の呼び出し）で、前の 5 つは `--visible --only-visible`、次の 4 つのうち上の 3 つは `--visible --only-m3b`、最後の 2 つはそれぞれ `--visible --only-m3c` と `--visible --only-m3d` で確かめる（前面を取りダイアログやメニューを出すので、利用者が画面を使っていないときに行う。画面がロックされていると SKIP になる）:
  - `[visible] keys through SendInput to the foreground window`（`SendInput` と前面）
  - `[visible] task dialogs: their look and their buttons`（TaskDialog の見た目と `TDM_CLICK_BUTTON`）
  - `[visible] the real clipboard: C-k / C-y through CF_UNICODETEXT`（本物のクリップボード）
  - `[visible] clipboard held open by another program`（他のプログラムがクリップボードを開いているとき）
  - `M-<f4> closes the window`（割り当てのない M-<f4> はシステムに渡り、システムの Alt+F4 はアクティブなウィンドウを閉じる。ヘッドレスのウィンドウはアクティブにならないので閉じない）
  - `[C6] F10 and a mouse click open the real menu loop (look, mnemonics, Esc)`（本物のメニューループ。マウスでメニューバーを開くのは人が確かめる）
  - `[C6] Alt+Space opens the system menu`（システムメニュー）
  - `[C4] the real right-click popup (look, position at the mouse)`（`TrackPopupMenu` の見た目と位置）
  - `[C5] the mouse cursor is an I-beam over text and an arrow elsewhere`（`WM_SETCURSOR` は本物のマウス位置を読むので、人が画面で確かめる）
  - `[M3c C5] F10 (SendInput press and release) drops the File menu down ...`（F10 で File が下に開き、左右で移る）
  - `[M3d C3] F10 (SendInput) drops File down; Left goes to View, ...`（F10 の端での折り返し）
- 日本語 IME の確認（下記）も画面が要る
- 最後の行に PASS / FAIL / SKIP の件数（そのうち `--visible` が要るもの）が出る。`--strict` を付けると `needs --visible` 以外の SKIP が 1 件でもあれば終了コード 1。`--only-retry1` は長いパス・最小化と復元・結合文字と不可視文字・ログオフ時の保存確認・クリップボード読込失敗の確認だけを行う
- 起動する geditor にはすべて `--settings` で `build/app_check/<日時>/` の下のファイルを渡す
- `--only-m2` は設定ファイルの確認（M2）だけを行う。geditor の起動中に設定ファイルを書き換えて、キーの割り当てが効くまでの時間を測り（1 秒以内）、構文の誤りで直前の設定が残ること、誤りが 4 件のときにエコー領域が 4 行に広がり次のキーで 1 行に戻ること、ファイルを消すと既定に戻ること、他のプログラムが共有なしで開いている間は読めない理由を表示して直前の設定を保つこと、`find-user-init-file` が例の入った未保存のバッファを開き、保存した例が誤りなしで読めることを確かめる。最後に、無いフォルダーの設定ファイルを指定して監視を始められないことの表示を確かめる
- `--only-m3a` は表示の確認（M3a）だけを行う。起動中に設定ファイルの表示項目を 1 つずつ書き換えて画面の画素を調べる（原色の配色で色を見分ける）。View のコマンドを M-x から実行し、text-scale のキー、`WM_VSCROLL`、本物の `WM_DPICHANGED`（lParam の RECT は Windows が geditor 側へ写す）を送る。フォントと描画先（render target）の作成をわざと失敗させるときは `WM_COPYDATA`（`dwData` = `'GFLT'`、本文 `font` か `target`）を送る。最後に `[C4 table]` の行で「設定項目 | 画像 | 見えるもの」の表を出す
- `--only-m3b` はメニューとマウスの確認（M3b）だけを行う。クリック・ドラッグ（下へはみ出したときの自動スクロールと離したときの停止）・ダブルクリック・Shift+クリック・ホイール（1 刻みに満たない量の累積）・undo の区切り・何もしないクリックを `PostMessage` のマウスのメッセージで確かめ、`shots/m3b_*.png` に撮る。本物のメニューバーを `GetMenu` と `GetMenuItemInfoW` で（`WM_INITMENUPOPUP` を送ってから）読んでコアの表と比べ、`WM_COMMAND` で項目を実行し、F10 と右クリックで待っているメニューに答える。`menu_bar: false` の出し入れ、Alt+F と Alt 単独、メニューと右クリックメニューの作成失敗の表示も確かめる。`--visible --only-m3b` は本物のメニューループ・システムメニュー・右クリックメニューを `PrintWindow` で撮る
- `--only-m3c` は書記素クラスタと F10 の確認（M3c）だけを行う。U+FE0F 付きの絵文字・ZWJ でつないだ家族・国旗・キーキャップ・か＋゛ が ASCII の隣で 2 升に描かれ、カーソルがその後ろに来ること（画素で調べる）、DEL 1 回で消えて undo 1 回で戻ることを確かめる。形を作るのをわざと失敗させるときは `WM_COPYDATA`（`dwData` = `'GFLT'`、本文 `shape`）を送る。M-x の途中で ESC を 3 回送るとミニバッファが `Quit` で閉じること（ESC 2 回では待つこと）、F10 は保留中の `menu_bar` の質問と、開くサブメニュー（状態の `menu_open=File`）を確かめる
- `--visible --only-m3c` は本物の F10 を `SendInput` で押して離し、File メニューが下に開いて離した後も残ること、右キーで Edit、左キーで File と View（端で折り返す）へ移ること、Esc で閉じることを状態の `menu_popup` / `menu_loop_active` と `#32768` の窓で確かめる。前面を取るので、利用者がデスクトップを使っていないときに実行する
- `--only-m3d` はカラー絵文字と F10 の折り返しの確認（M3d）だけを行う。灰色だけの配色（どのチャンネルも 0x20〜0x60。ClearType のにじみもこの範囲に収まる）で、U+2639 U+FE0F・U+263A U+FE0F・肌の色付きの 👍・ZWJ でつないだ家族・😀・✅ を「a＋絵文字＋b 漢x」の行に置き、その間に ASCII と漢字の行を挟んで撮る。範囲の外で色相のある画素を色付きと数え、絵文字の 2 升にあり、同じ行のほかの升目と下の行に無いこと、`a` と `漢` は描かれていて色が無いことを 1 つずつ確かめる。カーソルの縦棒が絵文字の上に描かれること、全体を選んだ画像で絵文字が色を保ち升目に選択の色が出ること、`GFLT` の `color` で単色になりエコー領域に `Colour emoji are not available; drawn in one colour` が 1 回だけ出ることも確かめる。別のウィンドウで、文字表示の記号（U+2639・U+2764・U+2714・U+2600）と U+FE0E 付き（U+2639・U+1F600）が描かれていて、色付きの画素が無いことを確かめる（`m3d_05_text_style.png`）。ヘッドレスでは、折り返し用に送ったしるし付きの ← がメニューループの外では捨てられ（カーソルが動かない）、F10 が M3c と同じく `menu_open=File` で待つことを確かめる。最後に、絵文字 30 行×40 個の画面・それを単色にした画面・ASCII の画面を `GRDW` で 60 枚ずつ描き、時間と全体の CPU 負荷を INFO に出す（デバッグビルドの exe を渡すと、色付きの画面が単色の画面より多く確保しないことも確かめる）
- `--visible --only-m3d` は本物の F10 を `SendInput` で押し、File、← で View、→ で File と 1 回の打鍵ずつで移ること（`menu_popups` の並びからシステムメニューを除くと `File,View,File`）、Esc でドロップダウンが閉じ、もう 1 回の Esc でメニューから抜けることを確かめ、各段階を `shots/m3d_v*.png` に撮る。前面を取るので、利用者がデスクトップを使っていないときに実行する
- `--only-m4a` は改行・保存・読み取り専用の確認（M4a）だけを行う。LF / CRLF / CR が混在したファイルを開いて行ごとの改行記号の色（既定の改行は `eol`、違う改行は `eol_other`）とモードラインの `Mixed(CRLF)` を画素と状態で調べ、File メニューの Line Endings と Read Only の印を読む。保存したバイト列・`file~`・外部で変わったファイルの確認（No なら書かない）・消されたファイルの保存を確かめ、保存の各段階を `WM_COPYDATA`（`dwData` = `'GFLT'`、本文 `save-inspect` / `save-link` / `save-temp-create` / `save-temp-write` / `save-flush` / `save-replace` / `save-move` / `save-backup` / `save-in-place` / `file-info`）でわざと失敗させて、理由の表示・元のファイルと `file~` が変わらないこと・一時ファイルが残らないことを見る。読み取り専用属性のファイル、UTF-8（BOM あり・なし）× 改行 4 種の読込と保存、ハードリンクとシンボリックリンク（作れない環境では理由付きの SKIP）も確かめる。M4a の修正では、CR・LF・CRLF の行のファイルで LF の行を空にして保存し、バイト列が `a\r\rb\r\n` になること・件数の表示・記号の色・開き直した行（`m4a_05_cr_lf_saved.png` / `m4a_06_cr_lf_reopened.png`）を確かめ（`[M4a fix F1]`）、ハードリンクの `save-backup` の表示に `file~` のパスが入ることを確かめる（`[M4a fix F5]`）。長いファイル名（240 文字とボリュームの最大長）の保存は `test_win_save.cpp` が確かめる
- `--only-m4b` は文字コードの確認（M4b）だけを行う。BOM の 3 種・BOM なしの UTF-16LE・Shift_JIS・UTF-8 のファイルを判定で開いて文字コードとテキストと本物の `HMENU` の Encoding for Saving のラジオを確かめる。`C-x RET c` でファイル名と文字コード名を Tab で補完して EUC-JP で開き、F10 のメニューを `GANS` で File > Open with Encoding... と答えて ISO-2022-JP のファイルを新しいウィンドウで開き、新しいウィンドウがその文字コードで読むこと（`--encoding` が渡ること）を見る。`--encoding=utf-8` で Shift_JIS のファイルを開いて U+FFFD・位置の表示・モードラインの `UTF-8!` を確かめて撮り、`revert-buffer-with-coding-system` の確認に No（何も変わらない）と Yes（正しく読み直す）で答える。絵文字を貼って Shift_JIS の保存が止まり、ファイル・更新日時・フォルダー（一時ファイルなし）が変わらないこと、本物の `HMENU` の項目（`WM_COMMAND`）で `utf-16le-bom` に変えて保存できることを確かめる。最後に `--encoding=euc-jp` の起動と、未知の名前（`--encoding=big5`）の表示を見る。画像は `shots/m4b_*.png`
- `--only-m5` は検索と置換の確認（M5）だけを行う（ヘッドレスだけ。`--visible` とは組み合わせられない）。原色の配色（文字は赤、`search_match` は緑、`search_current` は青）で `foo` を 3 つ含むファイルを開き、起動直後の `GSTA` の `search_compiles` が 0 であること（PCRE2 を起動時に使わない）を見る。isearch（`C-s foo`、もう 1 回 `C-s`）と問い合わせ置換（`M-%`）の画像で、今の一致の升目が青、ほかの一致が緑、一致でない語はどちらでもないことを画素で調べ、`RET` / `q` の後に緑と青の画素が 1 つも残らないことを確かめる。両方の途中で本物の `HMENU` の全項目が灰色で、終わると元に戻ること、`y` の 1 回の置換が `C-/` 1 回で戻ることを見る。Edit > Find... / Replace... / Go to Line... を `WM_COMMAND` で実行して、それぞれ isearch・`Query replace: `・`Goto line: ` が始まることを確かめる。`C-S-f` / `C-S-g` / `C-S-h` / `C-S-r` と、isearch の中の `C-S-s`（次の一致。保存しない）と `C-S-z`（isearch を終えて `C-S-z is undefined`。undo しない）も見る。最後に 100MB のファイル（`build/m5_search/m5_100mb.txt` に一度だけ作る。80 文字の行で、末尾から 100 行目に `§zebracorn`）で isearch に `§` を送ってから、状態の `point` がその語の後ろになるまでの時間を測る（1 秒以内で PASS。状態の読み取りの時間を含む）。画像は `shots/m5_*.png`
- `--only-m6` は M6 の確認だけを行う（ヘッドレスだけ。`--visible` とは組み合わせられない）。`C-k` を 3 回続けて 2 行分がつながってプライベートのクリップボードに入り、`C-n` の後の `C-k` で入れ直されること、`S-<right>` 2 回で一時的なマークからの選択になり `<right>` で消えること、`C-x (` `x` クリック `y` `C-x )` で記録中のモードラインに `Def` が出てイベントが 2 つ（クリックは記録しない）になり、`C-x e` と `e` で実行し、`C-/` 1 回で 1 回分が戻ることを確かめる（状態の `macro_recording` / `macro_events`）。修正 R1: `C-k C-k` のマクロを `C-u 4 C-x e` で実行した後、プライベートのクリップボードが 8 回の kill をつなげた文字列であること。修正 R2: 10 文字のマクロを `C-u 100000 C-x e` で実行し始めて 1 秒後に `C-g` を送り、2 秒以内に `Quit` で止まり、`C-/` 1 回で止まった実行の全部が戻ること。B2: ファイルなしの `--encoding=shift_jis` でモードラインが `Shift_JIS` になり、`C-x C-w` で Shift_JIS のバイト列が書かれること、`--encoding=big5` で `Unknown encoding` が出ること。B3: `C-x C-q` の後の `C-x RET r shift_jis` で読み取り専用のままなこと。B4: 読めないバイトのある Shift_JIS のファイルで U+FFFD を消して保存し、確認（`GANS`）に No で何も書かず、Yes で Shift_JIS のバイト列が書かれて `!` が消えること。B5: `UTF-8!` のバッファを Shift_JIS に変えて保存すると、確認が `was read as UTF-8` と `Save as Shift_JIS` を名指しし、Yes の後に U+FFFD で止まること。B6: 10MB の 1 行（`build/m6_scroll/` に一度だけ作る）を `window` 折り返しで開き、`GetScrollInfo` の範囲があり（状態の `scroll_bar_unit=offset`）、`<end>` でつまみが末尾へ動き、`SB_THUMBTRACK` を送ると先頭の行がその位置へ動くこと、行の多いファイルは論理行のままなこと。画像は `shots/m6_*.png`
- `tools/` のスクリプトは `__pycache__` を作業ツリーに残さない（`sys.dont_write_bytecode` と、`app_driver.py` が読み込み時に自分の `.pyc` を消す処理による）
- ヘッドレスではいつも `PostMessage` で `WM_KEYDOWN` / `WM_CHAR` を送り、修飾キーは各キーの `lParam` のビットで渡す（上の「キーは `PostMessage` で受ける」）。キーボードの状態には触れない（`AttachThreadInput` と `SetKeyboardState` は `app_driver.py` がヘッドレスで禁止する。例外は、利用者の押している Ctrl を真似る `--only-headless` の `[M4a fix F2]` の確認が使う `held_keyboard_state` だけ）。`--visible` で `PostMessage` を使うとき（下）は今までどおり修飾キーの状態を `AttachThreadInput` と `SetKeyboardState` で与える。`--visible` では、画面がロックされていなければ `SendInput` で打鍵する（geditor を前面に出してから送り、前面でなければ送らずに止まる）。ロック中か `--input post` なら `PostMessage` で送る（`--input post` では geditor を `SW_SHOWNOACTIVATE` で起動し、前面を取らない）。どちらで動いたかは `input mode` の行に出る
- `--visible` で画面がロックされていると `OpenClipboard` がアクセス拒否になるため、クリップボードを使う確認は SKIP になる（geditor 側の書き込み失敗の表示は確かめる）。`--visible` では実行の前後でクリップボードの文字列を保存・復元する（ヘッドレスでは触れない）
- 画像は geditor に `WM_COPYDATA`（`dwData` = `'GCAP'`、BMP の保存先パス）を送って描かせる。ロック中は画面にも `PrintWindow` にも描画が出ないため。ダイアログ（`--visible` のみ）は `PrintWindow` で撮る
- `tools/app_driver.py` が起動・打鍵・撮影の部品。ライブラリとして読み込んだだけでもヘッドレスが既定（`HEADLESS = True`）で、そのまま `App(...)` を作ると `--headless` で起動する。画面に出すには呼ぶ側が明示的に `HEADLESS = False` にする（app_check の `--visible` がこれをする）。全体の実行と `--only-headless` は、別プロセスで読み込んだ `App(...)` がヘッドレスで起動することも確かめる（`[R3]`）

日本語 IME の確認（受入条件 M1-4）は人が画面で行う（ヘッドレスでは確かめられない）。変換ウィンドウがカーソル位置に出ること、確定した文字列が 1 回の undo で消えることを見る。

## 検索・置換・goto-line（M5）

PLAN 4.4 の「isearch 中」「問い合わせ置換中」と 2 章「検索の単位」「大文字小文字」の実装。PLAN に無かった細部は Emacs に合わせて決めた（PLAN 5 章 M5 の「M5 の実装」）。

- 照合はコアの `src/core/search.*`（`SearchPattern`）だけが行い、isearch・一致の強調表示・問い合わせ置換が同じものを使う。PCRE2（16bit・UTF・JIT）で、通常の検索は `PCRE2_LITERAL`、正規表現は `PCRE2_UCP`（`\w` が漢字やカタカナにも当たる）。どちらにも `PCRE2_MATCH_INVALID_UTF` を付ける（付けないと、対になっていないサロゲートのある行の照合が `-25` のエラーになった。PCRE2 10.49 で実測）。大文字小文字を区別しないときは `PCRE2_CASELESS`（`UCP` なしの `LITERAL` でも全角の `ａ` と `Ａ`、`é` と `É` が同じに扱われることを実測）
- 論理行ごとに照合し、パターンは `\n` をまたがない（`a.b`・`a\sb`・`a\nb`・`a[\s\S]b`・`a\Rb` も行をまたいでは当たらない）。piece tree の 1 つの chunk に収まる行はその場で、複数の chunk にまたがる行だけを使い回しのバッファに写して照合し、バッファ全体を 1 つの文字列に写さない
- PCRE2 は最初のパターンのコンパイルで初めて呼ぶ（起動時には呼ばない）。`search_pattern_compiles()` とヘッドレスの `GSTA` の `search_compiles` がその回数で、起動直後は 0
- 置換文字列の `$1` / `${1}` / `$$` / `${name}` は `pcre2_substitute`（`PCRE2_SUBSTITUTE_MATCHED`）で展開する。この関数は、照合したときと同じ文字列（同じアドレス）・開始位置・照合のオプションでないと断る（`substitute subject differs from prior match call` など。10.49 で実測）ので、置換する一致の行は写しを取って照合し直してから展開する。通常の問い合わせ置換は置換文字列をそのまま入れる
- isearch: `C-s` / `C-r` で始め、エコー領域に `I-search: ` / `I-search backward: ` / `Regexp I-search: ` とパターンを出す（`C-u C-s` / `C-u C-r` は正規表現）。文字（IME の確定文字列も 1 回の入力）でパターンが伸び、今の一致の先頭から探し直す。前向きはカーソルを一致の後ろ、後ろ向きは一致の先頭に置く（書記素クラスタの境目に広げる）。見つからなければ `Failing I-search: <パターン>` と出してカーソルは最後の一致に残り、次の `C-s` で先頭から（`C-r` は末尾から）探して `Wrapped I-search` と出す。向きを変える `C-r` / `C-s` は、まず今の一致の反対の端へ移る。パターンが空の `C-s` は前回の検索文字列（通常と正規表現で別）を使う。`DEL` は最後の入力（文字・繰り返し・切り替え）を取り消す。`RET` は一致の位置で終わり、開始位置にマークを置いて `Mark saved where search started` と出す（リージョンが有効なら置かない）。`C-g` は開始位置と表示の先頭の行に戻して `Quit`（失敗中でも 1 回で戻る）。`M-r` は正規表現、`M-c` は大文字小文字の区別を切り替え、プロンプトに `Regexp` / `Case-sensitive` / `Case-insensitive` を出す。`M-%` は isearch を終えて一致の先頭から `Query replace <パターン> with: `（正規表現なら `query-replace-regexp`）を尋ねる。不正な正規表現は PCRE2 のメッセージをプロンプトの後ろに `[missing closing parenthesis]` のように出し、カーソルとバッファは変えない
- 大文字小文字（`search.case_fold`）: `smart` はパターンに大文字（`to_lower` で変わる文字。`unicode.h` の大小の表の範囲）があれば区別する。正規表現では `\` の直後の文字と、その直後の `{...}`（`\p{Lu}` / `\x{1F600}`）を数えない。`always` は区別しない、`never` は区別する。`M-c` はそのときの規則を反対にする
- 問い合わせ置換（`M-%` / `C-M-%`）: `Query replace: ` と `Query replace <FROM> with: `（正規表現は `Query replace regexp ...`）を読み、カーソルから末尾までを対象にする（折り返さない。リージョンでの範囲指定はしない）。今の一致の後ろにカーソルを置き、`Query replacing <FROM> with <TO>: ` と出す。`y` / `SPC` 置換して次へ、`n` / `DEL` 飛ばす、`!` 残りを全部、`.` 置換して終わり、`q` / `RET` 終わり。終わると `Replaced N occurrence(s)` と出す。1 回の問い合わせ置換で行った置換は全部で 1 つの undo 単位（`UndoHistory::merge_groups_since`）で、undo でカーソルは置換を始めた位置に戻る。空の一致（`x*`）は、同じ位置で続けて空の一致を取らずに進む。`!` は行ごとに、その行の写しの上で残りの一致を全部探して展開し、1 回の編集で置き換える（置換した文字列から新しい一致は生まれない。1 回ずつ探し直すと長い行で一致ごとに行全体を集め直していた。M5 の修正 R3。行の途中で展開に失敗すると、その前までを置換して止まる）。大文字小文字は `search.case_fold` に従う（isearch の `M-c` は引き継がない）。読み取り専用のバッファでは始めず `Buffer is read-only`、途中で `C-x C-q` されたら次の置換で `Buffer is read-only` と出して終わる。`$9` のような展開できない置換文字列は `Invalid replacement: unknown substring; Replaced N occurrences` で終わる。始めた時点で一致が無ければ `Replaced 0 occurrences` と出してすぐ終わる
- ほかの層のキー（M5 の決定。保留だったレビューの指摘も解消）: Windows 互換キー（`C-S-a` などの 12 個）は、ミニバッファ・isearch・問い合わせ置換の途中では働かない。`C-S-<英字>` はそのモードのキーマップの中でシフト変換するので、`C-S-g` は `C-g`（取消）、isearch の `C-S-s` は `C-s`（次の一致）になる。isearch のキーマップに無いキーは、isearch を今の一致で終えて（`RET` と同じ）から、Windows 互換キーを除いたグローバルのキーマップで実行する（Emacs と同じ。`C-S-z` は `C-z` として引かれ `C-S-z is undefined` になり、undo しない）。ミニバッファと問い合わせ置換は、自分のキーマップに無いキーを Windows 互換キーを除いたグローバルのキーマップで引く（M-x の `C-S-w` は `C-w` で、終了しない）。問い合わせ置換の途中のグローバルのコマンド（`C-e` など）はそのまま動き、その後はカーソルから次の一致を探し直す。問い合わせ置換では割り当ての無い文字キーは `<key> is undefined` で、バッファに入らない（IME の文字列も入らない）。問い合わせ置換の途中に開いたミニバッファ（`M-x`、`M-g g` など）には今までどおり文字も IME の文字列も入り、そこでの `C-g` / `ESC ESC ESC` はミニバッファだけを閉じて問い合わせ置換を続ける（ミニバッファの外の `C-g` は問い合わせ置換を終える）。設定の `keys` の割り当てはこれらのモードでも働く（除くのは Windows 互換キーの層だけ）
- 問い合わせ置換はクリックを無視し、isearch はクリックで終わる（`RET` と同じ）。ファイルのドロップ・ログオフの確認・ウィンドウを閉じる操作は、先に isearch（`RET` と同じ）と問い合わせ置換（`q` と同じ）を終える
- goto-line（`M-g g` / `M-g M-g` / `C-S-g` / Edit > Go to Line...）: ミニバッファで `Goto line: ` を読む。前置引数があればその行へ直接移る（`C-u` だけなら 4）。範囲外は最初か最後の行に寄せ、カーソルは行頭に置く。数でない入力は `[Please enter a whole number]` と出してミニバッファに残る。Emacs と同じく、リージョンが有効でなければ元の位置にマークを置いて `Mark set` と出す
- 強調表示: レンダラーは `Editor::search_highlights`（画面の行の範囲の一致と今の一致。書記素クラスタの境目に広げる。空の一致は出さない。最大 10000 個）を受け取って描くだけで、自分では探さない。重ね順は選択範囲・`search_match`・`search_current`・文字・カーソル（PLAN 4.7）。64K 文字を超える行は、見えている範囲の前後 4096 文字だけで一致を探す（その外から続く一致は強調しない。行頭・行末の `^` / `$` はその窓の端では当たらない）。isearch と問い合わせ置換が終わると強調は消える。isearch のパターンが空・不正・失敗中のときは今の一致を強調しない

## 保留だった性能の改善（M6 の前）

M5 までに保留にした 3 件の遅さ（Unicode 系以外の読込・長い行の後ろ向きの正規表現検索・長い 1 行の `<end>`）を、結果を変えずに速くした（2026-11-15）。どれも HEAD 63a24a6 の作りをテストの中に写し、生成した入力で新旧の結果が同じことを比べる（下の各項）。

- Unicode 系以外の読込（`src/core/encoding.cpp`）: 測ると、変換関数の大きさの問い合わせが変換と同じくらいかかり（100MB の Shift_JIS で約 250ms）、判定は Shift_JIS を一度 UTF-8 として全部読んでいた（約 500ms）。ファイル全体が書き戻しで元に戻らないときの代わりの読み方は、1 行ごと（ISO-2022-JP は 1 文字ごと）に変換関数を呼んでいた。変えたこと: 変換は問い合わせをせず余裕のある領域へ 1 回で行う（足りないときだけ問い合わせる）。判定の UTF-8 は最初の不正なバイトで止め、CP 932 で全バイトを読めたらその結果を Shift_JIS のテキストにし、読めなければ止めた所から UTF-8 を読み続ける（判定の順と規則は同じ）。代わりの読み方は、行末で区切った約 64KiB（ISO-2022-JP は U+FFFD にする単位を含まない約 64KiB）をまとめて 1 回で読み、まとめて読めて書き戻しも元に戻る区切りはそのまま使い、そうでない区切りだけを今までどおり 1 行・1 文字ずつ読む。ISO-2022-JP は、まとめて読んだ結果が文字ごとに 1 単位・そのまま残すバイトはそのまま、のときだけ使う。ISO-2022-JP の区切りには、ASCII 以外の状態の中の制御バイト（CP 50220 は `ESC ( I` の状態で NUL を読むと ASCII に戻る。M6 の前の修正 R1）を入れず 1 バイトだけの区切りにし、ASCII と `ESC $ B` 以外へのエスケープの前で区切る（全制御バイトと 0x80〜0xDF × 6 状態をテストで比べる）。メモリ（M6 の前の修正 R2）: 開いている間のテキストの容量は 63a24a6 と同じ（読む文字数を `dbcs_unit_length` で数えてその大きさに読み、余れば縮める）、保存（`encode_text`）は 63a24a6 と同じく大きさを問い合わせてから書く。判定では、後ろに 2 バイト目の続かない先頭バイトがあれば CP 932 で読まない（CP 932 はそれを読まないことをテストで全部確かめる）。書き戻しの比較は今までどおりすべてのコードページで行う（ISO-2022-JP で U+FFFD が 1 つも無いときは、最初に違うバイトを全体の書き戻しで決めるので区切りごとの書き戻しを省き、U+FFFD があれば区切りごとに書き戻して読み直す）。テストは `tests/test_encoding_stretches.cpp`（63a24a6 の読み方の写しと、区切りの大きさを 1 バイトから既定まで変えて比べる。`set_code_page_stretch_for_testing`）
- 後ろ向きの正規表現検索（`src/core/search.cpp`）: 63a24a6 は行の先頭から一致を全部たどって最後に条件を満たすものを取るので、40 万文字の 1 行の末尾から `.*` で約 140 秒かかった（一致ごとに行末まで読む）。今は、行の最初の一致を確かめた後、`max_start` から後ろへ 256 文字（倍々に広げる）の窓ごとに、窓の始まりから前向きに一致をたどる。一致の試みは始めた位置に依らない（行全体を照合の対象にするので後読みや `^` も同じ）ので、窓の中の一致は行の先頭からたどったものと同じになる。始めた位置で結果が変わる `\G` / `\K` / `(*...)` を含む正規表現は今までどおり行の先頭からたどる。PCRE2 が途中で止まる（照合の上限や JIT のスタック）ときは、新旧で呼ぶ位置が違うので `kError` になるかどうかが違うことがある（比べていない）。テストは `tests/test_search_backward.cpp`
- 長い 1 行の `<end>`（`src/core/layout.cpp`）: 測ると、`<end>` の時間はほぼ全部が行の先頭から行末までの 1 回の折り返しの計算（10MB で 7.4M 単位）で、そのうち書記素クラスタを作る `for_each_cluster` が約半分（HEAD で 60〜87ms と 113〜195ms）、クラスタごとの呼び出しと升目の計算が残り。`<end>` の後に描く 1 画面のコアの部分は 2ms 未満だった。今は、1 単位で 1 クラスタになり次の単位もそうである文字（印字できる ASCII と、書記素クラスタの性質も絵文字の性質も無い BMP の文字。`lone_cluster_width`、Unicode 18.0.0 で 49699 字）が続くところを、呼ぶ側がその位置を素通りするだけのとき（行・桁・区切りの記録は同じ規則で付ける）、クラスタを作らずに 1 単位ずつ升目に置く。ほかの文字は今までどおり `for_each_cluster` で数える。区切り・桁は `grapheme.*` の表と同じ（テストが全 BMP で `ClusterBuilder` と一致することを確かめる）。テストは `tests/test_layout_lone.cpp`（その道を使わない `use_general_walk_for_testing` の配置と、行・桁・升目・区切りの記録・位置を、折り返し幅と TAB 幅を変えて編集の前後で比べる）

測った値（2026-11-15、開発 PC、ほかのセッションの CPU 負荷あり。HEAD 63a24a6 と交互に測った）:

| 項目 | HEAD 63a24a6 | 変更後 | 測り方 |
|---|---|---|---|
| 100MB の Shift_JIS（判定） | 1497〜1523ms | 937〜941ms | `geditor_perf decode`（3 回の中央値、優先度 High、CPU 負荷 7〜29%。修正 R1 / R2 の後） |
| 100MB の Shift_JIS（指定） | 932〜935ms | 825〜830ms | 同上 |
| 読めないバイトが 1 つある 100MB の Shift_JIS（指定） | 1296〜1326ms | 953〜963ms | 同上 |
| 同じファイルを判定（UTF-8 でも Shift_JIS でもない） | 446〜465ms | 514ms | 同上（約 1 割遅い。速くはしていない） |
| 最後の `ESC ( B` が無い 10MB の 1 行の ISO-2022-JP | 1954〜1970ms | 320〜321ms | 同上 |
| `0x80` を 1 つ含む 20MB の EUC-JP | 534〜535ms | 291ms | 同上 |
| 100MB の Shift_JIS を開いている間のテキストの容量 | 87150407 単位 | 87150407 単位 | `geditor_perf memory`（判定・指定・読めないバイト（HEAD 88992474）・ISO-2022-JP 10MB（6637239）も HEAD 以下） |
| 読込のときの private bytes の増分の最大（判定 / 指定 / 読めないバイト / ISO-2022-JP 10MB） | 522.6 / 266.8 / 285.3 / 75.5MB | 333.2 / 266.8 / 171.8 / 75.5MB | 同上 |
| 保存のときの増分の最大（Shift_JIS / EUC-JP / ISO-2022-JP、100MB 相当） | 433.3 / 433.3 / 990.3MB | 433.3 / 433.3 / 990.3MB | 同上（書いたバイト列の容量も同じ） |
| 40 万文字の 1 行の末尾から `C-u C-r .*` | 139716ms（10 万文字で 4542〜6268ms） | 0.4〜0.9ms | `geditor_perf backsearch` |
| 同じ行で `a*` / `\w+` / `C-r cdef` | 49 / 59 / 17ms | 0.1〜0.8 / 0.1 / 0.1ms | 同上 |
| 10MB の 1 行の `<end>`（アプリ、`window`） | 80〜81ms（4918c04 は 62〜70ms） | 29〜43ms | `measure_input.py --wrap window`（ヘッドレス） |
| 100MB の 1 行の `<end>`（アプリ、`window`） | 773〜840ms | 254〜286ms | `measure_input.py --m4-5 --wrap window`（ヘッドレス） |
| 100MB の 1 行の `<end>`（コア） | 1105ms | 248〜275ms | `geditor_perf endkey` |

## M6: キーボードマクロ・続けた kill・Shift 選択・README と保留の 5 件

PLAN 5 章 M6 と、保留だった利用者向けの 6 件（B1〜B6）。PLAN に無かった細部は Emacs に合わせて決めた（PLAN 5 章の「M6 の実装」）。テストは `tests/test_m6.cpp`（M6-1・M6-2・B2〜B6）と `tests/test_readme.cpp`（M6-3）、実機は `tools/app_check.py --only-m6`、性能は `geditor_perf macro`。

- 続けた kill（PLAN 2 章「kill と クリップボード」）: `kill-line` / `kill-word` / `backward-kill-word` / `kill-region` / `cut` の直後（前置引数のコマンドは間に入ってもよい）の kill は、それまでの kill の文字列に足してクリップボードに書く。前向きの kill は後ろに、`backward-kill-word` と前置引数 0 以下の `kill-line` は前に足す（Emacs の `kill-append`）。続けている文字列はエディターが持ち（`Editor::kill_add`。前に足す分は逆順に、後ろに足す分はそのまま別に持つので、足すたびに足した分だけの手間で済む）、クリップボードを読み戻さない。このため、続けている間に別のプログラムがクリップボードを書き換えても気づかず、つなげた文字列で上書きする。ほかのコマンド（`M-w` / `copy` を含む）とマウスのクリックで続きは切れ、持っていた文字列は捨てる
  - クリップボードに書く時（M6 の修正 R1。以前は kill のたびにつなげた全体を書いていたので、レビューの再現ではマクロで 4 万回続けるとウィンドウが 111 秒止まった。`geditor_perf kill` では修正前 1927ms・修正後 716ms）: キーを 1 つずつ打つ kill（最上位）は今までどおりその場でつなげた全体を書き、書けなければ `Cannot write to the clipboard` と出して消さない。キーボードマクロの実行とコマンド列（設定のキーの `[[...], [...]]`）の中の kill は、文字列をつなげるだけで書くのを待ち、(1) kill でも前置引数でもないコマンド（`yank` / `paste` / `copy` / `quit` などクリップボードを読み書きするものを含む）が動く前、(2) マクロの実行・コマンド列の終わり（`C-g` で止めたときと失敗で止まったときも）に 1 回だけ書く（`Editor::flush_kills`）。このため、入力 1 つの処理が終わってウィンドウが次の入力を待つ時には、つなげた文字列はいつもクリップボードにある（閉じる・ほかのウィンドウへ移る前に書く必要は無い）。待っていた書き込みが失敗したときは、もう消してあるので消さないことはできない。`Cannot write to the clipboard; the killed text is not on it (C-/ brings it back)` と出してマクロを止め、`C-/` で消した文字列を戻せる（マクロの実行は 1 つの undo 単位）
- Shift 選択（PLAN 4.4）: キーの解決でシフト変換（割り当ての無い `S-` 付きのキーを `S-` なしで引き直すこと）が起き、そのコマンドが移動のコマンド（`is_shift_select_command`: `forward-char` / `backward-char` / `next-line` / `previous-line` / `beginning-of-line` / `end-of-line` / `forward-word` / `backward-word` / `beginning-of-buffer` / `end-of-buffer` / `scroll-up-command` / `scroll-down-command`）なら、実行の前にリージョンが無ければ今の位置に一時的なマーク（`Buffer::shift_mark`）を置いて有効にする。有効なリージョンがあればそのまま広げる。シフト変換なしの移動のコマンドは一時的なマークを外す（`C-SPC` で置いたマークは外さない）。キーごとのコマンドは作らず、`Editor::run_call` の 1 か所で扱う。設定で `S-` 付きのキーに割り当てるとシフト変換が起きないので、その割り当てが動く。`C-S-b` のような、Windows 互換キーでない `C-S-<英字>` の移動も同じく選択する（Emacs と同じ）。ミニバッファでも働く。編集やほかのマークのコマンド（`set-mark-command` / `exchange-point-and-mark` / `mark-whole-buffer` / `select-all`）、マウスで一時的なしるしは消える
- キーボードマクロ（PLAN 4.4「マクロ」）: `Editor::handle_key` と `Editor::insert_text`（IME の確定文字列）に来たキーと文字列をそのまま記録し、実行はそれを同じ入口にもう一度流す（コマンドの記録や別の振り分けは作らない）。ミニバッファ・isearch・問い合わせ置換の中のキーも同じ。マウスとメニュー（`execute_menu_item`）・`execute_command` は記録しない。終わらせるキー列（`C-x )`・`<f4>`・`M-x kmacro-end-macro RET` の、ミニバッファの外で打ち始めたところから）は記録から外す
  - `kmacro-start-macro`（`C-x (` / `<f3>`）: `Defining kbd macro...`。記録中は `Already defining keyboard macro`（そのキーは記録しない）。実行中は断る
  - `kmacro-end-macro`（`C-x )`）: `Keyboard macro defined`。記録していなければ `Not defining kbd macro`。何も記録しなかったら `Ignore empty macro` で前のマクロを残す
  - `kmacro-end-and-call-macro`（`C-x e` / `<f4>`）: 記録中は記録を終えるだけで実行しない（PLAN 4.4 の `<f4>` に合わせ、`C-x e` も同じにした。Emacs の `C-x e` は終えてから 1 回実行する）。マクロが無ければ `No kbd macro has been defined`。前置引数の回数だけ実行する（0 以下は `Repeat count must be positive`、100 万回より多いと `Repeat count too large`）。実行はこのコマンドの後、入口（`handle_key` / `execute_command` / `execute_menu_item`）の最後で行い、成功すると `(Type e to repeat macro)` と出して、次のキーが `e` なら同じく実行する。ほかのキー・クリック・ホイール・スクロールバー・メニュー・ファイルのドロップ・IME の文字列で終わる（M6 の修正 R4。`Editor::end_macro_repeat`）。実行の後に isearch・ミニバッファ・問い合わせ置換が開いたままなら、`e` はその入力になり、`(Type e to repeat macro)` も出さない（M6 の修正 R5）
  - 実行中に `C-g` を押すと止まる（M6 の修正 R2）: 実行は約 50ms ごとに `Host::quit_requested` を呼ぶ（イベントごとではない）。Win32 の実装は、待っている最初の `WM_KEYDOWN` を取り出さずに見て（Ctrl / Shift / Alt そのものの押下は取り除いて次を見る）、Ctrl+G（Alt なし）ならそれを取り除いて止める。ほかのメッセージは処理しない（`PeekMessage` の性質で、ほかのスレッドから送られた `WM_COPYDATA` の `GSTA` などはその場で処理される）。ヘッドレスでは修飾をメッセージの `lParam` のビットだけから読み、画面に出すときは `GetAsyncKeyState` で読む。止まると `Quit` と出し、それまでの分は 1 つの undo 単位で残る（`C-/` 1 回で戻る）。`e` の繰り返しは無い
  - 1 回の呼び出し（前置引数の回数分を含む）を 1 つの undo 単位にする（`UndoHistory::merge_groups_since`。undo でカーソルは呼び出した位置に戻る）。直前の入力のまとまりには足さない
  - 失敗（`fail()`・割り当ての無いキー・isearch の失敗・問い合わせ置換の失敗。`Editor::errors_` が増える）と、確認に No / Cancel で答えたこと（M6 の修正 R3。`Editor::declined`: 開き直しの `Cancelled`、別名保存の上書きの `Cancelled`、保存の確認の `Not saved`（`fail`）、開き方の Cancel、保存するかの Cancel（`quit`・別のファイルを開くとき・ログオフ））で実行を止め、残りの回数も実行しない。isearch の途中で止まったら `C-g` と同じく始めた位置に戻し、問い合わせ置換の途中なら終える（それまでの置換は残る）。失敗の表示は残す
  - 記録中の `C-g`（`C-q` の後は除く）は記録をやめ、前のマクロが残る（キーはそのまま `C-g` として働く）。記録中はモードラインの最後に `Def` を出す
  - マクロはプロセスの中だけに持ち、設定ファイルには書かない
  - ヘッドレスの `GSTA` に `macro_recording`（1 か 0）と `macro_events`（最後のマクロのイベントの数）を出す
- README（M6-3）: 使い方・設定項目の表（PLAN 4.5 の全項目）・コマンド一覧・既定のキーの表。`tests/test_readme.cpp` が「## コマンド一覧」の表の 1 列目と登録表、「## 既定のキー」の 5 つの表（通常・Windows 互換・ミニバッファ・isearch・問い合わせ置換）と `keymap.cpp` の既定の表を突き合わせ、足りない・余分・違うものを表示して失敗する（`ESC ESC ESC` は `M-ESC ESC` として比べる）
- 「未実装」のコマンドは無くなった（`tests/test_mx.cpp` の `registry_every_command_is_implemented`）。`<command> is not implemented yet` の表示とメニューの灰色の仕組みは残してある
- B1（Shift を押したまま `C-x C-s`）: `C-S-x`（切り取り）と `C-S-s`（保存）の 2 つになる。Windows 互換キーの 12 個を変えずに直す方法は無い（`C-S-x` を押した時点で切り取りを実行しなければならず、後のキーを待つと切り取りが遅れるか、プレフィックスとして衝突する）ので、README の「Windows 互換キーと Shift」に書いた（2026-11-16 にユーザーが確定）
- B2（ファイルなしの `--encoding`）: 上の起動引数の項
- B3（開き直しで `read-only-mode` が外れる）: 上の文字コードのコマンドの項
- B4 / B5（`!` のバッファの保存）: 上の「読めないバイト」の項。2026-11-16 にユーザーが確定（コードページでも断らずに確かめる）
- B6（長い 1 行のスクロールバー）: 論理行がウィンドウの行数より多ければ今までどおり論理行で近似する。少なければ、先頭から画面の行数だけ折り返して収まるかを調べ（行全体は折り返さない）、収まらなければ文字の位置で近似する（`ScrollBarState::by_offset`）。位置は `unit` 文字ずつ（範囲が `0xFFFF` に収まるように。`WM_VSCROLL` の 16 ビットの位置がそのまま使える）、つまみの位置は先頭の画面の行の最初の文字、ページは画面に見えている文字の数。つまみを動かすと、その位置の文字の画面の行が先頭に来る（その下が画面を埋めなければ最後の画面）。いちばん下（`SB_BOTTOM` を含む）は最後の画面。行の位置は `WrapLayout` の記録した区切りから求め、Win32 側は折り返しを計算しない。ヘッドレスの `GSTA` に `scroll_bar`（`min,max,page,pos`）と `scroll_bar_unit`（`line` か `offset`）を出す。キーやマウスなどの入力の後のスクロールバーの設定（`SetScrollInfo`）は、その入力の画面を描き終えた後に行う（`MainWindow::after_input` が `paint` に頼む）。変わったつまみを描き直す時間が本文の描画を遅らせないため（描画の前に設定した版では、10MB の 1 行の `<end>` の `end_key_ms` が HEAD 84cbaeb の 43〜45ms に対して 56〜57ms になった。後に回すと交互に 10 回ずつ測った中央値がどちらも約 51ms）。行の多いファイルでもつまみの位置と大きさは今までと同じで、変わるのは設定する時刻だけ
- 測った値（2026-11-16、開発 PC、ヘッドレス、HEAD 84cbaeb と交互。ほかのセッションの負荷で全体の CPU 負荷は 28〜98%）: `measure_startup.py` の中央値は負荷 28〜36% の 2 組で HEAD 67.7 / 70.9ms・M6 66.8 / 67.2ms（負荷 47〜85% の組は両方 83〜112ms）。`measure_input.py` を 10 回ずつ: 100MB の打鍵の中央値（各回の中央値の中央値）HEAD 1.64ms・M6 1.63ms、10MB の 1 行の `open_ms` HEAD 38.9ms・M6 34.5ms、`end_key_ms` どちらも 50.9ms。`--m4-5 --wrap window` を 2 回ずつ: 100MB の 1 行の `open_ms` HEAD 482 / 272ms・M6 347 / 249ms、`end_key_ms` HEAD 357 / 317ms・M6 391 / 247ms、交互の改行のファイルの `open_ms` HEAD 836 / 446ms・M6 405 / 351ms。`geditor_perf macro` を 3 回実行し、組ごとの比の中央値は 0.968 / 1.002 / 1.007 倍（基準 1.25 倍以下。3 回とも終了コード 0）。マクロ 100 回の時間の中央値は 20.8 / 19.3 / 19.3 秒、同じキーを直接打つ時間の中央値は 23.6 / 19.3 / 19.1 秒。どちらも 10 万文字まで伸びる 1 行での 1 打鍵あたりの処理（10 万文字で約 0.2ms。内訳は測っていない）が占める。この長い行の 1 打鍵あたりのコアの処理は保留の課題で、M6 では速くしていない（検証の最初の基準「1 秒以内」は、この処理を測らずに決めた誤りとして、2026-11-16 に直接打つ時間との比に改めた）。参考の、1000 文字ごとに `RET` を入れたマクロは 2.44〜2.46 秒（直接打つと 2.42〜2.46 秒）

## 構成

- `src/core/`: 編集の核（静的ライブラリ `geditor_core`）。書記素クラスタの区切りと幅（`grapheme.*`）・piece tree・undo・文字コード・キー表記・キーマップ・コマンド・ミニバッファ・升目計算・JSONC パーサー（`jsonc.*`。リポジトリで唯一の JSON パーサー）・設定の検査とキーマップの層（`config.*`）を持つ。折り返し（論理行を画面の行に分ける計算。長い行は 64K 文字ごとの区切りを記録して再利用する）は `layout.*` の `WrapLayout` にあり、描画・上下移動・スクロール・スクロールバーのすべてがこれを使う（描画側で折り返し位置を計算しない）。行番号の幅・空白記号・改行記号・text-scale の大きさ・View の切り替えは `display.*`。メニューの中身（項目・コマンドと引数・区切り線・サブメニュー・チェックとラジオ・有効無効・キー表示）は `menu.*`、ホイールの量の累積は `mouse.*`。検索の照合（PCRE2。isearch・強調表示・問い合わせ置換が共用）は `search.*`、isearch と問い合わせ置換の状態と動きは `editor.*`。ファイル・クリップボード・ダイアログ・ウィンドウ起動などの環境依存の処理は `src/core/host.h` の `Host`（抽象クラス）を通して呼ぶ。Win32 側（`src/win/`）がこれを実装し、テストは `tests/fake_host.*` の偽物を使う
- `geditor_core` には `user32` などの UI 系ライブラリをリンクしない
- `src/win/`: Win32 層。`window.*`（ウィンドウ・メッセージ・`Host` の実装）、`input.*`（キー変換）、`renderer.*` と `text_measurer.*`（Direct2D / DirectWrite の升目描画と代替フォント）、`ime.*`、`clipboard.*`、`file_io.*`、`dialogs.*`（TaskDialog）、`process.*`（新しいウィンドウ）、`command_line.*`、`measure.*`（起動・打鍵の計測）、`config_watcher.*`（設定ファイルの監視スレッド）、`menu_bar.*`（コアのメニューから `HMENU` を組み立て、`WM_INITMENUPOPUP` で更新し、読み戻す）、`question.h`（確認の文言とボタン。TaskDialog とヘッドレスが共用。ヘッドレスのメニューもここで確認の形にする）、`headless.h`（ヘッドレスの `WM_COPYDATA` と状態の書式）。キー変換・起動引数・クリップボードの改行変換・ファイル種別の判定・`HMENU` の組み立てなどウィンドウなしで試せる部分は静的ライブラリ `geditor_win_logic` にまとめ、テストからも使う。`text_measurer.*` は静的ライブラリ `geditor_text`（`dwrite` をリンク）にして、どのフォントで文字を描くかをテストからも確かめる
- 高 DPI は Per-Monitor V2（`geditor.manifest` と `SetProcessDpiAwarenessContext`）。`WM_DPICHANGED` でフォントと升目を作り直し、勧められた大きさに変え、先頭の行を保つ。ウィンドウの右端に Win32 の縦スクロールバーを初回描画の後から常に出す（初回の描画を起動時間に含めないため。位置は論理行で近似。論理行がウィンドウの行数以下で画面の行が収まらないときは文字の位置で近似する。M6 の B6、下の「M6」）
- `comctl32`（TaskDialog）と `shell32`（ドラッグ＆ドロップ）は遅延読み込みにして起動時間に含めない。TaskDialog のために `src/win/geditor.manifest` で Common Controls v6 を指定する
- `tests/`: `geditor_tests`（ctest に登録）と `tests/perf/` の `geditor_perf`（登録しない）。`test_win_font.cpp` は本物の DirectWrite で、英字が Consolas、日本語と絵文字が代替フォントで描かれることを確かめ、選ばれたフォント名を表示する。😀 と U+2639 U+FE0F には色の層があり、`A` と `漢` には無いこと、変換の失敗で単色になることも確かめる。`test_win_save.cpp` は本物のファイルで安全な保存（各段階の失敗・`file~`・ハードリンク・シンボリックリンク・240 文字とボリュームの最大長のファイル名）を確かめ、ファイルはテストの exe の隣の `m4a_save\`（`build/` の下）にだけ作る。シンボリックリンクを作れない環境（開発者モードでも特権も無い）ではその 1 件だけ `SKIP` と理由を表示して先へ進む。`test_readme.cpp` はソースの `README.md`（`GEDITOR_SOURCE_DIR`）を読み、コマンド一覧と既定のキーの表がコマンドの登録表と `keymap.cpp` の既定の表に一致することを確かめる（M6-3。README のコマンドやキーを変えたら表も直す）
- `.clangd` は `build/release/compile_commands.json` を読む（`cmake --preset release` で作られる）

## 文字幅表と書記素クラスタ表の生成

次の 3 つは `tools/gen_char_width.py` が Unicode Character Database から作る生成物で、手で直さない。

- `src/core/char_width_table.inc`: 文字幅表（各文字の 0 / 1 / 2 桁）
- `src/core/grapheme_table.inc`: 書記素クラスタの区切りと桁の数え方に使う性質（Grapheme_Cluster_Break・Extended_Pictographic・Indic_Conjunct_Break・Emoji_Presentation・Emoji_Modifier・Emoji）と、文字幅表と同じ桁（レイアウトが 1 回の表引きで性質と桁を得るため。テストが全コードポイントで文字幅表と一致することを確かめる）
- `tests/grapheme_break_test.inc`: `GraphemeBreakTest.txt` の全例（テストが全例を確かめる）

Unicode を更新するときは、新しい空のフォルダーに次の 6 ファイルを置いて実行する（`<version>` は unicode.org の最新版。6 ファイルの版が揃っていないと止まる。生成物の先頭に版が記録される）:

- `https://www.unicode.org/Public/<version>/ucd/EastAsianWidth.txt`
- `https://www.unicode.org/Public/<version>/ucd/DerivedCoreProperties.txt`
- `https://www.unicode.org/Public/<version>/ucd/extracted/DerivedGeneralCategory.txt`
- `https://www.unicode.org/Public/<version>/ucd/auxiliary/GraphemeBreakProperty.txt`
- `https://www.unicode.org/Public/<version>/ucd/auxiliary/GraphemeBreakTest.txt`
- `https://www.unicode.org/Public/<version>/ucd/emoji/emoji-data.txt`

```powershell
python -I tools/gen_char_width.py <folder>
```

同じ入力からは同じバイト列が出る。幅の規則（0 / 1 / 2 桁）はスクリプト冒頭の説明にある。区切りの規則（UAX #29 の GB1〜GB999）が版で変わったときは `src/core/grapheme.cpp` の `GraphemeBreaker::next` も直す（18.0.0 では GB9c が「Linker の後の Consonant」になった）。

## 書記素クラスタ

カーソル・削除・単語移動の端・マウスの位置・折り返し・選択範囲の端・桁は、拡張書記素クラスタ（UAX #29）を単位にする（`docs/PLAN.md` 2 章のカーソル単位・桁の数え方）。

- 区切りと幅は `src/core/grapheme.*` だけが決める。レイアウト・コマンド・マウス・表示の行・エコー領域・描画はこれを使い、描画側で区切りを数えない
- クラスタの幅: 絵文字として表示するもの（Emoji の性質を持つ文字の後に U+FE0F（`g` ＋ U+FE0F は文字のまま）、肌の色、絵文字どうしを ZWJ でつないだもの、国旗の 2 文字、先頭が Emoji_Presentation の文字。地域指示子 1 文字は除く）は 2 桁。それ以外は先頭の文字の幅（幅 0 の文字だけのクラスタは 0）
- 外から来た位置（`set_point`・マウス・`+LINE`）と、編集でクラスタがつながったときの point / mark は、その位置以前のクラスタの境目に寄せる
- 長い行は 64K 単位ごとの記録（区切りの状態も持つ）から数え直すので、区切りの計算は描く行と動く位置の近くだけで行う
- 複数の文字からなるクラスタと、1 文字でも絵文字として表示するクラスタ（😀 など）は、`MapCharacters` が選んだ 1 つのフォントで 1 つの文字列として形を作り（絵文字として表示するクラスタや、選ばれたフォントにない文字があるときは、主フォントを指定しない `MapCharacters` の結果を使う）、升目より広ければ横に縮める。どのフォントにも無いときや形を作れないときは、先頭の文字のグリフ（無ければ欠字の箱）を描く

## カラー絵文字（M3d）

- 色で描くのは、絵文字として表示するクラスタ（2 章の桁の数え方で絵文字として 2 桁に数えるもの。コアの `Cluster::emoji`）のグリフで、そのフォントが色付きの表現を持つもの（`IDWriteFontFace2::IsColorFont` のフォントで、`TranslateColorGlyphRun` が色の層を返すもの）だけ。☹ ❤ ✔ ☀ のように文字表示が既定の記号と、U+FE0E を付けたもの（😀＋U+FE0E も）は、フォントに色付きの絵があっても 1 色で描く（2026-11-14 にユーザーが確定）。それ以外は今までどおり設定の `foreground` などの 1 色。色の設定項目は作らない（いつも有効）
- 描き方は描画先（render target）に問い合わせて決める: COLR の層は `DrawGlyphRun` を層の色で重ねる（どの描画先でも描ける）。`ID2D1DeviceContext7` があれば COLR のペイントツリー（`DrawPaintGlyphRun`。Segoe UI Emoji の今の絵文字はこれで、`DrawText` の `D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT` と同じ画素になることを実測した）、`ID2D1DeviceContext4` があれば SVG と画像のグリフも描く。開発 PC のソフトウェア描画先（ウィンドウと `GCAP` の DC の両方）は 3 つとも持つ（`GSTA` の `color_support=colr,svg,bitmap,paint`）
- 層は升目に収める規則・選択範囲の背景・カーソルの縦棒の重ね方が他の文字と同じ（背景の後に描き、縦棒はその後）。空白記号と改行記号は変わらない
- インターフェースは最初の色付きグリフで作る（色付きフォントかどうかの問い合わせはフォントごとに 1 回）。層はグリフ（とクラスタ）ごとに 1 回だけ変換して覚えておき、毎フレームは確保しない
- 変換に失敗したとき（`TranslateColorGlyphRun` が無い古い Windows を含む）は、そのフォントを作り直すまで単色で描き、エコー領域に `Colour emoji are not available; drawn in one colour` と 1 回だけ出す
- `IDWriteFactory4` / `IDWriteFactory8` / `ID2D1DeviceContext4` / `ID2D1DeviceContext7` はプロジェクトの `_WIN32_WINNT`（Windows 10）より新しい `NTDDI_VERSION` でしか宣言されないので、`text_measurer.cpp` と `renderer.cpp` だけがファイルの先頭で `NTDDI_VERSION` を上げる（ヘッダーには前方宣言だけを置く）。実行時は `QueryInterface` で確かめるので、無い Windows でも起動する

## コードの約束

- C++20。Win32 API を直接使う。GUI フレームワークや COM の初期化を持ち込まない
- 起動を遅くしない: 静的初期化子を持つグローバルを作らない。重い初期化は初回描画の後に回す。変更後は `app_ms` が悪化していないか計測する
- `src/core/` は Win32 の UI に依存させない（キー列を流してバッファを検査するテストをウィンドウなしで書けるようにする）。UI・描画・入力は `src/win/`
- テストは `tests/` の自作ハーネス（`TEST` / `CHECK` / `CHECK_EQ`）で書き、`tests/CMakeLists.txt` に追加する
- `third_party/` は外部のソースをそのまま置く場所で、手を入れない。PCRE2 のビルド設定は `cmake/pcre2.cmake`
- 警告を出さない（`/W4`）

## 文書と文字

- 説明文書（README・docs・この文書）は日本語、コード・識別子・コメント・図中の文字は英語
- 改行は LF、UTF-8（BOM なし）。`.bat` だけ CRLF
