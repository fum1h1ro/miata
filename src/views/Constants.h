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

    // 左右2ペインのどちらか。Luaには "left" / "right" の文字列で公開する
    enum class Pane : uint8_t {
        Left,
        Right,
    };
}




#endif // VIEW_CONSTANTS_H__
