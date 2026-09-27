#include <cassert>
#include <cstring>
#include "../firmware/CubeSDCardReader/reader_policy.h"

int main()
{
    using namespace Reader;
    State s;
    assert(!s.request_reboot());
    s.media = Media::Ready;
    assert(!s.request_reboot());
    s.media = Media::Ejected; s.active = true;
    assert(!s.request_reboot());
    s.active = false; assert(s.request_reboot());
    assert(!s.begin());
    assert(valid_range(31, 1, 32));
    assert(!valid_range(UINT32_MAX, 2, 32));
    assert(!valid_range(32, 1, 32));
    LineParser p;
    for (unsigned i = 0; i < 129; i++) { p.push('x'); }
    for (char c : "bootloader") { if (c) { p.push(c); } }
    assert(p.push('\n') == LineParser::Result::Invalid);
    for (char c : "info") { if (c) { p.push(c); } }
    assert(p.push('\r') == LineParser::Result::Line);
    assert(strcmp(p.line(), "info") == 0);
    assert(p.push('\n') == LineParser::Result::None);
    p.push(0);
    assert(p.push('\n') == LineParser::Result::Invalid);
}
