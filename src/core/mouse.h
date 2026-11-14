// The mouse wheel of decision B5: wheel deltas to screen rows. The positions
// of clicks and drags map through WrapLayout (Editor::offset_at_screen).
#pragma once

namespace geditor {

// One wheel notch (WHEEL_DELTA of Windows).
inline constexpr int kWheelDelta = 120;

// Turns wheel deltas into screen rows, keeping the part of a notch that has
// not scrolled a whole row yet (high-resolution wheels and touchpads send
// deltas smaller than a notch). A turn the other way drops what was kept.
class WheelAccumulator {
 public:
  // `delta`: positive turns the wheel away from the user (the text scrolls
  // towards its start); `rows_per_notch`: rows for kWheelDelta (at least 1).
  // Returns the rows to scroll: negative towards the start of the text.
  long long add(int delta, int rows_per_notch);
  void reset() { kept_ = 0; }

 private:
  long long kept_ = 0;  // delta * rows_per_notch that has not scrolled yet
};

}  // namespace geditor
