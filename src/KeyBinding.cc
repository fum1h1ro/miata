#include "KeyBinding.h"
#include <print>
#include <expected>

namespace miata {
    uint16_t KeyBinding::ascii_to_keycode_[128] = {
        0, // 0 NUL (Null char)
        0, // 1 SOH (Start of Heading)
        0, // 2 STX (Start of Text)
        0, // 3 ETX (End of Text)
        0, // 4 EOT (End of Transmission)
        0, // 5 ENQ (Enquiry)
        0, // 6 ACK (Acknowledge)
        0, // 7 BEL (Bell)
        0, // 8 BS  (Backspace)
        0, // 9 TAB (Horizontal Tab)
        0, // 10 LF  (Line Feed)
        0, // 11 VT  (Vertical Tab)
        0, // 12 FF  (Form Feed)
        0, // 13 CR  (Carriage Return)
        0, // 14 SO  (Shift Out)
        0, // 15 SI  (Shift In)
        0, // 16 DLE (Data Link Escape)
        0, // 17 DC1 (Device Control 1)
        0, // 18 DC2 (Device Control 2)
        0, // 19 DC3 (Device Control 3)
        0, // 20 DC4 (Device Control 4)
        0, // 21 NAK (Negative Acknowledge)
        0, // 22 SYN (Synchronous Idle)
        0, // 23 ETB (End of Transmit Block)
        0, // 24 CAN (Cancel)
        0, // 25 EM  (End of Medium)
        0, // 26 SUB (Substitute)
        0, // 27 ESC (Escape)
        0, // 28 FS  (File Separator)
        0, // 29 GS  (Group Separator)
        0, // 30 RS  (Record Separator)
        0, // 31 US  (Unit Separator)
        SAPP_KEYCODE_SPACE, // 32 ' ' (Space)
        0, // 33 '!'
        0, // 34 '"'
        0, // 35 '#'
        0, // 36 '$'
        0, // 37 '%'
        0, // 38 '&'
        SAPP_KEYCODE_APOSTROPHE, // 39 '\''
        0, // 40 '('
        0, // 41 ')'
        0, // 42 '*'
        0, // 43 '+'
        SAPP_KEYCODE_COMMA, // 44 ','
        SAPP_KEYCODE_MINUS, // 45 '-'
        SAPP_KEYCODE_PERIOD, // 46 '.'
        SAPP_KEYCODE_SLASH, // 47 '/'
        SAPP_KEYCODE_0, // 48 '0'
        SAPP_KEYCODE_1, // 49 '1'
        SAPP_KEYCODE_2, // 50 '2'
        SAPP_KEYCODE_3, // 51 '3'
        SAPP_KEYCODE_4, // 52 '4'
        SAPP_KEYCODE_5, // 53 '5'
        SAPP_KEYCODE_6, // 54 '6'
        SAPP_KEYCODE_7, // 55 '7'
        SAPP_KEYCODE_8, // 56 '8'
        SAPP_KEYCODE_9, // 57 '9'
        0, // 58 ':'
        SAPP_KEYCODE_SEMICOLON, // 59 ';'
        0, // 60 '<'
        SAPP_KEYCODE_EQUAL, // 61 '='
        0, // 62 '>'
        0, // 63 '?'
        0, // 64 '@'
        SAPP_KEYCODE_A, // 'A'
        SAPP_KEYCODE_B, // 'B'
        SAPP_KEYCODE_C, // 'C'
        SAPP_KEYCODE_D, // 'D'
        SAPP_KEYCODE_E, // 'E'
        SAPP_KEYCODE_F, // 'F'
        SAPP_KEYCODE_G, // 'G'
        SAPP_KEYCODE_H, // 'H'
        SAPP_KEYCODE_I, // 'I'
        SAPP_KEYCODE_J, // 'J'
        SAPP_KEYCODE_K, // 'K'
        SAPP_KEYCODE_L, // 'L'
        SAPP_KEYCODE_M, // 'M'
        SAPP_KEYCODE_N, // 'N'
        SAPP_KEYCODE_O, // 'O'
        SAPP_KEYCODE_P, // 'P'
        SAPP_KEYCODE_Q, // 'Q'
        SAPP_KEYCODE_R, // 'R'
        SAPP_KEYCODE_S, // 'S'
        SAPP_KEYCODE_T, // 'T'
        SAPP_KEYCODE_U, // 'U'
        SAPP_KEYCODE_V, // 'V'
        SAPP_KEYCODE_W, // 'W'
        SAPP_KEYCODE_X, // 'X'
        SAPP_KEYCODE_Y, // 'Y'
        SAPP_KEYCODE_Z, // 'Z'
        SAPP_KEYCODE_LEFT_BRACKET, // 91 '['
        SAPP_KEYCODE_BACKSLASH, // 92 '\\'
        SAPP_KEYCODE_RIGHT_BRACKET, // 93 ']'
        0, // 94 '^'
        0, // 95 '_'
        SAPP_KEYCODE_GRAVE_ACCENT, // 96 '`'
        SAPP_KEYCODE_A, // 'a'
        SAPP_KEYCODE_B, // 'b'
        SAPP_KEYCODE_C, // 'c'
        SAPP_KEYCODE_D, // 'd'
        SAPP_KEYCODE_E, // 'e'
        SAPP_KEYCODE_F, // 'f'
        SAPP_KEYCODE_G, // 'g'
        SAPP_KEYCODE_H, // 'h'
        SAPP_KEYCODE_I, // 'i'
        SAPP_KEYCODE_J, // 'j'
        SAPP_KEYCODE_K, // 'k'
        SAPP_KEYCODE_L, // 'l'
        SAPP_KEYCODE_M, // 'm'
        SAPP_KEYCODE_N, // 'n'
        SAPP_KEYCODE_O, // 'o'
        SAPP_KEYCODE_P, // 'p'
        SAPP_KEYCODE_Q, // 'q'
        SAPP_KEYCODE_R, // 'r'
        SAPP_KEYCODE_S, // 's'
        SAPP_KEYCODE_T, // 't'
        SAPP_KEYCODE_U, // 'u'
        SAPP_KEYCODE_V, // 'v'
        SAPP_KEYCODE_W, // 'w'
        SAPP_KEYCODE_X, // 'x'
        SAPP_KEYCODE_Y, // 'y'
        SAPP_KEYCODE_Z, // 'z'
        0, // 123 '{'
        0, // 124 '|'
        0, // 125 '}'
        0, // 126 '~'
        0, // 127 DEL (Delete)
    };

    std::map<std::string_view, sapp_keycode> KeyBinding::special_to_keycode_ = {
        { "esc", SAPP_KEYCODE_ESCAPE },
        { "enter", SAPP_KEYCODE_ENTER },
        { "tab", SAPP_KEYCODE_TAB },
        { "bs", SAPP_KEYCODE_BACKSPACE },
        { "del", SAPP_KEYCODE_DELETE },
        { "right", SAPP_KEYCODE_RIGHT },
        { "left", SAPP_KEYCODE_LEFT },
        { "down", SAPP_KEYCODE_DOWN },
        { "up", SAPP_KEYCODE_UP },
        { "f1", SAPP_KEYCODE_F1 },
        { "f2", SAPP_KEYCODE_F2 },
        { "f3", SAPP_KEYCODE_F3 },
        { "f4", SAPP_KEYCODE_F4 },
        { "f5", SAPP_KEYCODE_F5 },
        { "f6", SAPP_KEYCODE_F6 },
        { "f7", SAPP_KEYCODE_F7 },
        { "f8", SAPP_KEYCODE_F8 },
        { "f9", SAPP_KEYCODE_F9 },
        { "f10", SAPP_KEYCODE_F10 },
        { "f11", SAPP_KEYCODE_F11 },
        { "f12", SAPP_KEYCODE_F12 },
        { "f13", SAPP_KEYCODE_F13 },
        { "f14", SAPP_KEYCODE_F14 },
        { "f15", SAPP_KEYCODE_F15 },
        { "f16", SAPP_KEYCODE_F16 },
        { "f17", SAPP_KEYCODE_F17 },
        { "f18", SAPP_KEYCODE_F18 },
        { "f19", SAPP_KEYCODE_F19 },
        { "f20", SAPP_KEYCODE_F20 },
        { "f21", SAPP_KEYCODE_F21 },
        { "f22", SAPP_KEYCODE_F22 },
        { "f23", SAPP_KEYCODE_F23 },
        { "f24", SAPP_KEYCODE_F24 },
        { "f25", SAPP_KEYCODE_F25 },
    };






    KeyBinding::KeyBinding()
    {
        //printf("Keysize: %lu\n", sizeof(Key));

        //auto keys = ParseKey("a<S-A><C-up><S-m>");
        //for (auto& k : keys) {
        //    printf("key: %s\n", k.ToString().c_str());
        //}

        //Register("k", 1);
        //Register("a", 2);
        //Register("fg", 3);


        //if (Has("k")) {
        //    printf("k is registered\n");
        //}
        //if (Has("K")) {
        //    printf("K is registered\n");
        //}



    }
    KeyBinding::~KeyBinding()
    {
    }
    std::expected<int, KeyBinding::ErrorReason> KeyBinding::Has(const char* key_string)
    {
        auto r = ParseKey(key_string);
        if (!r) return std::unexpected(r.error());
        return Has(r.value());
    }
    std::expected<int, KeyBinding::ErrorReason> KeyBinding::Has(const KeyStroke& ks)
    {
        auto it = bindings_.find(ks);
        if (it != bindings_.end()) {
            return it->second;
        }

        KeyStroke route;
        for (auto i = 0; i < ks.Size(); ++i) {
            auto& key = ks.Keys()[(size_t)i];
            auto r = route.Add(key);
            if (!r) return std::unexpected(r.error());
            auto it = routes_.find(route);
            if (it == routes_.end() || it->second <= 0) {
                return std::unexpected(ErrorReason::NotFound);
            }
        }
        return std::unexpected(ErrorReason::MaybeTooShort);
    }
    std::expected<bool, KeyBinding::ErrorReason> KeyBinding::Register(const char* key_string, int func_ref)
    {
        auto r = ParseKey(key_string);
        if (!r) return std::unexpected(r.error());
        return Register(r.value(), func_ref);
    }
    std::expected<bool, KeyBinding::ErrorReason> KeyBinding::Register(const KeyStroke& ks, int func_ref)
    {
        KeyStroke route;
        for (auto i = 0; i < ks.Size(); ++i) {
            auto& key = ks.Keys()[(size_t)i];
            auto r = route.Add(key);
            if (!r) return std::unexpected(r.error());
            auto it = routes_.find(route);
            if (it == routes_.end()) {
                routes_[route] = 1;
            }
            else {
                it->second += 1;
            }
        }

        bindings_[ks] = func_ref;
        return true;
    }
    std::expected<int, KeyBinding::ErrorReason> KeyBinding::Unregister(const char* key_string)
    {
        auto r = ParseKey(key_string);
        if (!r) return std::unexpected(r.error());
        return Unregister(r.value());
    }
    std::expected<int, KeyBinding::ErrorReason> KeyBinding::Unregister(const KeyStroke& ks)
    {
        auto r = Has(ks);
        if (!r) return std::unexpected(r.error());

        KeyStroke route;
        for (auto i = 0; i < ks.Size(); ++i) {
            auto& key = ks.Keys()[(size_t)i];
            auto r = route.Add(key);
            if (!r) return std::unexpected(r.error());
            auto it = routes_.find(route);
            if (it == routes_.end()) {
                return std::unexpected(ErrorReason::NotFound);
            }
            else {
                it->second -= 1;
            }
        }
        bindings_.erase(ks);
        return r.value();
    }

    void KeyBinding::DumpAll()
    {
        std::println("-- DumpAll --");
        for (auto& [ks, ref] : bindings_) {
            std::println("key: {} ref: {}", ks.ToString(), ref);
        }
        for (auto& [ks, count] : routes_) {
            std::println("route: {} count: {}", ks.ToString(), count);
        }
    }
    void KeyBinding::Dump(int& indent, std::map<Key, std::unique_ptr<Setting>>& bindings)
    {
        struct temp {
            static void Indent(int n)
            {
                for (int i = 0; i < n; ++i) {
                    printf("  ");
                }
            }
        };
        for (auto& [k, s] : bindings) {
            temp::Indent(indent);
            printf("key: %s\n", k.ToString().c_str());
            if (s->func_ref_ != LUA_NOREF) {
                temp::Indent(indent);
                printf("func_ref: %d\n", s->func_ref_);
            }
            ++indent;
            Dump(indent, s->next_);
            --indent;
        }
    }



    void KeyBinding::Clear(lua_State* L, const char* key)
    {
    }

    void KeyBinding::ClearAll(lua_State* L)
    {
    }


    void KeyBinding::ClearImpl(lua_State* L, Setting& setting)
    {
    }

    std::expected<KeyBinding::KeyStroke, KeyBinding::ErrorReason> KeyBinding::ParseKey(const std::string_view& key_stroke)
    {
        KeyStroke ks;

        bool tagged = false;
        size_t tag_start = 0;

        for (size_t i = 0; i < key_stroke.size(); ++i) {
            const auto c = key_stroke[i];
            //
            if (!tagged) {
                if (c == '<') {
                    tagged = true;
                    tag_start = i;
                    continue;
                }
                else {
                    auto keycode = ascii_to_keycode_[(uint8_t)c];
                    auto r = ks.Add(Key::MakeKey(0, keycode));
                    if (!r) return std::unexpected(ErrorReason::MaxDepthOver);
                    continue;
                }
            }
            if (tagged) {
                if (c == '>') {
                    tagged = false;
                    std::string_view sv = key_stroke.substr(tag_start + 1, i - tag_start - 1);
                    auto r = ks.Add(ParseTag(sv));
                    if (!r) return std::unexpected(ErrorReason::MaxDepthOver);
                    continue;
                }
            }
        }
        return ks;
    }

    KeyBinding::Key KeyBinding::ParseTag(const std::string_view& tag)
    {
        std::vector<std::string_view> tokens;
        size_t start = 0;
        size_t end = 0;
        while ((end = tag.find('-', start)) != std::string::npos) {
            tokens.emplace_back(tag.substr(start, end - start));
            start = end + 1;
        }
        if (start < tag.size()) tokens.emplace_back(tag.substr(start));

        uint8_t mods = 0;
        uint16_t key = 0;

        for (size_t i = 0; i < tokens.size() - 1; ++i) {
            auto& t = tokens[i];
            if (t == "S" || t == "s") {
                mods |= SAPP_MODIFIER_SHIFT;
            }
            else if (t == "C" || t == "c") {
                mods |= SAPP_MODIFIER_CTRL;
            }
            else if (t == "A" || t == "a") {
                mods |= SAPP_MODIFIER_ALT;
            }
            else if (t == "M" || t == "m") {
                mods |= SAPP_MODIFIER_SUPER;
            }
            else {
                throw std::runtime_error("Invalid modifier");
            }
        }
        auto& t = tokens.back();
        if ((key = ParseSpecialKey(t)) == 0) {
            key = ascii_to_keycode_[(uint8_t)t[0]];
        }
        return Key::MakeKey(mods, key);
    }

    uint16_t KeyBinding::ParseSpecialKey(const std::string_view& tok)
    {
        auto it = special_to_keycode_.find(tok);
        if (it != special_to_keycode_.end()) {
            return (*it).second;
        }
        return 0;
    }
}
