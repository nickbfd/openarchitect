#pragma once

#include "oa/structural/loads.h"

#include <string>
#include <vector>

namespace oa::structural {

// A solid sawn post with NDS Supplement reference values (psi). Sizes are
// actual (dressed) inches; `b` is the least dimension, the one it buckles about.
struct PostMember {
    std::string id;   // stable key stored in plan files, e.g. "dfl-no2-4x4"
    std::string name; // e.g. "4x4 DF-L No.2"
    double b = 0.0;
    double d = 0.0;
    double Fc = 0.0;   // compression parallel to grain, with the size factor applied
    double E = 0.0;
    double Emin = 0.0;
    double FcPerp = 625.0;

    double area() const { return b * d; }
};

// 4x4, 4x6, 6x6 and 8x8 Douglas Fir-Larch posts, smallest first.
const std::vector<PostMember>& postCatalog();
const PostMember* findPost(const std::string& id);

// NDS 3.6 and 3.7 column check for an axial load: pinned ends (Ke = 1),
// unbraced over its full height, dry service, CD by load combination.
struct PostCheck {
    const PostMember* member = nullptr;
    double demand = 0.0;     // lb, under the governing combination
    double capacity = 0.0;   // lb, allowable under that combination
    double ratio = 0.0;      // worst demand over capacity across combinations
    double slenderness = 0.0; // le / b; NDS limits it to 50
    std::string combo;
    bool passes = false;
};
PostCheck checkPost(const PostMember& post, const CaseLoads& load, double heightInches);
// The smallest catalog post that passes, or nullptr when none do.
const PostMember* recommendPost(const CaseLoads& load, double heightInches);

} // namespace oa::structural
