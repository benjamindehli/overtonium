#pragma once

namespace ovt {

/// Exact equality, and meant.
///
/// Two kinds of comparison in here want bit-for-bit equality rather than a
/// tolerance. One is "has this changed since the last block", which decides
/// whether a set of coefficients has to be worked out again, and a tolerance
/// there leaves a coefficient stale for any change smaller than it. The other
/// is an exact neutral: zero stretch has to be the plain harmonic series to
/// the last bit, or every preset ever saved moves underneath itself.
///
/// Written as two comparisons because the warning that catches an accidental
/// float comparison fires on == whatever the intent, and that warning is worth
/// keeping switched on for the places that did not mean it. Says the same
/// thing as == for everything that is not a NaN, and none of these are.
template <typename T> constexpr bool exactly(T a, T b) noexcept {
  return !(a < b) && !(b < a);
}

} // namespace ovt
