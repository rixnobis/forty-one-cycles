// Section 8: CREDITS + END (bars 113-121). The recap scene runs on until its layers have dropped out
// (balls with the clap, copper with the bass fade, stars last); the credits appear over the dimming
// starfield on the counter line's falling notes. The final hit at bar 119 is one white frame, then the
// S0 dot grid with the title fully typed, its dots dying out with the reverb tail.
#include "common/syscalls/syscalls.h"
#include "demo.hh"
#include "handoff.hh"
#include "s00_glyphs.hh"
#include "s00_rects.hh"
#include "s07_scene.hh"

namespace demo {
namespace {

constexpr int COLS = 40, ROWS = 30, NDOTS = COLS * ROWS;
constexpr int TITLE_LEN = 16;
constexpr char TITLE[TITLE_LEN + 1] = "FORTY-ONE CYCLES";
constexpr int TITLE_X = 16, TITLE_Y = 110, GLYPH_PITCH = 18;
constexpr int TITLE_ROW_LO = 13, TITLE_ROW_HI = 16;

struct Line {
    const char* text;
    int y, w, h;  // glyph size; advance = w
};
constexpr Line kLines[] = {
    {"FORTY-ONE CYCLES", 52, 16, 16},
    {"CODE AND MUSIC: RIX", 96, 8, 16},
    {"MADE WITH PSYQO, NUGGET AND MIDI2PSM", 124, 8, 16},
    {"TESTED IN PCSX-REDUX AND ON SILICON", 152, 8, 16},
    {"2026", 192, 16, 16},
};
constexpr unsigned kNumLines = sizeof(kLines) / sizeof(kLines[0]);

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline psyqo::Color phosphor(int b, int w) {
    int r = (b * 45) >> 8, g = b, bl = (b * 60) >> 8;
    r += ((255 - r) * w) >> 9;
    g += ((255 - g) * w) >> 9;
    bl += ((255 - bl) * w) >> 9;
    return rgb(r, g, bl);
}

class Credits final : public Section {
  public:
    const char* name() const override { return "S8 CREDITS_END"; }

    void begin(const Clock& clock, Section*) override {
        if (!recapScene().begun()) recapScene().begin(clock);
        m_white = false;
        m_whiteTick = 0;
        m_nCounter = 0;
        for (auto& t : m_lineAt) t = -1;
        // a debug jump past the first slots: those lines are already up
        int pos = int(clock.pos256);
        for (unsigned k = 0; k < kNumLines; k++)
            if (pos > slot(k) + 64) m_lineAt[k] = slot(k);
    }

    void onNote(const Clock& c, unsigned ch, uint8_t note, uint8_t vel, uint32_t tick) override {
        recapScene().onNote(c, ch, note, vel, tick);
        if (ch == CH_COUNTER) {
#ifdef L6_LOG
            ramsyscall_printf("S8 counter note %d at bar %d frac %d\n", int(note), c.bar, c.frac);
#endif
        }
    }

    void update(const Clock& c) override {
        int pos = int(c.pos256);
        if (c.bar < 119) recapScene().update(c);
        for (unsigned k = 0; k < kNumLines; k++)
            if (m_lineAt[k] < 0 && pos >= slot(k)) m_lineAt[k] = pos;
    }

    void render(Frame& f) override {
        const Clock& c = f.clock;
        if (c.bar < 119) {
            recapScene().render(f);
            credits(f);
            return;
        }
        auto& gpu = f.gpu;
        unsigned p = gpu.getParity();
        Buf& b = m_buf[p];
        if (!m_white) {  // one white frame
            m_white = true;
            m_whiteTick = c.ticks;
            gpu.getNextClear(b.clear.primitive, {{.r = 255, .g = 255, .b = 255}});
            gpu.chain(b.clear);
            return;
        }
        grid(f, b);
    }

  private:
    struct Buf {
        psyqo::Fragments::SimpleFragment<psyqo::Prim::FastFill> clear;
        RectList<NDOTS + 400> rects;
        GlyphBatch<112> text;
    };

    // credits slot k starts this many clock units into bar 113
    // (lines 1.. land on the counter line's falling notes at the starts of bars 114, 115, 116, 117)
    static int slot(unsigned k) { return k == 0 ? int(barPos(113)) + 40 : int(barPos(113 + int(k))); }

    void credits(Frame& f) {
        const Clock& c = f.clock;
        int pos = int(c.pos256);
        unsigned p = f.gpu.getParity();
        Buf& b = m_buf[p];
        b.text.reset();
        // everything fades out over the last half bar before the hit
        int out = clampi((int(barPos(119)) - pos) * 255 / 96, 0, 255);
        for (unsigned k = 0; k < kNumLines; k++) {
            if (m_lineAt[k] < 0) continue;
            int in = clampi((pos - m_lineAt[k]) * 255 / 40, 0, 255);
            int a = in < out ? in : out;
            if (a <= 0) continue;
            const Line& l = kLines[k];
            int len = 0;
            while (l.text[len]) len++;
            int x0 = (320 - len * l.w) / 2;
            psyqo::Color col = phosphor(clampi(a, 0, 255), clampi(255 - in, 0, 255) / 2);
            psyqo::Color sh = rgb(a / 8, a / 8, a / 8);
            for (int i = 0; i < len; i++) b.text.add(x0 + i * l.w, l.y, l.w, l.h, l.text[i], col, sh);
        }
        b.text.chain(f.gpu);
    }

    // The S0 grid with the title fully typed. t = clock units since the hit.
    void grid(Frame& f, Buf& b) {
        const Clock& c = f.clock;
        auto& gpu = f.gpu;
        int t = int(c.pos256) - int(barPos(119));
        uint32_t now = c.ticks;
        b.rects.reset();
        int flash = clampi(255 - int(now - m_whiteTick) * 255 / 40, 0, 255);  // the hit's afterglow
        const int base = 110;
        for (int r = 0; r < ROWS; r++) {
            bool titleRow = r >= TITLE_ROW_LO && r <= TITLE_ROW_HI;
            if (titleRow) continue;
            for (int cc = 0; cc < COLS; cc++) {
                int i = r * COLS + cc;
                int bright = dim(i, t, 48, 480, base);
                if (flash > bright) bright = flash * 2 / 3 + bright / 3;
                if (bright <= 3) continue;
                b.rects.add(4 + 8 * cc - 1, 8 * r + 3, 2, 2, phosphor(clampi(bright, 0, 255), flash));
            }
        }
        for (int g = 0; g < TITLE_LEN; g++) {
            const uint8_t* rows = glyphRows(TITLE[g]);
            for (int py = 0; py < 7; py++)
                for (int px = 0; px < 5; px++) {
                    if (!(rows[py] & (0x10 >> px))) continue;
                    int i = 4000 + g * 64 + py * 8 + px;
                    int bright = dim(i, t, 260, 560, 235);
                    if (bright <= 3) continue;
                    b.rects.add(TITLE_X + g * GLYPH_PITCH + px * 3, TITLE_Y + py * 3, 2, 2, phosphor(clampi(bright, 0, 255), flash));
                }
        }
        // cursor after the title, blinking on the beat until the title is gone
        if (t < 520 && (c.frac & 63) < 32) {
            int cx = TITLE_X + TITLE_LEN * GLYPH_PITCH;
            if (cx + 14 <= 320) b.rects.add(cx, TITLE_Y, 14, 20, phosphor(clampi(200 - t / 3, 0, 200), 0));
        }
        gpu.getNextClear(b.clear.primitive, {{.r = 0, .g = 0, .b = 0}});
        gpu.chain(b.clear);
        b.rects.chain(gpu);
    }

    // Brightness of dot i at time t: full until its own moment inside [from, to), then gone.
    static int dim(int i, int t, int from, int to, int level) {
        uint32_t h = uint32_t(i) * 2654435761u;
        h ^= h >> 15;
        h *= 0x2c1b3c6du;
        h ^= h >> 12;
        int death = from + int(h % uint32_t(to - from));
        int d = clampi((t - death) * 256 / 40, 0, 256);
        return level * (256 - d) >> 8;
    }

    Buf m_buf[2];
    bool m_white = false;
    uint32_t m_whiteTick = 0;
    unsigned m_nCounter = 0;
    int m_lineAt[kNumLines] = {};
};

Credits s_section;
}  // namespace

Section& section8() { return s_section; }
}  // namespace demo
