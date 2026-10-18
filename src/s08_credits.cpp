// Section 8: CREDITS_END (stub). Replaced by the lane that owns it; see the design notes.
#include "demo.hh"

namespace demo {
namespace {
StubSection s_section("S8 CREDITS_END", {{.r = 0x90, .g = 0x90, .b = 0x00}});
}
Section& section8() { return s_section; }
}  // namespace demo
