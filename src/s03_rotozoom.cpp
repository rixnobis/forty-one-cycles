// Section 3: ROTOZOOM (bars 33-48). The live copper page (S2's offscreen render, still drawn
// here) is the texture: a 20x15 grid of textured quads under a 128x128 texture window.
// Angle steps once per lead note, zoom follows the lead pitch, the kick pulses the zoom,
// a mirrored additive plane joins at bar 41, and the zoom dives into the centre on bar 48.
#include "common/syscalls/syscalls.h"
#include "s02_copper.hh"

namespace demo {
namespace {

using namespace cu;

constexpr int GX = 20, GY = 15, CELL = 16;
using TQuad = psyqo::Fragments::SimpleFragment<psyqo::Prim::TexturedQuad>;

struct TexWindow {
    // GP0(E2h): window 128x128 (mask 16 x 8 px), offset 0
    uint32_t command = 0xe2000000 | 16 | (16 << 5);
};

constexpr int ease(int el, int d) { return smooth(el * 256 / d); }  // 0..256

class Rotozoom final : public Section {
  public:
    const char* name() const override { return "S3 ROTOZOOM"; }

    void begin(const Clock&, Section*) override {
        m_base = 0;
        m_n = 0;
        m_zFrom = m_zTo = 256;
        m_zTick = 0;
        m_kickTick = 0;
        m_frames = 0;
        m_fpsTick = 0;
    }

    void onNote(const Clock& c, unsigned channel, uint8_t note, uint8_t, uint32_t tick) override {
        if (channel == CH_DRUMS && note == KEY_KICK) {
            engine().onKick(tick);
            m_kickTick = tick;
        }
        if (channel == CH_STABMIN || channel == CH_STABMAJ) engine().onStab(tick);
        if (channel == CH_LEAD) {
            if (m_n == kRing) {  // drop the oldest into the base
                m_base += kStep;
                for (unsigned i = 1; i < m_n; i++) m_ticks[i - 1] = m_ticks[i];
                m_n--;
            }
            m_zFrom = zoomAt(tick);
            int n = note < 52 ? 52 : (note > 88 ? 88 : note);
            m_zTo = 205 + ((n - 52) * 114) / 10;
            m_zTick = tick;
            m_ticks[m_n++] = tick;
        }
    }

    void render(Frame& f) override {
        auto& gpu = f.gpu;
        const Clock& c = f.clock;
        const unsigned p = gpu.getParity();
        const uint32_t pos = c.pos256;
        auto& e = engine();

        e.layout(c, f.trig, 120);

        // angle: settled steps plus the ones still easing
        int angle = m_base;
        for (unsigned i = 0; i < m_n;) {
            int el = int(c.ticks - m_ticks[i]);
            if (el >= kEase) {
                m_base += kStep;
                angle += kStep;
                for (unsigned j = i + 1; j < m_n; j++) m_ticks[j - 1] = m_ticks[j];
                m_n--;
                continue;
            }
            angle += (kStep * ease(el, kEase)) >> 8;
            i++;
        }
        int zoom = zoomAt(c.ticks);
        if (m_kickTick && c.ticks - m_kickTick < 16) zoom = zoom * (256 + (40 * int(16 - (c.ticks - m_kickTick))) / 16) >> 8;

        // toms bar 48: dive, exponential in octaves
        int pd = pos >= bar(48) ? int(pos - bar(48)) : 0;
        if (pd > 0) {
            int k = (pd * 2000) >> 8;  // octaves in 1/256: 7.8 over the bar
            int n = k >> 8, fr = k & 255;
            int base = 256 << (n > 8 ? 8 : n);
            zoom = int((int64_t(zoom) * (base + ((base * fr) >> 8))) >> 8);
        }
        bool flat = pd >= 246;
        psyqo::Color fill = e.sample(60);
        if (pos >= bar(47)) g_handoff.fill = fill;

        e.chainState(gpu, p);
        e.texPass(gpu, p);
        e.screenStart(gpu, p, flat ? fill : psyqo::Color{{0, 0, 0}});
        if (!flat) {
            m_win[p].primitive = TexWindow{};
            gpu.chain(m_win[p]);
            bool two = pos >= bar(41);
            int fade2 = pd > 0 ? 256 - clampi(pd * 2, 0, 256) : 256;
            plane(f, p, 0, angle, zoom, 64 * 65536 + 32768, 60 * 65536 + 32768, rgb(two ? 120 : 128, two ? 120 : 128, two ? 120 : 128), false);
            if (two && fade2 > 0) {
                int k = (110 * fade2) >> 8;
                plane(f, p, 1, -angle + 512, zoom * 205 >> 8, 64 * 65536 + 32768, (60 + 32) * 65536 + 32768, rgb(k, k, k), true);
            }
            int fl = 0;
            if (pos >= bar(33) && pos < bar(33) + 40) fl = 160 - int(pos - bar(33)) * 4;
            if (pos >= bar(41) && pos < bar(41) + 40) fl = 160 - int(pos - bar(41)) * 4;
            if (fl > 0) {
                auto& r = m_flash[p].primitive;
                r.setColor(rgb(fl, fl, fl)).setSemiTrans();
                r.position = {{.x = 0, .y = 0}};
                r.size = {{.w = 320, .h = 240}};
                gpu.chain(m_flash[p]);
            }
        }

        m_frames++;
        if (m_fpsTick == 0) m_fpsTick = c.ticks;
        if (c.ticks - m_fpsTick >= 224) {
            ramsyscall_printf("S3 fps*10 %d (frames %d ticks %d) bar %d\n", int(m_frames * 112 * 10 / (c.ticks - m_fpsTick)),
                              int(m_frames), int(c.ticks - m_fpsTick), c.bar);
            m_frames = 0;
            m_fpsTick = c.ticks;
        }
    }

  private:
    static constexpr unsigned kRing = 8;
    static constexpr int kStep = 64;  // 1/32 turn per lead note
    static constexpr int kEase = 20;  // ticks

    int zoomAt(uint32_t tick) const {
        int el = int(tick - m_zTick);
        if (el < 0) el = 0;
        return m_zFrom + (((m_zTo - m_zFrom) * ease(el, 36)) >> 8);
    }

    void plane(Frame& f, unsigned p, unsigned pl, int angle, int zoomQ8, int ucx, int vcy, psyqo::Color col, bool add) {
        auto& gpu = f.gpu;
        if (zoomQ8 < 64) zoomQ8 = 64;
        const int s16 = (32768 * 256) / zoomQ8;
        const int a = (cosq(f.trig, angle) * s16) >> 12;
        const int b = (sinq(f.trig, angle) * s16) >> 12;
        static int32_t U[GY + 1][GX + 1], V[GY + 1][GX + 1];
        for (int j = 0; j <= GY; j++) {
            int dy = j * CELL - 120;
            int u = ucx - a * 160 - b * dy;
            int v = vcy - b * 160 + a * dy;
            for (int i = 0; i <= GX; i++) {
                U[j][i] = u;
                V[j][i] = v;
                u += a * CELL;
                v += b * CELL;
            }
        }
        psyqo::PrimPieces::TPageAttr attr;
        attr.setPageX(TEX_X >> 6).setPageY(TEX_Y >> 8).set(psyqo::Prim::TPageAttr::Tex16Bits).setDithering(false).enableDisplayArea();
        if (add) attr.set(psyqo::Prim::TPageAttr::FullBackSubFullFront);  // plane 2 subtracts: keeps the darks
        TQuad* cells = m_cells[pl][p];
        for (int j = 0; j < GY; j++) {
            for (int i = 0; i < GX; i++) {
                int ua = U[j][i] >> 16, ub = U[j][i + 1] >> 16, uc = U[j + 1][i] >> 16, ud = U[j + 1][i + 1] >> 16;
                int va = V[j][i] >> 16, vb = V[j][i + 1] >> 16, vc = V[j + 1][i] >> 16, vd = V[j + 1][i + 1] >> 16;
                int umin = ua < ub ? ua : ub;
                if (uc < umin) umin = uc;
                if (ud < umin) umin = ud;
                int vmin = va < vb ? va : vb;
                if (vc < vmin) vmin = vc;
                if (vd < vmin) vmin = vd;
                int bu = (umin >> 7) << 7, bv = (vmin >> 7) << 7;
                auto& q = cells[j * GX + i].primitive;
                q.setColor(col);
                if (add) q.setSemiTrans(); else q.setOpaque();
                int x = i * CELL, y = j * CELL;
                q.pointA = {{.x = int16_t(x), .y = int16_t(y)}};
                q.pointB = {{.x = int16_t(x + CELL), .y = int16_t(y)}};
                q.pointC = {{.x = int16_t(x), .y = int16_t(y + CELL)}};
                q.pointD = {{.x = int16_t(x + CELL), .y = int16_t(y + CELL)}};
                q.uvA = {uint8_t(ua - bu), uint8_t(va - bv)};
                q.uvB = {uint8_t(ub - bu), uint8_t(vb - bv)};
                q.uvC = {uint8_t(uc - bu), uint8_t(vc - bv)};
                q.uvD = {uint8_t(ud - bu), uint8_t(vd - bv)};
                q.tpage = attr;
            }
        }
        chainRun(gpu, cells, GX * GY);
    }

    int m_base = 0;
    unsigned m_n = 0;
    uint32_t m_ticks[kRing] = {};
    int m_zFrom = 256, m_zTo = 256;
    uint32_t m_zTick = 0, m_kickTick = 0;
    uint32_t m_frames = 0, m_fpsTick = 0;
    psyqo::Fragments::SimpleFragment<TexWindow> m_win[2];
    psyqo::Fragments::SimpleFragment<psyqo::Prim::Rectangle> m_flash[2];
    TQuad m_cells[2][2][GX * GY];
};

Rotozoom s_section;
}  // namespace

Section& section3() { return s_section; }
}  // namespace demo
