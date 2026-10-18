// Copper bars, shared by S2 (COPPER_SCROLLER) and S3 (ROTOZOOM).
//
// One engine computes the bar layout from the music clock and emits two renderings of the
// same bars: the full-size one on screen, and a half-size one into an offscreen VRAM page
// (TEX_X, TEX_Y, 128x128) which S3 uses as its texture. Coordinates are kept in half-pixel
// units so the on-screen bars are exactly the texture doubled.
#pragma once

#include <stdint.h>

#include "demo.hh"
#include "handoff.hh"
#include "psyqo/fragments.hh"
#include "psyqo/primitives/common.hh"
#include "psyqo/primitives/control.hh"
#include "psyqo/primitives/misc.hh"
#include "psyqo/primitives/quads.hh"
#include "psyqo/primitives/rectangles.hh"

namespace demo::cu {

// VRAM map (display buffers: 0..319 x 0..239 and 0..319 x 256..495; system font: 960,464).
constexpr int TEX_X = 384, TEX_Y = 0, TEX_W = 128, TEX_H = 128;  // texture page 6, window 128
constexpr int SCR_X = 512, SCR_Y = 0;                            // scratch for the zap tear

constexpr uint32_t bar(int b) { return uint32_t(b - 1) * 256; }  // clock.pos256 at the start of a bar

// First bar whose frames also render the offscreen page.
constexpr int TEX_FIRST_BAR = 31;

constexpr unsigned NB = 16;  // bars per layer

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
// smoothstep on 0..256
inline int smooth(int x) {
    x = clampi(x, 0, 256);
    return (x * x * (768 - 2 * x)) >> 16;
}

inline psyqo::Color rgb(int r, int g, int b) {
    return psyqo::Color{{.r = uint8_t(clampi(r, 0, 255)), .g = uint8_t(clampi(g, 0, 255)), .b = uint8_t(clampi(b, 0, 255))}};
}
// a toward b, t in 0..256
inline psyqo::Color mix(psyqo::Color a, psyqo::Color b, int t) {
    return rgb(a.r + (((int(b.r) - a.r) * t) >> 8), a.g + (((int(b.g) - a.g) * t) >> 8),
               a.b + (((int(b.b) - a.b) * t) >> 8));
}
inline psyqo::Color scale(psyqo::Color a, int k) {  // k/256
    return rgb((a.r * k) >> 8, (a.g * k) >> 8, (a.b * k) >> 8);
}

inline int sinq(psyqo::Trig<>& trig, int a) { return trig.sin(psyqo::Angle(a & 2047, psyqo::Angle::RAW)).raw(); }
inline int cosq(psyqo::Trig<>& trig, int a) { return trig.cos(psyqo::Angle(a & 2047, psyqo::Angle::RAW)).raw(); }

// 0..256 while the S2 -> S3 fold is closing (bar 32), then opening again after the bar 33 crash.
inline int foldP(uint32_t pos) {
    if (pos < bar(32)) return 0;
    if (pos < bar(33)) return smooth(int(pos - bar(32)));
    if (pos < bar(33) + 192) return 256 - smooth(int((pos - bar(33)) * 256 / 192));
    return 0;
}

using GQuad = psyqo::Fragments::SimpleFragment<psyqo::Prim::GouraudQuad>;

// Link n contiguous single-primitive fragments into one run and queue it on the next chain.
template <class Frag>
inline void chainRun(psyqo::GPU& gpu, Frag* a, unsigned n) {
    if (n == 0) return;
    constexpr uintptr_t words = sizeof(typename Frag::FragmentBaseType) / sizeof(uint32_t);
    for (unsigned i = 0; i + 1 < n; i++) {
        a[i].head = (words << 24) | (reinterpret_cast<uintptr_t>(&a[i + 1]) & 0xffffff);
    }
    gpu.chain(&a[0], &a[n - 1]);
}

struct Layout {
    int yh[2][NB];  // bar centre, half-pixel units
    int hh[2][NB];  // bar height, half-pixel units
    int z[2][NB];
    uint8_t order[2][NB];  // back to front
    psyqo::Color mid[2][NB], edge[2][NB];
    int k2;  // layer 1 intensity, 0 = not drawn
};

class Engine {
  public:
    // Packets of one target (screen or texture page) for one buffer parity.
    struct Packets {
        GQuad q[2][NB * 2];
    };

    void onKick(uint32_t tick) {
        m_kickTick = tick;
        m_kickSign = -m_kickSign;
    }
    void onStab(uint32_t tick) {
        if (tick != m_stabTick) {
            m_stabTick = tick;
            m_stabPending = true;
        }
    }

    // Compute the layout for this clock. scrollerY is the screen y the stab flash looks for.
    void layout(const demo::Clock& clock, psyqo::Trig<>& trig, int scrollerY) {
        const uint32_t pos = clock.pos256;
        const int tt = int(pos) - int(bar(17));
        // bar height: stretch out of the 2 px line on the bar 17 crash
        int hhBase = 1 + ((10 * smooth(tt * 8)) >> 8);
        int amp = (18 * smooth((tt - 64) * 256 / 384)) >> 8;
        int fold = foldP(pos);
        int pulse = clampi(220 - tt * 7, 0, 220);  // white pulse on the crash
        if (pos >= bar(25)) {
            int t25 = int(pos - bar(25));
            m_l.k2 = (smooth(t25 * 2) * 120 >> 8) * (256 - ((fold * 190) >> 8)) >> 8;
            if (!m_l.k2) m_l.k2 = 1;
            pulse = clampi(pulse + 200 - t25 * 6, 0, 220);
        } else {
            m_l.k2 = 0;
        }
        int shake = 0;
        if (m_kickTick && clock.ticks - m_kickTick < 6) shake = m_kickSign;

        for (unsigned l = 0; l < 2; l++) {
            for (unsigned i = 0; i < NB; i++) {
                int theta = int(pos) * 3 + int(i) * 150 + (l ? 1024 : 0);
                int sw = (amp * sinq(trig, theta)) >> 12;
                sw += (amp * sinq(trig, theta * 2 + int(pos) * 2) * 3) >> 14;
                int y = 15 + 6 * int(i) + (l ? 3 : 0) + sw;
                int h = hhBase;
                if (fold) {
                    int centre = 60 + 3 * int(i) - 22 + int(l);
                    y += ((centre - y) * fold) >> 8;
                    h += ((4 - h) * fold) >> 8;
                }
                m_l.yh[l][i] = y + shake;
                m_l.hh[l][i] = h;
                m_l.z[l][i] = cosq(trig, theta);
            }
            // painter order, back to front: ascending z
            for (unsigned i = 0; i < NB; i++) m_l.order[l][i] = uint8_t(i);
            for (unsigned a = 1; a < NB; a++) {
                uint8_t v = m_l.order[l][a];
                int b = int(a) - 1;
                while (b >= 0 && m_l.z[l][m_l.order[l][b]] > m_l.z[l][v]) {
                    m_l.order[l][b + 1] = m_l.order[l][b];
                    b--;
                }
                m_l.order[l][b + 1] = v;
            }
        }
        // stab: flash the layer-0 bar nearest the scroller line
        if (m_stabPending) {
            m_stabPending = false;
            int best = 0, bd = 1 << 30;
            for (unsigned i = 0; i < NB; i++) {
                int d = m_l.yh[0][i] * 2 - scrollerY;
                if (d < 0) d = -d;
                if (d < bd) bd = d, best = int(i);
            }
            m_flashTick[best] = m_stabTick;
        }
        for (unsigned l = 0; l < 2; l++) {
            for (unsigned i = 0; i < NB; i++) {
                psyqo::Color c = l ? demo::rowColor(NB - 1 - i) : demo::rowColor(i);
                if (l) c = rgb(c.b, c.g, c.r);
                psyqo::Color core = mix(c, rgb(255, 255, 255), 50);
                psyqo::Color edge = scale(c, 40);
                int f = pulse;
                if (!l && m_flashTick[i]) {
                    int ff = clampi(255 - int(clock.ticks - m_flashTick[i]) * 26, 0, 255);
                    if (ff > f) f = ff;
                }
                if (f > 0) {
                    core = mix(core, rgb(255, 255, 255), f);
                    edge = mix(edge, rgb(255, 255, 255), f / 2);
                }
                if (l) {
                    core = scale(core, m_l.k2);
                    edge = scale(edge, m_l.k2);
                }
                m_l.mid[l][i] = core;
                m_l.edge[l][i] = edge;
            }
        }
    }

    const Layout& get() const { return m_l; }

    // Fill the packets of one layer. mul = 2 for the screen, 1 for the texture page.
    void fill(Packets& pk, unsigned l, int mul, int width) const {
        for (unsigned s = 0; s < NB; s++) {
            unsigned i = m_l.order[l][s];
            int yc = m_l.yh[l][i] * mul;
            int hh = m_l.hh[l][i] * mul;
            int yt = yc - hh, yb = yc + hh;
            auto& top = pk.q[l][s * 2].primitive;
            auto& bot = pk.q[l][s * 2 + 1].primitive;
            if (l) {
                top.setSemiTrans();
                bot.setSemiTrans();
            }
            top.setColorA(m_l.edge[l][i]).setColorB(m_l.edge[l][i]).setColorC(m_l.mid[l][i]).setColorD(m_l.mid[l][i]);
            top.setPointA({{.x = 0, .y = int16_t(yt)}}).setPointB({{.x = int16_t(width), .y = int16_t(yt)}});
            top.setPointC({{.x = 0, .y = int16_t(yc)}}).setPointD({{.x = int16_t(width), .y = int16_t(yc)}});
            bot.setColorA(m_l.mid[l][i]).setColorB(m_l.mid[l][i]).setColorC(m_l.edge[l][i]).setColorD(m_l.edge[l][i]);
            bot.setPointA({{.x = 0, .y = int16_t(yc)}}).setPointB({{.x = int16_t(width), .y = int16_t(yc)}});
            bot.setPointC({{.x = 0, .y = int16_t(yb)}}).setPointD({{.x = int16_t(width), .y = int16_t(yb)}});
        }
    }

    // Colour of the copper at a half-pixel row of the texture page (for the S3 -> S4 hand-off).
    psyqo::Color sample(int rowH) const {
        int r = 0, g = 0, b = 0;
        for (unsigned l = 0; l < 2; l++) {
            if (l && !m_l.k2) break;
            bool hit = false;
            psyqo::Color out = {{0, 0, 0}};
            for (unsigned s = 0; s < NB; s++) {
                unsigned i = m_l.order[l][s];
                int d = rowH - m_l.yh[l][i];
                if (d < 0) d = -d;
                if (d >= m_l.hh[l][i]) continue;
                int t = 256 - (d * 256) / m_l.hh[l][i];
                out = mix(m_l.edge[l][i], m_l.mid[l][i], t);
                hit = true;
            }
            if (!hit) continue;
            if (l) {
                r += out.r;
                g += out.g;
                b += out.b;
            } else {
                r = out.r;
                g = out.g;
                b = out.b;
            }
        }
        return rgb(r, g, b);
    }

    // ---- chain assembly -------------------------------------------------------------------
    // The chain built in frame() lands in the NEXT buffer, so every setup uses the Next variants.

    // Draw state for the whole chain: dithering on, additive blending for the semi-transparent layer.
    void chainState(psyqo::GPU& gpu, unsigned p) {
        auto& t = m_set[p].tpage.primitive;
        psyqo::PrimPieces::TPageAttr a;
        a.set(psyqo::Prim::TPageAttr::FullBackAndFullFront).setDithering(true).enableDisplayArea();
        t.attr = a;
        gpu.chain(m_set[p].tpage);
    }

    // Offscreen pass: the bars at half size into the texture page. Mask bit is forced so every
    // drawn texel is semi-transparent-capable for S3's additive plane; the clear stays 0x0000
    // (fully transparent texels).
    void texPass(psyqo::GPU& gpu, unsigned p) {
        auto& s = m_set[p];
        s.offScissor.primitive.start = psyqo::Prim::DrawingAreaStart(psyqo::Vertex{{.x = TEX_X, .y = TEX_Y}});
        s.offScissor.primitive.end = psyqo::Prim::DrawingAreaEnd(psyqo::Vertex{{.x = TEX_X + TEX_W, .y = TEX_Y + TEX_H}});
        s.offScissor.primitive.offset = psyqo::Prim::DrawingOffset(psyqo::Vertex{{.x = TEX_X, .y = TEX_Y}});
        s.texClear.primitive.setColor({{0, 0, 0}});
        s.texClear.primitive.rect = psyqo::Rect{TEX_X, TEX_Y, TEX_W, TEX_H};
        s.mask1.primitive = psyqo::Prim::MaskControl(psyqo::Prim::MaskControl::ForceSet);
        s.mask0.primitive = psyqo::Prim::MaskControl(psyqo::Prim::MaskControl::FromSource);
        gpu.chain(s.offScissor);
        gpu.chain(s.mask1);
        gpu.chain(s.texClear);
        fill(m_tex[p], 0, 1, TEX_W);
        chainRun(gpu, m_tex[p].q[0], NB * 2);
        if (m_l.k2) {
            fill(m_tex[p], 1, 1, TEX_W);
            chainRun(gpu, m_tex[p].q[1], NB * 2);
        }
        gpu.chain(s.mask0);
        gpu.chain(s.flush);
    }

    // Back to the next display buffer, cleared.
    void screenStart(psyqo::GPU& gpu, unsigned p, psyqo::Color bg = {{0, 0, 0}}) {
        auto& s = m_set[p];
        gpu.getNextScissor(s.scrScissor.primitive);
        gpu.getNextClear(s.clear.primitive, bg);
        gpu.chain(s.scrScissor);
        gpu.chain(s.clear);
    }

    void screenBars(psyqo::GPU& gpu, unsigned p) {
        fill(m_scr[p], 0, 2, 320);
        chainRun(gpu, m_scr[p].q[0], NB * 2);
        if (m_l.k2) {
            fill(m_scr[p], 1, 2, 320);
            chainRun(gpu, m_scr[p].q[1], NB * 2);
        }
    }

  private:
    struct Setup {
        psyqo::Fragments::SimpleFragment<psyqo::Prim::TPage> tpage;
        psyqo::Fragments::SimpleFragment<psyqo::Prim::Scissor> offScissor, scrScissor;
        psyqo::Fragments::SimpleFragment<psyqo::Prim::MaskControl> mask1, mask0;
        psyqo::Fragments::SimpleFragment<psyqo::Prim::FastFill> texClear, clear;
        psyqo::Fragments::SimpleFragment<psyqo::Prim::FlushCache> flush;
    };
    Setup m_set[2];
    Packets m_scr[2], m_tex[2];
    Layout m_l = {};
    uint32_t m_kickTick = 0;
    int m_kickSign = 1;
    uint32_t m_stabTick = 0;
    bool m_stabPending = false;
    uint32_t m_flashTick[NB] = {};
};

Engine& engine();

}  // namespace demo::cu
