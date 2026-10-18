// Shared by S0 and S1: flat lists of rectangles / lines packed into 255-word chain nodes.
#pragma once
#include <stdint.h>

#include "psyqo/fragments.hh"
#include "psyqo/gpu.hh"
#include "psyqo/primitives/triangles.hh"
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

// Streaks are thin flat triangles: GP0 line primitives in a chain stalled the DMA on silicon (SCPH-5501 #3 and
// SCPH-7001 #5, every variant with lines hung and the same build without them ran on) while Redux drew them.
template <unsigned CAP>
struct TriList {
    static constexpr unsigned PER = 60;  // 4 words each, 240 per node
    psyqo::Fragments::FixedFragment<psyqo::Prim::Triangle, PER> frag[(CAP + PER - 1) / PER];
    unsigned n = 0;
    void reset() { n = 0; }
    bool add(uint32_t a, uint32_t b, uint32_t c, psyqo::Color col) {
        if (n >= CAP) return false;
        auto& p = frag[n / PER].primitives[n % PER];
        n++;
        p.setColor(col);
        p.pointA.packed = a;
        p.pointB.packed = b;
        p.pointC.packed = c;
        return true;
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

}  // namespace demo

#include "common/syscalls/syscalls.h"
#include "demo.hh"

namespace demo {

// Frames rendered and vsyncs elapsed per bar, printed on the serial port: a bar is 102.9 frames at 60 fps.
struct BarStat {
    uint32_t frames = 0, vsyncs = 0, lastFrames = 0, lastVsyncs = 0;
    bool primed = false;
    void sample(Frame& f) {
        frames = f.frameNo;
        vsyncs = f.gpu.getFrameCount();
    }
    void bar(const char* tag, int n) {
        if (primed) ramsyscall_printf("%s bar %d frames %d vsyncs %d\n", tag, n, int(frames - lastFrames), int(vsyncs - lastVsyncs));
        primed = true;
        lastFrames = frames;
        lastVsyncs = vsyncs;
    }
};

}  // namespace demo
