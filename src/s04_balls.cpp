// Section 4: VECTOR BALLS (bars 49-64). See s04_balls.hh for the reusable ball field.
#include "s04_balls.hh"

#include "log.hh"
#include "handoff.hh"
#include "psyqo/fixed-point.hh"
#include "psyqo/gte-kernels.hh"
#include "psyqo/gte-registers.hh"
#include "psyqo/soft-math.hh"

namespace demo {

namespace {

constexpr int TEX_X = 960, TEX_Y = 0;
constexpr int CLUT_X = 768, CLUT_Y = 500;
constexpr int BALL_R = 130;   // world radius, GTE units
constexpr int PROJ_H = 256;   // GTE projection distance
constexpr int S4_FIRST_BAR = 49;
constexpr int S4_LAST_BAR = 64;

inline int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int32_t mini(int32_t a, int32_t b) { return a < b ? a : b; }

uint32_t isqrt(uint32_t n) {
    uint32_t r = 0, bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) {
            n -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

inline uint32_t hash8(uint32_t i) { return ((i + 1) * 2654435761u) >> 24; }

inline psyqo::Angle angRaw(int32_t raw) { return psyqo::Angle(raw & 2047, psyqo::Angle::RAW); }

// 0..1535 around the wheel
void hueRGB(int32_t h, int32_t& r, int32_t& g, int32_t& b) {
    h %= 1536;
    if (h < 0) h += 1536;
    int32_t f = h & 255;
    switch (h >> 8) {
        case 0: r = 255; g = f; b = 0; break;
        case 1: r = 255 - f; g = 255; b = 0; break;
        case 2: r = 0; g = 255; b = f; break;
        case 3: r = 0; g = 255 - f; b = 255; break;
        case 4: r = f; g = 0; b = 255; break;
        default: r = 255; g = 0; b = 255 - f; break;
    }
}

}  // namespace

void BallField::build(psyqo::Trig<>& trig) {
    auto cs = [&](int32_t raw, int32_t& c, int32_t& s) {
        c = trig.cos(angRaw(raw)).value;
        s = trig.sin(angRaw(raw)).value;
    };
    for (unsigned i = 0; i < COUNT; i++) {
        int32_t c, s;
        // sphere: Fibonacci spiral, golden angle = 0.7639 turn = 1564 raw
        {
            int32_t y = 4096 - int32_t(2 * i + 1) * 32;
            int32_t r = int32_t(isqrt(uint32_t(4096 * 4096 - y * y)));
            cs(int32_t(i) * 1564, c, s);
            m_forms[SPHERE][i][0] = int16_t((((r * c) >> 12) * 1000) >> 12);
            m_forms[SPHERE][i][1] = int16_t((y * 1000) >> 12);
            m_forms[SPHERE][i][2] = int16_t((((r * s) >> 12) * 1000) >> 12);
        }
        // torus: 16 around, 8 tube
        {
            int32_t u = int32_t(i & 15) * 128, v = int32_t(i >> 4) * 256;
            int32_t cv, sv;
            cs(v, cv, sv);
            int32_t tube = 800 + ((330 * cv) >> 12);
            cs(u, c, s);
            m_forms[TORUS][i][0] = int16_t((tube * c) >> 12);
            m_forms[TORUS][i][1] = int16_t((330 * sv) >> 12);
            m_forms[TORUS][i][2] = int16_t((tube * s) >> 12);
        }
        // cube lattice: 5x5x5 (+3 at the centre)
        {
            if (i < 125) {
                m_forms[LATTICE][i][0] = int16_t((int32_t(i % 5) - 2) * 430);
                m_forms[LATTICE][i][1] = int16_t((int32_t((i / 5) % 5) - 2) * 430);
                m_forms[LATTICE][i][2] = int16_t((int32_t(i / 25) - 2) * 430);
            } else {
                m_forms[LATTICE][i][0] = int16_t((int32_t(i) - 126) * 40);
                m_forms[LATTICE][i][1] = 0;
                m_forms[LATTICE][i][2] = 0;
            }
        }
        // double helix: 2 strands of 64
        {
            int32_t st = int32_t(i >> 6), sidx = int32_t(i & 63);
            cs(sidx * 96 + st * 1024, c, s);
            m_forms[HELIX][i][0] = int16_t((650 * c) >> 12);
            m_forms[HELIX][i][1] = int16_t((sidx * 2 - 63) * 17);
            m_forms[HELIX][i][2] = int16_t((650 * s) >> 12);
        }
        m_swellAt[i] = -1000000;
    }
    for (unsigned k = 0; k < 3; k++) {
        for (unsigned a = 0; a < 3; a++) m_cur[COUNT + k][a] = 0;
    }
}

// Sphere: lit with a fixed light, 255 shades. Blob: smooth falloff. Both in one 8bpp page.
void BallField::uploadSprite(Frame& f) {
    static uint16_t tex[32 * 32];  // 64x32 texels, 8bpp -> 32 halfwords per row
    static uint16_t clut[256];
    for (int i = 1; i < 256; i++) {
        uint32_t v = (uint32_t(i) * 31 + 127) / 255;
        clut[i] = uint16_t(v | (v << 5) | (v << 10) | 0x8000);
    }
    clut[0] = 0;
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 64; x += 2) {
            uint8_t px[2];
            for (int k = 0; k < 2; k++) {
                int tx = (x + k) & 31;
                int32_t nx = (tx * 2 - 31) * 32, ny = (y * 2 - 31) * 32;  // +-0.97 in 1.10
                int32_t r2 = nx * nx + ny * ny;
                if (r2 >= (1 << 20)) {
                    px[k] = 0;
                    continue;
                }
                if (x + k < 32) {
                    int32_t nz = int32_t(isqrt(uint32_t((1 << 20) - r2)));
                    int32_t d = (nx * -480 + ny * -600 + nz * 700) >> 10;  // light (-.47,-.59,.68)
                    if (d < 0) d = 0;
                    int32_t e = (d * d) >> 10;
                    e = (e * e) >> 10;
                    e = (e * e) >> 10;  // d^8
                    int32_t v = 160 + ((d * 640) >> 10) + ((e * 400) >> 10);
                    v = (v * (640 + ((nz * 384) >> 10))) >> 10;  // darker toward the silhouette
                    px[k] = uint8_t(clampi(v >> 2, 1, 255));
                } else {
                    int32_t q = ((1 << 20) - r2) >> 6;  // 0..16384
                    int32_t v = (q * q) >> 12;          // 0..65536
                    px[k] = uint8_t(clampi((v * 230) >> 16, 1, 255));
                }
            }
            tex[y * 32 + (x >> 1)] = uint16_t(px[0] | (px[1] << 8));
        }
    }
    f.gpu.waitChainIdle();
    f.gpu.uploadToVRAM(tex, psyqo::Rect{.pos = {{.x = TEX_X, .y = TEX_Y}}, .size = {{.w = 32, .h = 32}}});
    f.gpu.uploadToVRAM(clut, psyqo::Rect{.pos = {{.x = CLUT_X, .y = CLUT_Y}}, .size = {{.w = 256, .h = 1}}});
}

void BallField::init(Frame& f) {
    if (m_ready) return;
    m_trig = &f.trig;
    build(f.trig);
    uploadSprite(f);
    psyqo::PrimPieces::TPageAttr tp;
    tp.setPageX(TEX_X / 64).setPageY(TEX_Y / 256).set(psyqo::Prim::TPageAttr::Tex8Bits);
    tp.set(psyqo::Prim::TPageAttr::FullBackAndFullFront).setDithering(true);
    for (unsigned p = 0; p < 2; p++) {
        for (unsigned i = 0; i < COUNT; i++) {
            auto& q = m_quads[p][i].frag.primitive;
            q.clutIndex = psyqo::PrimPieces::ClutIndex(CLUT_X >> 4, CLUT_Y);
            q.tpage = tp;
        }
    }
    m_ready = true;
}

void BallField::setFormation(Formation f, uint32_t pos256, bool instant) {
    if (instant) {
        m_form = m_next = f;
        m_morphing = false;
        return;
    }
    morphPositions(pos256, m_from);
    m_next = f;
    m_morphAt = pos256;
    m_morphing = true;
}

void BallField::morphPositions(uint32_t pos256, int16_t (*out)[3]) const {
    if (!m_morphing) {
        for (unsigned i = 0; i < COUNT; i++) {
            for (unsigned a = 0; a < 3; a++) out[i][a] = m_forms[m_form][i][a];
        }
        return;
    }
    int32_t s = clampi(int32_t(pos256 - m_morphAt), 0, 256);
    int32_t e = ((768 - 2 * s) * s * s) >> 16;  // smoothstep, 0..256
    for (unsigned i = 0; i < COUNT; i++) {
        for (unsigned a = 0; a < 3; a++) {
            int32_t from = m_from[i][a], to = m_forms[m_next][i][a];
            out[i][a] = int16_t(from + (((to - from) * e) >> 8));
        }
    }
}

void BallField::lead(uint8_t note, uint32_t pos256) {
    if (!m_ready || !m_trig) return;
    int32_t c = m_trig->cos(angRaw(int32_t(note) * 171)).value;
    int32_t s = m_trig->sin(angRaw(int32_t(note) * 171)).value;
    int best = -1;
    int32_t bestKey = 1 << 30;
    for (unsigned i = 0; i < COUNT; i++) {
        int32_t dx = m_sx[i] - 160, dy = m_sy[i] - 120;
        int32_t dot = dx * c + dy * s;
        if (dot <= 0) continue;
        int32_t ax = iabs(dx), ay = iabs(dy);
        int32_t len = (ax > ay ? ax + (ay >> 1) : ay + (ax >> 1)) + 4;
        int32_t key = iabs(dx * s - dy * c) / len + ((m_sz[i] - m_dist) >> 4);
        if (key < bestKey) {
            bestKey = key;
            best = int(i);
        }
    }
    if (best >= 0) m_swellAt[best] = int32_t(pos256);
}

void BallField::stepAxis() {
    m_axis = (m_axis + 1) % 3;
    m_sign[m_axis] = -m_sign[m_axis];
    m_rate[m_axis] = 4200 * m_sign[m_axis];
}

void BallField::cameraCut(unsigned preset) {
    static const int16_t startDist[4] = {4700, 1900, 3900, 2100};
    static const int16_t endDist[4] = {3000, 3300, 2700, 3200};
    static const int16_t jx[4] = {0, 640, -420, 900}, jy[4] = {0, 500, 900, -300};
    preset &= 3;
    camDist = startDist[preset];
    m_camTarget = endDist[preset];
    m_ang[0] += jx[preset] * 256;
    m_ang[1] += jy[preset] * 256;
}

void BallField::update(const Clock& c) {
    m_pos = c.pos256;
    int32_t d = m_havePos ? int32_t(c.pos256 - m_lastPos) : 0;
    if (d < 0 || d > 64) d = 0;  // loop wrap or a hitch: skip rather than jump
    m_lastPos = c.pos256;
    m_havePos = true;
    int32_t ds = mini(d, 16);

    if (m_morphing && int32_t(c.pos256 - m_morphAt) >= 256) {
        m_form = m_next;
        m_morphing = false;
    }
    for (unsigned a = 0; a < 3; a++) {
        int32_t tgt = (a == m_axis ? 1400 : 220) * m_sign[a];
        m_rate[a] += ((tgt - m_rate[a]) * ds) >> 5;
        m_ang[a] += ((m_rate[a] * spinMul) >> 8) * d;
    }
    camDist += ((m_camTarget - camDist) * ds) >> 6;
    spread += ((spreadTarget - spread) * ds) >> 3;
    m_hue = (m_hue + d * 2) % 1536;
}

void BallField::submit(Frame& f, BallOT& ot) {
    namespace G = psyqo::GTE;
    unsigned parity = f.gpu.getParity();
    m_dist = int16_t(camDist);

    G::write<G::Register::TRX, G::Safe>(uint32_t(0));
    G::write<G::Register::TRY, G::Safe>(uint32_t(0));
    G::write<G::Register::TRZ, G::Safe>(uint32_t(camDist));
    G::write<G::Register::OFX, G::Safe>(160u << 16);
    G::write<G::Register::OFY, G::Safe>(120u << 16);
    G::write<G::Register::H, G::Safe>(PROJ_H);

    auto rx = psyqo::SoftMath::generateRotationMatrix33(angRaw(m_ang[0] >> 8), psyqo::SoftMath::Axis::X, f.trig);
    auto ry = psyqo::SoftMath::generateRotationMatrix33(angRaw(m_ang[1] >> 8), psyqo::SoftMath::Axis::Y, f.trig);
    auto rz = psyqo::SoftMath::generateRotationMatrix33(angRaw(m_ang[2] >> 8), psyqo::SoftMath::Axis::Z, f.trig);
    psyqo::SoftMath::multiplyMatrix33(rx, ry, &rx);
    psyqo::SoftMath::multiplyMatrix33(rx, rz, &rx);
    G::writeSafe<G::PseudoRegister::Rotation>(rx);

    morphPositions(m_pos, m_cur);
    int32_t extra = spread - 256;
    if (extra != 0) {
        for (unsigned i = 0; i < COUNT; i++) {
            int32_t sc = 256 + ((extra * (160 + int32_t(hash8(i) & 127))) >> 8);
            for (unsigned a = 0; a < 3; a++) m_cur[i][a] = int16_t(clampi((m_cur[i][a] * sc) >> 8, -30000, 30000));
        }
    }

    for (unsigned i = 0; i < COUNT; i += 3) {
        psyqo::Vec3 v0, v1, v2;
        v0.x.value = m_cur[i][0]; v0.y.value = m_cur[i][1]; v0.z.value = m_cur[i][2];
        v1.x.value = m_cur[i + 1][0]; v1.y.value = m_cur[i + 1][1]; v1.z.value = m_cur[i + 1][2];
        v2.x.value = m_cur[i + 2][0]; v2.y.value = m_cur[i + 2][1]; v2.z.value = m_cur[i + 2][2];
        G::writeSafe<G::PseudoRegister::V0>(v0);
        G::writeSafe<G::PseudoRegister::V1>(v1);
        G::writeSafe<G::PseudoRegister::V2>(v2);
        G::Kernels::rtpt();
        uint32_t p0, p1, p2, z1, z2, z3;
        G::read<G::Register::SXY0>(&p0);
        G::read<G::Register::SXY1>(&p1);
        G::read<G::Register::SXY2>(&p2);
        G::read<G::Register::SZ1>(&z1);
        G::read<G::Register::SZ2>(&z2);
        G::read<G::Register::SZ3>(&z3);
        m_sx[i] = int16_t(p0); m_sy[i] = int16_t(p0 >> 16); m_sz[i] = int32_t(z1);
        m_sx[i + 1] = int16_t(p1); m_sy[i + 1] = int16_t(p1 >> 16); m_sz[i + 1] = int32_t(z2);
        m_sx[i + 2] = int16_t(p2); m_sy[i + 2] = int16_t(p2 >> 16); m_sz[i + 2] = int32_t(z3);
    }

    int32_t kickDt = int32_t(m_pos - m_kickAt);
    int32_t kickEnv = (kickDt >= 0 && kickDt < 24) ? (24 - kickDt) * 10 : 0;  // 0..240
    bool semi = blur > 0;
    int u0 = blur >= 128 ? 32 : 0;
    int sizeMul = 256 + ((blur * 190) >> 8);
    int dimMul = 256 - ((blur * 150) >> 8);

    unsigned visible = 0;
    for (unsigned i = 0; i < COUNT; i++) {
        int32_t z = m_sz[i];
        if (z < 300) continue;
        int32_t bucket = (z - (camDist - 1800)) >> 4;
        bucket = clampi(bucket, 0, BALL_OT_SIZE - 1);

        int32_t rev = clampi((reveal - int32_t(hash8(i & 0xff) >> 1)) * 2, 0, 256);
        if (rev == 0) continue;
        int32_t swellDt = int32_t(m_pos) - m_swellAt[i];
        int32_t swell = (swellDt >= 0 && swellDt < 64) ? (64 - swellDt) * 4 : 0;
        int32_t r = BALL_R + ((BALL_R * swell) >> 8);
        r += (r * kickEnv) >> 11;
        r = (r * rev) >> 8;
        r = (r * sizeMul) >> 8;
        int32_t half = clampi((r * PROJ_H) / z, 1, 48);

        int32_t hr, hg, hb;
        hueRGB(m_hue + int32_t(i) * 3, hr, hg, hb);
        hr = (hr * 180 >> 8) + 76;
        hg = (hg * 180 >> 8) + 76;
        hb = (hb * 180 >> 8) + 76;
        int32_t dep = 255 - ((bucket * 140) >> 8);
        int32_t m = (150 * dep) >> 8;
        int32_t cr = (hr * m) >> 8, cg = (hg * m) >> 8, cb = (hb * m) >> 8;
        cr += ((255 - cr) * swell * 3) >> 10;
        cg += ((255 - cg) * swell * 3) >> 10;
        cb += ((255 - cb) * swell * 3) >> 10;
        int32_t fb = (flash * 70) >> 8;
        cr = ((cr + fb) * dimMul) >> 8;
        cg = ((cg + fb) * dimMul) >> 8;
        cb = ((cb + fb) * dimMul) >> 8;

        auto& frag = m_quads[parity][i].frag;
        auto& p = frag.primitive;
        p.setColor(psyqo::Color{{.r = uint8_t(clampi(cr, 0, 255)), .g = uint8_t(clampi(cg, 0, 255)),
                                 .b = uint8_t(clampi(cb, 0, 255))}});
        if (semi) {
            p.setSemiTrans();
        } else {
            p.setOpaque();
        }
        int16_t x = m_sx[i], y = m_sy[i];
        p.pointA.x = int16_t(x - half); p.pointA.y = int16_t(y - half);
        p.pointB.x = int16_t(x + half); p.pointB.y = int16_t(y - half);
        p.pointC.x = int16_t(x - half); p.pointC.y = int16_t(y + half);
        p.pointD.x = int16_t(x + half); p.pointD.y = int16_t(y + half);
        p.uvA.u = uint8_t(u0);      p.uvA.v = 0;
        p.uvB.u = uint8_t(u0 + 31); p.uvB.v = 0;
        p.uvC.u = uint8_t(u0);      p.uvC.v = 31;
        p.uvD.u = uint8_t(u0 + 31); p.uvD.v = 31;
        ot.insert(frag, bucket);

        if (writeHandoff && f.clock.bar == S4_LAST_BAR && x > -40 && x < 360 && y > -40 && y < 280 &&
            visible < Handoff::MAX_BALLS) {
            g_handoff.ballX[visible] = x;
            g_handoff.ballY[visible] = y;
            visible++;
        }
    }
    if (writeHandoff && f.clock.bar == S4_LAST_BAR) g_handoff.ballCount = visible;
}

namespace {

class BallsSection final : public Section {
  public:
    const char* name() const override { return "S4 VECTOR_BALLS"; }

    void begin(const Clock& c, Section*) override {
#ifdef BALLS_TEST_FILL
        g_handoff.fill = {{.r = 0xb0, .g = 0x80, .b = 0x30}};
#endif
        m_fill = g_handoff.fill;
        // Formation and camera follow the bar, so DEBUG_SKIP into the middle of the section still looks right.
        int bar = c.sectionBar;
        unsigned form = bar >= S4_FIRST_BAR ? unsigned((bar - S4_FIRST_BAR) / 2) & 3 : 0;
        m_field.setFormation(BallField::Formation(form), 0, true);
        m_field.camDist = 3000;
        m_field.spread = m_field.spreadTarget = 256;
        m_field.blur = 0;
        m_field.writeHandoff = true;
        m_orchCount = bar > S4_FIRST_BAR ? 1 : 0;
        m_cuts = bar >= 57 ? 2 : (bar > S4_FIRST_BAR ? 1 : 0);
        m_lastOrch = m_lastCut = -100000;
        m_throwHits = 0;
        m_nLead = 0;
        m_pendKick = m_pendCounter = m_pendOrch = m_pendCrash = false;
        m_pendSnare = 0;
        m_flashAt = -100000;
    }

    void onNote(const Clock& c, unsigned ch, uint8_t note, uint8_t, uint32_t tick) override {
#ifdef BALLS_LOG
        if (ch == CH_ORCH || (ch == CH_DRUMS && note == KEY_CRASH)) {
            DEMO_LOG("S4 ch %d note %d at bar %d frac %d tick %d\n", int(ch), int(note), c.bar, c.frac, int(tick));
        }
#endif
        switch (ch) {
            case CH_LEAD:
                if (m_nLead < 4) m_lead[m_nLead++] = note;
                break;
            case CH_COUNTER: m_pendCounter = true; break;
            case CH_ORCH: m_pendOrch = true; break;
            case CH_DRUMS:
                if (note == KEY_KICK) m_pendKick = true;
                if (note == KEY_CRASH) m_pendCrash = true;
                if (note == KEY_SNARE || note == KEY_SNARE2) m_pendSnare++;
                break;
            default: break;
        }
    }

    void update(const Clock& c) override {
        int32_t pos = int32_t(c.pos256);
        if (m_pendOrch) {
            m_pendOrch = false;
            if (pos - m_lastOrch > 128) {
                m_lastOrch = pos;
                if (m_orchCount++ > 0) {
                    // the hit at bar 49 is the entrance; every later hit morphs
                    m_field.setFormation(BallField::Formation((m_field.target() + 1) & 3), c.pos256);
                }
                m_flashAt = pos;
                m_flashAmt = 120;
            }
        }
        if (m_pendCrash) {
            m_pendCrash = false;
            if (pos - m_lastCut > 64) {
                m_lastCut = pos;
                m_field.cameraCut(m_cuts++);
                m_flashAt = pos;
                m_flashAmt = 256;
            }
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
        if (c.bar == S4_LAST_BAR) {
            m_throwHits += m_pendSnare;
            m_field.spreadTarget = mini(256 + int32_t(m_throwHits) * 24, 1100);
        }
        m_pendSnare = 0;

        int32_t rel = pos - (S4_FIRST_BAR - 1) * 256;
        m_field.reveal = clampi(rel * 2, 0, 384);
        int32_t fd = pos - m_flashAt;
        m_field.flash = (fd >= 0 && fd < 64) ? (m_flashAmt * (64 - fd)) >> 6 : 0;
        m_field.blur = (c.bar == S4_LAST_BAR && c.frac > 128) ? (c.frac - 128) * 2 : 0;
        m_field.spinMul = 256 + ((m_field.spread - 256) >> 1);
        m_rel = rel;
        m_field.update(c);
    }

    void render(Frame& f) override {
        if (!m_field.ready()) m_field.init(f);
        unsigned parity = f.gpu.getParity();
        // serial: rendered frames per music second (112 ticks), measured over 64 frames
        if (f.frameNo - m_logFrame >= 64) {
            uint32_t dt = f.clock.ticks - m_logTicks;
            if (m_logFrame && dt) {
                uint32_t fps10 = (f.frameNo - m_logFrame) * 1120 / dt;
                DEMO_LOG("S4 fps %d.%d at bar %d frac %d\n", int(fps10 / 10), int(fps10 % 10), f.clock.bar,
                                  f.clock.frac);
            }
            m_logFrame = f.frameNo;
            m_logTicks = f.clock.ticks;
        }
        int32_t t = clampi(m_rel * 2, 0, 256);
        auto mix = [&](int ch, int32_t dark) { return (ch * (256 - t) + dark * t) >> 8; };
        int32_t fl = m_field.flash >> 2;
        psyqo::Color bg{{.r = uint8_t(clampi(mix(m_fill.r, 5) + fl, 0, 255)),
                         .g = uint8_t(clampi(mix(m_fill.g, 7) + fl, 0, 255)),
                         .b = uint8_t(clampi(mix(m_fill.b, 18) + fl, 0, 255))}};
        f.gpu.getNextClear(m_clear[parity].primitive, bg);
        f.gpu.chain(m_clear[parity]);
        m_field.submit(f, m_ots[parity]);
        f.gpu.chain(m_ots[parity]);
    }

  private:
    BallField m_field;
    BallOT m_ots[2];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::FastFill> m_clear[2];
    psyqo::Color m_fill{{.r = 0, .g = 0, .b = 0}};
    int32_t m_rel = 0;
    uint32_t m_logFrame = 0, m_logTicks = 0;
    unsigned m_orchCount = 0, m_cuts = 0, m_throwHits = 0;
    int32_t m_lastOrch = 0, m_lastCut = 0, m_flashAt = 0, m_flashAmt = 0;
    uint8_t m_lead[4] = {};
    unsigned m_nLead = 0, m_pendSnare = 0;
    bool m_pendKick = false, m_pendCounter = false, m_pendOrch = false, m_pendCrash = false;
};

BallsSection s_section;

}  // namespace

Section& section4() { return s_section; }

}  // namespace demo
