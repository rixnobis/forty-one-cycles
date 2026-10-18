// 5x7 dot-matrix glyphs for the S0 title: only the letters FORTY-ONE CYCLES needs.
#pragma once
#include <stdint.h>

namespace demo {

// Seven rows, bit 4 = leftmost column. Unknown characters (and space) are blank.
inline const uint8_t* glyphRows(char c) {
    static const uint8_t blank[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t F[7] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10};
    static const uint8_t O[7] = {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
    static const uint8_t R[7] = {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11};
    static const uint8_t T[7] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t Y[7] = {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t D[7] = {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00};
    static const uint8_t N[7] = {0x11, 0x19, 0x15, 0x15, 0x13, 0x11, 0x11};
    static const uint8_t E[7] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f};
    static const uint8_t C[7] = {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e};
    static const uint8_t L[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f};
    static const uint8_t S[7] = {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e};
    switch (c) {
        case 'F': return F;
        case 'O': return O;
        case 'R': return R;
        case 'T': return T;
        case 'Y': return Y;
        case '-': return D;
        case 'N': return N;
        case 'E': return E;
        case 'C': return C;
        case 'L': return L;
        case 'S': return S;
        default: return blank;
    }
}

}  // namespace demo
