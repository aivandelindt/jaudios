#pragma once

#include <array>

namespace jaudios::analysis::profiles
{

// Pitch-class profiles, index 0 = tonic. Correlated with a chroma vector rotated to each tonic.

// Krumhansl & Kessler (1982) probe-tone ratings.
inline constexpr std::array<double, 12> krumhanslMajor { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
inline constexpr std::array<double, 12> krumhanslMinor { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

// Temperley (1999), Kostka-Payne corpus-derived profiles.
inline constexpr std::array<double, 12> temperleyMajor { 5.0, 2.0, 3.5, 2.0, 4.5, 4.0, 2.0, 4.5, 2.0, 3.5, 1.5, 4.0 };
inline constexpr std::array<double, 12> temperleyMinor { 5.0, 2.0, 3.5, 4.5, 2.0, 4.0, 2.0, 4.5, 3.5, 2.0, 1.5, 4.0 };

} // namespace jaudios::analysis::profiles
