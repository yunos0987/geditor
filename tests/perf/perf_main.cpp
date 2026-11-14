// geditor_perf: performance checks of the editing core on a generated 100MB
// UTF-8 text (about 80 characters per line, ASCII mixed with Japanese).
// Not registered in ctest; run the Release build by hand:
//
//   build/release/tests/geditor_perf.exe
//
// Exits with 1 when a limit is exceeded.

#include <windows.h>

#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
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
  bool write_file(const std::wstring&, std::string_view, std::wstring& error) override {
    error = L"read-only perf host";
    return false;
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
  bool launch_new_window(const std::wstring&, std::wstring&) override { return false; }
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
      geditor::PieceTree built(std::move(decoded.text), std::move(decoded.line_feeds));
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

  std::printf("%s\n", ok ? "all limits met" : "LIMIT EXCEEDED");
  return ok ? 0 : 1;
}
