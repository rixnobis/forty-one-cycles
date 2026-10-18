// The contract between the framework (main.cpp) and the sections (sNN_*.cpp).
//
// Music clock: PSM_Poll runs from a periodic timer; one tick is one MIDI tick (PPQ 48), 112/s at 140 bpm. BAR cues (tag 1,
// payload = bar 1..121) and SECTION cues (tag 2, payload = section index 0..8) come
// from the song. All animation is a function of Clock, never of the frame count, so
// motion is identical at 60 and 50 fps.
#pragma once

#include <stdint.h>

#include "psyqo/font.hh"
#include "psyqo/gpu.hh"
#include "psyqo/trigonometry.hh"

namespace demo {

constexpr unsigned NUM_SECTIONS = 9;
constexpr uint8_t TAG_BAR = 1;
constexpr uint8_t TAG_SECTION = 2;

// MIDI channels, 0-based as in PSM_noteMirrorMask / PSM_Notify.tag.
constexpr unsigned CH_LEAD = 0, CH_COUNTER = 1, CH_ARP = 2, CH_ARPECHO = 3, CH_BASS = 4, CH_SUB = 5,
                   CH_STABMIN = 6, CH_STABMAJ = 7, CH_PAD0 = 8, CH_DRUMS = 9, CH_PAD1 = 10,
                   CH_PAD2 = 11, CH_ORCH = 12, CH_REV = 13, CH_FX = 14;
// GM kit keys on CH_DRUMS.
constexpr uint8_t KEY_KICK = 36, KEY_SNARE = 38, KEY_CLAP = 39, KEY_SNARE2 = 40, KEY_TOM_LO = 41,
                  KEY_HAT = 42, KEY_HAT2 = 44, KEY_TOM_MID = 45, KEY_OHH = 46, KEY_TOM_HI = 48,
                  KEY_CRASH = 49;

struct Clock {
    uint32_t ticks;        // PSM_Poll calls since boot
    int bar;               // 1-based bar of the song, 0 before the first BAR cue
    int beat;              // 1..4
    int frac;              // 0..255 position inside the bar
    uint32_t pos256;       // (bar - 1) * 256 + frac, monotonic inside one pass of the song
    uint32_t ticksPerBar;  // 192 (PPQ 48 x 4 beats)
    uint32_t sinceBar;     // ticks since the BAR cue
    int section;           // active section index, -1 before the first SECTION cue
    int sectionBar;        // bar the active section began on
};

struct Frame {
    psyqo::GPU& gpu;
    psyqo::Font<>& font;
    psyqo::Trig<>& trig;
    const Clock& clock;
    uint32_t frameNo;      // rendered frames since boot
};

class Section {
  public:
    virtual ~Section() = default;
    virtual const char* name() const = 0;
    // The SECTION cue arrived. prev is the section that just ended (nullptr on the first
    // pass and after the song loops), for hand-off state.
    virtual void begin(const Clock& clock, Section* prev) = 0;
    virtual void onBar(const Clock& clock, int bar) {}
    // note-ons of the channels in the mirror mask; tick is the PSM tick it was played on.
    virtual void onNote(const Clock& clock, unsigned channel, uint8_t note, uint8_t velocity, uint32_t tick) {}
    virtual void update(const Clock& clock) {}
    virtual void render(Frame& frame) = 0;
};

// One accessor per section, defined in sNN_*.cpp.
Section& section0();
Section& section1();
Section& section2();
Section& section3();
Section& section4();
Section& section5();
Section& section6();
Section& section7();
Section& section8();

// A solid-colour stub, used until a lane replaces its section.
class StubSection : public Section {
  public:
    StubSection(const char* n, psyqo::Color c) : m_name(n), m_color(c) {}
    const char* name() const override { return m_name; }
    void begin(const Clock&, Section*) override {}
    void render(Frame& f) override { f.gpu.clear(m_color); }

  private:
    const char* m_name;
    psyqo::Color m_color;
};

}  // namespace demo
