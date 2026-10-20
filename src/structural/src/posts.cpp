#include "oa/structural/posts.h"

#include <cmath>

namespace oa::structural {

const std::vector<PostMember>& postCatalog() {
    // NDS Supplement Table 4A (dimension lumber, CF for Fc of 1.15 at 4" and
    // 1.1 at 6" wide) and Table 4D (posts and timbers).
    static const std::vector<PostMember> catalog = {
        {"dfl-no2-4x4", "4x4 DF-L No.2", 3.5, 3.5, 1350 * 1.15, 1.6e6, 580e3, 625},
        {"dfl-no2-4x6", "4x6 DF-L No.2", 3.5, 5.5, 1350 * 1.1, 1.6e6, 580e3, 625},
        {"dfl-no2-6x6", "6x6 DF-L No.2", 5.5, 5.5, 700, 1.3e6, 470e3, 625},
        {"dfl-no1-6x6", "6x6 DF-L No.1", 5.5, 5.5, 1000, 1.6e6, 580e3, 625},
        {"dfl-no1-8x8", "8x8 DF-L No.1", 7.5, 7.5, 1000, 1.6e6, 580e3, 625},
    };
    return catalog;
}

const PostMember* findPost(const std::string& id) {
    for (const auto& p : postCatalog())
        if (p.id == id) return &p;
    return nullptr;
}

PostCheck checkPost(const PostMember& post, const CaseLoads& load, double heightInches) {
    PostCheck out;
    out.member = &post;
    out.slenderness = heightInches / post.b;
    constexpr double c = 0.8; // sawn lumber
    const double FcE = 0.822 * post.Emin / (out.slenderness * out.slenderness);
    for (const auto& combo : asdGravityCombinations()) {
        const double demand = combo.apply(load);
        if (demand <= 0.0) continue;
        const double FcStar = post.Fc * combo.durationFactor;
        const double alpha = FcE / FcStar;
        const double k = (1.0 + alpha) / (2.0 * c);
        const double Cp = k - std::sqrt(k * k - alpha / c);
        // Bearing of the post end on wood beneath it can also govern.
        const double capacity = std::min(FcStar * Cp * post.area(), post.FcPerp * post.area());
        const double ratio = demand / capacity;
        if (ratio > out.ratio || out.combo.empty()) {
            out.ratio = ratio;
            out.demand = demand;
            out.capacity = capacity;
            out.combo = combo.name;
        }
    }
    out.passes = out.ratio <= 1.0 && out.slenderness <= 50.0;
    return out;
}

const PostMember* recommendPost(const CaseLoads& load, double heightInches) {
    for (const auto& p : postCatalog())
        if (checkPost(p, load, heightInches).passes) return &p;
    return nullptr;
}

} // namespace oa::structural
