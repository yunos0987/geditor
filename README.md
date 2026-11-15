# geditor

Windows 専用の、起動の速いテキストエディターです。既定のキーバインドは Emacs で、キーバインド・フォント・配色は設定ファイル（JSONC）で変えられます。C++20 と Win32 API・Direct2D・DirectWrite で書いていて、GUI フレームワークは使っていません。

開発中です（段階 M0〜M5 まで実装済み。キーボードマクロ・Shift 選択・使い方の文書は M6 で入れる予定）。要件・設計・段階計画は [docs/PLAN.md](docs/PLAN.md) にあります。

## できること

- Emacs のキーバインド（kill/yank は Windows のクリップボードと連携）。Windows の Ctrl+英字の操作は Ctrl+Shift+英字（`C-S-z` で undo、`C-S-s` で保存など）
- M-x（コマンド名を Tab で補完）、前置引数 `C-u`、undo / redo
- インクリメンタル検索（`C-s` / `C-r`、`C-u` を付けると正規表現）、問い合わせ置換（`M-%` / `C-M-%`）、一致の強調表示、行番号を指定して移動（`M-g g`）。正規表現は PCRE2
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

- 1 ウィンドウで 1 ファイルを開きます。別のファイルは別のウィンドウ（プロセス）で開きます
- `+LINE` で指定の行へ移ります。`--encoding` は判定の代わりに文字コードを指定します（`utf-8` / `utf-8-bom` / `utf-16le` / `utf-16le-bom` / `utf-16be` / `utf-16be-bom` / `shift_jis` / `euc-jp` / `iso-2022-jp`）

## 設定

設定ファイルは `%USERPROFILE%\.geditor.json`（`--settings` で変更可）です。`M-x find-user-init-file` で開くと、ファイルが無いときは書き方の例が入った新しいバッファになります。例:

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

設定項目の一覧は [docs/PLAN.md](docs/PLAN.md) の「4.5 設定ファイル」にあります。

## ライセンス

MIT License（[LICENSE](LICENSE)）。同梱の PCRE2（`third_party/pcre2/`）は BSD-3-Clause WITH PCRE2-exception で、ライセンスは [third_party/pcre2/LICENCE.md](third_party/pcre2/LICENCE.md) にあります。
