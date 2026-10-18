// The recap scene (S7, bars 97-112) and its run-out (S8, bars 113-118) share one scene: a starfield,
// the S4 ball field, a copper strip with the greets scroller, and the radial flashes. S7 begins it,
// S8 keeps running it until the layers have dropped out, then draws the credits and the end itself.
#pragma once

#include <stdint.h>

#include "demo.hh"
#include "psyqo/fragments.hh"
#include "psyqo/primitives/misc.hh"
#include "psyqo/primitives/quads.hh"

namespace demo {

constexpr uint32_t barPos(int b) { return uint32_t(b - 1) * 256; }  // clock.pos256 at the start of a bar

class RecapScene {
  public:
    virtual ~RecapScene() = default;
    // (Re)start the scene at this clock. Called by S7, and by S8 when the demo is started inside it.
    virtual void begin(const Clock& clock) = 0;
    virtual bool begun() const = 0;
    virtual void onNote(const Clock& clock, unsigned channel, uint8_t note, uint8_t velocity, uint32_t tick) = 0;
    virtual void update(const Clock& clock) = 0;
    // Everything into the chain, clear included.
    virtual void render(Frame& frame) = 0;
};

RecapScene& recapScene();

// Textured 8x16 system-font glyphs (uppercase, digits and punctuation only: S4's CLUT overwrites the
// font's third row), drawn as a shadow layer under a glyph layer, all through the chain.
template <unsigned N>
struct GlyphBatch {
    struct Pro {
        psyqo::Prim::VRAMUpload upload;
        uint32_t pixel;
        psyqo::Prim::FlushCache flush;
        psyqo::Prim::TPage tpage;
    };
    using TQuad = psyqo::Fragments::SimpleFragment<psyqo::Prim::TexturedQuad>;
    psyqo::Fragments::SimpleFragment<Pro> pro;
    TQuad shadow[N], glyph[N];
    unsigned n = 0;

    void reset() { n = 0; }
    bool add(int x, int y, int w, int h, char ch, psyqo::Color c, psyqo::Color sh) {
        if (ch <= ' ' || n >= N) return false;
        psyqo::PrimPieces::TPageAttr a;
        a.setPageX(960 >> 6).setPageY(464 >> 8).set(psyqo::Prim::TPageAttr::Tex4Bits).setDithering(false).enableDisplayArea();
        unsigned idx = unsigned(ch - 32);
        uint8_t u0 = uint8_t(idx * 8), v0 = uint8_t(208 + 16 * (idx / 32));
        uint8_t u1 = uint8_t(u0 + 7);  // last texel of the cell; u0 + 8 is the next glyph
        uint8_t v1 = uint8_t(v0 + 15);
        for (int layer = 0; layer < 2; layer++) {
            auto& q = (layer ? glyph : shadow)[n].primitive;
            int o = layer ? 0 : 2;
            q.setColor(layer ? c : sh);
            q.pointA = {{.x = int16_t(x + o), .y = int16_t(y + o)}};
            q.pointB = {{.x = int16_t(x + o + w), .y = int16_t(y + o)}};
            q.pointC = {{.x = int16_t(x + o), .y = int16_t(y + o + h)}};
            q.pointD = {{.x = int16_t(x + o + w), .y = int16_t(y + o + h)}};
            q.uvA = {u0, v0};
            q.uvB = {u1, v0};
            q.uvC = {u0, v1};
            q.uvD = {u1, v1};
            q.clutIndex = psyqo::PrimPieces::ClutIndex(psyqo::Vertex{{.x = 960, .y = 464}});
            q.tpage = a;
        }
        n++;
        return true;
    }
    template <class Frag>
    static void run(psyqo::GPU& gpu, Frag* a, unsigned count) {
        constexpr uintptr_t words = sizeof(typename Frag::FragmentBaseType) / sizeof(uint32_t);
        for (unsigned i = 0; i + 1 < count; i++) a[i].head = (words << 24) | (reinterpret_cast<uintptr_t>(&a[i + 1]) & 0xffffff);
        gpu.chain(&a[0], &a[count - 1]);
    }
    void chain(psyqo::GPU& gpu) {
        if (n == 0) return;
        auto& p = pro.primitive;
        p.upload.region = psyqo::Rect{960, 464, 2, 1};
        p.pixel = 0x7fff0000;
        psyqo::PrimPieces::TPageAttr a;
        a.setPageX(960 >> 6).setPageY(464 >> 8).set(psyqo::Prim::TPageAttr::Tex4Bits).setDithering(false).enableDisplayArea();
        p.tpage.attr = a;
        gpu.chain(pro);
        run(gpu, shadow, n);
        run(gpu, glyph, n);
    }
};

}  // namespace demo
