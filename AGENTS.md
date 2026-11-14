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
- 計測スクリプト（`measure_startup.py` / `measure_input.py`）は既定でヘッドレス（`--headless`、下の「アプリの起動と実機確認」）で geditor を起動し、最初の行に `mode=headless` と出す。描画の経路は画面に出すときと同じ。画面に出して測るときは `--visible` を付ける。`--headless` を知らない版（ヘッドレスより前の commit。たとえば M3a の比較に使う HEAD 449cfe5）は `--visible` でしか測れない。ヘッドレスの geditor は `--measure-startup` / `--measure-input` で標準エラーに `headless=1` と出し、計測スクリプトはこれが無いと値を出さずにエラーで止まる（app_check も状態に `headless=1` が無い版は確認しない）
- 計測スクリプトは結果の行（各値・中央値・判定）のすべてに `mode=headless` か `mode=visible` を付ける。ヘッドレスと画面に出したときの値は比べられない:
  - ヘッドレスの `input_ms` に入るもの: メッセージの受け取り・キーの処理・コアの計算・Direct2D の描画・`EndDraw` の完了。入らないもの: 画面への表示の待ち（モニターの垂直同期や DWM の合成に合わせて待つ時間）。ウィンドウはどのモニターにも映らない位置にある透明でない layered ウィンドウなので、`EndDraw` がこの待ちをしない。このため画面に出したときの約 13ms に対して約 1ms になる（M3a のレビューで測定）
  - ヘッドレスの `startup_ms` に入るもの: プロセスの生成・初期化・ウィンドウの作成・設定の読込・初回描画の `EndDraw`。入らないもの・違うもの: `ShowWindow` による表示と活性化（ヘッドレスは `SetWindowPos` で活性化せずに出す）、画面への合成の待ち
  - 受入条件の基準（M1-5 の 16ms、M0-3 / M3-4 の 100ms など）は画面に出した値で決めたもの。ヘッドレスの値で判定すると黙って甘くなる。基準を判定するとき、または以前の画面に出した値（M3a 以前の報告など）と比べるときは `--visible` で測る（利用者が画面を使っている間は測らない）。基準の値そのものは変えていない

- `startup_ms`: プロセス生成から初回描画の `EndDraw` 完了まで
- `entry_ms`: プロセス生成から `wWinMain` 開始まで。開発 PC では Norton 360 が未知の exe の起動を約 2 秒止めるため、この値が大きくなる
- `app_ms`: `wWinMain` 開始から初回描画まで（プログラム自身の起動コスト）
- `geditor.exe --measure-startup` は段階ごとの時刻（`stage <name>=<ms>`）も標準エラーに出す

コアの性能計測（M1 の 100MB 基準）。生成した 100MB の UTF-8 テキストで、読込（デコード＋構築）・1 文字挿入と行番号検索・自己挿入キーの処理時間・全削除と undo のメモリ増分を測り、基準を超えると終了コード 1 で終わる。ctest には入れていないので、Release ビルドを手で実行する:

```powershell
build/release/tests/geditor_perf.exe
```

キー入力から再描画までの時間（受入条件 M1-5）。100MB（1 行約 80 文字、ASCII と日本語）と 10MB の 1 行だけのファイルを `build/measure_input/` に一度だけ生成し、`geditor.exe --measure-input <file>` を起動して `WM_CHAR` を 1 文字ずつ送る（打鍵と同じ経路）。アプリはメッセージを受け取ってからその文字を描いた `EndDraw` の完了までを `input_ms=` として標準エラーに出す。中央値・95 パーセンタイル・ファイルを開く時間（`open_ms`）を表示し、100MB の中央値が 16ms を超えると終了コード 1 で終わる。1 行のファイルはカーソルを行末（`<end>`）に置いて測り、基準は持たない:

```powershell
python tools/measure_input.py build/release/geditor.exe            # --samples prints every sample
python tools/measure_input.py build/release/geditor.exe --wrap window
```

- 設定ファイルは既定で `{}`（行番号あり・折り返し `max`）。`--wrap none|window|fixed|max` を付けると、その折り返しモードを書いた設定ファイルで測る
- 1 行のファイルの `end_key_ms` は、先頭から行末までのスクロール（`<end>` 1 回）にかかった時間。`--wrap window` で M4-5 の参考値になる

- `measure_input.py` は `build/measure_input/settings.json`（中身は `{}`）を `--settings` で渡す。利用者の `%USERPROFILE%\.geditor.json` は読まない。`--measure-input` では設定ファイルの監視は始めない
- `measure_input.py` もヘッドレスが既定（`--visible` で画面に出す）。文字は今も `PostMessage` の `WM_CHAR` で送る

## アプリの起動と実機確認

```powershell
build/release/geditor.exe [--settings=PATH] [+LINE] [file]
```

- 存在しないファイル名なら新規として開く。`+LINE` はその行へ移る。未知のオプションはエコー領域に表示する
- 設定ファイルは既定で `%USERPROFILE%\.geditor.json`。`--settings=PATH` で場所を変える（相対パスは今のフォルダーから。指定したときは `%USERPROFILE%` のファイルを読みも監視もしない。「新しいウィンドウで開く」にも同じ指定を渡す）
- テスト・計測・実機確認では必ず `--settings` で `build/` の下のファイルを指定し、利用者の `%USERPROFILE%\.geditor.json` を読み書き・作成・削除しない（今は存在しない）
- 設定ファイルは起動時に同期で読み、初回描画の後に別スレッドで監視する。保存すると約 200ms 後に読み直す。構文の誤りはファイル全体を採用せず、意味の誤りはその項目だけ無視する。誤りは行・桁付きでエコー領域に出し、2 件以上ならエコー領域を最大 10 行まで広げる（ウィンドウが低いときはモードラインと本文 1 行が残る行数まで。収まらない分は最後の行の `... and N more` に数える。次のキーで 1 行に戻る）。`M-x find-user-init-file` で設定ファイルを開く（無ければ書き方の例を入れた未保存の新規バッファになる）
- M3a までに反映する項目: キー設定（`keys` / `minibuffer_keys` / `isearch_keys` / `query_replace_keys`）と表示の項目（`font` / `colors` / `tab_width` / `line_numbers` / `wrap` / `whitespace` / `eol_marks`）。`colors` の `search_match` / `search_current` は検索（M5）まで、`eol_other` は改行コードの混在（M4）まで使わない。M3b で `menu_bar` も反映する（読み直すとその場でメニューバーを出し入れし、先頭の行は変えない）。ほかの項目は型と値を検査して保持し、後の段階で使う
- 入っていないフォント名（`font.family` / `font.fallback`）はエコー領域に `Font not found: <name>` と出し、Consolas（とシステムの代替フォント）を使う。出すのは `font.family` / `font.fallback` の値が変わったときだけで、text-scale・DPI の変更・`font.size` だけの変更でフォントを作り直しても繰り返さない。`font.fallback` を書かなければシステムの代替フォントだけを使う
- F10（`menu-bar-open`）は File メニューを下に開いた状態でメニューに入る。F10 を押している間は待ち、離したときに開く（離す操作でメニューから抜けないため）。左右キーはメニューバーの端で折り返し、システムメニューへは移らない（Alt+Space では開く）
- View のコマンド（`toggle-line-numbers` / `toggle-eol-marks` / `toggle-whitespace-marks` / `set-wrap-mode`）と text-scale（`C-x C-+` / `C-x C--` / `C-x C-0`、1 段 1.2 倍、6〜200 ピクセル）はそのウィンドウだけに効き、設定ファイルには書かない。設定を読み直しても、その項目のファイルの値が変わるまでは切り替えた値を保つ
- メニュー（M3b、PLAN 4.9）: 中身はコアの `src/core/menu.*` にデータとして持ち、`src/win/menu_bar.*` がそれだけから `HMENU` を組み立てる。メニューバーは初回描画から出す。各項目の右には、今のキーマップでその項目のコマンド（と引数）にちょうど割り当たっているキー列のうち最も短いものを 1 つ出す（同じ長さなら設定ファイル > Windows 互換キー > Emacs の順で先に見つかったもの。例: Undo は `C-M-z`、Save は `C-M-s`。`C-c &` のようにキーに `&` があれば、メニューの文字列では `&&` と書いてアクセスキーにしない）。表示・灰色・チェックとラジオは `WM_INITMENUPOPUP` でそのつどコアに問い合わせる。灰色になるのは未実装のコマンドの項目と、ミニバッファの途中の全項目だけ（Cut / Copy はリージョンが無くても選べ、コマンドが理由を表示する）。File メニューのチェックとラジオは M4-9 まで付かない。F10（`menu-bar-open`）とマウスでメニューに入る。Alt+英字はキーマップのまま（`M-f` は `forward-word`）、Alt 単独ではメニューに入らない。アクセスキー（`&Undo` など）はメニューに入ってからだけ効く。`menu_bar: false` ではメニューバーが消え、F10 は `Menu bar is hidden` と出す
- 右クリックの簡易メニュー（Undo / Redo / Cut / Copy / Paste / Select All）は Edit メニューの先頭の項目そのもの（同じ定義）から作る。マウスではその位置に、Apps キーと Shift+F10（`S-<f10>` に割り当てが無いとき）ではカーソルの位置に出す。右クリックではカーソルと選択範囲を動かさない。`menu_bar: false` でも出る
- マウス（M3b）: 左クリックはクリックした升目を含む文字の前にカーソルを置く（行末より右と予約の最後の升目はその画面行の終わり。折り返した行の途中の画面行では最後の文字の前、行番号の欄はその画面行の先頭、最後の行より下はバッファの末尾。ウィンドウの高さが行の高さの倍数でないときに最後の画面行とモードラインの間にできる余白は、最後の画面行として扱い、スクロールしない）。押す・ドラッグするとマークは外れ、ドラッグで押した位置からのリージョンになる（本文の上下の外へドラッグすると、押している間 50ms ごとに 1 行ずつスクロールする）。ダブルクリックは `forward-word` / `backward-word` と同じ定義の単語を選ぶ（単語の文字でなければその 1 文字）。Shift+クリックは有効なマークから、無ければ元のカーソル位置からリージョンを広げる。ホイールは `SPI_GETWHEELSCROLLLINES` 行ずつ（ページ指定ならページ単位）で、1 刻みに満たない量はためておき、カーソルは画面から外れるときだけ動く。クリックは入力中の undo のまとまりを区切る。モードライン・エコー領域のクリックと、ミニバッファの途中の本文のクリックは何もしない。位置は升目から `Editor::offset_at_screen`（`WrapLayout`）で求め、描画側もマウス側も折り返しを自前で計算しない。選択した文字列のドラッグによる移動・複写は作らない
- Norton 360 は `build/` の外に置いた新しい exe を約 2 秒止めるので、計測や確認は `build/` の exe で行う

ヘッドレス（`--headless`）はテスト・計測・実機確認のための起動引数で、利用者向けには案内しない（付けても未知のオプションの表示は出ない）。確認の最中に利用者の画面・打鍵・クリップボードを奪わないためのもので、app_check と計測スクリプトはこれを既定にする:

- ウィンドウは仮想画面の右の外に置く（どのモニターにも映らない）。ツールウィンドウなのでタスクバーと Alt+Tab に出ず、`WS_EX_NOACTIVATE` で前面にもならない。画面の外の普通のウィンドウには `WM_PAINT` が来ず初回描画が終わらなかった（Windows 11 で実測）ので、不透明のレイヤードウィンドウにしてある。描画は通常と同じ経路（`EndDraw` まで）で、`--measure-startup` / `--measure-input` / `GCAP` の画像はそのまま使える
- キーは `PostMessage` で受ける（`app_check.py --input post` と同じ経路）。geditor 自身も前面を取らない（ドロップ時の `SetForegroundWindow` もしない）
- クリップボードは本物を開かず、プロセス内の専用の置き場を使う（中身は本物と同じ CRLF）。読み書きの失敗は `GFLT` の `clipboard-read` / `clipboard-write` で 1 回だけ起こせる
- 確認（開き方・保存・はい/いいえ・ログオフ時の保存）はダイアログを出さずに待つ。待っている間はキー・マウス・ドロップを受け付けず、`WM_CLOSE` とログオフの問い合わせも答えが出るまで断る（`WM_COPYDATA`・描画・画像の保存は続く）。答えが来るまで待ち続けるので、ツールの側で時間切れを決める。起動時の Direct2D 初期化の失敗はメッセージボックスの代わりに標準エラーへ `headless_error=init ...` と出して終了コード 1 で終わる（`GEDITOR_HEADLESS_FAULT=init` を環境変数に入れるとヘッドレスのときだけわざと失敗させられる）
- 確認の文言とボタン名は `src/win/question.h` の 1 か所で作り、TaskDialog とヘッドレスの両方が使う
- 「新しいウィンドウで開く」にも `--headless` を渡す（`--settings` と同じ）
- メニューは画面に出さない（M3b）。F10（`menu-bar-open`）と `WM_CONTEXTMENU` は、メニューを開く代わりに確認と同じ形で待つ（`GSTA` の `question=menu_bar` / `context_menu`、組み立てた `HMENU` から読み戻した全項目の `menu_item=<経路>\t<キー>\t<状態>`、位置の `menu_x` / `menu_y`）。`GANS` で項目の経路（`Edit > Undo`、右クリックでは `Undo`）か `Cancel` を答えると、本物のメニューと同じ `WM_COMMAND` が送られる。無い項目と灰色の項目は断り、メニューは待ったまま。`SC_KEYMENU` / `SC_MOUSEMENU`（Alt+Space のシステムメニューを含む）は握りつぶして、回数と最後の値を `GSTA` の `keymenu_count` / `last_keymenu` に出す。メニューループに入った回数は `menu_loops`
- マウスは `PostMessage` の `WM_LBUTTONDOWN` / `WM_MOUSEMOVE` / `WM_LBUTTONUP` / `WM_LBUTTONDBLCLK` / `WM_MOUSEWHEEL` / `WM_RBUTTONUP`（クライアント座標と `MK_*`）で受ける。どの升目がどこにあるかは `GSTA` の `line_height` / `cell_width` / `text_left` / `mode_line_top` / `text_rows`（物理ピクセル）から求める（`App.cell_point`）
- 設定ファイルを書き換える確認（app_check の `apply_settings` / `write_settings`）は、決まった時間だけ待つのではなく、その設定ファイルで起動した geditor の `GSTA` の `settings_reloads`（監視スレッドからの読み直しを反映した回数）が増え、`reloads_drawn`（描き終えた画面に反映済みの読み直しの回数）が追いつくまで待ち、さらに 0.3 秒その回数が変わらないことを確かめる（1 回の保存で 2 回読み直すことがあるため）。`frames` は描き終えた画面の数
- ツールとのやり取りは `WM_COPYDATA`（`src/win/headless.h` に一覧）。`GSTA`（状態をファイルに書く: ヘッドレスか・設定ファイル・タイトル・エコー領域のメッセージ・変更の有無・文字数・クリップボードの中身・カーソルとマークと先頭の行・本文（64K 文字まで）・升目の位置・メニューバーの有無・断った要求の数と理由・待っている確認やメニューの種類・文言・ボタン名）、`GCBW`（クリップボードに入れる）、`GANS`（ボタン名を指定して答える。無いボタン名は断り、確認は待ったまま）。`GFLT` の `menu` / `popup` で次のメニューの作成と右クリックメニューの表示を 1 回だけ失敗させられる（エコー領域に理由が出る）。分からない要求は 0 を返して数え、`OutputDebugString` にも出す

実機確認（受入条件 M1 の画面・キー・ダイアログ・ファイル・クリップボード）は `tools/app_check.py` で行う。テスト用ファイルを `build/app_check/<日時>/files/` に作って geditor を起動し、キーを送り、画面を `shots/*.png` に保存し、ファイルのバイト列・クリップボード・タイトルを確かめて、項目ごとに PASS / FAIL / SKIP と証拠を表示する。画像は目でも確かめる:

```powershell
python tools/app_check.py build/release/geditor.exe            # headless; --strict: SKIP also fails
python tools/app_check.py build/release/geditor.exe --only-retry1
python tools/app_check.py build/release/geditor.exe --only-m2
python tools/app_check.py build/release/geditor.exe --only-m3a
python tools/app_check.py build/release/geditor.exe --only-m3b
python tools/app_check.py build/release/geditor.exe --only-m3c
python tools/app_check.py build/release/geditor.exe --visible --only-m3c   # F10: needs the unlocked desktop
python tools/app_check.py build/release/geditor.exe --only-headless
python tools/app_check.py build/release/geditor.exe --visible --only-visible
python tools/app_check.py build/release/geditor.exe --visible --only-m3b
```

- 既定はヘッドレスで、最初の行に `mode headless` と出る。`--visible` を付けると従来どおり画面に出し、`SendInput` と前面（画面がロックされていなければ）・本物のクリップボード・本物の TaskDialog を使う（`mode visible`）
- ヘッドレスの実行中は、別プロセスの見張りが 20ms ごとに前面のウィンドウと見えている `#32770`（ダイアログ）と `#32768`（メニューとポップアップメニュー）のウィンドウを調べ、geditor（同じ exe）のものが 1 度でもあれば FAIL にする。前面の見張りの間隔が 1 度でも 50ms を超えたときも FAIL にする。同じ見張りは 20ms ごとに、ヘッドレスの geditor のウィンドウ（`geditor.main` のツールウィンドウ）がどのモニターとも重ならず、最小化もされていないことを調べ、外れたら FAIL にする（最小化したツールウィンドウは主モニターにタイトルバーだけ置かれるため、ヘッドレスでは本当には最小化しない。`[S2]` は `WM_SIZE` の `SIZE_MINIMIZED` と元の大きさの `SIZE_RESTORED` を送って確かめる）。新しいウィンドウを探すときは、この実行が起動した geditor とその子プロセスのウィンドウだけを見る（ほかの geditor が開いていても数えない）。本物のクリップボードは開かず、`GetClipboardSequenceNumber` が前後で同じことを確かめる。利用者が実行中にコピーすることがあるので、同じ見張りが番号の変化ごとに `GetClipboardOwner` の持ち主のプロセスと、そのとき開いていた geditor を記録し、持ち主が geditor のとき（または持ち主が分からず、この実行の geditor が開いていたとき）だけ FAIL にする。ほかのプログラムが書いたときは持ち主のプログラム名を INFO に出す（判定の部分は作り物の持ち主で `[R3]` が確かめる）。`app_driver.py` はヘッドレスのとき `SendInput`・`keybd_event`・`SetForegroundWindow`・`BringWindowToTop`・`SetActiveWindow`・`SetFocus`・クリップボードの関数・`MoveWindow`・`SW_HIDE` / `SW_SHOWNOACTIVATE` / `SW_SHOWNA` 以外の `ShowWindow`（最小化も含む）・移動する `SetWindowPos` を呼ぶと例外にする
- `--only-headless` はヘッドレスそのものの確認（ウィンドウの位置と種類・打鍵後の画像・見張り・ツール側の禁止・クリップボードの置き場と失敗・確認の種類とボタンごとの答え・待っている間のキー・新しいウィンドウ・おかしな要求・起動の失敗・計測スクリプト）だけを行う。全体の実行にも含まれる
- 画面が要る確認はヘッドレスでは SKIP（`needs --visible`）になり、`--strict` でも失敗に数えない。今は次の 9 つ（`app_check.py` の `needs_visible` の呼び出し）で、前の 5 つは `--visible --only-visible`、後の 4 つのうち上の 3 つは `--visible --only-m3b` で確かめる（前面を取りダイアログやメニューを出すので、利用者が画面を使っていないときに行う。画面がロックされていると SKIP になる）:
  - `[visible] keys through SendInput to the foreground window`（`SendInput` と前面）
  - `[visible] task dialogs: their look and their buttons`（TaskDialog の見た目と `TDM_CLICK_BUTTON`）
  - `[visible] the real clipboard: C-k / C-y through CF_UNICODETEXT`（本物のクリップボード）
  - `[visible] clipboard held open by another program`（他のプログラムがクリップボードを開いているとき）
  - `M-<f4> closes the window`（割り当てのない M-<f4> はシステムに渡り、システムの Alt+F4 はアクティブなウィンドウを閉じる。ヘッドレスのウィンドウはアクティブにならないので閉じない）
  - `[C6] F10 and a mouse click open the real menu loop (look, mnemonics, Esc)`（本物のメニューループ。マウスでメニューバーを開くのは人が確かめる）
  - `[C6] Alt+Space opens the system menu`（システムメニュー）
  - `[C4] the real right-click popup (look, position at the mouse)`（`TrackPopupMenu` の見た目と位置）
  - `[C5] the mouse cursor is an I-beam over text and an arrow elsewhere`（`WM_SETCURSOR` は本物のマウス位置を読むので、人が画面で確かめる）
- 日本語 IME の確認（下記）も画面が要る
- 最後の行に PASS / FAIL / SKIP の件数（そのうち `--visible` が要るもの）が出る。`--strict` を付けると `needs --visible` 以外の SKIP が 1 件でもあれば終了コード 1。`--only-retry1` は長いパス・最小化と復元・結合文字と不可視文字・ログオフ時の保存確認・クリップボード読込失敗の確認だけを行う
- 起動する geditor にはすべて `--settings` で `build/app_check/<日時>/` の下のファイルを渡す
- `--only-m2` は設定ファイルの確認（M2）だけを行う。geditor の起動中に設定ファイルを書き換えて、キーの割り当てが効くまでの時間を測り（1 秒以内）、構文の誤りで直前の設定が残ること、誤りが 4 件のときにエコー領域が 4 行に広がり次のキーで 1 行に戻ること、ファイルを消すと既定に戻ること、他のプログラムが共有なしで開いている間は読めない理由を表示して直前の設定を保つこと、`find-user-init-file` が例の入った未保存のバッファを開き、保存した例が誤りなしで読めることを確かめる。最後に、無いフォルダーの設定ファイルを指定して監視を始められないことの表示を確かめる
- `--only-m3a` は表示の確認（M3a）だけを行う。起動中に設定ファイルの表示項目を 1 つずつ書き換えて画面の画素を調べる（原色の配色で色を見分ける）。View のコマンドを M-x から実行し、text-scale のキー、`WM_VSCROLL`、本物の `WM_DPICHANGED`（lParam の RECT は Windows が geditor 側へ写す）を送る。フォントと描画先（render target）の作成をわざと失敗させるときは `WM_COPYDATA`（`dwData` = `'GFLT'`、本文 `font` か `target`）を送る。最後に `[C4 table]` の行で「設定項目 | 画像 | 見えるもの」の表を出す
- `--only-m3b` はメニューとマウスの確認（M3b）だけを行う。クリック・ドラッグ（下へはみ出したときの自動スクロールと離したときの停止）・ダブルクリック・Shift+クリック・ホイール（1 刻みに満たない量の累積）・undo の区切り・何もしないクリックを `PostMessage` のマウスのメッセージで確かめ、`shots/m3b_*.png` に撮る。本物のメニューバーを `GetMenu` と `GetMenuItemInfoW` で（`WM_INITMENUPOPUP` を送ってから）読んでコアの表と比べ、`WM_COMMAND` で項目を実行し、F10 と右クリックで待っているメニューに答える。`menu_bar: false` の出し入れ、Alt+F と Alt 単独、メニューと右クリックメニューの作成失敗の表示も確かめる。`--visible --only-m3b` は本物のメニューループ・システムメニュー・右クリックメニューを `PrintWindow` で撮る
- `--only-m3c` は書記素クラスタと F10 の確認（M3c）だけを行う。U+FE0F 付きの絵文字・ZWJ でつないだ家族・国旗・キーキャップ・か＋゛ が ASCII の隣で 2 升に描かれ、カーソルがその後ろに来ること（画素で調べる）、DEL 1 回で消えて undo 1 回で戻ることを確かめる。形を作るのをわざと失敗させるときは `WM_COPYDATA`（`dwData` = `'GFLT'`、本文 `shape`）を送る。M-x の途中で ESC を 3 回送るとミニバッファが `Quit` で閉じること（ESC 2 回では待つこと）、F10 は保留中の `menu_bar` の質問と、開くサブメニュー（状態の `menu_open=File`）を確かめる
- `--visible --only-m3c` は本物の F10 を `SendInput` で押して離し、File メニューが下に開いて離した後も残ること、右キーで Edit、左キーで File と View（端で折り返す）へ移ること、Esc で閉じることを状態の `menu_popup` / `menu_loop_active` と `#32768` の窓で確かめる。前面を取るので、利用者がデスクトップを使っていないときに実行する
- `tools/` のスクリプトは `__pycache__` を作業ツリーに残さない（`sys.dont_write_bytecode` と、`app_driver.py` が読み込み時に自分の `.pyc` を消す処理による）
- ヘッドレスではいつも `PostMessage` で `WM_KEYDOWN` / `WM_CHAR` を送り、修飾キーの状態は `AttachThreadInput` と `SetKeyboardState` で与える。`--visible` では、画面がロックされていなければ `SendInput` で打鍵する（geditor を前面に出してから送り、前面でなければ送らずに止まる）。ロック中か `--input post` なら `PostMessage` で送る（`--input post` では geditor を `SW_SHOWNOACTIVATE` で起動し、前面を取らない）。どちらで動いたかは `input mode` の行に出る
- `--visible` で画面がロックされていると `OpenClipboard` がアクセス拒否になるため、クリップボードを使う確認は SKIP になる（geditor 側の書き込み失敗の表示は確かめる）。`--visible` では実行の前後でクリップボードの文字列を保存・復元する（ヘッドレスでは触れない）
- 画像は geditor に `WM_COPYDATA`（`dwData` = `'GCAP'`、BMP の保存先パス）を送って描かせる。ロック中は画面にも `PrintWindow` にも描画が出ないため。ダイアログ（`--visible` のみ）は `PrintWindow` で撮る
- `tools/app_driver.py` が起動・打鍵・撮影の部品。ライブラリとして読み込んだだけでもヘッドレスが既定（`HEADLESS = True`）で、そのまま `App(...)` を作ると `--headless` で起動する。画面に出すには呼ぶ側が明示的に `HEADLESS = False` にする（app_check の `--visible` がこれをする）。全体の実行と `--only-headless` は、別プロセスで読み込んだ `App(...)` がヘッドレスで起動することも確かめる（`[R3]`）

日本語 IME の確認（受入条件 M1-4）は人が画面で行う（ヘッドレスでは確かめられない）。変換ウィンドウがカーソル位置に出ること、確定した文字列が 1 回の undo で消えることを見る。

## 構成

- `src/core/`: 編集の核（静的ライブラリ `geditor_core`）。書記素クラスタの区切りと幅（`grapheme.*`）・piece tree・undo・文字コード・キー表記・キーマップ・コマンド・ミニバッファ・升目計算・JSONC パーサー（`jsonc.*`。リポジトリで唯一の JSON パーサー）・設定の検査とキーマップの層（`config.*`）を持つ。折り返し（論理行を画面の行に分ける計算。長い行は 64K 文字ごとの区切りを記録して再利用する）は `layout.*` の `WrapLayout` にあり、描画・上下移動・スクロール・スクロールバーのすべてがこれを使う（描画側で折り返し位置を計算しない）。行番号の幅・空白記号・改行記号・text-scale の大きさ・View の切り替えは `display.*`。メニューの中身（項目・コマンドと引数・区切り線・サブメニュー・チェックとラジオ・有効無効・キー表示）は `menu.*`、ホイールの量の累積は `mouse.*`。ファイル・クリップボード・ダイアログ・ウィンドウ起動などの環境依存の処理は `src/core/host.h` の `Host`（抽象クラス）を通して呼ぶ。Win32 側（`src/win/`）がこれを実装し、テストは `tests/fake_host.*` の偽物を使う
- `geditor_core` には `user32` などの UI 系ライブラリをリンクしない
- `src/win/`: Win32 層。`window.*`（ウィンドウ・メッセージ・`Host` の実装）、`input.*`（キー変換）、`renderer.*` と `text_measurer.*`（Direct2D / DirectWrite の升目描画と代替フォント）、`ime.*`、`clipboard.*`、`file_io.*`、`dialogs.*`（TaskDialog）、`process.*`（新しいウィンドウ）、`command_line.*`、`measure.*`（起動・打鍵の計測）、`config_watcher.*`（設定ファイルの監視スレッド）、`menu_bar.*`（コアのメニューから `HMENU` を組み立て、`WM_INITMENUPOPUP` で更新し、読み戻す）、`question.h`（確認の文言とボタン。TaskDialog とヘッドレスが共用。ヘッドレスのメニューもここで確認の形にする）、`headless.h`（ヘッドレスの `WM_COPYDATA` と状態の書式）。キー変換・起動引数・クリップボードの改行変換・ファイル種別の判定・`HMENU` の組み立てなどウィンドウなしで試せる部分は静的ライブラリ `geditor_win_logic` にまとめ、テストからも使う。`text_measurer.*` は静的ライブラリ `geditor_text`（`dwrite` をリンク）にして、どのフォントで文字を描くかをテストからも確かめる
- 高 DPI は Per-Monitor V2（`geditor.manifest` と `SetProcessDpiAwarenessContext`）。`WM_DPICHANGED` でフォントと升目を作り直し、勧められた大きさに変え、先頭の行を保つ。ウィンドウの右端に Win32 の縦スクロールバーを初回描画の後から常に出す（初回の描画を起動時間に含めないため。位置は論理行で近似）
- `comctl32`（TaskDialog）と `shell32`（ドラッグ＆ドロップ）は遅延読み込みにして起動時間に含めない。TaskDialog のために `src/win/geditor.manifest` で Common Controls v6 を指定する
- `tests/`: `geditor_tests`（ctest に登録）と `tests/perf/` の `geditor_perf`（登録しない）。`test_win_font.cpp` は本物の DirectWrite で、英字が Consolas、日本語と絵文字が代替フォントで描かれることを確かめ、選ばれたフォント名を表示する
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
- 複数の文字からなるクラスタは、`MapCharacters` が選んだ 1 つのフォントで 1 つの文字列として形を作り（絵文字として表示するクラスタや、選ばれたフォントにない文字があるときは、主フォントを指定しない `MapCharacters` の結果を使う）、升目より広ければ横に縮める。絵文字は単色のまま。どのフォントにも無いときや形を作れないときは、先頭の文字のグリフ（無ければ欠字の箱）を描く

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
