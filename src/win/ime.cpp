#include "ime.h"

#include <imm.h>

#include <cwchar>

namespace geditor::win {

void place_ime_windows(HWND hwnd, POINT caret, int line_height, const wchar_t* font_family,
                       int font_height) {
  HIMC imc = ImmGetContext(hwnd);
  if (imc == nullptr) return;
  LOGFONTW font{};
  font.lfHeight = -font_height;
  font.lfCharSet = DEFAULT_CHARSET;
  wcsncpy_s(font.lfFaceName, font_family, _TRUNCATE);
  ImmSetCompositionFont(imc, &font);

  COMPOSITIONFORM composition{};
  composition.dwStyle = CFS_POINT;
  composition.ptCurrentPos = caret;
  ImmSetCompositionWindow(imc, &composition);

  CANDIDATEFORM candidate{};
  candidate.dwIndex = 0;
  candidate.dwStyle = CFS_EXCLUDE;
  candidate.ptCurrentPos = caret;
  candidate.rcArea = RECT{caret.x, caret.y, caret.x + 1, caret.y + line_height};
  ImmSetCandidateWindow(imc, &candidate);
  ImmReleaseContext(hwnd, imc);
}

bool take_ime_result(HWND hwnd, std::wstring& text) {
  text.clear();
  HIMC imc = ImmGetContext(hwnd);
  if (imc == nullptr) return false;
  const LONG bytes = ImmGetCompositionStringW(imc, GCS_RESULTSTR, nullptr, 0);
  if (bytes > 0) {
    text.resize(static_cast<size_t>(bytes) / sizeof(wchar_t));
    ImmGetCompositionStringW(imc, GCS_RESULTSTR, text.data(), static_cast<DWORD>(bytes));
  }
  ImmReleaseContext(hwnd, imc);
  return !text.empty();
}

}  // namespace geditor::win
