// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace Reader {
enum class Media : uint8_t { Initializing, Ready, Ejected, Absent, Error };

struct State {
    Media media = Media::Initializing;
    bool active = false;
    bool quiescing = false;
    bool prevent_removal = false;
    uint32_t read_sectors = 0;
    uint32_t written_sectors = 0;
    uint32_t errors = 0;

    bool request_reboot()
    {
        if (active || quiescing || media == Media::Initializing || media == Media::Ready) {
            return false;
        }
        quiescing = true;
        return true;
    }
    bool begin()
    {
        if (quiescing) {
            return false;
        }
        active = true;
        return true;
    }
};

inline bool valid_range(uint32_t first, uint32_t count, uint32_t capacity)
{
    return first <= capacity && count <= capacity - first;
}

// Drop an entire malformed line, including suffixes resembling valid commands.
class LineParser {
public:
    enum class Result { None, Line, Invalid };
    Result push(uint8_t byte)
    {
        if (byte == '\n' || byte == '\r') {
            if (_bad) {
                reset();
                return Result::Invalid;
            }
            if (_size == 0) {
                return Result::None;
            }
            _line[_size] = 0;
            _size = 0;
            return Result::Line;
        }
        if (byte < 32 || byte > 126 || _size == 128) {
            _bad = true;
        }
        if (!_bad) {
            _line[_size++] = char(byte);
        }
        return Result::None;
    }
    const char *line() const { return _line; }
    void reset() { _size = 0; _bad = false; }
private:
    char _line[129] {};
    size_t _size = 0;
    bool _bad = false;
};
}
