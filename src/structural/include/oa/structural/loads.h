#pragma once

#include <array>
#include <string>
#include <vector>

namespace oa::structural {

// Load cases follow ASCE 7 notation. Values are always stored unfactored,
// per case; combinations are applied only when a member is checked.
enum class LoadCase { Dead, Live, RoofLive, Snow };
constexpr int kCaseCount = 4;
constexpr std::array<LoadCase, kCaseCount> kAllCases = {LoadCase::Dead, LoadCase::Live,
                                                        LoadCase::RoofLive, LoadCase::Snow};
const char* shortName(LoadCase c); // "D", "L", "Lr", "S"

struct CaseLoads {
    std::array<double, kCaseCount> v{};

    double& operator[](LoadCase c) { return v[static_cast<size_t>(c)]; }
    double operator[](LoadCase c) const { return v[static_cast<size_t>(c)]; }
    CaseLoads operator+(const CaseLoads& o) const;
    CaseLoads operator-(const CaseLoads& o) const;
    CaseLoads operator*(double s) const;
    CaseLoads& operator+=(const CaseLoads& o);
    bool isZero() const;

    static CaseLoads of(double d, double l = 0, double lr = 0, double s = 0) { return {{d, l, lr, s}}; }
};

// A load combination with the NDS load duration factor CD for the
// shortest-duration load in it.
struct Combination {
    std::string name;
    std::array<double, kCaseCount> factors{};
    double durationFactor = 1.0;

    double apply(const CaseLoads& c) const;
    // The same combination without its dead load part, for live deflection.
    double applyLiveOnly(const CaseLoads& c) const;
};

// ASCE 7 allowable stress design combinations for gravity loads (2.4.1, 1 to 4).
const std::vector<Combination>& asdGravityCombinations();

// A distributed load along a wall or beam, varying linearly from start to
// end. Positions are inches from the member's start; intensities are plf.
struct LineLoad {
    double start = 0.0;
    double end = 0.0;
    CaseLoads wStart;
    CaseLoads wEnd;
    std::string source;

    double length() const { return end - start; }
    CaseLoads at(double s) const; // zero outside [start, end]
    // The part of this load between a and b, or a zero-length load if none.
    LineLoad clipped(double a, double b) const;
    CaseLoads total() const;      // pounds
};

// A concentrated load in pounds at a position in inches from the member's start.
struct PointLoad {
    double position = 0.0;
    CaseLoads p;
    std::string source;
};

// Peak plf per case and the largest combined plf along a set of line loads.
struct LineLoadSummary {
    CaseLoads peakPlf;
    double governingPlf = 0.0;
    std::string governingCombo;
};
LineLoadSummary summarize(const std::vector<LineLoad>& loads);

} // namespace oa::structural
