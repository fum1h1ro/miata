#ifndef VIEW_CONSTANTS_H__
#define VIEW_CONSTANTS_H__

#include <cstdint>

namespace miata::views::constants {
    enum class Navigate : uint8_t {
        Up,
        Down,
        Left,
        Right,
        Ok,
        Cancel,
    };
}




#endif // VIEW_CONSTANTS_H__
