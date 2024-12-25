#ifndef CONST_H__
#define CONST_H__

#include <cstdint>

namespace constants {
    enum class osx_modifier_flags : uint8_t {
        none = 0,
        caps_lock = 1<<0,
        shift = 1<<1,
        control = 1<<2,
        option = 1<<3,
        command = 1<<4,
        numeric_pad = 1<<5,
        help = 1 << 6,
        function = 1<<7,
    };
}



#endif // CONST_H__
