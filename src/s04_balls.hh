// Vector balls: a field of shaded sprites projected through the GTE and depth sorted in an
// ordering table. Used by S4 and, later, the recap (S7).
//
// VRAM claimed by the ball sprite (8bpp, 64x32 texels = sphere at u 0..31, soft blob at u 32..63):
//   texture   x 960..991, y 0..31   (tpage x = 15, y = 0)
//   CLUT      x 768..1023, y 500    (256 entries; entry 0 transparent, the rest carry the STP bit)
#pragma once

#include <stdint.h>

#include "demo.hh"
#include "psyqo/fragments.hh"
#include "psyqo/ordering-table.hh"
#include "psyqo/primitives/quads.hh"

namespace demo {

constexpr unsigned BALL_OT_SIZE = 256;
using BallOT = psyqo::OrderingTable<BALL_OT_SIZE>;

class BallField {
  public:
    static constexpr unsigned COUNT = 128;
    enum Formation { SPHERE, TORUS, LATTICE, HELIX, NUM_FORMATIONS };

    // Public look parameters, written by the owner every frame.
    int32_t reveal = 384;       // 0..384: balls grow in, staggered by index
    int32_t blur = 0;           // 0..256: balls turn into soft additive blobs
    int32_t flash = 0;          // 0..256: white boost
    int32_t spread = 256;       // 8.8 radial scale of the whole field
    int32_t camDist = 3000;     // camera distance, GTE units (1.0 = 4096)
    int32_t spinMul = 256;      // 8.8 multiplier on the rotation speed
    int32_t spreadTarget = 256; // spread eases toward this
    bool writeHandoff = false;  // store the projected positions in g_handoff

    // Builds the formation tables, the sprite and its CLUT. Needs the GPU idle of chain DMA.
    void init(Frame& f);
    bool ready() const { return m_ready; }

    // Start a morph to formation f at pos256 (instant = jump, no morph).
    void setFormation(Formation f, uint32_t pos256, bool instant = false);
    Formation formation() const { return m_form; }
    Formation target() const { return m_next; }

    // CH_LEAD note: swell the ball nearest the pitch-mapped angle.
    void lead(uint8_t note, uint32_t pos256);
    // CH_COUNTER note: the spin moves to the next axis.
    void stepAxis();
    // Hard camera cut: rotation jump plus a distance that eases back.
    void cameraCut(unsigned preset);
    // Kick: small size bump.
    void kick(uint32_t pos256) { m_kickAt = pos256; }

    void update(const Clock& clock);
    // Projects everything and inserts the quads into ot (which the caller chains).
    void submit(Frame& f, BallOT& ot);

  private:
    struct Quad {
        psyqo::Fragments::SimpleFragment<psyqo::Prim::TexturedQuad> frag;
    };
    void build(psyqo::Trig<>& trig);
    void uploadSprite(Frame& f);
    void morphPositions(uint32_t pos256, int16_t (*out)[3]) const;

    bool m_ready = false;
    psyqo::Trig<>* m_trig = nullptr;
    int16_t m_forms[NUM_FORMATIONS][COUNT][3];
    int16_t m_from[COUNT][3];
    int16_t m_cur[COUNT + 3][3];
    Formation m_form = SPHERE, m_next = SPHERE;
    uint32_t m_morphAt = 0;
    bool m_morphing = false;

    int32_t m_ang[3] = {0, 0, 0};  // raw angle * 256, 2048 raw per turn
    int32_t m_rate[3] = {0, 0, 0};
    int32_t m_sign[3] = {1, -1, 1};
    unsigned m_axis = 0;
    int32_t m_camTarget = 3000;
    uint32_t m_lastPos = 0;
    bool m_havePos = false;
    uint32_t m_pos = 0;
    uint32_t m_kickAt = 0;
    int32_t m_swellAt[COUNT];
    int32_t m_hue = 0;

    // last projection
    int16_t m_sx[COUNT + 3], m_sy[COUNT + 3];
    int32_t m_sz[COUNT + 3];
    int16_t m_dist = 3000;

    Quad m_quads[2][COUNT];
};

}  // namespace demo
