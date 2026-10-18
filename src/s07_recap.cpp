// Section 7: RECAP (stub). Replaced by the lane that owns it; see the design notes.
#include "demo.hh"

namespace demo {
namespace {
StubSection s_section("S7 RECAP", {{.r = 0xc0, .g = 0xc0, .b = 0xc0}});
}
Section& section7() { return s_section; }
}  // namespace demo
