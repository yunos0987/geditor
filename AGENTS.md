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
```

- `measure_input.py` は `build/measure_input/settings.json`（中身は `{}`）を `--settings` で渡す。利用者の `%USERPROFILE%\.geditor.json` は読まない。`--measure-input` では設定ファイルの監視は始めない

## アプリの起動と実機確認

```powershell
build/release/geditor.exe [--settings=PATH] [+LINE] [file]
```

- 存在しないファイル名なら新規として開く。`+LINE` はその行へ移る。未知のオプションはエコー領域に表示する
- 設定ファイルは既定で `%USERPROFILE%\.geditor.json`。`--settings=PATH` で場所を変える（相対パスは今のフォルダーから。指定したときは `%USERPROFILE%` のファイルを読みも監視もしない。「新しいウィンドウで開く」にも同じ指定を渡す）
- テスト・計測・実機確認では必ず `--settings` で `build/` の下のファイルを指定し、利用者の `%USERPROFILE%\.geditor.json` を読み書き・作成・削除しない（今は存在しない）
- 設定ファイルは起動時に同期で読み、初回描画の後に別スレッドで監視する。保存すると約 200ms 後に読み直す。構文の誤りはファイル全体を採用せず、意味の誤りはその項目だけ無視する。誤りは行・桁付きでエコー領域に出し、2 件以上ならエコー領域を最大 10 行まで広げる（ウィンドウが低いときはモードラインと本文 1 行が残る行数まで。収まらない分は最後の行の `... and N more` に数える。次のキーで 1 行に戻る）。`M-x find-user-init-file` で設定ファイルを開く（無ければ書き方の例を入れた未保存の新規バッファになる）
- M2 で反映するのはキー設定（`keys` / `minibuffer_keys` / `isearch_keys` / `query_replace_keys`）だけ。ほかの項目は型と値を検査して保持し、M3 以降で使う
- Norton 360 は `build/` の外に置いた新しい exe を約 2 秒止めるので、計測や確認は `build/` の exe で行う

実機確認（受入条件 M1 の画面・キー・ダイアログ・ファイル・クリップボード）は `tools/app_check.py` で行う。テスト用ファイルを `build/app_check/<日時>/files/` に作って geditor を起動し、キーを送り、画面を `shots/*.png` に保存し、ファイルのバイト列・クリップボード・タイトルを確かめて、項目ごとに PASS / FAIL / SKIP と証拠を表示する。画像は目でも確かめる:

```powershell
python tools/app_check.py build/release/geditor.exe            # --strict: SKIP also fails
python tools/app_check.py build/release/geditor.exe --only-retry1
python tools/app_check.py build/release/geditor.exe --only-m2
```

- 最後の行に PASS / FAIL / SKIP の件数が出る。`--strict` を付けると SKIP が 1 件でもあれば終了コード 1。`--only-retry1` は長いパス・最小化と復元・結合文字と不可視文字・ログオフ時の保存確認・クリップボード読込失敗の確認だけを行う
- 起動する geditor にはすべて `--settings` で `build/app_check/<日時>/` の下のファイルを渡す
- `--only-m2` は設定ファイルの確認（M2）だけを行う。geditor の起動中に設定ファイルを書き換えて、キーの割り当てが効くまでの時間を測り（1 秒以内）、構文の誤りで直前の設定が残ること、誤りが 4 件のときにエコー領域が 4 行に広がり次のキーで 1 行に戻ること、ファイルを消すと既定に戻ること、他のプログラムが共有なしで開いている間は読めない理由を表示して直前の設定を保つこと、`find-user-init-file` が例の入った未保存のバッファを開き、保存した例が誤りなしで読めることを確かめる。最後に、無いフォルダーの設定ファイルを指定して監視を始められないことの表示を確かめる
- `tools/` のスクリプトは `__pycache__` を作業ツリーに残さない（`sys.dont_write_bytecode` と、`app_driver.py` が読み込み時に自分の `.pyc` を消す処理による）
- 画面がロックされていなければ `SendInput` で打鍵する（geditor を前面に出してから送り、前面でなければ送らずに止まる）。ロック中は `PostMessage` で `WM_KEYDOWN` / `WM_CHAR` を送り、修飾キーの状態は `AttachThreadInput` と `SetKeyboardState` で与える。どちらで動いたかは `input mode` の行に出る
- 画面のロック中は `OpenClipboard` がアクセス拒否になるため、クリップボードを使う確認は SKIP になる（geditor 側の書き込み失敗の表示は確かめる）。実行の前後でクリップボードの文字列を保存・復元する
- 画像は geditor に `WM_COPYDATA`（`dwData` = `'GCAP'`、BMP の保存先パス）を送って描かせる。ロック中は画面にも `PrintWindow` にも描画が出ないため。ダイアログは `PrintWindow` で撮る
- `tools/app_driver.py` が起動・打鍵・撮影の部品

日本語 IME の確認（受入条件 M1-4）は人が行う。変換ウィンドウがカーソル位置に出ること、確定した文字列が 1 回の undo で消えることを見る。

## 構成

- `src/core/`: 編集の核（静的ライブラリ `geditor_core`）。piece tree・undo・文字コード・キー表記・キーマップ・コマンド・ミニバッファ・升目計算・JSONC パーサー（`jsonc.*`。リポジトリで唯一の JSON パーサー）・設定の検査とキーマップの層（`config.*`）を持つ。ファイル・クリップボード・ダイアログ・ウィンドウ起動などの環境依存の処理は `src/core/host.h` の `Host`（抽象クラス）を通して呼ぶ。Win32 側（`src/win/`）がこれを実装し、テストは `tests/fake_host.*` の偽物を使う
- `geditor_core` には `user32` などの UI 系ライブラリをリンクしない
- `src/win/`: Win32 層。`window.*`（ウィンドウ・メッセージ・`Host` の実装）、`input.*`（キー変換）、`renderer.*` と `text_measurer.*`（Direct2D / DirectWrite の升目描画と代替フォント）、`ime.*`、`clipboard.*`、`file_io.*`、`dialogs.*`（TaskDialog）、`process.*`（新しいウィンドウ）、`command_line.*`、`measure.*`（起動・打鍵の計測）、`config_watcher.*`（設定ファイルの監視スレッド）。キー変換・起動引数・クリップボードの改行変換・ファイル種別の判定などウィンドウなしで試せる部分は静的ライブラリ `geditor_win_logic` にまとめ、テストからも使う
- `comctl32`（TaskDialog）と `shell32`（ドラッグ＆ドロップ）は遅延読み込みにして起動時間に含めない。TaskDialog のために `src/win/geditor.manifest` で Common Controls v6 を指定する
- `tests/`: `geditor_tests`（ctest に登録）と `tests/perf/` の `geditor_perf`（登録しない）
- `.clangd` は `build/release/compile_commands.json` を読む（`cmake --preset release` で作られる）

## 文字幅表の生成

`src/core/char_width_table.inc` は `tools/gen_char_width.py` が Unicode Character Database から作る生成物で、手で直さない。Unicode を更新するときは、新しい空のフォルダーに次の 3 ファイルを置いて実行する（`<version>` は unicode.org の最新版。生成物の先頭に版が記録される）:

- `https://www.unicode.org/Public/<version>/ucd/EastAsianWidth.txt`
- `https://www.unicode.org/Public/<version>/ucd/DerivedCoreProperties.txt`
- `https://www.unicode.org/Public/<version>/ucd/extracted/DerivedGeneralCategory.txt`

```powershell
python -I tools/gen_char_width.py <folder>
```

同じ入力からは同じバイト列が出る。幅の規則（0 / 1 / 2 桁）はスクリプト冒頭の説明にある。

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
