#include "clipboard.h"

#include <cstring>

namespace geditor::win {
namespace {

// Another program may hold the clipboard for a moment: retry briefly.
bool open_clipboard(HWND owner) {
  for (int attempt = 0; attempt < 10; ++attempt) {
    if (OpenClipboard(owner)) return true;
    Sleep(10);
  }
  return false;
}

}  // namespace

std::wstring to_clipboard_text(std::wstring_view text) {
  std::wstring out;
  out.reserve(text.size() + text.size() / 16);
  for (const wchar_t c : text) {
    if (c == L'\n') out += L'\r';
    out += c;
  }
  return out;
}

std::wstring from_clipboard_text(const wchar_t* data, size_t max_units) {
  std::wstring out;
  if (data == nullptr) return out;
  size_t n = 0;
  while (n < max_units && data[n] != L'\0') ++n;
  out.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    if (data[i] == L'\r') {
      out += L'\n';
      if (i + 1 < n && data[i + 1] == L'\n') ++i;
    } else {
      out += data[i];
    }
  }
  return out;
}

ClipboardRead read_clipboard(HWND owner, std::wstring& text) {
  text.clear();
  if (!open_clipboard(owner)) return ClipboardRead::kFailed;
  ClipboardRead result = ClipboardRead::kNoText;
  if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
    result = ClipboardRead::kFailed;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
      if (const auto* p = static_cast<const wchar_t*>(GlobalLock(data))) {
        text = from_clipboard_text(p, GlobalSize(data) / sizeof(wchar_t));
        GlobalUnlock(data);
        result = ClipboardRead::kOk;
      }
    }
  }
  CloseClipboard();
  return result;
}

bool write_clipboard(HWND owner, const std::wstring& text) {
  const std::wstring data = to_clipboard_text(text);
  const size_t bytes = (data.size() + 1) * sizeof(wchar_t);
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (memory == nullptr) return false;
  void* p = GlobalLock(memory);
  if (p == nullptr) {
    GlobalFree(memory);
    return false;
  }
  std::memcpy(p, data.c_str(), bytes);
  GlobalUnlock(memory);
  if (!open_clipboard(owner)) {
    GlobalFree(memory);
    return false;
  }
  bool ok = false;
  if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory) != nullptr) {
    ok = true;  // the clipboard owns the memory now
  }
  CloseClipboard();
  if (!ok) GlobalFree(memory);
  return ok;
}

}  // namespace geditor::win
