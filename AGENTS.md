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
python tools/measure_startup.py build/release/geditor.exe --metric app
```

- `startup_ms`: プロセス生成から初回描画の `EndDraw` 完了まで
- `entry_ms`: プロセス生成から `wWinMain` 開始まで。開発 PC では Norton 360 が未知の exe の起動を約 2 秒止めるため、この値が大きくなる
- `app_ms`: `wWinMain` 開始から初回描画まで（プログラム自身の起動コスト）
- `geditor.exe --measure-startup` は段階ごとの時刻（`stage <name>=<ms>`）も標準エラーに出す

コアの性能計測（M1 の 100MB 基準）。生成した 100MB の UTF-8 テキストで、読込（デコード＋構築）・1 文字挿入と行番号検索・自己挿入キーの処理時間・全削除と undo のメモリ増分を測り、基準を超えると終了コード 1 で終わる。ctest には入れていないので、Release ビルドを手で実行する:

```powershell
build/release/tests/geditor_perf.exe
```

## 構成

- `src/core/`: 編集の核（静的ライブラリ `geditor_core`）。piece tree・undo・文字コード・キー表記・キーマップ・コマンド・ミニバッファ・升目計算を持つ。ファイル・クリップボード・ダイアログ・ウィンドウ起動などの環境依存の処理は `src/core/host.h` の `Host`（抽象クラス）を通して呼ぶ。Win32 側（`src/win/`）がこれを実装し、テストは `tests/fake_host.*` の偽物を使う
- `geditor_core` には `user32` などの UI 系ライブラリをリンクしない
- `tests/`: `geditor_tests`（ctest に登録）と `tests/perf/` の `geditor_perf`（登録しない）

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
