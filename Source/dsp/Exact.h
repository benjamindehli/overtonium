#pragma once

namespace ovt {

/// Bit-for-bit equality, for "has this changed since the last block" checks
/// and exact neutral values, where a tolerance would leave a coefficient stale
/// or move saved presets.
///
/// Written as two comparisons so the float-equality warning can stay on
/// elsewhere. Same as == for anything that is not a NaN.
template <typename T> constexpr bool exactly(T a, T b) noexcept {
  return !(a < b) && !(b < a);
}

} // namespace ovt
