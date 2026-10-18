// FORTY-ONE CYCLES: framework. Runs the song, keeps the music clock, dispatches cues and
// notes to the active section, draws the debug overlay.

#include <stdint.h>

#include "common/hardware/spu.h"
#include "common/syscalls/syscalls.h"
#include "demo.hh"
#include "psyqo/application.hh"
#include "psyqo/font.hh"
#include "psyqo/gpu.hh"
#include "psyqo/scene.hh"
#include "psyqo/trigonometry.hh"

extern "C" {
#include "psmplayer/psmplayer.h"
extern const uint8_t _binary_music_psm_start[];
extern const uint8_t _binary_music_psm_end[];
extern const uint8_t _binary_music_vab_start[];
extern const uint8_t _binary_music_vab_end[];
}

#ifndef MIRROR_MASK
// lead, counter, arp, stabs, orch, drums
#define MIRROR_MASK                                                                                     \
    ((1u << demo::CH_LEAD) | (1u << demo::CH_COUNTER) | (1u << demo::CH_ARP) | (1u << demo::CH_STABMIN) | \
     (1u << demo::CH_STABMAJ) | (1u << demo::CH_ORCH) | (1u << demo::CH_DRUMS))
#endif

#ifndef START_FRAME
#define START_FRAME 4
#endif

namespace {

using namespace demo;

// Notifications are popped from the player's ring inside the timer, stamped with the tick
// they were played on, and queued here for the frame loop.
struct Event {
    uint8_t kind, tag;
    uint32_t data;
    uint32_t tick;
};
constexpr unsigned QUEUE = 256;
Event g_queue[QUEUE];
volatile unsigned g_head = 0, g_tail = 0;
volatile uint32_t g_ticks = 0;
volatile uint32_t g_queueDropped = 0;

uintptr_t g_musicTimer;

void pump() {
    g_ticks = g_ticks + 1;
#ifndef SKIP_POLL
    PSM_Poll();
#endif
    PSM_Notify n;
    while (PSM_PopNotify(&n)) {
        unsigned next = (g_head + 1) % QUEUE;
        if (next == g_tail) {
            g_queueDropped = g_queueDropped + 1;
            continue;
        }
        g_queue[g_head] = {n.kind, n.tag, n.data, g_ticks};
        g_head = next;
    }
}

class Demo final : public psyqo::Application {
    void prepare() override;
    void createScene() override;

  public:
    psyqo::Font<> m_font;
    psyqo::Trig<> m_trig;
};

class DemoScene final : public psyqo::Scene {
    void frame() override;
    void drain();
    void switchTo(unsigned index, int bar);
    void overlay(Frame& f);

    Section* m_sections[NUM_SECTIONS] = {};
    Section* m_active = nullptr;
    Clock m_clock = {};
    uint32_t m_barTick = 0;
    uint32_t m_tpb256 = 192 * 256;
    uint32_t m_frames = 0;
    uint32_t m_fpsFrames = 0, m_fpsVsync = 0, m_fps10 = 0;
    bool m_logged[16] = {};
    bool m_loggedKick = false;
    bool m_started = false;

  public:
    void init();
};

Demo g_app;
DemoScene g_scene;

// Read by scripts/capture.lua from guest RAM (address via nm): magic, bar, frac, section, frames, ticks.
extern "C" volatile uint32_t demo_dbg[8] = {0x31474244, 0, 0, 0, 0, 0, 0, 0};

#ifdef DEBUG_SKIP
void push(const Event& e) {
    g_queue[g_head] = e;
    g_head = (g_head + 1) % QUEUE;
}

// Run the player at full speed with the master volume off until the target bar, then hand the
// frame loop that bar's BAR cue and the SECTION cue of the section it is in. begin() therefore
// sees sectionBar = the target bar, and notes held across the jump keep ringing.
void skipTo(int bar) {
    SPU_VOL_MAIN_LEFT = 0;
    SPU_VOL_MAIN_RIGHT = 0;
    Event section = {};
    bool haveSection = false;
    int cur = 0;
    while (PSM_playing && cur < bar) {
        pump();
        while (g_tail != g_head) {
            Event e = g_queue[g_tail];
            g_tail = (g_tail + 1) % QUEUE;
            if (e.kind != PSM_NOTIFY_USER) continue;
            if (e.tag == TAG_BAR) cur = int(e.data);
            if (e.tag == TAG_SECTION) {
                section = e;
                haveSection = true;
            }
        }
    }
    SPU_VOL_MAIN_LEFT = 0x3800;
    SPU_VOL_MAIN_RIGHT = 0x3800;
    push({PSM_NOTIFY_USER, TAG_BAR, uint32_t(cur), g_ticks});
    if (haveSection) push({PSM_NOTIFY_USER, TAG_SECTION, section.data, g_ticks});
}
#endif

}  // namespace

void Demo::prepare() {
    psyqo::GPU::Configuration config;
    config.set(psyqo::GPU::Resolution::W320)
        .set(psyqo::GPU::VideoMode::AUTO)
        .set(psyqo::GPU::ColorMode::C15BITS)
        .set(psyqo::GPU::Interlace::PROGRESSIVE);
    gpu().initialize(config);

    // psmplayer zeroes the reverb work area itself before it enables reverb.
    PSM_LoadBank(_binary_music_vab_start, _binary_music_vab_end - _binary_music_vab_start);
    PSM_noteMirrorMask = MIRROR_MASK;
    PSM_LoadSong(_binary_music_psm_start, _binary_music_psm_end - _binary_music_psm_start);
}

void Demo::createScene() {
    m_font.uploadSystemFont(gpu());
    g_scene.init();
#ifdef DEBUG_SKIP
    skipTo(DEBUG_SKIP);
#endif
    pushScene(&g_scene);
}

void DemoScene::init() {
    m_sections[0] = &section0();
    m_sections[1] = &section1();
    m_sections[2] = &section2();
    m_sections[3] = &section3();
    m_sections[4] = &section4();
    m_sections[5] = &section5();
    m_sections[6] = &section6();
    m_sections[7] = &section7();
    m_sections[8] = &section8();
    m_clock.section = -1;
    m_clock.ticksPerBar = 192;
}

void DemoScene::switchTo(unsigned index, int bar) {
    Section* prev = m_active;
    m_active = m_sections[index];
    m_clock.section = int(index);
    m_clock.sectionBar = bar;
    m_active->begin(m_clock, prev);
}

void DemoScene::drain() {
    while (g_tail != g_head) {
        Event e = g_queue[g_tail];
        g_tail = (g_tail + 1) % QUEUE;
        if (e.kind == PSM_NOTIFY_USER) {
            if (e.tag == TAG_BAR) {
                if (m_clock.bar && int(e.data) == m_clock.bar + 1) {
                    uint32_t delta = e.tick - m_barTick;
                    ramsyscall_printf("BAR %d tick %d delta %d\n", int(e.data), int(e.tick), int(delta));
                    m_tpb256 = (m_tpb256 * 7 + delta * 256) / 8;  // smooth the 171/172 jitter
                }
                m_barTick = e.tick;
                m_clock.bar = int(e.data);
                m_clock.sinceBar = 0;
                if (m_active) m_active->onBar(m_clock, m_clock.bar);
            } else if (e.tag == TAG_SECTION && e.data < NUM_SECTIONS) {
                ramsyscall_printf("SECTION %d at bar %d tick %d\n", int(e.data), m_clock.bar, int(e.tick));
                // a new pass of the song starts with section 0: nothing precedes it
                if (e.data == 0) m_active = nullptr;
                switchTo(e.data, m_clock.bar);
            }
        } else if (m_active) {
            uint8_t note = e.data & 0xff, vel = (e.data >> 8) & 0xff;
            if (e.tag < 16 && !m_logged[e.tag]) {
                m_logged[e.tag] = true;
                ramsyscall_printf("NOTE first on mirrored channel %d: note %d vel %d tick %d\n", e.tag, note, vel,
                                  int(e.tick));
            }
            if (e.tag == CH_DRUMS && note == KEY_KICK && !m_loggedKick) {
                m_loggedKick = true;
                ramsyscall_printf("KICK seen: tag %d note %d vel %d tick %d\n", e.tag, note, vel, int(e.tick));
            }
            m_active->onNote(m_clock, e.tag, note, vel, e.tick);
        }
    }
}

void DemoScene::frame() {
    // The music starts with the first frame, so the visuals never miss the intro.
    if (!m_started && m_frames >= START_FRAME) {
        m_started = true;
        g_musicTimer = g_app.gpu().armPeriodicTimer(PSM_hblanks * psyqo::GPU::US_PER_HBLANK, [](uint32_t) {
            pump();
            g_app.gpu().changeTimerPeriod(g_musicTimer, PSM_hblanks * psyqo::GPU::US_PER_HBLANK);
        });
    }
    drain();

    uint32_t now = g_ticks;
    m_clock.ticks = now;
    m_clock.sinceBar = m_clock.bar ? now - m_barTick : 0;
    m_clock.ticksPerBar = m_tpb256 >> 8;
    int frac = m_clock.bar ? int((m_clock.sinceBar * 65536) / m_tpb256) : 0;
    if (frac > 255) frac = 255;
    m_clock.frac = frac;
    m_clock.beat = 1 + (frac >> 6);
    m_clock.pos256 = m_clock.bar ? uint32_t(m_clock.bar - 1) * 256 + frac : 0;

    Frame f{g_app.gpu(), g_app.m_font, g_app.m_trig, m_clock, m_frames};
    if (m_active) {
        m_active->update(m_clock);
        m_active->render(f);
    } else {
        f.gpu.clear({{.r = 0, .g = 0, .b = 0}});
    }
    overlay(f);

    demo_dbg[1] = m_clock.bar;
    demo_dbg[2] = m_clock.frac;
    demo_dbg[3] = m_clock.section;
    demo_dbg[4] = m_frames;
    demo_dbg[5] = now;
    m_frames++;
    m_fpsFrames++;
    // rendered frames per vsync over ~1 s, scaled by the refresh rate
    uint32_t vs = f.gpu.getFrameCount();
    if (vs - m_fpsVsync >= f.gpu.getRefreshRate()) {
        m_fps10 = m_fpsFrames * f.gpu.getRefreshRate() * 10 / (vs - m_fpsVsync);
        m_fpsFrames = 0;
        m_fpsVsync = vs;
    }
}

void DemoScene::overlay(Frame& f) {
#ifndef NO_OVERLAY
    auto& font = f.font;
    auto& gpu = f.gpu;
    font.chainprintf(gpu, {{.x = 4, .y = 4}}, {{.r = 255, .g = 255, .b = 255}}, "bar %d.%d  %s", m_clock.bar,
                     m_clock.beat, m_active ? m_active->name() : "-");
    font.chainprintf(gpu, {{.x = 4, .y = 20}}, {{.r = 255, .g = 255, .b = 255}}, "fps %d.%d  tpb %d  drop %d/%d",
                     int(m_fps10 / 10), int(m_fps10 % 10), int(m_clock.ticksPerBar), int(PSM_notifyDropped),
                     int(g_queueDropped));
#endif
}

int main() { return g_app.run(); }
