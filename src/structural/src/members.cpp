#include "oa/structural/members.h"

#include <cmath>
#include <map>
#include <sstream>

namespace oa::structural {
namespace {

struct Species {
    const char* key;
    const char* label;
    double Fb, Fv, FcPerp, E, Emin;
    bool sizeSpecific; // Southern Pine publishes values per width; CF is already included
};

// NDS Supplement (2018) Table 4A / 4B, visually graded dimension lumber,
// 2" to 4" thick. Southern Pine Fb is given per nominal width below.
constexpr Species kDimension[] = {
    {"dfl-no2", "DF-L No.2", 900, 180, 625, 1.6e6, 580000, false},
    {"dfl-no1", "DF-L No.1", 1000, 180, 625, 1.7e6, 620000, false},
    {"syp-no2", "SYP No.2", 0, 175, 565, 1.4e6, 510000, true},
    {"spf-no2", "SPF No.1/No.2", 875, 135, 425, 1.4e6, 510000, false},
    {"hf-no2", "Hem-Fir No.2", 850, 150, 405, 1.3e6, 470000, false},
};

struct Size {
    int nominal;
    double actual;
};
constexpr Size kWidths[] = {{6, 5.5}, {8, 7.25}, {10, 9.25}, {12, 11.25}};

// NDS Supplement Table 4A size factor CF for Fb, grades No.2 and better.
double sizeFactor2x(int nominal) {
    switch (nominal) {
    case 6: return 1.3;
    case 8: return 1.2;
    case 10: return 1.1;
    default: return 1.0;
    }
}
double sizeFactor4x(int nominal) {
    switch (nominal) {
    case 6: return 1.3;
    case 8: return 1.3;
    case 10: return 1.2;
    default: return 1.1;
    }
}
// NDS Supplement Table 4B, Southern Pine No.2.
double sypNo2Fb(int nominal) {
    switch (nominal) {
    case 6: return 1000;
    case 8: return 925;
    case 10: return 800;
    default: return 750;
    }
}

std::string fmt(double inches) {
    std::ostringstream ss;
    const double whole = std::floor(inches + 1e-9);
    const double frac = inches - whole;
    ss << whole;
    if (std::abs(frac - 0.125) < 1e-6) ss << "-1/8";
    else if (std::abs(frac - 0.25) < 1e-6) ss << "-1/4";
    else if (std::abs(frac - 0.375) < 1e-6) ss << "-3/8";
    else if (std::abs(frac - 0.5) < 1e-6) ss << "-1/2";
    else if (std::abs(frac - 0.75) < 1e-6) ss << "-3/4";
    else if (std::abs(frac - 0.875) < 1e-6) ss << "-7/8";
    return ss.str();
}

std::vector<Member> buildCatalog() {
    std::vector<Member> out;

    for (const auto& sp : kDimension) {
        for (const auto& w : kWidths) {
            for (int plies = 1; plies <= 4; ++plies) {
                Member m;
                m.id = std::string(sp.key) + "-" + std::to_string(plies) + "-2x" + std::to_string(w.nominal);
                m.name = (plies > 1 ? "(" + std::to_string(plies) + ") " : std::string()) + "2x" +
                         std::to_string(w.nominal) + " " + sp.label;
                m.kind = MemberKind::Sawn;
                m.b = 1.5 * plies;
                m.d = w.actual;
                m.plies = plies;
                m.Fb = sp.sizeSpecific ? sypNo2Fb(w.nominal) : sp.Fb;
                m.Fv = sp.Fv;
                m.FcPerp = sp.FcPerp;
                m.E = sp.E;
                m.Emin = sp.Emin;
                m.sizeFactor = sp.sizeSpecific ? 1.0 : sizeFactor2x(w.nominal);
                out.push_back(m);
            }
        }
    }

    // DF-L No.2 4x members (still dimension lumber in the NDS).
    for (const auto& w : kWidths) {
        Member m;
        m.id = "dfl-no2-1-4x" + std::to_string(w.nominal);
        m.name = "4x" + std::to_string(w.nominal) + " DF-L No.2";
        m.kind = MemberKind::Sawn;
        m.b = 3.5;
        m.d = w.actual;
        m.Fb = 900; m.Fv = 180; m.FcPerp = 625; m.E = 1.6e6; m.Emin = 580000;
        m.sizeFactor = sizeFactor4x(w.nominal);
        out.push_back(m);
    }

    // DF-L No.2 beams and stringers, NDS Supplement Table 4D.
    constexpr Size kTimberDepths[] = {{8, 7.5}, {10, 9.5}, {12, 11.5}, {14, 13.5}};
    for (const auto& t : kTimberDepths) {
        Member m;
        m.id = "dfl-no2-1-6x" + std::to_string(t.nominal);
        m.name = "6x" + std::to_string(t.nominal) + " DF-L No.2";
        m.kind = MemberKind::Timber;
        m.b = 5.5;
        m.d = t.actual;
        m.Fb = 875; m.Fv = 170; m.FcPerp = 625; m.E = 1.3e6; m.Emin = 470000;
        m.sizeFactor = t.actual > 12.0 ? std::pow(12.0 / t.actual, 1.0 / 9.0) : 1.0;
        out.push_back(m);
    }

    // Douglas Fir 24F-V4 glulam, NDS Supplement Table 5A, 1-1/2" laminations.
    for (double b : {3.125, 3.5, 5.125, 5.5, 6.75}) {
        for (double d = 6.0; d <= 24.0 + 1e-9; d += 1.5) {
            Member m;
            m.id = "glulam-24fv4-" + fmt(b) + "x" + fmt(d);
            m.name = fmt(b) + " x " + fmt(d) + " 24F-V4 DF glulam";
            m.kind = MemberKind::Glulam;
            m.b = b;
            m.d = d;
            m.Fb = 2400; m.Fv = 265; m.FcPerp = 650; m.E = 1.8e6; m.Emin = 950000;
            out.push_back(m);
        }
    }

    // Generic 2.0E LVL in 1-3/4" plies. Verify against the chosen
    // manufacturer's published values before construction.
    for (double d : {7.25, 9.25, 9.5, 11.25, 11.875, 14.0, 16.0, 18.0}) {
        for (int plies = 1; plies <= 4; ++plies) {
            Member m;
            m.id = "lvl-2.0e-" + std::to_string(plies) + "-" + fmt(d);
            m.name = (plies > 1 ? "(" + std::to_string(plies) + ") " : std::string()) + "1-3/4 x " +
                     fmt(d) + " LVL 2.0E";
            m.kind = MemberKind::Lvl;
            m.b = 1.75 * plies;
            m.d = d;
            m.plies = plies;
            m.Fb = 2600; m.Fv = 285; m.FcPerp = 750; m.E = 2.0e6; m.Emin = 1016000;
            m.densityPcf = 42.0;
            out.push_back(m);
        }
    }
    return out;
}

} // namespace

const char* toString(MemberKind k) {
    switch (k) {
    case MemberKind::Sawn: return "Sawn lumber";
    case MemberKind::Timber: return "Timber";
    case MemberKind::Glulam: return "Glulam";
    case MemberKind::Lvl: return "LVL";
    }
    return "";
}

const std::vector<Member>& defaultCatalog() {
    static const std::vector<Member> catalog = buildCatalog();
    return catalog;
}

const Member* findMember(const std::string& id) {
    static const std::map<std::string, const Member*> index = [] {
        std::map<std::string, const Member*> m;
        for (const auto& member : defaultCatalog()) m[member.id] = &member;
        return m;
    }();
    auto it = index.find(id);
    return it == index.end() ? nullptr : it->second;
}

} // namespace oa::structural
