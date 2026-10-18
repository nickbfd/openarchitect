#include "oa/core/units.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <numeric>

namespace oa {
namespace {

struct Cursor {
    std::string_view s;
    size_t i = 0;

    void skipSpace() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
    }
    bool done() const { return i >= s.size(); }
    char peek() const { return done() ? '\0' : s[i]; }
    bool eat(char c) {
        skipSpace();
        if (peek() == c) {
            ++i;
            return true;
        }
        return false;
    }

    // Reads a non-negative decimal number.
    std::optional<double> number() {
        skipSpace();
        size_t start = i;
        bool digits = false;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            ++i;
            digits = true;
        }
        if (i < s.size() && s[i] == '.') {
            ++i;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
                ++i;
                digits = true;
            }
        }
        if (!digits) {
            i = start;
            return std::nullopt;
        }
        return std::strtod(std::string(s.substr(start, i - start)).c_str(), nullptr);
    }

    // Reads "a", "a b/c" or "b/c".
    std::optional<double> mixedNumber() {
        size_t start = i;
        auto whole = number();
        if (!whole) return std::nullopt;
        if (eat('/')) {
            auto den = number();
            if (!den || *den == 0.0) {
                i = start;
                return std::nullopt;
            }
            return *whole / *den;
        }
        size_t afterWhole = i;
        skipSpace();
        if (auto num = number()) {
            if (eat('/')) {
                if (auto den = number(); den && *den != 0.0)
                    return *whole + *num / *den;
            }
        }
        i = afterWhole;
        return whole;
    }
};

} // namespace

std::optional<double> parseLength(std::string_view text) {
    Cursor c{text};
    c.skipSpace();
    if (c.done()) return std::nullopt;

    double sign = 1.0;
    if (c.eat('-')) sign = -1.0;
    else c.eat('+');

    auto first = c.mixedNumber();
    if (!first) return std::nullopt;

    double inches = 0.0;
    if (c.eat('\'')) {
        inches = feet(*first);
        c.eat('-');
        c.skipSpace();
        if (!c.done()) {
            auto rest = c.mixedNumber();
            if (!rest) return std::nullopt;
            inches += *rest;
            c.eat('"');
        }
    } else {
        inches = *first;
        c.eat('"');
    }

    c.skipSpace();
    if (!c.done()) return std::nullopt;
    return sign * inches;
}

std::string formatFeetInches(double inches, int denominator) {
    if (denominator < 1) denominator = 1;
    const bool negative = inches < 0;
    long long ticks = std::llround(std::abs(inches) * denominator);
    const long long perFoot = 12LL * denominator;
    long long ft = ticks / perFoot;
    ticks %= perFoot;
    long long whole = ticks / denominator;
    long long frac = ticks % denominator;

    std::string out = negative && (ft || ticks) ? "-" : "";
    out += std::to_string(ft) + "'-" + std::to_string(whole);
    if (frac) {
        long long g = std::gcd(frac, static_cast<long long>(denominator));
        out += " " + std::to_string(frac / g) + "/" + std::to_string(denominator / g);
    }
    out += "\"";
    return out;
}

} // namespace oa
