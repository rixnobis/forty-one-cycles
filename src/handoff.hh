// State one section leaves for the next. Each field has one writer (the section
// that ends) and one reader (the section that begins); the comment names both.
#pragma once
#include <stdint.h>

#include "psyqo/primitives/common.hh"

namespace demo {

// S1 -> S2: the stars settle onto these rows, which become the copper bars.
constexpr unsigned HANDOFF_ROWS = 16;
constexpr int16_t rowY(unsigned i) { return int16_t(30 + 12 * i); }
inline psyqo::Color rowColor(unsigned i) {
    // phosphor green at the top to amber at the bottom
    uint8_t t = uint8_t(i * 255 / (HANDOFF_ROWS - 1));
    return psyqo::Color{{.r = uint8_t(40 + (215 * t) / 255), .g = uint8_t(220 - (60 * t) / 255), .b = 40}};
}

struct Handoff {
    // S3 -> S4: colour of the rotozoom's centre pixel when it fills the screen.
    psyqo::Color fill{{.r = 0, .g = 0, .b = 0}};
    // S4 -> S5: last projected ball positions (screen space), used as plasma centres.
    static constexpr unsigned MAX_BALLS = 128;
    int16_t ballX[MAX_BALLS] = {};
    int16_t ballY[MAX_BALLS] = {};
    unsigned ballCount = 0;
    // S0 -> S1: screen positions of the dots on the collapsed line.
    static constexpr unsigned MAX_DOTS = 64;
    int16_t dotX[MAX_DOTS] = {};
    unsigned dotCount = 0;
    int16_t dotLineY = 120;
    // S6 -> S7: ring vertices of the opening tunnel, screen space, rewritten every frame of bar 96's last half.
    // tunnelZ = ring index, 0 near .. 15 far. Only on-screen vertices are kept, compacted.
    static constexpr unsigned MAX_TUNNEL = 256;
    int16_t tunnelX[MAX_TUNNEL] = {};
    int16_t tunnelY[MAX_TUNNEL] = {};
    uint16_t tunnelZ[MAX_TUNNEL] = {};
    unsigned tunnelCount = 0;
};

inline Handoff g_handoff;

}  // namespace demo
