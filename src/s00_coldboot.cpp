// Section 0: COLD_BOOT (stub). Replaced by the lane that owns it; see the design notes.
#include "demo.hh"

namespace demo {
namespace {
StubSection s_section("S0 COLD_BOOT", {{.r = 0x00, .g = 0x90, .b = 0x30}});
}
Section& section0() { return s_section; }
}  // namespace demo
