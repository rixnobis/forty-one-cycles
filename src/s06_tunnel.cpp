// Section 6: TUNNEL (bars 81-96, B minor climax). 16 rings x 16 segments projected through the GTE,
// each ring-to-ring patch a gouraud-textured quad, depth sorted in a 16-bucket ordering table.
// The walls are the plasma page (128x128 texture window, 8bpp) with its palette cycling.
//
// Rings sit at geometric depths so every patch spans the same depth ratio (1.3): affine texture
// error is then the same few percent of a 4 texel span everywhere. Forward motion is the texture
// scrolling along the tunnel (v), the kick adds a burst of scroll plus a z-squeeze (surge).
#include "common/syscalls/syscalls.h"
#include "demo.hh"
#include "handoff.hh"
#include "psyqo/fixed-point.hh"
#include "psyqo/fragments.hh"
#include "psyqo/gte-kernels.hh"
#include "psyqo/gte-registers.hh"
#include "psyqo/ordering-table.hh"
#include "psyqo/primitives/misc.hh"
#include "psyqo/primitives/quads.hh"
#include "psyqo/primitives/rectangles.hh"
#include "psyqo/soft-math.hh"
#include "s05_plasma.hh"

namespace demo {
namespace {

constexpr int NR = 16;           // rings
constexpr int NS = 16;           // segments per ring
constexpr int NV = NR * NS;      // projected vertices
constexpr int NQ = (NR - 1) * NS;
constexpr int PROJ_H = 256;
constexpr int R0 = 520;          // ring radius, GTE units
constexpr int FIRST_BAR = 81, LAST_BAR = 96;


inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
inline psyqo::Angle angRaw(int32_t raw) { return psyqo::Angle(raw & 2047, psyqo::Angle::RAW); }
inline int32_t smooth256(int32_t t) {  // t 0..256 -> 0..256
    t = clampi(t, 0, 256);
    return ((768 - 2 * t) * t * t) >> 16;
}

using TQuad = psyqo::Fragments::SimpleFragment<psyqo::Prim::GouraudTexturedQuad>;
using TunnelOT = psyqo::OrderingTable<NR>;

struct TexWindow {
    uint32_t command;
};

// Records the surviving vertex positions for S7 once the handoff contract grows the fields
// (tunnelX, tunnelY, tunnelZ: int16[256]; tunnelCount: unsigned). Compiles either way.
template <class H>
void writeStars(H& h, const int16_t* sx, const int16_t* sy, unsigned count) {
    if constexpr (requires { h.tunnelCount; }) {
        unsigned n = 0;
        for (unsigned i = 0; i < count; i++) {
            if (sx[i] > -8 && sx[i] < 328 && sy[i] > -8 && sy[i] < 248 && n < sizeof(h.tunnelX) / sizeof(h.tunnelX[0])) {
                h.tunnelX[n] = sx[i];
                h.tunnelY[n] = sy[i];
                h.tunnelZ[n] = uint16_t(i / NS);  // ring index, 0 near .. 15 far
                n++;
            }
        }
        h.tunnelCount = n;
    }
}

class Tunnel final : public Section {
  public:
    const char* name() const override { return "S6 TUNNEL"; }

    void begin(const Clock& c, Section*) override {
        m_havePos = false;
        m_voff = 0;
        m_scroll = 0;
        m_twist = 0;
        m_twistSign = c.sectionBar >= 89 ? -1 : 1;
        m_twistRate = m_twistSign * kTwistBase;
        m_bendX = m_bendY = m_bendTX = m_bendTY = 0;
        m_leadMid = 0;
        m_lastLead = 0;
        m_kickAt = m_bandAt = m_invAt = m_flashAt = m_lastOrch = m_lastCrash = -100000;
        m_flashAmt = 0;
        m_pendKick = m_pendCounter = m_pendOrch = m_pendCrash = false;
        m_nLead = 0;
        m_toms = 0;
        m_pendToms = 0;
        m_open = 0;
        m_logFrame = 0;
    }

    void onNote(const Clock&, unsigned ch, uint8_t note, uint8_t, uint32_t) override {
        switch (ch) {
            case CH_LEAD:
                if (m_nLead < 4) m_lead[m_nLead++] = note;
                break;
            case CH_COUNTER: m_pendCounter = true; break;
            case CH_ORCH: m_pendOrch = true; break;
            case CH_DRUMS:
                if (note == KEY_KICK) m_pendKick = true;
                if (note == KEY_CRASH) m_pendCrash = true;
                if (note == KEY_TOM_LO || note == KEY_TOM_MID || note == KEY_TOM_HI) m_pendToms++;
                break;
            default: break;
        }
    }

    void update(const Clock& c) override {
        int32_t pos = int32_t(c.pos256);
        int32_t d = m_havePos ? int32_t(c.pos256 - m_lastPos) : 0;
        if (d < 0 || d > 64) d = 0;  // loop wrap or a hitch
        m_lastPos = c.pos256;
        m_havePos = true;
        int32_t ds = d > 16 ? 16 : d;

        if (m_pendKick) {
            m_pendKick = false;
            m_kickAt = pos;
            m_scroll += kKickScroll;
            pl::cycler().bump(14);
        }
        if (m_pendCounter) {
            m_pendCounter = false;
            m_bandAt = pos;
        }
        if (m_pendOrch) {
            m_pendOrch = false;
            if (pos - m_lastOrch > 128) {
                m_lastOrch = pos;
                m_invAt = pos;
                pl::cycler().bump(40);
            }
        }
        if (m_pendCrash) {
            m_pendCrash = false;
            if (pos - m_lastCrash > 64) {
                m_lastCrash = pos;
                m_flashAt = pos;
                m_flashAmt = 64;
                if (c.bar == 89) m_twistSign = -1;  // the crash on bar 89 reverses the twist
            }
        }
        // lead pitch contour -> bend target: offset from the running mean horizontally, the last
        // interval vertically
        for (unsigned i = 0; i < m_nLead; i++) {
            int32_t n = m_lead[i];
            if (m_leadMid == 0) m_leadMid = n << 4;
            m_leadMid += ((n << 4) - m_leadMid) >> 3;
            int32_t dev = n - (m_leadMid >> 4);
            int32_t iv = m_lastLead ? n - m_lastLead : 0;
            m_lastLead = n;
            m_bendTX = clampi(dev * 520, -3600, 3600);
            m_bendTY = clampi(iv * -380, -3000, 3000);
        }
        m_nLead = 0;

        // bend eases toward its target; a slow sway keeps it alive between notes
        m_bendX += ((m_bendTX - m_bendX) * ds) >> 4;
        m_bendY += ((m_bendTY - m_bendY) * ds) >> 4;

        // scroll: base rate plus the kick burst, which decays exponentially over a 16th
        m_scroll -= ((m_scroll - 0) * ds) >> 4;
        int32_t rate = kBaseScroll + m_scroll;
        m_voff = (m_voff + rate * d) % (128 * 256);

        // palette cycle speed (entries/s): climbs through the section, the kick and orch hits bump it
        {
            int32_t tgtSpeed = 90 + ((pos - (FIRST_BAR - 1) * 256) * 60 >> 12);
            if (c.bar >= LAST_BAR) tgtSpeed += 200;
            int32_t dt = d > 12 ? 12 : d;
            m_speed += ((tgtSpeed * 256 - m_speed) * dt) >> 6;
        }
        // twist: spins at the base rate, direction follows the sign, the crash flips it smoothly
        int32_t tgt = m_twistSign * kTwistBase;
        m_twistRate += ((tgt - m_twistRate) * ds) >> 5;
        m_twist += m_twistRate * d;

        // bar 96: the tunnel opens. Toms add jolts, the clock does the rest.
        m_toms += m_pendToms;
        m_pendToms = 0;
        if (c.bar >= LAST_BAR) {
            int32_t e = clampi(c.bar > LAST_BAR ? 256 : c.frac, 0, 256);
            m_open = clampi((e * 232) / 256 + int32_t(m_toms) * 3, 0, 250);
        } else {
            m_open = 0;
        }
    }

    void render(Frame& f) override {
        auto& gpu = f.gpu;
        unsigned p = gpu.getParity();
        if (!m_ready) init(f);
        int32_t pos = int32_t(f.clock.pos256);
        int32_t rel = pos - (FIRST_BAR - 1) * 256;  // 0 at bar 81 beat 1

        if (f.frameNo - m_logFrame >= 64) {
            uint32_t dt = f.clock.ticks - m_logTicks;
            if (m_logFrame && dt) {
                uint32_t fps10 = (f.frameNo - m_logFrame) * 1120 / dt;
                ramsyscall_printf("S6 fps %d.%d at bar %d frac %d quads %d+%d\n", int(fps10 / 10), int(fps10 % 10),
                                  f.clock.bar, f.clock.frac, int(m_drawn), int(m_drawn2));
            }
            m_logFrame = f.frameNo;
            m_logTicks = f.clock.ticks;
        }

        int32_t glow = 0;  // the vanishing point is a dark hole
        int32_t wh = rel < 84 ? 255 - clampi(rel - 12, 0, 72) * 3 : 0;  // the entry white, same ramp as the flash below
        gpu.getNextClear(m_clear[p].primitive, {{.r = uint8_t(clampi((200 * glow >> 8) + wh, 0, 255)), .g = uint8_t(clampi((230 * glow >> 8) + wh, 0, 255)), .b = uint8_t(clampi(glow + wh, 0, 255))}});
        gpu.chain(m_clear[p]);

        // palette: cycles with the clock; inverted for one beat after an orch hit
        int32_t invDt = pos - m_invAt;
        bool inv = invDt >= 0 && invDt < 64;
        m_inverted = inv;
        pl::cycler().setPalette(pl::Set::B, pl::Set::A, inv);
        pl::cycler().advance(f.clock, m_speed >> 8);
        pl::cycler().chainClut(f);

        gpu.chain(m_win[p]);
        layer(f, m_ots[p], m_quads[p], pos, Layer{256, 256, 256, 256, 0, 256, false});
        gpu.chain(m_ots[p]);
        // the counter (a third below the lead) lights a second, mirrored ring set that fades over a beat and a half
        int32_t cd = pos - m_bandAt;
        int32_t env2 = (cd >= 0 && cd < 96) ? (96 - cd) * 255 / 96 : 0;
        m_drawn2 = 0;
        if (env2 > 0 && m_open < 200) {
            layer(f, m_ots2[p], m_quads2[p], pos, Layer{176, -256, 292, 140, 64, (env2 * 110) >> 8, true});
            gpu.chain(m_ots2[p]);
        }
        gpu.chain(m_winOff[p]);  // window off again for whoever draws next

        // white: the entry frame at bar 81, the crashes
        int32_t fl = 0;
        if (rel < 12) fl = 255;
        else if (rel < 84) fl = 255 - ((255 * smooth256((rel - 12) * 256 / 72)) >> 8);
        int32_t fd = pos - m_flashAt;
        if (fd >= 0 && fd < 40) fl = fl > (m_flashAmt * (40 - fd)) / 40 ? fl : (m_flashAmt * (40 - fd)) / 40;
        if (fl > 0) {
            m_tpAdd[p].primitive.attr = m_tp;
            gpu.chain(m_tpAdd[p]);
            auto& r = m_flash[p].primitive;
            r.setColor({{.r = uint8_t(fl), .g = uint8_t(fl), .b = uint8_t(fl)}}).setSemiTrans();
            r.position = {{.x = 0, .y = 0}};
            r.size = {{.w = 320, .h = 240}};
            gpu.chain(m_flash[p]);
        }
    }

  private:
    static constexpr int32_t kBaseScroll = 72;     // v Q8 per pos256 unit (0.28 texel)
    static constexpr int32_t kKickScroll = 800;
    static constexpr int32_t kTwistBase = 22;      // raw angle Q8 per pos256 unit
    static constexpr int32_t kRingTwist = 34;      // raw angle per ring, static spiral

    // ring depths, 330 * 1.3^k
    static constexpr int16_t kZ[NR] = {330, 429, 558, 725, 943, 1225, 1593, 2071, 2692, 3499, 4549, 5914, 7688, 9995, 12993, 16891};

    void init(Frame& f) {
        auto& trig = f.trig;
        for (int j = 0; j < NS; j++) {
            m_cj[j] = int16_t(trig.cos(angRaw(j * 128)).value);
            m_sj[j] = int16_t(trig.sin(angRaw(j * 128)).value);
        }
        // Entered from S5 the texture is in VRAM already; with DEBUG_SKIP it is built here, blocking.
        if (!pl::cycler().textureReady()) pl::cycler().ensureAll(f);

        psyqo::PrimPieces::TPageAttr tp;
        tp.setPageX(pl::TPAGE_X).setPageY(pl::TPAGE_Y).set(psyqo::Prim::TPageAttr::Tex8Bits);
        tp.set(psyqo::Prim::TPageAttr::FullBackAndFullFront).setDithering(true);
        m_tp = tp;
        for (unsigned p = 0; p < 2; p++) {
            for (unsigned i = 0; i < NQ; i++) {
                auto& q = m_quads[p][i].primitive;
                q.clutIndex = psyqo::PrimPieces::ClutIndex(psyqo::Vertex{{.x = pl::CLUT_X, .y = pl::CLUT_Y_A}});
                q.tpage = tp;
                auto& q2 = m_quads2[p][i].primitive;
                q2.clutIndex = psyqo::PrimPieces::ClutIndex(psyqo::Vertex{{.x = pl::CLUT_X, .y = pl::CLUT_Y_B}});
                q2.tpage = tp;
            }
            m_win[p].primitive.command = 0xe2000000 | 16 | (16 << 5);  // 128x128 window, offset 0
            m_winOff[p].primitive.command = 0xe2000000;
        }
        m_ready = true;
    }

    struct Layer {
        int32_t radScale;   // Q8 on the ring radius
        int32_t twistMul;   // Q8 on the twist angle (negative mirrors it)
        int32_t zScale;     // Q8 on the ring depths (a half step interleaves the rings)
        int32_t bendMul;    // Q8 on the bend
        int32_t vOff;       // texels added to the scroll
        int32_t bright;     // Q8 on the ring colours
        bool second;        // additive overlay on CLUT B, draws nothing into the handoff
    };

    void layer(Frame& f, TunnelOT& ot, TQuad* quads, int32_t pos, const Layer& L) {
        namespace G = psyqo::GTE;
        auto& trig = f.trig;
        G::write<G::Register::TRX, G::Safe>(uint32_t(0));
        G::write<G::Register::TRY, G::Safe>(uint32_t(0));
        G::write<G::Register::TRZ, G::Safe>(uint32_t(0));
        G::write<G::Register::OFX, G::Safe>(160u << 16);
        G::write<G::Register::OFY, G::Safe>(120u << 16);
        G::write<G::Register::H, G::Safe>(PROJ_H);
        auto ident = psyqo::SoftMath::generateRotationMatrix33(angRaw(0), psyqo::SoftMath::Axis::X, trig);
        G::writeSafe<G::PseudoRegister::Rotation>(ident);

        int32_t kickDt = pos - m_kickAt;
        int32_t kickEnv = (kickDt >= 0 && kickDt < 24) ? (24 - kickDt) * 10 : 0;  // 0..240
        int32_t surge = (kickEnv * 40) >> 8;                                         // z squeeze, 0..37 of 256
        // opening: radius multiplier 1/(1-e), e up to .97
        int32_t mulR = 256;
        if (m_open > 0) mulR = int32_t((256 * 256) / (256 - clampi(m_open, 0, 248)));
        int32_t tw = (int32_t(m_twist >> 8) * L.twistMul) >> 8;
        // counter band: sweeps from the far end toward the camera
        int32_t bandDt = pos - m_bandAt;
        int32_t bandRing = -1;
        if (bandDt >= 0 && bandDt < 96) bandRing = (NR - 1) - (bandDt * (NR - 1)) / 96;
        int32_t sway = trig.sin(angRaw(pos * 3)).value;

        int16_t vx[NV + 3], vy[NV + 3], vz[NV + 3];
        for (int k = 0; k < NR; k++) {
            int32_t z = (((int32_t(kZ[k]) * (256 - surge)) >> 8) * L.zScale) >> 8;
            int32_t kk = (k * k * 256) / (NR * NR);  // bend weight 0..~230
            int32_t cx = (((m_bendX * kk >> 8) + ((sway * k * 40) >> 12)) * L.bendMul) >> 8;
            int32_t cy = ((m_bendY * kk >> 8) * L.bendMul) >> 8;
            int32_t r = (R0 * L.radScale) >> 8;
            if (bandRing >= 0) {
                int32_t dk = iabs(k - bandRing);
                if (dk < 3) r += (90 * (3 - dk)) / 3;
            }
            r += (r * kickEnv) >> 12;
            r = (r * mulR) >> 8;
            int32_t ta = tw + k * kRingTwist;
            int32_t ct = trig.cos(angRaw(ta)).value, st = trig.sin(angRaw(ta)).value;
            for (int j = 0; j < NS; j++) {
                int32_t ux = (int32_t(m_cj[j]) * ct - int32_t(m_sj[j]) * st) >> 12;
                int32_t uy = (int32_t(m_sj[j]) * ct + int32_t(m_cj[j]) * st) >> 12;
                int i = k * NS + j;
                vx[i] = int16_t(clampi(cx + ((r * ux) >> 12), -30000, 30000));
                vy[i] = int16_t(clampi(cy + ((r * uy) >> 12), -30000, 30000));
                vz[i] = int16_t(z);
            }
        }
        for (int i = NV; i < NV + 3; i++) vx[i] = vy[i] = 0, vz[i] = 256;

        static int16_t sx[NV + 3], sy[NV + 3];
        for (int i = 0; i < NV; i += 3) {
            psyqo::Vec3 v0, v1, v2;
            v0.x.value = vx[i]; v0.y.value = vy[i]; v0.z.value = vz[i];
            v1.x.value = vx[i + 1]; v1.y.value = vy[i + 1]; v1.z.value = vz[i + 1];
            v2.x.value = vx[i + 2]; v2.y.value = vy[i + 2]; v2.z.value = vz[i + 2];
            G::writeSafe<G::PseudoRegister::V0>(v0);
            G::writeSafe<G::PseudoRegister::V1>(v1);
            G::writeSafe<G::PseudoRegister::V2>(v2);
            G::Kernels::rtpt();
            uint32_t p0, p1, p2;
            G::read<G::Register::SXY0>(&p0);
            G::read<G::Register::SXY1>(&p1);
            G::read<G::Register::SXY2>(&p2);
            sx[i] = int16_t(p0); sy[i] = int16_t(p0 >> 16);
            sx[i + 1] = int16_t(p1); sy[i + 1] = int16_t(p1 >> 16);
            sx[i + 2] = int16_t(p2); sy[i + 2] = int16_t(p2 >> 16);
        }

        // ring colours: fog toward the far end, kick pulse, the counter band as a warm highlight
        psyqo::Color rc[NR];
        for (int k = 0; k < NR; k++) {
            // depth shade: full at the near ring, near black at the far one (modulation 128 is neutral)
            int32_t t = ((NR - 1 - k) * 256) / (NR - 1);
            int32_t b = 8 + ((142 * ((t * 154 >> 8) + ((t * t >> 8) * 102 >> 8))) >> 8);
            b += (kickEnv * 30) >> 8;
            b = (b * L.bright) >> 8;
            if (m_inverted) b = (b * 190) >> 8;
            int32_t cr = b, cg = b, cb = b;
            if (bandRing >= 0) {
                int32_t dk = iabs(k - bandRing);
                if (dk < 3) {
                    int32_t w = (3 - dk) * 85;  // 0..255
                    cr += (w * 90) >> 8;
                    cg += (w * 50) >> 8;
                    cb -= (w * 40) >> 8;
                }
            }
            if (L.second) cr = (cr * 3) >> 2, cg = (cg * 3) >> 2, cb = (cb * 3) >> 2;
            rc[k] = psyqo::Color{{.r = uint8_t(clampi(cr, 0, 255)), .g = uint8_t(clampi(cg, 0, 255)), .b = uint8_t(clampi(cb, 0, 255))}};
        }

        int32_t v0b = ((m_voff >> 8) + L.vOff) & 127;  // m_voff is texels in Q8, one 128 texel period
        unsigned drawn = 0;
        for (int k = 0; k < NR - 1; k++) {
            int32_t va = v0b + k * 8, vb = va + 8;
            for (int j = 0; j < NS; j++) {
                int j1 = (j + 1) % NS;
                int a = k * NS + j, b = k * NS + j1, c = (k + 1) * NS + j, d = (k + 1) * NS + j1;
                int32_t xa = sx[a], ya = sy[a], xb = sx[b], yb = sy[b], xc = sx[c], yc = sy[c], xd = sx[d], yd = sy[d];
                // the GPU drops primitives with a span over 1023 x 511 and wraps coordinates at 11 bits
                int32_t minx = xa, maxx = xa, miny = ya, maxy = ya;
                minx = xb < minx ? xb : minx; maxx = xb > maxx ? xb : maxx; miny = yb < miny ? yb : miny; maxy = yb > maxy ? yb : maxy;
                minx = xc < minx ? xc : minx; maxx = xc > maxx ? xc : maxx; miny = yc < miny ? yc : miny; maxy = yc > maxy ? yc : maxy;
                minx = xd < minx ? xd : minx; maxx = xd > maxx ? xd : maxx; miny = yd < miny ? yd : miny; maxy = yd > maxy ? yd : maxy;
                if (minx < -1000 || maxx > 1000 || miny < -1000 || maxy > 1000 || maxx - minx > 1020 || maxy - miny > 508) continue;
                if (maxx < 0 || minx > 320 || maxy < 0 || miny > 240) continue;
                auto& fr = quads[k * NS + j];
                auto& g = fr.primitive;
                g.setColorA(rc[k]).setColorB(rc[k]).setColorC(rc[k + 1]).setColorD(rc[k + 1]);
                if (L.second) {
                    g.setSemiTrans();
                } else {
                    g.setOpaque();
                }
                g.pointA.x = int16_t(xa); g.pointA.y = int16_t(ya);
                g.pointB.x = int16_t(xb); g.pointB.y = int16_t(yb);
                g.pointC.x = int16_t(xc); g.pointC.y = int16_t(yc);
                g.pointD.x = int16_t(xd); g.pointD.y = int16_t(yd);
                // two repeats of the 128 texel tile around the ring; the last patch ends on 255, a texel short
                uint8_t u0 = uint8_t(j * 16), u1 = uint8_t(j * 16 + 16 > 255 ? 255 : j * 16 + 16);
                g.uvA.u = u0; g.uvA.v = uint8_t(va);
                g.uvB.u = u1; g.uvB.v = uint8_t(va);
                g.uvC.u = u0; g.uvC.v = uint8_t(vb);
                g.uvD.u = u1; g.uvD.v = uint8_t(vb);
                ot.insert(fr, k);
                drawn++;
            }
        }

        if (L.second) {
            m_drawn2 = drawn;
        } else {
            m_drawn = drawn;
            if (f.clock.bar == LAST_BAR && f.clock.frac >= 128) writeStars(g_handoff, sx, sy, NV);
        }
    }

    bool m_ready = false;
    int16_t m_cj[NS], m_sj[NS];
    psyqo::PrimPieces::TPageAttr m_tp;
    int32_t m_speed = 90 << 8;

    // state
    uint32_t m_lastPos = 0;
    bool m_havePos = false;
    int32_t m_voff = 0, m_scroll = 0;
    int32_t m_twist = 0, m_twistRate = 0, m_twistSign = 1;
    int32_t m_bendX = 0, m_bendY = 0, m_bendTX = 0, m_bendTY = 0;
    int32_t m_leadMid = 0, m_lastLead = 0;
    int32_t m_kickAt = 0, m_bandAt = 0, m_invAt = 0, m_flashAt = 0, m_flashAmt = 0, m_lastOrch = 0, m_lastCrash = 0;
    int32_t m_open = 0;
    unsigned m_toms = 0, m_pendToms = 0;
    uint8_t m_lead[4] = {};
    unsigned m_nLead = 0;
    bool m_pendKick = false, m_pendCounter = false, m_pendOrch = false, m_pendCrash = false;
    unsigned m_drawn = 0, m_drawn2 = 0;
    bool m_inverted = false;
    uint32_t m_logFrame = 0, m_logTicks = 0;

    // packets
    TQuad m_quads[2][NQ];
    TunnelOT m_ots[2], m_ots2[2];
    TQuad m_quads2[2][NQ];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::FastFill> m_clear[2];
    psyqo::Fragments::SimpleFragment<TexWindow> m_win[2], m_winOff[2];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::TPage> m_tpAdd[2];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::Rectangle> m_flash[2];
};

Tunnel s_section;

}  // namespace

Section& section6() { return s_section; }
}  // namespace demo
