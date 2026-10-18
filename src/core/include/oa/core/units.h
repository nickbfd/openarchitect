#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace oa {

// All lengths in the model are stored in inches; areas in square feet only
// where a value is reported to the user. Loads use pounds and feet (psf, plf)
// because that is how US residential practice states them.
constexpr double kInchesPerFoot = 12.0;
constexpr double kSqInPerSqFt = 144.0;

inline constexpr double feet(double ft) { return ft * kInchesPerFoot; }
inline constexpr double toFeet(double inches) { return inches / kInchesPerFoot; }

// Parses lengths the way people type them in US drafting:
//   12'  12'6"  12' 6 1/2"  12'-6"  150"  150  6 1/2"  12.5'  -3"
// A bare number is read as inches. Returns nullopt when the text is not a length.
std::optional<double> parseLength(std::string_view text);

// Formats inches as feet and inches rounded to 1/denominator of an inch,
// for example 150.5 -> 12'-6 1/2".
std::string formatFeetInches(double inches, int denominator = 16);

} // namespace oa
