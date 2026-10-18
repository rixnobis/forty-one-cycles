// Section 7: RECAP (bars 97-112), and the scene S8 keeps running until the layers have dropped out.
// Layers, back to front: starfield (S1 style, seeded from the tunnel's ring vertices), the S4 ball field
// in a new formation on every orchestra hit, a copper strip across the bottom carrying the greets
// scroller, radial flashes. Crash 97 and 105 cut the camera, the shot at 105 flashes, the fills at 104
// and 112 scatter the balls and they re-form on the next bar. From bar 113 the balls leave with the
// clap, the copper with the bass fade, the stars last.
#include "log.hh"
#include "demo.hh"
#include "handoff.hh"
#include "psyqo/fixed-point.hh"
#include "psyqo/gte-kernels.hh"
#include "psyqo/gte-registers.hh"
#include "psyqo/matrix.hh"
#include "psyqo/primitives/control.hh"
#include "psyqo/primitives/misc.hh"
#include "psyqo/primitives/rectangles.hh"
#include "psyqo/primitives/triangles.hh"
#include "s00_rects.hh"
#include "s02_copper.hh"
#include "s04_balls.hh"
#include "s07_scene.hh"

namespace demo {
namespace {

constexpr unsigned NSTARS = 258;
constexpr int Z_NEAR = 128, Z_FAR = 1024, H = 256, CX = 160, CY = 120;
constexpr int FIRST_BAR = 97, END_BAR = 113;
constexpr int FLASH_LEN = 40;  // clock units

constexpr char kGreets[] =
    "GREETINGS TO PSX.DEV  ...  THE PCSX-REDUX CONTRIBUTORS  ...  "
    "EVERYONE STILL WRITING CODE FOR THIS MACHINE";
constexpr unsigned kGreetsLen = sizeof(kGreets) - 1;
// The text moves at half a pixel per clock unit and enters half a bar after the crash.
constexpr uint32_t kTextPos0 = barPos(FIRST_BAR) + 128;
static_assert(kTextPos0 + 2 * (320 + 16 * kGreetsLen) <= barPos(END_BAR) + 96, "greets too long for S7");

constexpr unsigned NBS = 9;  // bars in the strip
constexpr unsigned kMaxGlyphs = 24;

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int smoothi(int x) {
    x = x < 0 ? 0 : (x > 256 ? 256 : x);
    return (x * x * (768 - 2 * x)) >> 16;
}

struct TexWindowOff {  // GP0(E2h), zero mask: S3 leaves a 128x128 window set
    uint32_t command = 0xe2000000;
};

struct Star {
    int32_t x, y, z;  // world, 8 fractional bits
    uint32_t prev;
    bool fresh;
};

template <class H2>
unsigned seedFromTunnel(H2& h, int16_t* sx, int16_t* sy, uint16_t* ring, unsigned cap) {
    if constexpr (requires { h.tunnelCount; }) {
        unsigned n = h.tunnelCount < cap ? h.tunnelCount : cap;
        for (unsigned i = 0; i < n; i++) {
            sx[i] = h.tunnelX[i];
            sy[i] = h.tunnelY[i];
            ring[i] = h.tunnelZ[i];
        }
        return n;
    } else {
        return 0;
    }
}

class Scene final : public RecapScene {
  public:
    bool begun() const override { return m_begun; }

    void begin(const Clock& clock) override {
        m_begun = true;
        m_rng = 0x6c0ffeeu ^ clock.ticks;
        int16_t tx[Handoff::MAX_TUNNEL], ty[Handoff::MAX_TUNNEL];
        uint16_t tr[Handoff::MAX_TUNNEL];
        unsigned nt = clock.bar == FIRST_BAR ? seedFromTunnel(g_handoff, tx, ty, tr, Handoff::MAX_TUNNEL) : 0;
        for (unsigned i = 0; i < NSTARS; i++) {
            Star& s = m_star[i];
            if (i < nt) {
                int z = 64 * (3 + int(rnd() % 9));  // multiples of 64 project the vertex back to where S6 had it
                s.z = z << 8;
                s.x = ((tx[i] - CX) * z * 256) / H;
                s.y = ((ty[i] - CY) * z * 256) / H;
                s.fresh = false;
                s.prev = pack(tx[i], ty[i]);
            } else {
                respawn(s, int(Z_NEAR + rnd() % (Z_FAR - Z_NEAR)));
            }
        }
        m_seeded = nt;
        m_first = true;
        m_kick = 0;
        m_haveKick = false;
        m_speedBoost = 0;
        m_surgeAt = -100000;

        // balls: the formation follows the bar, so a debug jump into the middle still looks right
        int bar = clock.bar < FIRST_BAR ? FIRST_BAR : clock.bar;
        unsigned hit = unsigned((bar - FIRST_BAR) / 2);
        m_field.setFormation(BallField::Formation((2 + hit) & 3), 0, true);
        m_field.camDist = 3000;
        m_field.spread = m_field.spreadTarget = 256;
        m_field.blur = 0;
        m_field.writeHandoff = false;
        int32_t pos = int32_t(clock.pos256);
        m_lastOrch = pos;  // the entrance hit, if it arrives after this call, must not morph
        m_lastCut = -100000;
        m_cuts = unsigned(bar >= 105 ? 1 : 0);
        m_lastClap = -100000;
        m_nLead = 0;
        m_pendKick = m_pendCounter = m_pendOrch = m_pendCrash = false;
        m_pendSnare = 0;
        m_flashAt = -100000;
        m_flashAmt = 0;
        m_throwHits = 0;
        m_logFrame = 0;
        if (clock.bar == FIRST_BAR && clock.sinceBar < 24) cut(clock, true);
    }

    void onNote(const Clock& c, unsigned ch, uint8_t note, uint8_t, uint32_t tick) override {
        switch (ch) {
            case CH_LEAD:
                if (m_nLead < 4) m_lead[m_nLead++] = note;
                break;
            case CH_COUNTER: m_pendCounter = true; break;
            case CH_ORCH: m_pendOrch = true; break;
            case CH_DRUMS:
                if (note == KEY_KICK) {
                    m_pendKick = true;
                    m_kick = tick;
                    m_kickBig = false;
                    m_haveKick = true;
                }
                if (note == KEY_CRASH) m_pendCrash = true;
                if (note == KEY_SNARE || note == KEY_SNARE2) m_pendSnare++;
                if (note == KEY_CLAP) m_lastClap = int32_t(c.pos256);
                break;
            default: break;
        }
    }

    void update(const Clock& c) override {
        int32_t pos = int32_t(c.pos256);
        if (m_pendOrch) {
            m_pendOrch = false;
            if (pos - m_lastOrch > 128 && c.bar < END_BAR) {
                m_lastOrch = pos;
                m_field.setFormation(BallField::Formation((m_field.target() + 1) & 3), c.pos256);
                m_flashAt = pos;
                m_flashAmt = 110;
            }
        }
        if (m_pendCrash) {
            m_pendCrash = false;
            if (pos - m_lastCut > 64 && c.bar < END_BAR - 1 && c.bar >= FIRST_BAR) cut(c, false);
        }
        if (m_pendCounter) {
            m_pendCounter = false;
            m_field.stepAxis();
        }
        if (m_pendKick) {
            m_pendKick = false;
            m_field.kick(c.pos256);
        }
        for (unsigned i = 0; i < m_nLead; i++) m_field.lead(m_lead[i], c.pos256);
        m_nLead = 0;

        // the fills (bars 104 and 112) scatter the balls with the snare count and the stars surge
        bool fill = c.bar == 104 || c.bar == 112;
        if (fill) {
            m_throwHits += m_pendSnare;
            m_field.spreadTarget = clampi(256 + int(m_throwHits) * 22, 256, 1100);
        } else {
            m_throwHits = 0;
            m_field.spreadTarget = 256;
        }
        m_pendSnare = 0;

        int32_t rel = pos - int32_t(barPos(FIRST_BAR));
        int32_t reveal = clampi(rel * 2, 0, 384);
        // the balls leave with the clap: once the clap has stopped for 140 clock units after bar 116
        // (bar 118 at the latest) they fly apart while they shrink
        if (c.bar >= 117) {
            if (m_fadeAt == 0) {
                int32_t at = m_lastClap + 140;
                if (at < int32_t(barPos(117))) at = int32_t(barPos(117));
                m_fadeAt = at;
            }
            if (c.bar >= 118 && m_fadeAt > int32_t(barPos(118))) m_fadeAt = int32_t(barPos(118));
        } else {
            m_fadeAt = 0;
        }
        if (m_fadeAt && pos >= m_fadeAt) {
            int32_t d = pos - m_fadeAt;
            reveal = clampi(384 - d * 3, 0, 384);
            m_field.spreadTarget = clampi(256 + d * 4, 256, 1100);
        }
        m_field.reveal = reveal;
        m_ballsOn = reveal > 0;
        int32_t fd = pos - m_flashAt;
        m_field.flash = (fd >= 0 && fd < 64) ? (m_flashAmt * (64 - fd)) >> 6 : 0;
        m_field.spinMul = 256 + ((m_field.spread - 256) >> 1);
        m_field.update(c);

        int enter, k;
        // the layers drop out in the order the instruments do
        int starDim = 256, speedMul = 256;
        if (pos >= int(barPos(113))) {
            int t = pos - int(barPos(113));
            starDim = 256 - clampi(t * 90 / (5 * 256), 0, 90);   // 256 -> 166 over bars 113-117
            starDim -= clampi((pos - int(barPos(118))) * 166 / 256, 0, 166);
            speedMul = 256 - clampi(t * 130 / (5 * 256), 0, 130);
        }
        // the copper: in over half a bar after the crash, out with the bass fade (bars 113-118)
        enter = smoothi((pos - int(barPos(FIRST_BAR))) * 256 / 128);
        k = 256;
        if (pos >= int(barPos(113))) k = 256 - clampi((pos - int(barPos(113))) * 256 / (5 * 256), 0, 256);

        m_starDim = starDim; m_speedMul = speedMul; m_enter = enter; m_k = k;

        // star clock, smoothed like S1
        uint32_t target = c.ticks << 8;
        if (m_first) {
            m_first = false;
            m_t256 = target;
            m_dt256 = 0;
            m_t0 = c.ticks;
        } else {
            int32_t before = int32_t(m_t256);
            m_t256 += int32_t(target - m_t256) >> 2;
            m_dt256 = int32_t(m_t256) - before;
            if (m_dt256 < 0) m_dt256 = 0;
        }
    }

    void render(Frame& f) override {
        const Clock& clock = f.clock;
        if (!m_field.ready()) m_field.init(f);
        unsigned p = f.gpu.getParity();
        Buf& b = m_buf[p];
        int pos = int(clock.pos256);
        int bar = clock.bar;

        int starDim = m_starDim, speedMul = m_speedMul, enter = m_enter, k = m_k;

        advanceStars(f, b, starDim, speedMul);

        f.gpu.getNextClear(b.clear.primitive, {{.r = 0, .g = 0, .b = 0}});
        f.gpu.chain(b.clear);
        f.gpu.chain(b.texWin);
        b.tpageOpaque.primitive.attr.setDithering(false);
        f.gpu.chain(b.tpageOpaque);
        b.rects.chain(f.gpu);
        b.streaks.chain(f.gpu);

        if (m_ballsOn) {
            m_field.submit(f, m_ots[p]);
            f.gpu.chain(m_ots[p]);
        }
        if (k > 0 && enter > 0) copper(f, b, p, k, enter);
        flashes(f, b);
    }

  private:
    struct Buf {
        psyqo::Fragments::SimpleFragment<psyqo::Prim::FastFill> clear;
        psyqo::Fragments::SimpleFragment<TexWindowOff> texWin;
        psyqo::Fragments::SimpleFragment<psyqo::Prim::TPage> tpageOpaque, tpageOpaque2, tpageAdd, tpageSub, tpageGour;
        psyqo::Fragments::SimpleFragment<psyqo::Prim::GouraudTriangle> flashTri[8];
        psyqo::Fragments::SimpleFragment<psyqo::Prim::Rectangle> band;
        cu::GQuad bar[NBS * 2];
        RectList<NSTARS> rects;
        TriList<NSTARS> streaks;
        GlyphBatch<kMaxGlyphs> text;
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
        s.fresh = true;
        s.prev = pack(CX, CY);
    }

    // Camera cut: balls jump to a new angle and distance, the stars' plane turns, everything flashes.
    void cut(const Clock& c, bool entrance) {
        int32_t pos = int32_t(c.pos256);
        m_lastCut = pos;
        m_field.cameraCut(m_cuts);
        // quarter turn of the star plane, alternating direction
        bool cw = (m_cuts & 1) == 0;
        for (unsigned i = 0; i < NSTARS; i++) {
            Star& s = m_star[i];
            int32_t x = s.x, y = s.y;
            if (cw) { s.x = -y; s.y = x; } else { s.x = y; s.y = -x; }
            s.fresh = true;
        }
        m_cuts++;
        m_flashAt = pos;
        m_flashAmt = (c.bar == 105 || entrance) ? 256 : 160;
        m_surgeAt = pos;
    }

    void advanceStars(Frame& f, Buf& b, int starDim, int speedMul) {
        const Clock& clock = f.clock;
        b.rects.reset();
        b.streaks.reset();
        uint32_t now = clock.ticks;
        int pos = int(clock.pos256);
        int speed16 = 90;
        if (m_haveKick) {
            int age = int(now - m_kick);
            if (age >= 0 && age < 12) speed16 += 420 * (12 - age) * (12 - age) / 144;
        }
        int sa = pos - m_surgeAt;  // camera cut / fill surge
        if (sa >= 0 && sa < 96) speed16 += 900 * (96 - sa) / 96;
        if (clock.bar == 104 || clock.bar == 112) speed16 += clock.frac * 3;
        int dz256 = (speed16 * m_dt256 >> 4) * speedMul >> 8;

        int step = clock.frac >> 4;
        bool amber = (((step + 2) >> 2) & 1) != 0;
        int cr = amber ? 255 : 60, cg = amber ? 170 : 255, cb = amber ? 40 : 90;
        int intro = clampi(int(now - m_t0) * 256 / 24, 0, 256);

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

        for (unsigned i = 0; i < NSTARS; i++) {
            Star& s = m_star[i];
            s.z -= dz256;
            if (s.z < (Z_NEAR << 8)) respawn(s, Z_FAR - 1 - int(rnd() % 64));
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
            for (int j = 0; j < 3; j++) {
                Star& s = a[j];
                int z = s.z >> 8;
                if (s.fresh) {
                    s.prev = sxy[j];
                    s.fresh = false;
                }
                int sx = int16_t(sxy[j] & 0xffff), sy = int16_t(sxy[j] >> 16);
                int bri = 255 - (((z - Z_NEAR) * 110) >> 10);
                if (z > 900) bri = bri * (Z_FAR - z) / (Z_FAR - 900);
                if (bri < 0) bri = 0;
                if (i + j >= m_seeded) bri = bri * intro >> 8;
                bri = bri * starDim * 3 >> 9;
                if (bri > 255) bri = 255;
                if (unsigned(sx) < 320 && unsigned(sy) < 240 && bri > 3) {
                    psyqo::Color c = rgb(cr * bri >> 8, cg * bri >> 8, cb * bri >> 8);
                    int dx = sx - int16_t(s.prev & 0xffff), dy = sy - int16_t(s.prev >> 16);
                    if (dx < -48 || dx > 48 || dy < -48 || dy > 48 || (dx | dy) == 0)
                        b.rects.add(sx, sy, z < 760 ? 2 : 1, z < 760 ? 2 : 1, c);
                    else
                        b.streaks.add(s.prev, pack(sx, sy), pack(sx + 1, sy + 1), c);
                }
                s.prev = pack(sx, sy);
            }
        }
    }

    // The strip: nine swinging bars over a dimmed band at the bottom of the screen, the scroller on top.
    void copper(Frame& f, Buf& b, unsigned p, int k, int enter) {
        auto& gpu = f.gpu;
        const int pos = int(f.clock.pos256);
        auto& trig = f.trig;
        int slide = ((256 - enter) * 70) >> 8;  // the strip rises out of the bottom edge
        // dimmed band behind the bars
        psyqo::PrimPieces::TPageAttr sub;
        sub.set(psyqo::Prim::TPageAttr::FullBackSubFullFront).setDithering(false).enableDisplayArea();
        b.tpageSub.primitive.attr = sub;
        gpu.chain(b.tpageSub);
        auto& r = b.band.primitive;
        int g = 110 * k >> 8;
        r.setColor(rgb(g, g, g));
        r.setSemiTrans();
        r.position = {{.x = 0, .y = int16_t(172 + slide)}};
        r.size = {{.w = 320, .h = int16_t(68 - slide > 0 ? 68 - slide : 1)}};
        gpu.chain(b.band);

        psyqo::PrimPieces::TPageAttr op;
        op.set(psyqo::Prim::TPageAttr::FullBackAndFullFront).setDithering(true).enableDisplayArea();
        b.tpageGour.primitive.attr = op;
        gpu.chain(b.tpageGour);

        int yc[NBS], zz[NBS];
        uint8_t order[NBS];
        int flash = f.clock.bar == 105 || f.clock.bar == FIRST_BAR ? clampi(200 - int(f.clock.sinceBar) * 6, 0, 200) : 0;
        for (unsigned i = 0; i < NBS; i++) {
            int theta = pos * 3 + int(i) * 190;
            yc[i] = 188 + 6 * int(i) + ((12 * cu::sinq(trig, theta)) >> 12) + slide;
            zz[i] = cu::cosq(trig, theta);
            order[i] = uint8_t(i);
        }
        for (unsigned a = 1; a < NBS; a++) {
            uint8_t v = order[a];
            int q = int(a) - 1;
            while (q >= 0 && zz[order[q]] > zz[v]) {
                order[q + 1] = order[q];
                q--;
            }
            order[q + 1] = v;
        }
        for (unsigned s = 0; s < NBS; s++) {
            unsigned i = order[s];
            psyqo::Color base = rowColor(i * (HANDOFF_ROWS - 1) / (NBS - 1));
            psyqo::Color core = cu::mix(cu::scale(base, 170), cu::rgb(255, 255, 255), flash);
            psyqo::Color edge = cu::scale(core, 40);
            core = cu::scale(core, k);
            edge = cu::scale(edge, k);
            int hh = 1 + ((6 * k) >> 8);
            int yt = yc[i] - hh, yb = yc[i] + hh, ym = yc[i];
            auto& top = b.bar[s * 2].primitive;
            auto& bot = b.bar[s * 2 + 1].primitive;
            top.setOpaque();
            bot.setOpaque();
            top.setColorA(edge).setColorB(edge).setColorC(core).setColorD(core);
            top.setPointA({{.x = 0, .y = int16_t(yt)}}).setPointB({{.x = 320, .y = int16_t(yt)}});
            top.setPointC({{.x = 0, .y = int16_t(ym)}}).setPointD({{.x = 320, .y = int16_t(ym)}});
            bot.setColorA(core).setColorB(core).setColorC(edge).setColorD(edge);
            bot.setPointA({{.x = 0, .y = int16_t(ym)}}).setPointB({{.x = 320, .y = int16_t(ym)}});
            bot.setPointC({{.x = 0, .y = int16_t(yb)}}).setPointD({{.x = 320, .y = int16_t(yb)}});
        }
        cu::chainRun(gpu, b.bar, NBS * 2);

        // greets scroller: glyph j at x = 320 + 16 j - scrolled, a half pixel per clock unit
        b.text.reset();
        int scrolled = (pos - int(kTextPos0)) / 2;
        if (scrolled >= 0 && k > 0) {
            int first = (scrolled - 320) / 16;
            if (first < 0) first = 0;
            for (int j = first; j < int(kGreetsLen); j++) {
                int x = 320 + 16 * j - scrolled;
                if (x >= 320) break;
                if (x <= -16) continue;
                char ch = kGreets[j];
                int y = 200 - 8 + slide + ((22 * cu::sinq(trig, x * 7 + pos * 5)) >> 12);
                psyqo::Color tint = cu::mix(cu::rgb(255, 255, 255), rowColor(unsigned(j) & 15), 50);
                b.text.add(x, y, 16, 16, ch, cu::scale(tint, 128 + (k >> 1)), cu::rgb(0, 0, 0));
            }
        }
        b.text.chain(gpu);
        (void)p;
    }

    // Radial flash: four additive gouraud triangles from the centre to a diamond that grows with the age of
    // the flash. A full-screen additive fill costs the GPU several ms and tips the frame past a vsync, and a
    // TPage packet after these triangles stalled the DMA chain on silicon, so the diamond is small and the
    // state is left additive (every frame's chain opens with its own TPage).
    void flashes(Frame& f, Buf& b) {
        const Clock& clock = f.clock;
        int age = int(clock.pos256) - m_flashAt;
        int flash = 0;
        if (age >= 0 && age < FLASH_LEN) flash = (m_flashAmt * 3 / 8) * (FLASH_LEN - age) * (FLASH_LEN - age) / (FLASH_LEN * FLASH_LEN);
        if (flash <= 0) return;
        psyqo::PrimPieces::TPageAttr add;
        add.set(psyqo::Prim::TPageAttr::FullBackAndFullFront).setDithering(false);
        b.tpageAdd.primitive.attr = add;
        f.gpu.chain(b.tpageAdd);
        psyqo::Color centre = rgb(flash, flash, flash);
        psyqo::Color zero = rgb(0, 0, 0);
        int rx = 40 + age * 3, ry = (rx * 3) >> 2;  // grows with the age
        int16_t px[9], py[9];
        for (int j = 0; j < 8; j++) {
            px[j] = int16_t(CX + ((rx * cu::cosq(f.trig, j * 256)) >> 12));
            py[j] = int16_t(CY + ((ry * cu::sinq(f.trig, j * 256)) >> 12));
        }
        px[8] = px[0];
        py[8] = py[0];
        for (int j = 0; j < 8; j++) {
            auto& t = b.flashTri[j].primitive;
            t.setColorA(centre).setColorB(zero).setColorC(zero);
            t.setSemiTrans();
            t.pointA = {{.x = CX, .y = CY}};
            t.pointB = {{.x = px[j], .y = py[j]}};
            t.pointC = {{.x = px[j + 1], .y = py[j + 1]}};
            f.gpu.chain(b.flashTri[j]);
        }
    }

    bool m_begun = false;
    int m_starDim = 256, m_speedMul = 256, m_enter = 0, m_k = 256;
    Star m_star[NSTARS];
    unsigned m_seeded = 0;
    Buf m_buf[2];
    BallField m_field;
    BallOT m_ots[2];
    uint32_t m_rng = 1, m_t256 = 0, m_t0 = 0, m_kick = 0;
    int32_t m_dt256 = 0, m_speedBoost = 0, m_surgeAt = 0;
    bool m_first = true, m_haveKick = false, m_kickBig = false, m_ballsOn = true;
    int32_t m_lastOrch = 0, m_lastCut = 0, m_flashAt = 0, m_flashAmt = 0, m_lastClap = 0, m_fadeAt = 0;
    unsigned m_cuts = 0, m_throwHits = 0, m_pendSnare = 0, m_nLead = 0;
    uint8_t m_lead[4] = {};
    bool m_pendKick = false, m_pendCounter = false, m_pendOrch = false, m_pendCrash = false;
    uint32_t m_logFrame = 0, m_logTicks = 0;

  public:
    void logFps(Frame& f) {
        if (f.frameNo - m_logFrame >= 128) {
            uint32_t dt = f.clock.ticks - m_logTicks;
            if (m_logFrame && dt) {
                uint32_t fps10 = (f.frameNo - m_logFrame) * 1120 / dt;
                DEMO_LOG("S7 fps %d.%d at bar %d frac %d\n", int(fps10 / 10), int(fps10 % 10), f.clock.bar, f.clock.frac);
            }
            m_logFrame = f.frameNo;
            m_logTicks = f.clock.ticks;
        }
    }
};

Scene s_scene;

class Recap final : public Section {
  public:
    const char* name() const override { return "S7 RECAP"; }
    void begin(const Clock& clock, Section*) override { s_scene.begin(clock); }
    void onNote(const Clock& c, unsigned ch, uint8_t note, uint8_t vel, uint32_t tick) override {
        s_scene.onNote(c, ch, note, vel, tick);
    }
    void update(const Clock& c) override { s_scene.update(c); }
    void render(Frame& f) override {
        s_scene.render(f);
        s_scene.logFps(f);
    }
};

Recap s_section;
}  // namespace

RecapScene& recapScene() { return s_scene; }
Section& section7() { return s_section; }
}  // namespace demo
