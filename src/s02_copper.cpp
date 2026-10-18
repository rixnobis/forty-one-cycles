// Section 2: COPPER_SCROLLER (stub). Replaced by the lane that owns it; see the design notes.
#include "demo.hh"

namespace demo {
namespace {
StubSection s_section("S2 COPPER_SCROLLER", {{.r = 0xf0, .g = 0x30, .b = 0x30}});
}
Section& section2() { return s_section; }
}  // namespace demo
