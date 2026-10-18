// Section 5: PLASMA + MESSAGE (bars 65-80). See s05_plasma.hh for the VRAM map and the cycler.
//
// The plasma is a static 8bpp index texture (interference rings around the last S4 ball positions plus two plane
// waves, periodic in 128 so S6 can tile it); all motion is the palette: two layers, each a 256-entry CLUT that
// is rotated by one VRAM-to-VRAM copy per frame inside the chain.
#include "s05_plasma.hh"

#include "log.hh"
#include "handoff.hh"
#include "psyqo/primitives/quads.hh"
#include "psyqo/primitives/rectangles.hh"

namespace demo {
namespace pl {

namespace {

alignas(4) uint8_t g_tex[TEX_H * TEX_W];
alignas(4) uint16_t g_rows[4][512];
int16_t g_sin[256];       // +-128, 256 steps per turn, from psyqo::Trig
uint8_t g_dist[8193];     // sqrt of a toroidal squared distance (max 64^2 + 64^2)
bool g_tables = false;

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int S(int k) { return g_sin[k & 255]; }

void initTables(psyqo::Trig<>& trig) {
    if (g_tables) return;
    for (int k = 0; k < 256; k++) {
        g_sin[k] = int16_t(trig.sin(psyqo::Angle(k * 8, psyqo::Angle::RAW)).value >> 5);
    }
    unsigned r = 0;
    for (unsigned i = 0; i <= 8192; i++) {
        while ((r + 1) * (r + 1) <= i) r++;
        g_dist[i] = uint8_t(r);
    }
    // Palette rows: 3 bands over the 256 entries, so the cycle shows three waves of colour.
    for (int set = 0; set < 2; set++) {
        for (int i = 0; i < 256; i++) {
            int r, g, b;
            if (set == 0) {
                r = 128 + (S(3 * i + 20) * 110 >> 7);
                g = 64 + (S(3 * i + 100) * 56 >> 7);
                b = 150 + (S(3 * i - 60) * 105 >> 7);
            } else {
                r = 52 + (S(3 * i + 180) * 52 >> 7);
                g = 140 + (S(3 * i) * 105 >> 7);
                b = 150 + (S(3 * i + 90) * 105 >> 7);
            }
            r = clampi(r, 0, 255);
            g = clampi(g, 0, 255);
            b = clampi(b, 0, 255);
            for (int inv = 0; inv < 2; inv++) {
                int rr = inv ? 255 - r : r, gg = inv ? 255 - g : g, bb = inv ? 255 - b : b;
                uint16_t e = uint16_t((rr >> 3) | ((gg >> 3) << 5) | ((bb >> 3) << 10) | 0x8000);
                g_rows[set + 2 * inv][i] = e;
                g_rows[set + 2 * inv][i + 256] = e;
            }
        }
    }
    g_tables = true;
}

constexpr int MUL[4] = {6, 5, 7, 4};
constexpr int PH[4] = {0, 70, 150, 210};

struct TexWindowOff {  // GP0(E2h) with a zero mask: S3 leaves a 128x128 window set
    uint32_t command = 0xe2000000;
};

}  // namespace

Cycler& cycler() {
    static Cycler c;
    return c;
}

void Cycler::prepare(psyqo::GPU& gpu, psyqo::Trig<>& trig) {
    if (m_prepared) return;
    initTables(trig);
    gpu.waitChainIdle();
    gpu.uploadToVRAM(&g_rows[0][0], psyqo::Rect{ROW_X, ROW_Y, 512, 4});
    m_prepared = true;
}

void Cycler::restart(const int16_t* x, const int16_t* y, unsigned count) {
    static const int dx[4] = {32, 96, 60, 120}, dy[4] = {40, 30, 90, 100};
    for (unsigned k = 0; k < 4; k++) {
        if (count >= 4) {
            unsigned i = k * count / 4;
            m_cx[k] = (x[i] >> 1) & 127;
            m_cy[k] = (y[i] >> 1) & 127;
        } else {
            m_cx[k] = dx[k];
            m_cy[k] = dy[k];
        }
    }
    m_row = 0;
}

void Cycler::computeRow(unsigned y) {
    int tdx[4][128];
    int rowsq[4];
    for (int k = 0; k < 4; k++) {
        for (int x = 0; x < 128; x++) {
            int d = (x - m_cx[k]) & 127;
            if (d > 64) d = 128 - d;
            tdx[k][x] = d * d;
        }
        int d = (int(y) - m_cy[k]) & 127;
        if (d > 64) d = 128 - d;
        rowsq[k] = d * d;
    }
    uint8_t* out = &g_tex[y * TEX_W];
    const int py = int(y);
    for (int x = 0; x < 128; x++) {
        int a = S(2 * x + 2 * py) + S(6 * x - 4 * py + 90);
        for (int k = 0; k < 4; k++) {
            int d = g_dist[tdx[k][x] + rowsq[k]];
            a += S(d * MUL[k] + PH[k]);
        }
        out[x] = uint8_t(clampi(128 + ((a * 200) >> 10), 0, 255));
    }
    for (int x = 0; x < 32; x++) out[128 + x] = out[x];
}

bool Cycler::buildStep(psyqo::GPU& gpu, psyqo::Trig<>& trig, unsigned rows) {
    if (m_row >= 128) return true;
    initTables(trig);
    unsigned n = 128 - m_row < rows ? 128 - m_row : rows;
    for (unsigned r = 0; r < n; r++) computeRow(m_row + r);
    gpu.waitChainIdle();
    gpu.uploadToVRAM(reinterpret_cast<const uint16_t*>(&g_tex[m_row * TEX_W]),
                       psyqo::Rect{TEX_X, TEX_Y + int(m_row), TEX_W / 2, int(n)});
    m_row += n;
    return m_row >= 128;
}

void Cycler::ensureAll(psyqo::GPU& gpu, psyqo::Trig<>& trig) {
    prepare(gpu, trig);
    while (!buildStep(gpu, trig, 32)) {
    }
}

void prewarm(psyqo::GPU& gpu, psyqo::Trig<>& trig) {
    auto& c = cycler();
    if (c.textureReady()) return;
    c.restart(nullptr, nullptr, 0);
    c.ensureAll(gpu, trig);
}

void Cycler::advance(const Clock& c, int32_t speedPerSec) {
    int32_t dt = m_haveTicks ? int32_t(c.ticks - m_lastTicks) : 0;
    if (dt < 0 || dt > 64) dt = 0;
    m_lastTicks = c.ticks;
    m_haveTicks = true;
    int32_t step = dt * speedPerSec * 585;  // 65536 / 112 ticks per second
    m_shiftA += uint32_t(step);
    m_shiftB -= uint32_t((step >> 3) * 3);
    int32_t k = dt < 32 ? dt : 32;
    m_bump16 -= (m_bump16 * k) >> 5;
}

void Cycler::chainClut(Frame& f) {
    unsigned p = f.gpu.getParity();
    uint32_t sa = ((m_shiftA + uint32_t(m_bump16)) >> 16) & 255;
    uint32_t sb = ((m_shiftB - uint32_t(m_bump16)) >> 16) & 255;
    auto& a = m_blit[p][0].primitive;
    auto& b = m_blit[p][1].primitive;
    a.src = (ROW_X + sa) | (rowY(m_setA) << 16);
    a.dst = uint32_t(CLUT_X) | (uint32_t(CLUT_Y_A) << 16);
    a.size = 256u | (1u << 16);
    b.src = (ROW_X + sb) | (rowY(m_setB) << 16);
    b.dst = uint32_t(CLUT_X) | (uint32_t(CLUT_Y_B) << 16);
    b.size = 256u | (1u << 16);
    f.gpu.chain(m_blit[p][0]);
    f.gpu.chain(m_blit[p][1]);
    f.gpu.chain(m_flush[p]);
}

}  // namespace pl

namespace {

using namespace pl;

constexpr uint32_t REL0 = 64 * 256;  // clock.pos256 of the start of bar 65 minus the bar 65 offset: rel = pos - REL0

struct Line {
    int bar;
    const char* text;
};
constexpr Line kLines[] = {
    {65, "THE DOCS SAID FORTY."}, {67, "THE BUS SAID FORTY-ONE."}, {69, "THE BUS DOES NOT LIE."},
    {71, "I READ THE BUS."},      {75, "EVERY CYCLE COUNTS."},     {77, "SILICON DECIDES."},
};
constexpr unsigned kNumLines = sizeof(kLines) / sizeof(kLines[0]);
constexpr int kSlot = 512;  // two bars
constexpr int kAdvance = 13;
constexpr unsigned kMaxGlyphs = 24;

struct GlyphPrologue {
    psyqo::Prim::VRAMUpload upload;
    uint32_t pixel;
    psyqo::Prim::FlushCache flush;
    psyqo::Prim::TPage tpage;
};

using TQuad = psyqo::Fragments::SimpleFragment<psyqo::Prim::TexturedQuad>;

inline psyqo::Color gray(int v) {
    v = clampi(v, 0, 255);
    return psyqo::Color{{.r = uint8_t(v), .g = uint8_t(v), .b = uint8_t(v)}};
}

class Plasma final : public Section {
  public:
    const char* name() const override { return "S5 PLASMA_MESSAGE"; }

    void begin(const Clock& c, Section*) override {
        // Texture resident (prewarm): the balls seed a view offset into the periodic page. Otherwise rebuild
        // with the balls as ring centres.
        m_du = m_dv = 0;
        if (g_handoff.ballCount >= 4) {
            int sx = 0, sy = 0;
            for (unsigned k = 0; k < 4; k++) {
                unsigned i = k * g_handoff.ballCount / 4;
                sx += g_handoff.ballX[i] >> 1;
                sy += g_handoff.ballY[i] >> 1;
            }
            m_du = ((sx / 4 - 77) & 127) % 96;
            m_dv = (sy / 4 - 65) & 127;
        }
        if (!cycler().textureReady()) cycler().restart(g_handoff.ballX, g_handoff.ballY, g_handoff.ballCount);
        for (auto& t : m_trig) t = -1;
        m_pendCounter = false;
        m_ripple = -100000;
        m_nSnare = 0;
        m_speed = 14 * 256;
        m_logFrame = 0;
        cycler().setPalette(Set::A, Set::A);
        (void)c;
    }

    void onNote(const Clock&, unsigned ch, uint8_t note, uint8_t, uint32_t tick) override {
        if (ch == CH_COUNTER) m_pendCounter = true;
        if (ch == CH_DRUMS && (note == KEY_SNARE || note == KEY_SNARE2)) m_snare[m_nSnare++ & 7] = tick;
    }

    void update(const Clock& c) override {
        const int32_t pos = int32_t(c.pos256);
        const int32_t rel = pos - int32_t(REL0);
        m_rel = rel;
        if (m_pendCounter) {
            m_pendCounter = false;
            m_ripple = pos;
            cycler().bump(36);
            int k = lineAt(rel);
            if (k >= 0 && m_trig[k] < 0 && rel - (kLines[k].bar - 65) * 256 <= 128) {
                m_trig[k] = rel - (kLines[k].bar - 65) * 256;
            }
        }
        {
            int k = lineAt(rel);
            if (k >= 0 && m_trig[k] < 0 && rel - (kLines[k].bar - 65) * 256 >= 64) m_trig[k] = 64;
        }

        // cycle speed, entries per second
        int32_t target;
        const int32_t padEnv = 255 - c.frac;  // chord change on the bar line, decaying over the bar
        if (rel < 2048) {
            target = 14 + ((padEnv * 26) >> 8);
        } else if (rel < 3072) {
            target = 40 + ((rel - 2048) * 30 >> 10);
        } else {
            int dens = 0;
            for (unsigned i = 0; i < 8; i++) {
                if (i < m_nSnare && int32_t(c.ticks - m_snare[i]) >= 0 && int32_t(c.ticks - m_snare[i]) < 48) dens++;
            }
            target = 60 + 55 * dens;
        }
        int32_t dt = m_haveTick ? int32_t(c.ticks - m_lastTick) : 0;
        if (dt < 0 || dt > 32) dt = 0;
        m_lastTick = c.ticks;
        m_haveTick = true;
        int32_t t256 = target * 256;
        m_speed += ((t256 - m_speed) * (dt < 12 ? dt : 12)) / 12;
        cycler().advance(c, m_speed >> 8);

        cycler().setPalette(rel >= 3840 ? Set::B : Set::A, rel >= 3968 ? Set::B : Set::A);
    }

    void render(Frame& f) override {
        auto& gpu = f.gpu;
        unsigned p = gpu.getParity();
        if (f.frameNo - m_logFrame >= 64) {
            uint32_t dt = f.clock.ticks - m_logTicks;
            if (m_logFrame && dt) {
                uint32_t fps10 = (f.frameNo - m_logFrame) * 1120 / dt;
                DEMO_LOG("S5 fps %d.%d at bar %d frac %d\n", int(fps10 / 10), int(fps10 % 10), f.clock.bar,
                                  f.clock.frac);
            }
            m_logFrame = f.frameNo;
            m_logTicks = f.clock.ticks;
        }
        cycler().prepare(f);
        bool ready = cycler().textureReady() || cycler().buildStep(f, 32);  // only without prewarm()

        gpu.getNextClear(m_clear[p].primitive, {{.r = 0, .g = 0, .b = 0}});
        gpu.chain(m_clear[p]);
        if (!ready) return;

        const int32_t pos = int32_t(f.clock.pos256);
        const int32_t rel = m_rel;
        const int32_t padEnv = 255 - f.clock.frac;

        // brightness (0x80 = neutral modulation)
        int32_t m;
        if (rel < 2048) {
            m = 0x30 + (rel * 0x10 >> 11) + ((rel < 2048 ? padEnv * 0x14 : 0) >> 8);
        } else if (rel < 3072) {
            m = 0x40 + ((rel - 2048) * 0x34 >> 10);
        } else {
            m = 0x74 + ((rel - 3072) * 0x1c >> 10);
        }
        m = (m * (150 + clampi(rel * 106 / 80, 0, 106))) >> 8;  // first frame already visible: S4's dark bed
        int32_t rd = pos - m_ripple;
        if (rd >= 0 && rd < 32) m += ((32 - rd) * 0x20) >> 5;
        int32_t mA = clampi(m, 0, 255), mB = clampi(m * 3 / 8, 0, 255);

        m_texWin[p].primitive.command = 0xe2000000 | 16 | (16 << 5);  // 128x128 window: the page is periodic
        gpu.chain(m_texWin[p]);
        cycler().chainClut(f);

        psyqo::PrimPieces::TPageAttr tpOpaque, tpAdd;
        tpOpaque.setPageX(TPAGE_X).setPageY(TPAGE_Y).set(psyqo::Prim::TPageAttr::Tex8Bits).setDithering(true).enableDisplayArea();
        tpAdd = tpOpaque;
        tpAdd.set(psyqo::Prim::TPageAttr::FullBackAndFullFront);

        auto& qa = m_qa[p].primitive;
        qa.setColor(gray(mA));
        qa.setOpaque();
        qa.pointA = {{.x = 0, .y = 0}};
        qa.pointB = {{.x = 320, .y = 0}};
        qa.pointC = {{.x = 0, .y = 240}};
        qa.pointD = {{.x = 320, .y = 240}};
        qa.uvA = {uint8_t(m_du), uint8_t(m_dv)};
        qa.uvB = {uint8_t(m_du + 159), uint8_t(m_dv)};
        qa.uvC = {uint8_t(m_du), uint8_t(m_dv + 119)};
        qa.uvD = {uint8_t(m_du + 159), uint8_t(m_dv + 119)};
#ifdef PLASMA_TILE_TEST
        // 128x128 texture window, UV 0..255: the texture shows twice in each direction
        m_texWin[p].primitive.command = 0xe2000000 | 16 | (16 << 5);
        qa.uvB = {255, 0};
        qa.uvC = {0, 239};
        qa.uvD = {255, 239};
#endif
        qa.clutIndex = psyqo::PrimPieces::ClutIndex(psyqo::Vertex{{.x = CLUT_X, .y = CLUT_Y_A}});
        qa.tpage = tpOpaque;
        gpu.chain(m_qa[p]);

        int u0 = (16 + ((S(pos >> 2) * 16) >> 7) + 64 - m_du / 2) & 127, v0 = (12 + ((S((pos * 3 >> 3) + 64) * 12) >> 7) + 32 + m_dv) & 127;
        auto& qb = m_qb[p].primitive;
        qb.setColor(gray(mB));
        qb.setSemiTrans();
        qb.pointA = {{.x = 0, .y = 0}};
        qb.pointB = {{.x = 320, .y = 0}};
        qb.pointC = {{.x = 0, .y = 240}};
        qb.pointD = {{.x = 320, .y = 240}};
        qb.uvA = {uint8_t(u0), uint8_t(v0)};
        qb.uvB = {uint8_t(u0 + 127), uint8_t(v0)};
        qb.uvC = {uint8_t(u0), uint8_t(v0 + 95)};
        qb.uvD = {uint8_t(u0 + 127), uint8_t(v0 + 95)};
        qb.clutIndex = psyqo::PrimPieces::ClutIndex(psyqo::Vertex{{.x = CLUT_X, .y = CLUT_Y_B}});
        qb.tpage = tpAdd;
        gpu.chain(m_qb[p]);

        gpu.chain(m_texOff[p]);
        text(f, p, rel);

        // bleach: risers over bars 77-78 and 79-80, a slow floor, then white into bar 81
        int32_t bl = 0;
        if (rel >= 3072) {
            bl = ((rel - 3072) * 40) >> 10;
            int32_t sw = (rel - 3072) & 511;
            bl += (sw * (rel < 3584 ? 60 : 110)) >> 9;
        }
        if (rel >= 4064) bl = clampi(((rel - 4064) * 255) >> 5, bl, 255);
        if (bl > 0) {
            m_tpAdd[p].primitive.attr = tpAdd;
            gpu.chain(m_tpAdd[p]);
            auto& r = m_bleach[p].primitive;
            r.setColor(gray(bl));
            r.setSemiTrans();
            r.position = {{.x = 0, .y = 0}};
            r.size = {{.w = 320, .h = 240}};
            gpu.chain(m_bleach[p]);
        }
    }

  private:
    int lineAt(int32_t rel) const {
        for (unsigned k = 0; k < kNumLines; k++) {
            int32_t s = (kLines[k].bar - 65) * 256;
            if (rel >= s && rel < s + kSlot) return int(k);
        }
        return -1;
    }

    void text(Frame& f, unsigned p, int32_t rel) {
        int k = lineAt(rel);
        if (k < 0 || m_trig[k] < 0) return;
        int32_t t = rel - (kLines[k].bar - 65) * 256;
        int32_t in = clampi((t - m_trig[k]) * 255 / 40, 0, 255);
        int32_t out = clampi((kSlot - t) * 255 / 48, 0, 255);
        int32_t a = in < out ? in : out;
        if (a <= 0) return;
        const char* s = kLines[k].text;
        unsigned len = 0;
        while (s[len]) len++;
        if (len > kMaxGlyphs) len = kMaxGlyphs;
        int x0 = (320 - int(len) * kAdvance) / 2, y0 = 112;

        auto& pro = m_pro[p].primitive;
        pro.upload.region = psyqo::Rect{960, 464, 2, 1};
        pro.pixel = 0xffff0000;
        psyqo::PrimPieces::TPageAttr base;
        base.setPageX(960 >> 6).setPageY(464 >> 8).set(psyqo::Prim::TPageAttr::Tex4Bits).setDithering(false).enableDisplayArea();
        psyqo::PrimPieces::TPageAttr sub = base, add = base;
        sub.set(psyqo::Prim::TPageAttr::FullBackSubFullFront);
        add.set(psyqo::Prim::TPageAttr::FullBackAndFullFront);
        pro.tpage.attr = add;

        unsigned n = 0;
        for (unsigned i = 0; i < len; i++) {
            char ch = s[i];
            if (ch <= ' ') continue;
            unsigned idx = unsigned(ch - 32);
            uint8_t u0 = uint8_t(idx * 8), v0 = uint8_t(208 + 16 * (idx / 32));
            uint8_t u1 = u0 + 8 > 255 ? 255 : uint8_t(u0 + 8);
            uint8_t v1 = v0 + 16 > 255 ? 255 : uint8_t(v0 + 16);
            int x = x0 + int(i) * kAdvance;
            for (int layer = 0; layer < 2; layer++) {
                auto& q = (layer ? m_glyph : m_shadow)[p][n].primitive;
                int ox = layer ? 0 : 2, oy = layer ? 0 : 2;
                q.setColor(layer ? psyqo::Color{{.r = uint8_t(a * 215 >> 8), .g = uint8_t(a), .b = uint8_t(a * 235 >> 8)}}
                                 : gray(a * 150 >> 8));
                q.setSemiTrans();
                q.pointA = {{.x = int16_t(x + ox), .y = int16_t(y0 + oy)}};
                q.pointB = {{.x = int16_t(x + ox + kAdvance), .y = int16_t(y0 + oy)}};
                q.pointC = {{.x = int16_t(x + ox), .y = int16_t(y0 + oy + 16)}};
                q.pointD = {{.x = int16_t(x + ox + kAdvance), .y = int16_t(y0 + oy + 16)}};
                q.uvA = {u0, v0};
                q.uvB = {u1, v0};
                q.uvC = {u0, v1};
                q.uvD = {u1, v1};
                q.clutIndex = psyqo::PrimPieces::ClutIndex(psyqo::Vertex{{.x = 960, .y = 464}});
                q.tpage = layer ? add : sub;
            }
            n++;
        }
        if (n == 0) return;
        {
            psyqo::PrimPieces::TPageAttr bandMode;
            bandMode.set(psyqo::Prim::TPageAttr::FullBackSubFullFront).enableDisplayArea();
            m_tpSub[p].primitive.attr = bandMode;
            f.gpu.chain(m_tpSub[p]);
            auto& r = m_band[p].primitive;
            r.setColor(gray(a * 0x60 >> 8));
            r.setSemiTrans();
            r.position = {{.x = 0, .y = 106}};
            r.size = {{.w = 320, .h = 28}};
            f.gpu.chain(m_band[p]);
        }
        f.gpu.chain(m_pro[p]);
        chainRun(f.gpu, m_shadow[p], n);
        chainRun(f.gpu, m_glyph[p], n);
    }

    template <class Frag>
    static void chainRun(psyqo::GPU& gpu, Frag* a, unsigned n) {
        constexpr uintptr_t words = sizeof(typename Frag::FragmentBaseType) / sizeof(uint32_t);
        for (unsigned i = 0; i + 1 < n; i++) {
            a[i].head = (words << 24) | (reinterpret_cast<uintptr_t>(&a[i + 1]) & 0xffffff);
        }
        gpu.chain(&a[0], &a[n - 1]);
    }

    psyqo::Fragments::SimpleFragment<psyqo::Prim::FastFill> m_clear[2];
    psyqo::Fragments::SimpleFragment<TexWindowOff> m_texWin[2], m_texOff[2];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::TexturedQuad> m_qa[2], m_qb[2];
    psyqo::Fragments::SimpleFragment<GlyphPrologue> m_pro[2];
    TQuad m_shadow[2][kMaxGlyphs], m_glyph[2][kMaxGlyphs];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::TPage> m_tpAdd[2];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::Rectangle> m_bleach[2];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::TPage> m_tpSub[2];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::Rectangle> m_band[2];

    int m_du = 0, m_dv = 0;
    int32_t m_trig[kNumLines] = {};
    int32_t m_rel = 0, m_ripple = -100000, m_speed = 14 * 256;
    uint32_t m_snare[8] = {};
    unsigned m_nSnare = 0;
    uint32_t m_lastTick = 0, m_logFrame = 0, m_logTicks = 0;
    bool m_haveTick = false, m_pendCounter = false;
};

Plasma s_section;

}  // namespace

Section& section5() { return s_section; }

}  // namespace demo
