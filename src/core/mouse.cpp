#include "mouse.h"

#include <algorithm>

namespace geditor {

long long WheelAccumulator::add(int delta, int rows_per_notch) {
  if (delta == 0) return 0;
  if ((delta > 0) != (kept_ > 0) && kept_ != 0) kept_ = 0;
  kept_ += static_cast<long long>(delta) * std::max(1, rows_per_notch);
  const long long rows = kept_ / kWheelDelta;  // towards zero
  kept_ -= rows * kWheelDelta;
  return -rows;
}

}  // namespace geditor
