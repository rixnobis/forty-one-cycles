// Section 5: PLASMA_MESSAGE (stub). Replaced by the lane that owns it; see the design notes.
#include "demo.hh"

namespace demo {
namespace {
StubSection s_section("S5 PLASMA_MESSAGE", {{.r = 0xf0, .g = 0x00, .b = 0xf0}});
}
Section& section5() { return s_section; }
}  // namespace demo
