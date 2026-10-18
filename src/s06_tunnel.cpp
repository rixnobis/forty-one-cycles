// Section 6: TUNNEL (stub). Replaced by the lane that owns it; see the design notes.
#include "demo.hh"

namespace demo {
namespace {
StubSection s_section("S6 TUNNEL", {{.r = 0x00, .g = 0xc0, .b = 0x60}});
}
Section& section6() { return s_section; }
}  // namespace demo
