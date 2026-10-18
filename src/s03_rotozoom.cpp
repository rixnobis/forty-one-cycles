// Section 3: ROTOZOOM (stub). Replaced by the lane that owns it; see the design notes.
#include "demo.hh"

namespace demo {
namespace {
StubSection s_section("S3 ROTOZOOM", {{.r = 0xc0, .g = 0x90, .b = 0x00}});
}
Section& section3() { return s_section; }
}  // namespace demo
