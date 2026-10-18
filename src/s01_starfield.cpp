// Section 1: STARFIELD (bars 9-16). 768 stars projected through the GTE, three per RTPT. Starts
// from the 40 dots S0 left on its centre line, kick = speed surge, offbeat bass = green/amber flip,
// streaks from bar 13, radial flashes on bars 9 and 13, and over the bar 16 fill the stars slide
// onto the 16 rows S2 turns into copper bars.
#include "demo.hh"
#include "handoff.hh"
#include "psyqo/fixed-point.hh"
#include "psyqo/gte-kernels.hh"
#include "psyqo/gte-registers.hh"
#include "psyqo/matrix.hh"
#include "psyqo/primitives/control.hh"
#include "psyqo/primitives/misc.hh"
#include "psyqo/primitives/triangles.hh"
#include "s00_rects.hh"

#ifndef L1_NOLINES
#define L1_NOLINES 0
#endif
#ifndef L1_MAXLEN
#define L1_MAXLEN 48
#endif
#ifndef L1_NOFLASH
#define L1_NOFLASH 0
#endif

namespace demo {
namespace {

constexpr unsigned NSTARS = 768;
constexpr int Z_NEAR = 128, Z_FAR = 1024;
constexpr int H = 256;
constexpr int CX = 160, CY = 120;

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Star {
    int32_t x, y, z;  // world, 8 fractional bits
    uint32_t prev;    // previous projected position, packed like a Vertex
    int8_t vy;        // shatter drift of a hand-off star
    bool hand;        // came from an S0 dot: visible from the first frame
    bool fresh;       // just respawned: no previous position to draw a streak from
};

class Starfield final : public Section {
  public:
    const char* name() const override { return "S1 STARFIELD"; }

    void begin(const Clock& clock, Section*) override {
        m_rng = 0x41414141u ^ clock.ticks;
        if (g_handoff.dotCount == 0) {  // debug jump straight here: make the line S0 would have left
            g_handoff.dotCount = 40;
            for (int c = 0; c < 40; c++) g_handoff.dotX[c] = int16_t(4 + 8 * c);
            g_handoff.dotLineY = 120;
        }
        unsigned nh = g_handoff.dotCount;
        for (unsigned i = 0; i < NSTARS; i++) {
            Star& s = m_star[i];
            if (i < nh) {
                int sx = g_handoff.dotX[i], sy = g_handoff.dotLineY;
                int z = 64 * (3 + int(rnd() % 6));  // multiples of 64 project the dot back exactly
                s.z = z << 8;
                s.x = ((sx - CX) * z * 256) / H;
                s.y = ((sy - CY) * z * 256) / H;
                s.vy = int8_t(int(rnd() % 13) - 6);
                s.hand = true;
                s.fresh = false;
                s.prev = pack(sx, sy);
            } else {
                respawn(s, int(Z_NEAR + rnd() % (Z_FAR - Z_NEAR)));
                s.hand = false;
            }
        }
        m_t256 = clock.ticks << 8;
        m_t0 = clock.ticks;
        m_kick = 0;
        m_haveKick = false;
        m_first = true;
    }

    void onBar(const Clock&, int bar) override { m_stat.bar("S1", bar); }

    void onNote(const Clock&, unsigned channel, uint8_t note, uint8_t, uint32_t tick) override {
        if (channel == CH_DRUMS && (note == KEY_KICK || note == KEY_CRASH)) {
            m_kick = tick;
            m_kickBig = note == KEY_CRASH;
            m_haveKick = true;
        }
    }

    void update(const Clock& clock) override {
        // The clock ticks in whole steps, 1 or 2 per frame; smooth it so motion does not stutter
        // while still tracking it exactly in the long run.
        uint32_t target = clock.ticks << 8;
        if (m_first) {
            m_first = false;
            m_t256 = target;
            m_t0 = clock.ticks;
            m_dt256 = 0;
            return;
        }
        int32_t before = int32_t(m_t256);
        m_t256 += (int32_t(target - m_t256)) >> 2;
        m_dt256 = int32_t(m_t256) - before;
        if (m_dt256 < 0) m_dt256 = 0;
    }

    void render(Frame& f) override {
        const Clock& clock = f.clock;
        m_stat.sample(f);
        int parity = f.gpu.getParity();
        Buf& b = m_buf[parity];
        b.rects.reset();
        b.lines.reset();

        int pos = int(clock.pos256);
        int bar = clock.bar;
        int frac = clock.frac;
        uint32_t now = clock.ticks;
        bool streaks = pos >= 12 * 256;
        // bar 16 from step 6: slide toward rows. u = 0..256
        int u = pos >= 15 * 256 + 96 ? clampi((pos - (15 * 256 + 96)) * 256 / 128, 0, 256) : 0;
        int ue = (u * u * (768 - 2 * u)) >> 16;  // smoothstep, 0..256

        // speed in 1/16 unit per tick
        int speed16 = 64;
        if (m_haveKick) {
            int age = int(now - m_kick);
            if (age >= 0 && age < 12) speed16 += (m_kickBig ? 700 : 450) * (12 - age) * (12 - age) / 144;
        }
        int brake = 256 - ue;
        brake = brake * brake >> 8;
        int dz256 = (speed16 * m_dt256 >> 4) * brake >> 8;

        // green/amber flips on the bass offbeats (steps 2, 6, 10, 14)
        int step = frac >> 4;
        bool amber = (((step + 2) >> 2) & 1) != 0;
        int cr = amber ? 255 : 60, cg = amber ? 170 : 255, cb = amber ? 40 : 90;

        // intro gain for non-hand-off stars after the bar 9 crash
        int intro = clampi(int(now - m_t0) * 256 / 24, 0, 256);

        // GTE setup: identity rotation, no translation, 256 projection plane, screen centre
        using namespace psyqo::fixed_point_literals;
        static constexpr psyqo::Matrix33 identity = {{
            {1.0_fp, 0.0_fp, 0.0_fp},
            {0.0_fp, 1.0_fp, 0.0_fp},
            {0.0_fp, 0.0_fp, 1.0_fp},
        }};
        psyqo::GTE::writeUnsafe<psyqo::GTE::PseudoRegister::Rotation>(identity);
        psyqo::GTE::clear<psyqo::GTE::Register::TRX, psyqo::GTE::Unsafe>();
        psyqo::GTE::clear<psyqo::GTE::Register::TRY, psyqo::GTE::Unsafe>();
        psyqo::GTE::clear<psyqo::GTE::Register::TRZ, psyqo::GTE::Unsafe>();
        psyqo::GTE::write<psyqo::GTE::Register::OFX, psyqo::GTE::Unsafe>(CX << 16);
        psyqo::GTE::write<psyqo::GTE::Register::OFY, psyqo::GTE::Unsafe>(CY << 16);
        psyqo::GTE::write<psyqo::GTE::Register::H, psyqo::GTE::Unsafe>(H);

        // advance the stars
        bool drifting = (now - m_t0) < 48;
        int drift = drifting ? int(48 - (now - m_t0)) : 0;
        for (unsigned i = 0; i < NSTARS; i++) {
            Star& s = m_star[i];
            s.z -= dz256;
            if (s.hand && drift) s.y += (int(s.vy) * drift * m_dt256) >> 6;
            if (s.z < (Z_NEAR << 8)) respawn(s, Z_FAR - 1 - int(rnd() % 64)), s.hand = false;
        }

        for (unsigned i = 0; i < NSTARS; i += 3) {
            Star* a = &m_star[i];
            psyqo::GTE::write<psyqo::GTE::Register::VXY0, psyqo::GTE::Unsafe>(vxy(a[0]));
            psyqo::GTE::write<psyqo::GTE::Register::VZ0, psyqo::GTE::Unsafe>(a[0].z >> 8);
            psyqo::GTE::write<psyqo::GTE::Register::VXY1, psyqo::GTE::Unsafe>(vxy(a[1]));
            psyqo::GTE::write<psyqo::GTE::Register::VZ1, psyqo::GTE::Unsafe>(a[1].z >> 8);
            psyqo::GTE::write<psyqo::GTE::Register::VXY2, psyqo::GTE::Unsafe>(vxy(a[2]));
            psyqo::GTE::write<psyqo::GTE::Register::VZ2, psyqo::GTE::Unsafe>(a[2].z >> 8);
            psyqo::GTE::Kernels::rtpt();
            uint32_t sxy[3];
            psyqo::GTE::read<psyqo::GTE::Register::SXY0>(&sxy[0]);
            psyqo::GTE::read<psyqo::GTE::Register::SXY1>(&sxy[1]);
            psyqo::GTE::read<psyqo::GTE::Register::SXY2>(&sxy[2]);
            for (int k = 0; k < 3; k++) {
                Star& s = a[k];
                unsigned idx = i + k;
                int z = s.z >> 8;
                uint32_t curp = sxy[k];
                if (s.fresh) {
                    s.prev = curp;
                    s.fresh = false;
                }
                int sx = int16_t(sxy[k] & 0xffff), sy = int16_t(sxy[k] >> 16);
                int bri = 255 - (((z - Z_NEAR) * 110) >> 10);
                if (z > 900) bri = bri * (Z_FAR - z) / (Z_FAR - 900);
                if (bri < 0) bri = 0;
                if (!s.hand) bri = bri * intro >> 8;
                if (s.hand && z < 192) bri = bri;  // a hand-off star stays at the full phosphor level

                if (u) {
                    // slide: screen position toward a slot on row (idx % 16), column (idx / 16)
                    unsigned row = idx & 15, col = idx >> 4;
                    int tx = int(col * 320 / (NSTARS / 16)) + 3, ty = rowY(row) + 1;
                    int px = sx + (((tx - sx) * ue) >> 8), py = sy + (((ty - sy) * ue) >> 8);
                    psyqo::Color rc = rowColor(row);
                    int r = (cr * bri >> 8), g = (cg * bri >> 8), bl = (cb * bri >> 8);
                    int mix = ue;
                    r += ((rc.r - r) * mix) >> 8;
                    g += ((rc.g - g) * mix) >> 8;
                    bl += ((rc.b - bl) * mix) >> 8;
                    int w = 1 + ((7 * ue) >> 8), h = 1 + ((2 * ue) >> 8);
                    if (ue > 170 && w < 8) w = 8;
                    if (unsigned(px) < 320 && unsigned(py) < 240 && bri + ue > 6)
                        b.rects.add(px, py, w, h, rgb(r, g, bl));
                    s.prev = pack(sx, sy);
                    continue;
                }
                if (unsigned(sx) < 320 && unsigned(sy) < 240 && bri > 3) {
                    psyqo::Color c = rgb(cr * bri >> 8, cg * bri >> 8, cb * bri >> 8);
                    if (streaks && !L1_NOLINES) {
                        uint32_t cur = pack(sx, sy);
                        int dx = sx - int16_t(s.prev & 0xffff), dy = sy - int16_t(s.prev >> 16);
                        if (dx < -L1_MAXLEN || dx > L1_MAXLEN || dy < -L1_MAXLEN || dy > L1_MAXLEN)
                            b.rects.add(sx, sy, 1, 1, c);  // a star that jumped: no streak
                        else
                            b.lines.add(s.prev, cur, c);
                    } else {
                        int sz = z < 560 ? 2 : 1;
                        b.rects.add(sx, sy, sz, sz, c);
                    }
                }
                s.prev = pack(sx, sy);
            }
        }

        // flashes: crash at bar 9, shot at bar 13, decaying over ~36 ticks
        int fage = -1;
        if (bar == 9 || bar == 13) fage = int(clock.sinceBar);
        else if (bar == 10 || bar == 14) fage = int(clock.sinceBar + clock.ticksPerBar);
        int flash = 0;
        if (fage >= 0 && fage < 36) flash = 95 * (36 - fage) * (36 - fage) / (36 * 36);
        if (bar == 9 && fage >= 0) flash = fage < 36 ? flash : 0;

        f.gpu.getNextClear(b.clear.primitive, {{.r = 0, .g = 0, .b = 0}});
        f.gpu.chain(b.clear);
        b.tpageOpaque.primitive.attr.setDithering(false);
        f.gpu.chain(b.tpageOpaque);
        b.rects.chain(f.gpu);
        b.lines.chain(f.gpu);
        if (flash > 0 && !L1_NOFLASH) {
            b.tpageAdd.primitive.attr.set(psyqo::Prim::TPageAttr::FullBackAndFullFront).setDithering(true);
            f.gpu.chain(b.tpageAdd);
            psyqo::Color centre = rgb(flash * cr >> 8, flash * cg >> 8, flash * cb >> 8);
            psyqo::Color zero = rgb(0, 0, 0);
            static const int16_t cxs[5] = {0, 320, 320, 0, 0};
            static const int16_t cys[5] = {0, 0, 240, 240, 0};
            for (int k = 0; k < 4; k++) {
                auto& t = b.flashTri[k].primitive;
                t.setColorA(centre).setColorB(zero).setColorC(zero);
                t.setSemiTrans();
                t.pointA = {{.x = CX, .y = CY}};
                t.pointB = {{.x = cxs[k], .y = cys[k]}};
                t.pointC = {{.x = cxs[k + 1], .y = cys[k + 1]}};
                f.gpu.chain(b.flashTri[k]);
            }
            b.tpageOpaque.primitive.attr.setDithering(false);
            f.gpu.chain(b.tpageOpaque2);
        }
    }

  private:
    struct Buf {
        psyqo::Fragments::SimpleFragment<psyqo::Prim::FastFill> clear;
        psyqo::Fragments::SimpleFragment<psyqo::Prim::TPage> tpageOpaque, tpageOpaque2, tpageAdd;
        psyqo::Fragments::SimpleFragment<psyqo::Prim::GouraudTriangle> flashTri[4];
        RectList<NSTARS> rects;
        LineList<NSTARS> lines;
    };

    static uint32_t pack(int x, int y) { return uint32_t(uint16_t(x)) | (uint32_t(uint16_t(y)) << 16); }
    static uint32_t vxy(const Star& s) { return pack(s.x >> 8, s.y >> 8); }
    uint32_t rnd() {
        m_rng ^= m_rng << 13;
        m_rng ^= m_rng >> 17;
        m_rng ^= m_rng << 5;
        return m_rng;
    }
    void respawn(Star& s, int z) {
        s.z = z << 8;
        s.x = (int(rnd() % 801) - 400) << 8;
        s.y = (int(rnd() % 601) - 300) << 8;
        s.vy = 0;
        s.fresh = true;
        s.prev = pack(CX, CY);
    }

    Star m_star[NSTARS];
    Buf m_buf[2];
    BarStat m_stat;
    uint32_t m_rng = 1;
    uint32_t m_t256 = 0, m_t0 = 0, m_kick = 0;
    int32_t m_dt256 = 0;
    bool m_haveKick = false, m_kickBig = false, m_first = true;
};

Starfield s_section;
}  // namespace

Section& section1() { return s_section; }
}  // namespace demo
