// Section 4: VECTOR_BALLS (stub). Replaced by the lane that owns it; see the design notes.
#include "demo.hh"

namespace demo {
namespace {
StubSection s_section("S4 VECTOR_BALLS", {{.r = 0x30, .g = 0xc0, .b = 0xc0}});
}
Section& section4() { return s_section; }
}  // namespace demo
