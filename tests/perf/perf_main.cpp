// geditor_perf: performance checks of the editing core on a generated 100MB
// UTF-8 text (about 80 characters per line, ASCII mixed with Japanese),
// including isearch for a word near the end ([M5-5]).
// Not registered in ctest; run the Release build by hand:
//
//   build/release/tests/geditor_perf.exe
//
// Exits with 1 when a limit is exceeded.

#include <windows.h>

#include <psapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

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

}  // namespace

int main() {
  bool ok = true;
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
