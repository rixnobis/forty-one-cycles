// Section 1: STARFIELD (stub). Replaced by the lane that owns it; see the design notes.
#include "demo.hh"

namespace demo {
namespace {
StubSection s_section("S1 STARFIELD", {{.r = 0x00, .g = 0x00, .b = 0xc0}});
}
Section& section1() { return s_section; }
}  // namespace demo
