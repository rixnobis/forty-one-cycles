// Section 5: the plasma page and its palette cycler, reusable by S6 (tunnel walls).
//
// VRAM (all claimed by S5; other lanes avoid):
//   texture   x 832..911 (80 halfwords = 160 texels, 8bpp), y 256..383   tpage (13, 1), Tex8Bits
//             The 128x128 block at texel u 0..127, v 0..127 is periodic: it tiles seamlessly under a
//             128x128 texture window (E2h mask 16,16, offset 0). Columns 128..159 repeat columns 0..31, so
//             a 160 wide view with no window also works. Rows 120..127 exist for the tiling.
//   CLUT A    x 512..767, y 498   CLUT B  x 512..767, y 499   (256 entries, STP set on every entry)
//   palette source rows, 512 halfwords each (the 256-entry palette twice), at x 320..831:
//             y 504 set A, y 505 set B, y 506 set A inverted, y 507 set B inverted.
// A cycled CLUT is a GP0(80h) VRAM-to-VRAM copy of 256 halfwords starting `shift` entries into the doubled
// row, so one 4 word chain node rotates a whole palette. Nothing here touches the bus outside the chain
// except prepare()/buildStep(), which wait for the chain to idle first.
#pragma once
#include <stdint.h>

#include "demo.hh"
#include "psyqo/fragments.hh"
#include "psyqo/primitives/control.hh"
#include "psyqo/primitives/misc.hh"

namespace demo::pl {

constexpr int TEX_X = 832, TEX_Y = 256, TEX_W = 160, TEX_H = 128, PERIOD = 128;
constexpr int TPAGE_X = TEX_X / 64, TPAGE_Y = TEX_Y / 256;
constexpr int CLUT_X = 512, CLUT_Y_A = 498, CLUT_Y_B = 499;
constexpr int ROW_X = 320, ROW_Y = 504;

enum class Set : uint8_t { A = 0, B = 1 };  // A: violet/magenta/amber, B: teal/cyan (the B minor set)

struct Blit {  // GP0(80h) VRAM to VRAM copy
    uint32_t command = 0x80000000;
    uint32_t src, dst, size;
};

class Cycler {
  public:
    // Palette source rows to VRAM (blocking, once). Idempotent.
    void prepare(psyqo::GPU& gpu, psyqo::Trig<>& trig);
    void prepare(Frame& f) { prepare(f.gpu, f.trig); }
    // Texture generation, rows at a time: each call computes up to `rows` rows and uploads them
    // (waits for the chain to idle). Returns true once all 128 rows are in VRAM.
    // restart(centres) re-arms it with new interference centres (screen coordinates).
    void restart(const int16_t* x, const int16_t* y, unsigned count);
    bool buildStep(psyqo::GPU& gpu, psyqo::Trig<>& trig, unsigned rows);
    bool buildStep(Frame& f, unsigned rows) { return buildStep(f.gpu, f.trig, rows); }
    bool textureReady() const { return m_row >= 128; }
    // S6 entry without S5 (DEBUG_SKIP): prepare + the whole texture, blocking.
    void ensureAll(psyqo::GPU& gpu, psyqo::Trig<>& trig);
    void ensureAll(Frame& f) { ensureAll(f.gpu, f.trig); }

    // Integrate the cycle: layer A runs forward at speedPerSec palette entries per second, layer B backward
    // at 0.4 of it. A function of clock.ticks only. bump() adds a decaying shift impulse (entries).
    void advance(const Clock& c, int32_t speedPerSec);
    void bump(int32_t entries) { m_bump16 += entries << 16; }
    void setPalette(Set a, Set b, bool inverted = false) {
        m_setA = a;
        m_setB = b;
        m_inv = inverted;
    }
    // Queue the two CLUT copies (and a cache flush) on the NEXT chain. Call before drawing with the
    // CLUTs; the node count is 3. Layer A draws with ClutIndex(CLUT_X >> 4, CLUT_Y_A), B with CLUT_Y_B.
    void chainClut(Frame& f);

  private:
    void computeRow(unsigned y);
    uint32_t rowY(Set s) const { return ROW_Y + (m_inv ? 2u : 0u) + uint32_t(s); }

    bool m_prepared = false;
    unsigned m_row = 0;
    int m_cx[4] = {32, 96, 60, 120}, m_cy[4] = {40, 30, 90, 100};
    uint32_t m_shiftA = 0, m_shiftB = 0;
    int32_t m_bump16 = 0;
    uint32_t m_lastTicks = 0;
    bool m_haveTicks = false;
    Set m_setA = Set::A, m_setB = Set::A;
    bool m_inv = false;
    psyqo::Fragments::SimpleFragment<Blit> m_blit[2][2];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::FlushCache> m_flush[2];
};

Cycler& cycler();

// Startup: palette rows + the whole texture into VRAM (blocking, ~40 ms), default ring centres. Call once, before
// the music timer arms. After this S5's first frame draws immediately and S6 never needs ensureAll().
void prewarm(psyqo::GPU& gpu, psyqo::Trig<>& trig);

}  // namespace demo::pl
