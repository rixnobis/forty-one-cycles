// Section 0: COLD BOOT (bars 1-8). A 40x30 grid of phosphor dots fades in with the pads, arp notes
// light dots, the title types on in dot-matrix, the riser shimmers the grid and the snare roll
// folds every row onto the centre line. The 40 dots left on that line are the hand-off to S1.
#include "demo.hh"
#include "handoff.hh"
#include "s00_glyphs.hh"
#include "s00_rects.hh"

namespace demo {
namespace {

constexpr int COLS = 40, ROWS = 30, NDOTS = COLS * ROWS;
constexpr int TITLE_LEN = 16;
constexpr char TITLE[TITLE_LEN + 1] = "FORTY-ONE CYCLES";
constexpr int TITLE_X = 16, TITLE_Y = 110, GLYPH_PITCH = 18;
constexpr int TITLE_ROW_LO = 13, TITLE_ROW_HI = 16;  // grid rows hidden under the title
constexpr int LINE_Y = 120;

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// phosphor green at brightness b (0..255), then pushed toward white by w (0..255)
inline psyqo::Color phosphor(int b, int w) {
    int r = (b * 45) >> 8, g = b, bl = (b * 60) >> 8;
    r += ((255 - r) * w) >> 9;
    g += ((255 - g) * w) >> 9;
    bl += ((255 - bl) * w) >> 9;
    return rgb(r, g, bl);
}

class ColdBoot final : public Section {
  public:
    const char* name() const override { return "S0 COLD_BOOT"; }

    void begin(const Clock& clock, Section*) override {
        for (auto& d : m_dot) d = {0, 0};
        m_pendingN = 0;
        m_lastSnare = 0;
        m_haveSnare = false;
        m_arpCount = 0;
        // a debug jump into the middle of the section: the title is as far along as the clock says
        int pos = int(clock.pos256);
        m_typed = pos > 512 ? clampi((pos - 512) / 32, 0, TITLE_LEN) : 0;
        for (auto& t : m_typedTick) t = 0;
        g_handoff.dotCount = COLS;
        for (int c = 0; c < COLS; c++) g_handoff.dotX[c] = int16_t(4 + 8 * c);
        g_handoff.dotLineY = LINE_Y;
    }

    void onBar(const Clock&, int bar) override { m_stat.bar("S0", bar); }

    void onNote(const Clock&, unsigned channel, uint8_t note, uint8_t, uint32_t tick) override {
        if (channel == CH_ARP && m_pendingN < PENDING) {
            m_pending[m_pendingN++] = {note, tick};
        } else if (channel == CH_DRUMS && (note == KEY_SNARE || note == KEY_SNARE2)) {
            m_lastSnare = tick;
            m_haveSnare = true;
        }
    }

    void update(const Clock& clock) override {
        for (unsigned i = 0; i < m_pendingN; i++) {
            Pending& p = m_pending[i];
            int age = int(clock.ticks - p.tick);
            int barPos = int(clock.sinceBar) - age;
            int bar = clock.bar;
            if (barPos < 0) {  // drained ahead of its bar cue: last step of the previous bar
                barPos = int(clock.ticksPerBar) - 1;
                bar--;
            }
            int step = clampi(barPos * 16 / int(clock.ticksPerBar), 0, 15);
            int col = (step * 5) >> 1;
            int row = 29 - clampi((int(p.note) - 62) * 29 / 26, 0, 29);
            light(col, row, 255, p.tick);
            light(col - 1, row, 90, p.tick);
            light(col + 1, row, 90, p.tick);
            light(col, row - 1, 90, p.tick);
            light(col, row + 1, 90, p.tick);
            if (bar >= 3) {  // one glyph per second arp note from bar 3
                if ((m_arpCount++ & 1) == 0 && m_typed < TITLE_LEN) {
                    m_typedTick[m_typed < 16 ? m_typed : 15] = p.tick;
                    m_typed++;
                }
            }
        }
        m_pendingN = 0;
    }

    void render(Frame& f) override {
        const Clock& clock = f.clock;
        m_stat.sample(f);
        int parity = f.gpu.getParity();
        Buf& b = m_buf[parity];
        b.rects.reset();

        int pos = int(clock.pos256);
        int bar = clock.bar;
        int frac = clock.frac;
        uint32_t now = clock.ticks;

        // pads: the grid brightens over the eight bars
        int base = 34 + (pos * 76) / 2048;
        // riser shimmer in bars 7-8
        int shimmer = pos > 1536 ? ((pos - 1536) * 80) / 512 : 0;
        // snare roll pulse
        int pulse = 0;
        if (m_haveSnare && now - m_lastSnare < 12) pulse = 70 * int(12 - (now - m_lastSnare)) / 12;
        // the title hides the middle rows of the grid while it is up
        int titleAlpha = 0;  // 0..256
        if (pos >= 512 && pos < 1792) titleAlpha = clampi((pos - 512) * 256 / 64, 0, 256);
        else if (pos >= 1792) titleAlpha = clampi(256 - (frac * 256) / 96, 0, 256);
        bool collapse = bar >= 8;

        for (int r = 0; r < ROWS; r++) {
            int home = 8 * r + 4;
            int e = 0;  // fold progress 0..256
            if (collapse) {
                int d = r * 2 - 29;
                if (d < 0) d = -d;
                int start = ((29 - d) / 2) * 6;
                int s = clampi((frac - start) * 256 / 100, 0, 256);
                e = (s * s * (768 - 2 * s)) >> 16;
            }
            int y = home + (((LINE_Y - home) * e) >> 8);
            bool titleRow = r >= TITLE_ROW_LO && r <= TITLE_ROW_HI;
            for (int c = 0; c < COLS; c++) {
                int i = r * COLS + c;
                int bright = base + pulse;
                if (shimmer) {
                    uint32_t h = (uint32_t(i) * 2654435761u) ^ (now * 40503u);
                    h ^= h >> 13;
                    h *= 0x5bd1e995u;
                    h ^= h >> 15;
                    bright += (int(h & 255) - 96) * shimmer >> 8;
                }
                int white = 0;
                if (m_dot[i].amp) {
                    int age = int(now + 1 - m_dot[i].tick);
                    if (age >= 0 && age < 48) {
                        int k = 48 - age;
                        int lit = (int(m_dot[i].amp) * k * k) / (48 * 48);
                        if (lit > bright) bright = lit;
                        white = lit;
                    }
                }
                if (titleRow) bright = bright * (256 - titleAlpha) >> 8;
                bright += ((255 - bright) * e) >> 8;
                if (bright <= 3) continue;
                if (white > 200 && e == 0)  // a fresh arp note flares for a moment
                    b.rects.add(4 + 8 * c - 2, y - 2, 4, 4, phosphor(255, white));
                else
                    b.rects.add(4 + 8 * c - 1, y - 1, 2, 2, phosphor(clampi(bright, 0, 255), white));
            }
        }

        // title, dot-matrix
        if (titleAlpha > 0 && m_typed > 0) {
            for (int g = 0; g < m_typed; g++) {
                const uint8_t* rows = glyphRows(TITLE[g]);
                int fresh = 0;
                int age = int(now + 1 - m_typedTick[g]);
                if (m_typedTick[g] && age >= 0 && age < 14) fresh = 255 * (14 - age) / 14;
                psyqo::Color col = phosphor(clampi(235 * titleAlpha >> 8, 0, 255), fresh);
                for (int py = 0; py < 7; py++)
                    for (int px = 0; px < 5; px++)
                        if (rows[py] & (0x10 >> px))
                            b.rects.add(TITLE_X + g * GLYPH_PITCH + px * 3, TITLE_Y + py * 3, 2, 2, col);
            }
        }
        // cursor blinks on the beat, half a beat on and half off
        if (titleAlpha > 0 && pos >= 512 && (frac & 63) < 32) {
            int cx = TITLE_X + (m_typed < TITLE_LEN ? m_typed : TITLE_LEN - 1) * GLYPH_PITCH;
            if (m_typed >= TITLE_LEN) cx += GLYPH_PITCH;
            if (cx + 14 <= 320) b.rects.add(cx, TITLE_Y, 14, 20, phosphor(clampi(200 * titleAlpha >> 8, 0, 255), 0));
        }

        f.gpu.getNextClear(b.clear.primitive, {{.r = 0, .g = 0, .b = 0}});
        f.gpu.chain(b.clear);
        b.rects.chain(f.gpu);
    }

  private:
    struct Dot {
        uint32_t tick;  // tick + 1 the dot was lit on
        uint8_t amp;
    };
    struct Pending {
        uint8_t note;
        uint32_t tick;
    };
    struct Buf {
        psyqo::Fragments::SimpleFragment<psyqo::Prim::FastFill> clear;
        RectList<NDOTS + 640> rects;
    };

    void light(int c, int r, int amp, uint32_t tick) {
        if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return;
        Dot& d = m_dot[r * COLS + c];
        d.tick = tick + 1;
        d.amp = uint8_t(amp);
    }

    static constexpr unsigned PENDING = 32;
    Dot m_dot[NDOTS] = {};
    Pending m_pending[PENDING] = {};
    unsigned m_pendingN = 0;
    uint32_t m_typedTick[TITLE_LEN] = {};
    uint32_t m_lastSnare = 0;
    bool m_haveSnare = false;
    int m_typed = 0;
    int m_arpCount = 0;
    Buf m_buf[2];
    BarStat m_stat;
};

ColdBoot s_section;
}  // namespace

Section& section0() { return s_section; }
}  // namespace demo
