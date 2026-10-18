// Section 2: COPPER + SINE SCROLLER (bars 17-32). Raster bars swing on sines, a scroller rides
// them one glyph per 16th note, and from bar 31 the bars are also drawn without the scroller
// into the offscreen page that S3 uses as its texture.
#include "log.hh"
#include "s02_copper.hh"

namespace demo {
namespace cu {
namespace {
Engine s_engine;
}
Engine& engine() { return s_engine; }
}  // namespace cu

namespace {

using namespace cu;

constexpr char kText[] =
    "FORTY-ONE CYCLES   ...   a PlayStation demo   ...   code and music by Rix   ...   "
    "made with psyqo and nugget   ...   runs on real hardware   ...   "
    "the bus said forty-one, the docs said forty   ...   ";
constexpr unsigned kLen = sizeof(kText) - 1;
// The last glyph has to be off screen before the bars start to fold in (bar 32).
static_assert(bar(18) + 320 + 16 * kLen <= bar(32) - 16, "scroller text too long");

constexpr uint32_t kPos0 = bar(18);  // scroller enters at bar 18
constexpr int kScrollerY = 112;      // screen y of the glyph centre line
constexpr unsigned kMaxGlyphs = 26;

struct GlyphPrologue {
    psyqo::Prim::VRAMUpload upload;
    uint32_t pixel;
    psyqo::Prim::FlushCache flush;
    psyqo::Prim::TPage tpage;
};

using TQuad = psyqo::Fragments::SimpleFragment<psyqo::Prim::TexturedQuad>;

// GP0(80h) VRAM to VRAM copy
struct Blit {
    uint32_t command = 0x80000000;
    uint32_t src, dst, size;
};
constexpr int kStrips = 12, kStripH = 20;

class Copper final : public Section {
  public:
    const char* name() const override { return "S2 COPPER_SCROLLER"; }

    void begin(const Clock&, Section*) override {
        m_frames = 0;
        m_fpsTick = 0;
    }

    void onNote(const Clock&, unsigned channel, uint8_t note, uint8_t, uint32_t tick) override {
        if (channel == CH_DRUMS && note == KEY_KICK) engine().onKick(tick);
        if (channel == CH_STABMIN || channel == CH_STABMAJ) engine().onStab(tick);
    }

    void render(Frame& f) override {
        auto& gpu = f.gpu;
        const Clock& c = f.clock;
        const unsigned p = gpu.getParity();
        auto& e = engine();

        e.layout(c, f.trig, kScrollerY);
        e.chainState(gpu, p);
        if (c.bar >= TEX_FIRST_BAR) e.texPass(gpu, p);
        e.screenStart(gpu, p);
        e.screenBars(gpu, p);
        scroller(f, p);
        tear(f, p);

        m_frames++;
        if (m_fpsTick == 0) m_fpsTick = c.ticks;
        if (c.ticks - m_fpsTick >= 224) {
            DEMO_LOG("S2 fps*10 %d (frames %d ticks %d) bar %d\n", int(m_frames * 112 * 10 / (c.ticks - m_fpsTick)),
                              int(m_frames), int(c.ticks - m_fpsTick), c.bar);
            m_frames = 0;
            m_fpsTick = c.ticks;
        }
    }

  private:
    // Zap, bar 29: horizontal strips of the finished frame slide sideways with wrap-around,
    // through the scratch area, the offset decaying over the bar.
    void tear(Frame& f, unsigned p) {
        const uint32_t pos = f.clock.pos256;
        if (pos < bar(29) || pos >= bar(30)) return;
        int amp = (40 * (256 - int(pos - bar(29)))) >> 8;
        psyqo::Prim::FastFill ff;
        f.gpu.getNextClear(ff);
        const int base = ff.rect.pos.y;
        unsigned n = 0;
        auto put = [&](int sx, int sy, int dx, int dy, int w) {
            auto& b = m_blit[p][n++].primitive;
            b.src = uint32_t(sx) | (uint32_t(sy) << 16);
            b.dst = uint32_t(dx) | (uint32_t(dy) << 16);
            b.size = uint32_t(w) | (uint32_t(kStripH) << 16);
        };
        for (int s = 0; s < kStrips; s++) {
            int d = (amp * sinq(f.trig, s * 330 + int(pos) * 24)) >> 12;
            d = (d + 320) % 320;
            if (d == 0) continue;
            int y = base + s * kStripH;
            put(0, y, SCR_X, SCR_Y, 320);
            put(SCR_X, SCR_Y, d, y, 320 - d);
            put(SCR_X + 320 - d, SCR_Y, 0, y, d);
        }
        chainRun(f.gpu, m_blit[p], n);
    }

    void scroller(Frame& f, unsigned p) {
        auto& gpu = f.gpu;
        const int pos = int(f.clock.pos256);
        auto& pro = m_pro[p].primitive;
        pro.upload.region = psyqo::Rect{960, 464, 2, 1};
        pro.pixel = 0x7fff0000;
        psyqo::PrimPieces::TPageAttr a;
        a.setPageX(960 >> 6).setPageY(464 >> 8).set(psyqo::Prim::TPageAttr::Tex4Bits).setDithering(false).enableDisplayArea();
        pro.tpage.attr = a;

        // glyph k sits at x = 320 + 16 k - (pos - kPos0)
        int scrolled = pos - int(kPos0);
        if (scrolled < 0) return;
        int first = (scrolled - 320) / 16;
        if (first < 0) first = 0;
        unsigned n = 0;
        for (int k = first; k < int(kLen) && n < kMaxGlyphs; k++) {
            int x = 320 + 16 * k - scrolled;
            if (x >= 320) break;
            if (x <= -16) continue;
            char ch = kText[k];
            if (ch <= ' ') continue;
            int y = kScrollerY - 8 + ((40 * sinq(f.trig, x * 7 + pos * 5)) >> 12);
            unsigned idx = unsigned(ch - 32);
            uint8_t u0 = uint8_t(idx * 8), v0 = uint8_t(208 + 16 * (idx / 32));
            uint8_t u1 = uint8_t(u0 + 7);  // last texel of the cell; u0 + 8 is the next glyph
            uint8_t v1 = uint8_t(v0 + 15);
            psyqo::Color tint = mix(rgb(255, 255, 255), demo::rowColor(unsigned(k) & 15), 70);
            for (int layer = 0; layer < 2; layer++) {
                auto& q = (layer ? m_glyph : m_shadow)[p][n].primitive;
                int ox = layer ? 0 : 2, oy = layer ? 0 : 2;
                q.setColor(layer ? scale(tint, 128) : rgb(26, 26, 26));
                q.pointA = {{.x = int16_t(x + ox), .y = int16_t(y + oy)}};
                q.pointB = {{.x = int16_t(x + ox + 16), .y = int16_t(y + oy)}};
                q.pointC = {{.x = int16_t(x + ox), .y = int16_t(y + oy + 16)}};
                q.pointD = {{.x = int16_t(x + ox + 16), .y = int16_t(y + oy + 16)}};
                q.uvA = {u0, v0};
                q.uvB = {u1, v0};
                q.uvC = {u0, v1};
                q.uvD = {u1, v1};
                q.clutIndex = psyqo::PrimPieces::ClutIndex(psyqo::Vertex{{.x = 960, .y = 464}});
                q.tpage = a;
            }
            n++;
        }
        if (n == 0) return;
        gpu.chain(m_pro[p]);
        chainRun(gpu, m_shadow[p], n);
        chainRun(gpu, m_glyph[p], n);
    }

    psyqo::Fragments::SimpleFragment<GlyphPrologue> m_pro[2];
    TQuad m_shadow[2][kMaxGlyphs], m_glyph[2][kMaxGlyphs];
    psyqo::Fragments::SimpleFragment<Blit> m_blit[2][kStrips * 3];
    uint32_t m_frames = 0, m_fpsTick = 0;
};

Copper s_section;
}  // namespace

Section& section2() { return s_section; }
}  // namespace demo
