#pragma once

#include <string>
#include <vector>

namespace oa::structural {

enum class MemberKind {
    Sawn,   // dimension lumber, 2" to 4" thick, single or built-up plies
    Timber, // beams and stringers, 5" and thicker
    Glulam,
    Lvl,
};
const char* toString(MemberKind k);

// A beam or header section with reference design values (psi) from the NDS
// Supplement for sawn lumber and glulam, and generic values for LVL.
// Dimensions are actual (dressed) sizes in inches; `b` is the total width
// of all plies.
struct Member {
    std::string id;       // stable key stored in plan files, e.g. "dfl-no2-2-2x10"
    std::string name;     // what people read, e.g. "(2) 2x10 DF-L No.2"
    MemberKind kind = MemberKind::Sawn;
    double b = 0.0;
    double d = 0.0;
    int plies = 1;

    double Fb = 0.0;      // bending
    double Fv = 0.0;      // shear parallel to grain
    double FcPerp = 0.0;  // compression perpendicular to grain (bearing)
    double E = 0.0;       // modulus of elasticity
    double Emin = 0.0;    // for stability
    double sizeFactor = 1.0; // CF for sawn lumber, already chosen for this size
    double densityPcf = 35.0;

    double area() const { return b * d; }
    double sectionModulus() const { return b * d * d / 6.0; }
    double momentOfInertia() const { return b * d * d * d / 12.0; }
    double selfWeightPlf() const { return densityPcf * area() / 144.0; }
};

// The built-in catalog: DF-L, SYP, SPF and Hem-Fir dimension lumber in
// 1 to 4 plies, DF-L 4x and 6x members, DF 24F-V4 glulam, and generic 2.0E LVL.
// Manufacturer-specific engineered lumber values belong in importable
// libraries, since they differ by brand and are published per product.
const std::vector<Member>& defaultCatalog();
const Member* findMember(const std::string& id);

} // namespace oa::structural
