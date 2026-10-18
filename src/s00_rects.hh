// Shared by S0 and S1: flat lists of rectangles / lines packed into 255-word chain nodes.
#pragma once
#include <stdint.h>

#include "psyqo/fragments.hh"
#include "psyqo/gpu.hh"
#include "psyqo/primitives/lines.hh"
#include "psyqo/primitives/rectangles.hh"

namespace demo {

inline psyqo::Color rgb(int r, int g, int b) {
    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (b < 0) b = 0;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return psyqo::Color{{.r = uint8_t(r), .g = uint8_t(g), .b = uint8_t(b)}};
}

// 80 three-word primitives = 240 words, under the 255-word node limit.
template <typename Prim, unsigned CAP>
struct PrimList {
    static constexpr unsigned PER = 80;
    psyqo::Fragments::FixedFragment<Prim, PER> frag[(CAP + PER - 1) / PER];
    unsigned n = 0;
    void reset() { n = 0; }
    Prim* next() {
        if (n >= CAP) return nullptr;
        Prim* p = &frag[n / PER].primitives[n % PER];
        n++;
        return p;
    }
    void chain(psyqo::GPU& gpu) {
        unsigned full = n / PER, rem = n % PER;
        for (unsigned i = 0; i < full; i++) {
            frag[i].count = PER;
            gpu.chain(frag[i]);
        }
        if (rem) {
            frag[full].count = rem;
            gpu.chain(frag[full]);
        }
    }
};

template <unsigned CAP>
struct RectList : PrimList<psyqo::Prim::Rectangle, CAP> {
    bool add(int x, int y, int w, int h, psyqo::Color c) {
        auto* p = this->next();
        if (!p) return false;
        p->setColor(c);
        p->position = {{.x = int16_t(x), .y = int16_t(y)}};
        p->size = {{.x = int16_t(w), .y = int16_t(h)}};
        return true;
    }
};

template <unsigned CAP>
struct LineList : PrimList<psyqo::Prim::Line, CAP> {
    bool add(uint32_t a, uint32_t b, psyqo::Color c) {
        auto* p = this->next();
        if (!p) return false;
        p->setColor(c);
        p->pointA.packed = a;
        p->pointB.packed = b;
        return true;
    }
};

}  // namespace demo
