// geditor_perf: performance checks of the editing core on a generated 100MB
// UTF-8 text (about 80 characters per line, ASCII mixed with Japanese),
// including isearch for a word near the end ([M5-5]), and the deferred
// items done before M6: loading Shift_JIS / EUC-JP / ISO-2022-JP
// ("decode"), a backward regexp isearch on a long single line
// ("backsearch") and <end> on long single lines ("endkey"), and a keyboard
// macro of 1000 self-inserts run 100 times ("macro", M6).
// Not registered in ctest; run the Release build by hand:
//
//   build/release/tests/geditor_perf.exe [core] [decode] [backsearch] [endkey] [memory] [macro]
//
// Without arguments every section runs. Exits with 1 when a limit is
// exceeded.

#include <windows.h>

#include <psapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "config.h"
#include "editor.h"
#include "encoding.h"
#include "host.h"
#include "key.h"
#include "piece_tree.h"

namespace {

constexpr size_t kTargetBytes = 100u * 1024u * 1024u;
constexpr double kDecodeLimitMs = 1000.0;
constexpr double kInsertLookupLimitMs = 0.5;
constexpr double kDispatchLimitMs = 1.0;
constexpr double kUndoMemoryLimitMb = 16.0;
constexpr double kSearchLimitMs = 1000.0;      // [M5-5]
constexpr double kSearchMemoryLimitMb = 16.0;  // no copy of the whole text (200MB)
constexpr double kReplaceLimitMs = 1000.0;     // M5 retry R3: "!" on a 160K line
constexpr wchar_t kPath[] = L"C:\\perf\\big.txt";

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

struct Rng {
  uint64_t state = 0x243F6A8885A308D3ull;
  uint64_t next() {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
  }
  size_t below(size_t n) { return static_cast<size_t>(next() % n); }
};

std::string generate_text() {
  // Words of ASCII and Japanese (3 bytes per character in UTF-8).
  const char* words[] = {"editor", "piece", "tree", "buffer", "line", "cursor", "undo", "redo",
                         "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E",              // 3 chars
                         "\xE3\x83\x86\xE3\x82\xAD\xE3\x82\xB9\xE3\x83\x88",  // 4 chars
                         "\xE7\xB7\xA8\xE9\x9B\x86",                          // 2 chars
                         "\xE3\x81\xB2\xE3\x82\x89\xE3\x81\x8C\xE3\x81\xAA"};
  const int word_chars[] = {6, 5, 4, 6, 4, 6, 4, 4, 3, 4, 2, 4};
  Rng rng;
  std::string s;
  s.reserve(kTargetBytes + 256);
  int column = 0;
  while (s.size() < kTargetBytes) {
    const size_t w = rng.below(std::size(words));
    s += words[w];
    column += word_chars[w];
    if (column >= 78) {
      s += '\n';
      column = 0;
    } else {
      s += ' ';
      ++column;
    }
  }
  return s;
}

double median(std::vector<double> v) {
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

double private_mb() {
  PROCESS_MEMORY_COUNTERS_EX pmc{};
  GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
  return static_cast<double>(pmc.PrivateUsage) / (1024.0 * 1024.0);
}

// Minimal host: one in-memory file, everything else declines.
class PerfHost : public geditor::Host {
 public:
  explicit PerfHost(const std::string* bytes) : bytes_(bytes) {}
  bool read_file(const std::wstring&, std::string& bytes, std::wstring&) override {
    bytes = *bytes_;
    return true;
  }
  bool save_file(const std::wstring&, std::string_view, bool, std::wstring& error) override {
    error = L"read-only perf host";
    return false;
  }
  bool file_info(const std::wstring& path, geditor::FileInfo& info, std::wstring&) override {
    info = geditor::FileInfo();
    info.exists = path == kPath;
    return true;
  }
  geditor::FileKind file_kind(const std::wstring& path) override {
    return path == kPath ? geditor::FileKind::kFile : geditor::FileKind::kNone;
  }
  bool list_directory(const std::wstring&, std::vector<geditor::DirEntry>&) override { return false; }
  std::wstring current_directory() override { return L"C:\\perf"; }
  geditor::ClipboardRead get_clipboard(std::wstring&) override {
    return geditor::ClipboardRead::kNoText;
  }
  bool set_clipboard(const std::wstring&) override { return false; }
  geditor::OpenChoice ask_open_choice(const std::wstring&) override { return geditor::OpenChoice::kCancel; }
  geditor::SaveChoice ask_save_changes(const std::wstring&) override { return geditor::SaveChoice::kCancel; }
  bool ask_yes_no(const std::wstring&) override { return false; }
  bool launch_new_window(const std::wstring&, const std::wstring&, std::wstring&) override { return false; }
  void quit() override {}
  void open_menu_bar() override {}
  int visible_lines() override { return 40; }
  int visible_columns() override { return 120; }
  int dpi() override { return 96; }

 private:
  const std::string* bytes_;
};

bool report(const char* name, double value, double limit, const char* unit) {
  const bool ok = value < limit;
  std::printf("%-40s %10.4f %s (limit < %.1f %s) %s\n", name, value, unit, limit, unit, ok ? "OK" : "FAIL");
  return ok;
}

// ---- Deferred items before M6 (decode, backsearch, endkey) ----

constexpr double kShiftJisDetectedLimitMs = 1500.0;  // 100MB Shift_JIS, auto-detected
constexpr double kShiftJisBadByteLimitMs = 2000.0;   // the same with one bad byte
constexpr double kIso2022JpLineLimitMs = 1000.0;     // 10MB one line without the final ESC ( B
constexpr double kBackSearchLimitMs = 100.0;         // C-u C-r .* from the end of a 400K line
constexpr double kEndKeyLimitMs = 500.0;             // <end> on a 100MB single line (wrap window)

// The words of tools/measure_input.py.
const wchar_t* const kWideWords[] = {L"editor", L"piece", L"tree", L"buffer", L"line", L"cursor", L"undo", L"redo",
                                     L"\x65E5\x672C\x8A9E", L"\x30C6\x30AD\x30B9\x30C8", L"\x7DE8\x96C6",
                                     L"\x3072\x3089\x304C\x306A"};

// Words separated by spaces until about `bytes` bytes in an encoding of
// `japanese_bytes` bytes per Japanese character and `japanese_extra` more
// per Japanese word (the escape sequences of ISO-2022-JP). `lines`: a line
// feed after about 80 columns; else one line that ends with a Japanese word.
std::wstring wide_words(size_t bytes, bool lines, size_t japanese_bytes, size_t japanese_extra) {
  Rng rng;
  std::wstring s;
  size_t estimate = 0;
  int column = 0;
  bool last_japanese = false;
  while (estimate < bytes || (!lines && !last_japanese)) {
    if (!s.empty()) {
      if (lines && column >= 78) {
        s += L'\n';
        column = 0;
      } else {
        s += L' ';
        ++column;
      }
      ++estimate;
    }
    const size_t w = rng.below(std::size(kWideWords));
    const std::wstring_view word = kWideWords[w];
    last_japanese = word[0] >= 0x3000;
    s += word;
    column += static_cast<int>(word.size()) * (last_japanese ? 2 : 1);
    estimate += word.size() * (last_japanese ? japanese_bytes : 1) + (last_japanese ? japanese_extra : 0);
  }
  return s;
}

std::string encode_with(UINT code_page, const std::wstring& s) {
  const int n = WideCharToMultiByte(code_page, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(std::max(n, 0)), '\0');
  WideCharToMultiByte(code_page, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
  return out;
}

// Replaces the "r" of the " editor " nearest after the middle with `byte`.
void put_bad_byte(std::string* bytes, char byte) {
  const size_t at = bytes->find(" editor ", bytes->size() / 2);
  if (at != std::string::npos) (*bytes)[at + 6] = byte;
  std::printf("  bad byte 0x%02X at %zu\n", static_cast<unsigned>(static_cast<unsigned char>(byte)), at + 6);
}

const char* codec_name(geditor::Codec c) {
  switch (c) {
    case geditor::Codec::kUtf8: return "utf-8";
    case geditor::Codec::kUtf16Le: return "utf-16le";
    case geditor::Codec::kUtf16Be: return "utf-16be";
    case geditor::Codec::kShiftJis: return "shift_jis";
    case geditor::Codec::kEucJp: return "euc-jp";
    case geditor::Codec::kIso2022Jp: return "iso-2022-jp";
  }
  return "?";
}

// Decodes (detected, or in `codec`) and builds the piece tree; the median of
// up to three runs (one when the first takes more than 3 s). Prints what
// was decoded, so that two builds can be compared.
double time_decode(const char* name, const std::string& bytes, bool detected, geditor::Codec codec) {
  std::vector<double> runs;
  for (int run = 0; run < 3; ++run) {
    const auto t0 = Clock::now();
    geditor::FileFormat format;
    format.codec = codec;
    geditor::DecodedText decoded = detected ? geditor::decode_detected(bytes) : geditor::decode_as(bytes, format);
    geditor::PieceTree tree = geditor::make_piece_tree(decoded);
    runs.push_back(ms_since(t0));
    if (run == 0) {
      uint64_t hash = 1469598103934665603ull;
      tree.for_each_chunk(0, tree.length(), [&](std::wstring_view chunk) {
        for (const wchar_t w : chunk) hash = (hash ^ static_cast<uint64_t>(w)) * 1099511628211ull;
        return true;
      });
      const size_t units = tree.length();
      std::printf("  %s: %zu bytes -> %s units %zu lines %zu hash %016llx undecodable %d replaced %d "
                  "error_offset %zu error_line %zu detection_failed %d\n",
                  name, bytes.size(), codec_name(decoded.format.codec), units, tree.line_count(),
                  static_cast<unsigned long long>(hash), decoded.format.undecodable ? 1 : 0,
                  decoded.replaced ? 1 : 0, decoded.error_offset, decoded.error_line,
                  decoded.detection_failed ? 1 : 0);
    }
    std::printf("  %s run %d: %.1f ms\n", name, run + 1, runs.back());
    if (runs.back() > 3000.0) break;
  }
  return median(runs);
}

bool decode_section() {
  bool ok = true;
  std::printf("decode: generating...\n");
  const std::wstring lines = wide_words(kTargetBytes, true, 2, 0);
  const std::string sjis = encode_with(932, lines);
  std::string sjis_bad = sjis;
  put_bad_byte(&sjis_bad, '\x81');  // a lead byte before a space
  ok &= report("decode: 100MB Shift_JIS detected", time_decode("sjis detected", sjis, true, geditor::Codec::kUtf8),
               kShiftJisDetectedLimitMs, "ms");
  report("decode: 100MB Shift_JIS named", time_decode("sjis named", sjis, false, geditor::Codec::kShiftJis), 1e9,
         "ms");
  ok &= report("decode: 100MB Shift_JIS one bad byte, named",
               time_decode("sjis bad named", sjis_bad, false, geditor::Codec::kShiftJis), kShiftJisBadByteLimitMs,
               "ms");
  report("decode: 100MB Shift_JIS one bad byte, detected",
         time_decode("sjis bad detected", sjis_bad, true, geditor::Codec::kUtf8), 1e9, "ms");

  const std::wstring one_line = wide_words(10u * 1024u * 1024u, false, 2, 6);
  std::string iso = encode_with(50220, one_line);
  if (iso.size() >= 3 && iso.compare(iso.size() - 3, 3, "\x1b(B") == 0) iso.resize(iso.size() - 3);
  ok &= report("decode: 10MB one-line ISO-2022-JP, no ESC ( B",
               time_decode("iso2022jp line", iso, false, geditor::Codec::kIso2022Jp), kIso2022JpLineLimitMs, "ms");

  const std::wstring euc_lines = wide_words(20u * 1024u * 1024u, true, 2, 0);
  std::string euc = encode_with(20932, euc_lines);
  put_bad_byte(&euc, '\x80');
  report("decode: 20MB EUC-JP with one 0x80", time_decode("eucjp 0x80", euc, false, geditor::Codec::kEucJp), 1e9,
         "ms");
  return ok;
}

// The highest private bytes (MB) of the process while `f` runs, sampled
// every millisecond on another thread.
template <typename F>
double peak_private_mb(F&& f) {
  std::atomic<bool> running{true};
  std::atomic<double> peak{private_mb()};
  std::thread sampler([&] {
    while (running.load()) {
      const double mb = private_mb();
      if (mb > peak.load()) peak.store(mb);
      Sleep(1);
    }
  });
  f();
  const double after = private_mb();
  running.store(false);
  sampler.join();
  return std::max(peak.load(), after);
}

// Memory of loading and saving the code pages (retry R2): the capacity
// that the decoded text keeps while the file is open, and the peak private
// bytes of a load and of a save (encode_text) of 100MB of Shift_JIS, EUC-JP
// and ISO-2022-JP text. Reported only.
void memory_section() {
  std::printf("memory: generating...\n");
  const std::wstring lines = wide_words(kTargetBytes, true, 2, 0);
  const std::string sjis = encode_with(932, lines);
  std::string sjis_bad = sjis;
  put_bad_byte(&sjis_bad, '\x81');
  std::string iso = encode_with(50220, wide_words(10u * 1024u * 1024u, false, 2, 6));
  if (iso.size() >= 3 && iso.compare(iso.size() - 3, 3, "\x1b(B") == 0) iso.resize(iso.size() - 3);
  struct Load {
    const char* name;
    const std::string* bytes;
    bool detected;
    geditor::Codec codec;
  };
  const Load loads[] = {
      {"100MB Shift_JIS detected", &sjis, true, geditor::Codec::kUtf8},
      {"100MB Shift_JIS named", &sjis, false, geditor::Codec::kShiftJis},
      {"100MB Shift_JIS one bad byte, named", &sjis_bad, false, geditor::Codec::kShiftJis},
      {"10MB one-line ISO-2022-JP, no ESC ( B", &iso, false, geditor::Codec::kIso2022Jp},
  };
  for (const Load& load : loads) {
    const double before = private_mb();
    size_t size = 0;
    size_t capacity = 0;
    const double peak = peak_private_mb([&] {
      geditor::FileFormat format;
      format.codec = load.codec;
      geditor::DecodedText d =
          load.detected ? geditor::decode_detected(*load.bytes) : geditor::decode_as(*load.bytes, format);
      size = d.text.size();
      capacity = d.text.capacity();
    });
    std::printf("memory: load %s: text size %zu capacity %zu units; private %.1f MB before, peak %.1f MB (+%.1f)\n",
                load.name, size, capacity, before, peak, peak - before);
  }
  for (const geditor::Codec codec : {geditor::Codec::kShiftJis, geditor::Codec::kEucJp, geditor::Codec::kIso2022Jp}) {
    geditor::PieceTree tree(lines);
    geditor::FileFormat format;
    format.codec = codec;
    const double before = private_mb();
    size_t size = 0;
    size_t capacity = 0;
    bool ok = false;
    const auto t0 = Clock::now();
    const double peak = peak_private_mb([&] {
      const geditor::EncodedText e = geditor::encode_text(tree, format);
      size = e.bytes.size();
      capacity = e.bytes.capacity();
      ok = e.ok;
    });
    std::printf("memory: save %s (%zu units): ok %d, bytes size %zu capacity %zu; private %.1f MB before, "
                "peak %.1f MB (+%.1f), %.1f ms\n",
                codec_name(codec), tree.length(), ok ? 1 : 0, size, capacity, before, peak, peak - before,
                ms_since(t0));
  }
}

// Types `keys` (key names) and then the characters of `typed` into `ed`.
void type_keys(geditor::Editor& ed, std::initializer_list<const wchar_t*> keys, std::wstring_view typed) {
  for (const wchar_t* k : keys) ed.handle_key(*geditor::parse_key(k));
  for (const wchar_t c : typed) ed.handle_key(geditor::Key{c, 0});
}

// Backward isearch from the end of one long line ("abcdefg " repeated).
// `max_line`: the longest line measured (0: all).
bool backsearch_section(size_t max_line) {
  bool ok = true;
  struct Case {
    size_t length;
    bool regexp;
    const wchar_t* pattern;
    double limit_ms;  // 0: reported only
  };
  const Case cases[] = {
      {100000, true, L".*", 0},   {400000, true, L".*", kBackSearchLimitMs}, {400000, true, L"a*", 0},
      {400000, true, L"\\w+", 0}, {400000, false, L"cdef", 0},                {400000, true, L"zq", 0},
  };
  for (const Case& c : cases) {
    if (max_line != 0 && c.length > max_line) continue;
    std::string text;
    text.reserve(c.length);
    while (text.size() < c.length) text += "abcdefg ";
    text.resize(c.length);
    PerfHost host(&text);
    geditor::Editor ed(host);
    ed.request_open(kPath);
    ed.set_point(ed.buffer().text.length());
    if (c.regexp) {
      type_keys(ed, {L"C-u", L"C-r"}, L"");
    } else {
      type_keys(ed, {L"C-r"}, L"");
    }
    const auto t0 = Clock::now();
    type_keys(ed, {}, c.pattern);
    const double ms = ms_since(t0);
    char name[96];
    std::snprintf(name, sizeof(name), "backsearch: %zuK line, %s %ls", c.length / 1000, c.regexp ? "regexp" : "literal",
                  c.pattern);
    std::printf("  %s: point %zu, echo \"%ls\"\n", name, ed.buffer().point, ed.echo_area_text().c_str());
    if (c.limit_ms > 0) {
      ok &= report(name, ms, c.limit_ms, "ms");
    } else {
      std::printf("%-40s %10.4f ms\n", name, ms);
    }
  }
  return ok;
}

// <end> on a single line of `bytes` UTF-8 bytes (the words of
// tools/measure_input.py), wrap "window" in a 120 x 40 window: the key
// itself and the first frame after it (the core part of drawing).
bool endkey_section() {
  bool ok = true;
  for (const size_t mb : {size_t{10}, size_t{100}}) {
    const std::wstring wide = wide_words(mb * 1024u * 1024u, false, 3, 0);
    const std::string bytes = encode_with(CP_UTF8, wide);
    PerfHost host(&bytes);
    std::vector<double> key_runs;
    std::vector<double> frame_runs;
    for (int run = 0; run < 3; ++run) {
      // A fresh editor each run (no recorded states).
      geditor::Editor ed(host);
      ed.request_open(kPath);
      ed.set_wrap_mode(geditor::WrapMode::kWindow);
      if (run == 0) {
        // Where the time goes: the cluster walk alone, and the wrap layout
        // of the whole line (a layout of its own).
        const geditor::PieceTree& t = ed.buffer().text;
        auto c0 = Clock::now();
        size_t cells = 0;
        geditor::for_each_cluster(t, 0, t.line_end(0), geditor::GraphemeBreaker(), nullptr,
                                  [&](const geditor::Cluster& c) {
                                    cells += static_cast<size_t>(c.width);
                                    return true;
                                  });
        const double clusters_ms = ms_since(c0);
        geditor::WrapLayout l;
        l.configure(ed.wrap_width(), ed.tab_width());
        c0 = Clock::now();
        const size_t rows = l.row_count(t, 0);
        const double rows_ms = ms_since(c0);
        std::printf("  %zuMB: for_each_cluster %.1f ms (%zu cells), WrapLayout::row_count %.1f ms (%zu rows)\n", mb,
                    clusters_ms, cells, rows_ms, rows);
      }
      ed.layout().reset_scanned();
      const auto t0 = Clock::now();
      ed.handle_key(*geditor::parse_key(L"<end>"));
      key_runs.push_back(ms_since(t0));
      const size_t key_scanned = ed.layout().scanned();
      const auto t1 = Clock::now();
      const geditor::Editor::PointView view = ed.point_view();
      const std::wstring mode_line = ed.mode_line();
      const geditor::Editor::SearchHighlights highlights = ed.search_highlights(40);
      geditor::RowPos pos = ed.top();
      geditor::DisplayRow row;
      size_t cells = 0;
      for (int r = 0; r < 40; ++r) {
        ed.display_row(pos, 0, ed.text_columns() + 1, &row);
        cells += row.cells.size();
        if (!ed.next_row(&pos)) break;
      }
      frame_runs.push_back(ms_since(t1));
      std::printf("  %zuMB run %d: key %.1f ms (scanned %zu), frame %.1f ms (scanned %zu); point %zu of %zu, "
                  "row %zu column %d, top row %zu, cells %zu, %zu highlights, %ls\n",
                  mb, run + 1, key_runs.back(), key_scanned, frame_runs.back(), ed.layout().scanned() - key_scanned,
                  ed.buffer().point, ed.buffer().text.length(), view.pos.row, view.column, ed.top().row, cells,
                  highlights.matches.size(), mode_line.c_str());
    }
    char name[64];
    std::snprintf(name, sizeof(name), "endkey: %zuMB line, <end> + frame", mb);
    const double total = median(key_runs) + median(frame_runs);
    if (mb == 100) {
      ok &= report(name, total, kEndKeyLimitMs, "ms");
    } else {
      std::printf("%-40s %10.4f ms\n", name, total);
    }
  }
  return ok;
}

// M6 [C5]: a keyboard macro of 1000 self-inserts replayed 100 times
// (C-u 100 C-x e) on an empty buffer, through the same key entry point as
// typing. The 100000 characters make one logical line, where each key costs
// what typing on a long line costs (about 0.2 ms at 100K characters, a
// known deferred item), so the replay is judged against the same 100000
// keys typed directly in the same run: three alternating pairs, judged by
// the median of the pairs' ratios (limit 1.25). The macro with a RET after
// the 1000 characters (lines of 1000) is printed for reference, not judged.
bool macro_section() {
  constexpr double kMacroRatioLimit = 1.25;
  const std::string empty;
  std::wstring letters;
  for (int i = 0; i < 1000; ++i) letters += static_cast<wchar_t>(L'a' + i % 26);
  // Records the 1000 letters (and RET when `newline`), then times C-u 100 C-x e.
  auto run_macro = [&](bool newline, size_t* length, size_t* events) {
    PerfHost host(&empty);
    geditor::Editor ed(host);
    type_keys(ed, {L"C-x", L"("}, letters);
    if (newline) type_keys(ed, {L"RET"}, L"");
    type_keys(ed, {L"C-x", L")"}, L"");
    const auto t0 = Clock::now();
    type_keys(ed, {L"C-u", L"1", L"0", L"0", L"C-x", L"e"}, L"");
    const double ms = ms_since(t0);
    *length = ed.buffer().text.length();
    *events = ed.macro_size();
    return ms;
  };
  // The same keys typed one by one, without a macro.
  auto run_typed = [&](bool newline) {
    PerfHost host(&empty);
    geditor::Editor ed(host);
    const auto t0 = Clock::now();
    for (int i = 0; i < 100; ++i) {
      type_keys(ed, {}, letters);
      if (newline) type_keys(ed, {L"RET"}, L"");
    }
    return ms_since(t0);
  };
  std::vector<double> macro_runs;
  std::vector<double> typed_runs;
  std::vector<double> ratios;
  size_t length = 0;
  size_t events = 0;
  for (int pair = 0; pair < 3; ++pair) {
    // Alternating order: macro first, then typed first, then macro first.
    double macro_ms = 0;
    double typed_ms = 0;
    if (pair % 2 == 0) {
      macro_ms = run_macro(false, &length, &events);
      typed_ms = run_typed(false);
    } else {
      typed_ms = run_typed(false);
      macro_ms = run_macro(false, &length, &events);
    }
    macro_runs.push_back(macro_ms);
    typed_runs.push_back(typed_ms);
    ratios.push_back(macro_ms / typed_ms);
    std::printf("  pair %d: macro %.1f ms (%zu characters, macro of %zu events), typed %.1f ms, ratio %.3f\n",
                pair + 1, macro_ms, length, events, typed_ms, ratios.back());
  }
  size_t lines_length = 0;
  size_t lines_events = 0;
  const double lines_ms = run_macro(true, &lines_length, &lines_events);
  std::printf("  reference (no limit): macro of 1000 characters and RET x 100 (lines of 1000): %.1f ms, %zu "
              "characters, %zu events; typed without a macro: %.1f ms\n",
              lines_ms, lines_length, lines_events, run_typed(true));
  bool ok = length == 101000;
  if (!ok) std::printf("  macro: expected 101000 characters, got %zu\n", length);
  std::printf("%-40s %10.4f ms\n", "macro: 1000 self-inserts x 100 (median)", median(macro_runs));
  std::printf("%-40s %10.4f ms\n", "macro: the same keys typed (median)", median(typed_runs));
  const double ratio = median(ratios);
  const bool ratio_ok = ratio <= kMacroRatioLimit;
  std::printf("%-40s %10.4f x (limit <= %.2f x) %s\n", "macro: replay / typed (median ratio)", ratio,
              kMacroRatioLimit, ratio_ok ? "OK" : "FAIL");
  ok &= ratio_ok;
  return ok;
}

// M6 retry R1: 40000 kills in a row by a keyboard macro (C-k C-k run 20000
// times on a file of 100000 short lines): the clipboard text of a run of
// kills must cost about linear time. Limit 1 s; the clipboard must hold the
// joined text afterwards.
bool kill_section() {
  constexpr double kKillLimitMs = 1000.0;
  std::string text;
  for (int i = 0; i < 100000; ++i) text += "line " + std::to_string(i) + "\n";
  std::string expected_utf8;
  // Recording kills line 0; the run (a new sequence after C-x e) kills
  // lines 1 to 20000 with their line ends (40000 kills).
  for (int i = 1; i <= 20000; ++i) expected_utf8 += "line " + std::to_string(i) + "\n";
  const std::wstring expected(expected_utf8.begin(), expected_utf8.end());
  // A host whose clipboard keeps the last text written (and counts writes).
  class KillHost : public PerfHost {
   public:
    using PerfHost::PerfHost;
    bool set_clipboard(const std::wstring& t) override {
      clip = t;
      ++writes;
      return true;
    }
    std::wstring clip;
    size_t writes = 0;
  };
  KillHost host(&text);
  geditor::Editor ed(host);
  ed.request_open(kPath);
  type_keys(ed, {L"C-x", L"(", L"C-k", L"C-k", L"C-x", L")"}, L"");
  host.writes = 0;
  const auto t0 = Clock::now();
  type_keys(ed, {L"C-u", L"2", L"0", L"0", L"0", L"0", L"C-x", L"e"}, L"");
  const double ms = ms_since(t0);
  const bool joined = host.clip == expected;
  std::printf("  kill: %zu characters left, clipboard %zu characters (joined text of lines 1-20000: %s), %zu clipboard "
              "writes during the run, \"%ls\"\n",
              ed.buffer().text.length(), host.clip.size(), joined ? "yes" : "NO", host.writes,
              ed.message().c_str());
  return report("kill: 40000 kills in a row by macro", ms, kKillLimitMs, "ms") && joined;
}

}  // namespace

int main(int argc, char** argv) {
  const bool all = argc <= 1;
  bool core = all;
  bool decode = all;
  bool backsearch = all;
  bool endkey = all;
  bool memory = all;
  bool macro = all;
  bool kill = all;
  size_t max_line = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string_view a = argv[i];
    if (a == "core") {
      core = true;
    } else if (a == "decode") {
      decode = true;
    } else if (a == "backsearch") {
      backsearch = true;
    } else if (a == "endkey") {
      endkey = true;
    } else if (a == "memory") {
      memory = true;
    } else if (a == "macro") {
      macro = true;
    } else if (a == "kill") {
      kill = true;
    } else if (a.starts_with("max-line=")) {
      max_line = std::strtoull(argv[i] + 9, nullptr, 10);
    } else {
      std::printf("unknown argument: %s (core, decode, backsearch, endkey, memory, macro, kill, max-line=N)\n",
                  argv[i]);
      return 2;
    }
  }
  bool ok = true;
  if (memory) memory_section();
  if (decode) ok &= decode_section();
  if (backsearch) ok &= backsearch_section(max_line);
  if (endkey) ok &= endkey_section();
  if (macro) ok &= macro_section();
  if (kill) ok &= kill_section();
  if (!core) {
    std::printf("%s\n", ok ? "all limits met" : "LIMIT EXCEEDED");
    return ok ? 0 : 1;
  }
  std::printf("generating text...\n");
  const std::string bytes = generate_text();
  std::printf("text: %zu bytes\n", bytes.size());

  // 1. Decode + build: three runs, judged by the median (single runs vary
  // with memory pressure from other processes).
  {
    std::vector<double> runs;
    geditor::PieceTree tree;
    for (int run = 0; run < 3; ++run) {
      tree = geditor::PieceTree();
      const auto t0 = Clock::now();
      geditor::DecodedText decoded = geditor::decode_utf8(bytes);
      geditor::PieceTree built = geditor::make_piece_tree(decoded);
      runs.push_back(ms_since(t0));
      if (decoded.status != geditor::DecodeStatus::kOk) {
        std::printf("decode failed\n");
        return 1;
      }
      tree = std::move(built);
      std::printf("decode + build run %d: %.1f ms\n", run + 1, runs.back());
    }
    std::printf("units: %zu, lines: %zu\n", tree.length(), tree.line_count());
    ok &= report("decode + build (median of 3)", median(runs), kDecodeLimitMs, "ms");

    // 2. Insert one character at a random offset + offset -> line lookup.
    Rng rng;
    std::vector<double> samples;
    samples.reserve(1000);
    size_t sink = 0;
    for (int i = 0; i < 1000; ++i) {
      const size_t offset = rng.below(tree.length());
      const auto t1 = Clock::now();
      tree.insert(offset, L"x");
      sink += tree.line_of(offset);
      samples.push_back(ms_since(t1));
    }
    std::printf("(line sum %zu)\n", sink);
    ok &= report("insert + line lookup (median of 1000)", median(samples), kInsertLookupLimitMs, "ms");
  }

  // 3. Core dispatch of a self-insert key, and 4. delete-all + undo memory.
  {
    PerfHost host(&bytes);
    geditor::Editor ed(host);
    ed.request_open(kPath);
    if (ed.buffer().text.length() == 0) {
      std::printf("open failed\n");
      return 1;
    }
    Rng rng;
    std::vector<double> samples;
    samples.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
      ed.set_point(rng.below(ed.buffer().text.length()));
      const auto t1 = Clock::now();
      ed.handle_key(geditor::Key{U'a', 0});
      samples.push_back(ms_since(t1));
    }
    ok &= report("dispatch self-insert key (median of 1000)", median(samples), kDispatchLimitMs, "ms");

    const size_t length_before = ed.buffer().text.length();
    const double mb_before = private_mb();
    for (const wchar_t* k : {L"C-x", L"h", L"<delete>"}) {
      ed.handle_key(*geditor::parse_key(k));
    }
    const size_t length_deleted = ed.buffer().text.length();
    ed.handle_key(*geditor::parse_key(L"C-/"));
    const double mb_after = private_mb();
    const size_t length_after = ed.buffer().text.length();
    std::printf("length before %zu, after delete %zu, after undo %zu\n", length_before, length_deleted,
                length_after);
    if (length_deleted != 0 || length_after != length_before) {
      std::printf("delete-all + undo did not restore the text\n");
      ok = false;
    }
    ok &= report("delete all + undo private bytes delta", mb_after - mb_before, kUndoMemoryLimitMb, "MB");
  }

  // 5. [M5-5] isearch for a word near the end of the 100MB text, after 1000
  // random one-character edits (the text is in many pieces). The word starts
  // with U+00A7, which the text has nowhere else, so its first key scans
  // the whole text. The private bytes are sampled every millisecond on
  // another thread: a copy of the whole text (200MB in UTF-16) would show.
  {
    PerfHost host(&bytes);
    geditor::Editor ed(host);
    ed.request_open(kPath);
    Rng rng;
    for (int i = 0; i < 1000; ++i) {
      ed.set_point(rng.below(ed.buffer().text.length()));
      ed.handle_key(geditor::Key{U'a', 0});
    }
    const geditor::PieceTree& t = ed.buffer().text;
    const std::wstring head = L"¶unicorn";  // on line 1, for the backward search below
    ed.set_point(0);
    ed.insert_text(head);
    const size_t word_at = t.line_start(t.line_count() - 100);
    const std::wstring word = L"§zebracorn";
    ed.set_point(word_at);
    ed.insert_text(word);
    std::printf("pieces: %zu, word at %zu of %zu\n", t.piece_count(), word_at, t.length());

    ed.set_point(0);
    ed.handle_key(*geditor::parse_key(L"C-s"));
    std::atomic<bool> running{true};
    std::atomic<double> peak{0};
    const double mb_before = private_mb();
    std::thread sampler([&] {
      while (running.load()) {
        const double mb = private_mb();
        if (mb > peak.load()) peak.store(mb);
        Sleep(1);
      }
    });
    const auto t0 = Clock::now();
    ed.handle_key(geditor::Key{word[0], 0});
    const double first_ms = ms_since(t0);
    for (size_t i = 1; i < word.size(); ++i) ed.handle_key(geditor::Key{word[i], 0});
    const double word_ms = ms_since(t0);
    running.store(false);
    sampler.join();
    const bool found = ed.isearch_active() && ed.buffer().point == word_at + word.size();
    std::printf("found: %s (point %zu, echo %ls)\n", found ? "yes" : "NO", ed.buffer().point,
                ed.echo_area_text().c_str());
    ok &= found;
    ok &= report("isearch: first key, whole-text scan", first_ms, kSearchLimitMs, "ms");
    ok &= report("isearch: the whole word typed", word_ms, kSearchLimitMs, "ms");
    ok &= report("isearch: peak private bytes delta", std::max(0.0, peak.load() - mb_before), kSearchMemoryLimitMb,
                 "MB");

    // The other way: from the end back to a word on line 1 (its first key
    // scans the whole text line by line; not a limit of PLAN, for the
    // report).
    ed.handle_key(*geditor::parse_key(L"RET"));
    ed.set_point(t.length());
    ed.handle_key(*geditor::parse_key(L"C-r"));
    const auto t1 = Clock::now();
    ed.handle_key(geditor::Key{head[0], 0});
    const double back_ms = ms_since(t1);
    for (size_t i = 1; i < head.size(); ++i) ed.handle_key(geditor::Key{head[i], 0});
    std::printf("isearch backward, first key, whole-text scan (to line 1): %.1f ms, point %zu (want 0)\n", back_ms,
                ed.buffer().point);
  }

  // 6. query-replace "!" (M5 retry R3): every match of long lines and of
  // many short lines, literal and regexp with $1. One undo restores the text.
  {
    struct Case {
      const char* name;
      std::string text;
      const wchar_t* keys;  // the query-replace key
      const wchar_t* from;
      const wchar_t* to;
      size_t hits;
      size_t point;     // after "!": the end of the last replacement (as before R3)
      double limit_ms;  // 0: reported only
    };
    auto repeat = [](const std::string& unit, size_t n) {
      std::string s;
      s.reserve(unit.size() * n);
      for (size_t i = 0; i < n; ++i) s += unit;
      return s;
    };
    const Case cases[] = {
        {"! on a 40K line, 5000 literal hits", repeat("abcdefg ", 5000) + "\n", L"M-%", L"a", L"Z", 5000, 39993,
         kReplaceLimitMs},
        {"! on a 160K line, 20000 literal hits", repeat("abcdefg ", 20000) + "\n", L"M-%", L"a", L"Z", 20000,
         159993, kReplaceLimitMs},
        {"! on a 160K line, 20000 regexp $1 hits", repeat("abcdefg ", 20000) + "\n", L"C-M-%", L"a(b)", L"<$1>",
         20000, 179994, kReplaceLimitMs},
        {"! on 500000 short lines, 500000 hits", repeat("abcdefg\n", 500000), L"M-%", L"a", L"Z", 500000, 3999993,
         0},
    };
    for (const Case& c : cases) {
      PerfHost host(&c.text);
      geditor::Editor ed(host);
      ed.request_open(kPath);
      const std::wstring before = ed.buffer().text.text();
      auto type = [&](std::wstring_view s) {
        for (const wchar_t ch : s) ed.handle_key(geditor::Key{ch, 0});
      };
      ed.handle_key(*geditor::parse_key(c.keys));
      type(c.from);
      ed.handle_key(*geditor::parse_key(L"RET"));
      type(c.to);
      ed.handle_key(*geditor::parse_key(L"RET"));
      const auto t0 = Clock::now();
      ed.handle_key(geditor::Key{U'!', 0});
      const double ms = ms_since(t0);
      const std::wstring expected_message = L"Replaced " + std::to_wstring(c.hits) + L" occurrences";
      const std::wstring message = ed.message();
      const bool replaced = message == expected_message && !ed.query_replace_active();
      const size_t point = ed.buffer().point;
      ed.handle_key(*geditor::parse_key(L"C-/"));
      const bool undone = ed.buffer().text.text() == before && ed.buffer().point == 0;
      std::printf("%s: %.1f ms, %ls, point %zu (want %zu), one undo restores: %s\n", c.name, ms, message.c_str(),
                  point, c.point, undone ? "yes" : "NO");
      ok &= point == c.point;
      if (!replaced) std::printf("  wrong result (want \"%ls\")\n", expected_message.c_str());
      ok &= replaced && undone;
      if (c.limit_ms > 0) ok &= report(c.name, ms, c.limit_ms, "ms");
    }
  }

  std::printf("%s\n", ok ? "all limits met" : "LIMIT EXCEEDED");
  return ok ? 0 : 1;
}
