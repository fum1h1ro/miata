#include "KeyBinding.h"
#include <algorithm>
#include <iterator>
#include <print>
#include <expected>

namespace miata {
    // 添字はASCIIコード、値はmacOSのvirtual keycode(NSEvent.keyCode / Carbon kVK_*)
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
        kVK_Space, // 32 ' ' (Space)
        0, // 33 '!'
        0, // 34 '"'
        0, // 35 '#'
        0, // 36 '$'
        0, // 37 '%'
        0, // 38 '&'
        kVK_ANSI_Quote, // 39 '\''
        0, // 40 '('
        0, // 41 ')'
        0, // 42 '*'
        0, // 43 '+'
        kVK_ANSI_Comma, // 44 ','
        kVK_ANSI_Minus, // 45 '-'
        kVK_ANSI_Period, // 46 '.'
        kVK_ANSI_Slash, // 47 '/'
        kVK_ANSI_0, // 48 '0'
        kVK_ANSI_1, // 49 '1'
        kVK_ANSI_2, // 50 '2'
        kVK_ANSI_3, // 51 '3'
        kVK_ANSI_4, // 52 '4'
        kVK_ANSI_5, // 53 '5'
        kVK_ANSI_6, // 54 '6'
        kVK_ANSI_7, // 55 '7'
        kVK_ANSI_8, // 56 '8'
        kVK_ANSI_9, // 57 '9'
        0, // 58 ':'
        kVK_ANSI_Semicolon, // 59 ';'
        0, // 60 '<'
        kVK_ANSI_Equal, // 61 '='
        0, // 62 '>'
        0, // 63 '?'
        0, // 64 '@'
        kVK_ANSI_A, // 'A'
        kVK_ANSI_B, // 'B'
        kVK_ANSI_C, // 'C'
        kVK_ANSI_D, // 'D'
        kVK_ANSI_E, // 'E'
        kVK_ANSI_F, // 'F'
        kVK_ANSI_G, // 'G'
        kVK_ANSI_H, // 'H'
        kVK_ANSI_I, // 'I'
        kVK_ANSI_J, // 'J'
        kVK_ANSI_K, // 'K'
        kVK_ANSI_L, // 'L'
        kVK_ANSI_M, // 'M'
        kVK_ANSI_N, // 'N'
        kVK_ANSI_O, // 'O'
        kVK_ANSI_P, // 'P'
        kVK_ANSI_Q, // 'Q'
        kVK_ANSI_R, // 'R'
        kVK_ANSI_S, // 'S'
        kVK_ANSI_T, // 'T'
        kVK_ANSI_U, // 'U'
        kVK_ANSI_V, // 'V'
        kVK_ANSI_W, // 'W'
        kVK_ANSI_X, // 'X'
        kVK_ANSI_Y, // 'Y'
        kVK_ANSI_Z, // 'Z'
        kVK_ANSI_LeftBracket, // 91 '['
        kVK_ANSI_Backslash, // 92 '\\'
        kVK_ANSI_RightBracket, // 93 ']'
        0, // 94 '^'
        0, // 95 '_'
        kVK_ANSI_Grave, // 96 '`'
        kVK_ANSI_A, // 'a'
        kVK_ANSI_B, // 'b'
        kVK_ANSI_C, // 'c'
        kVK_ANSI_D, // 'd'
        kVK_ANSI_E, // 'e'
        kVK_ANSI_F, // 'f'
        kVK_ANSI_G, // 'g'
        kVK_ANSI_H, // 'h'
        kVK_ANSI_I, // 'i'
        kVK_ANSI_J, // 'j'
        kVK_ANSI_K, // 'k'
        kVK_ANSI_L, // 'l'
        kVK_ANSI_M, // 'm'
        kVK_ANSI_N, // 'n'
        kVK_ANSI_O, // 'o'
        kVK_ANSI_P, // 'p'
        kVK_ANSI_Q, // 'q'
        kVK_ANSI_R, // 'r'
        kVK_ANSI_S, // 's'
        kVK_ANSI_T, // 't'
        kVK_ANSI_U, // 'u'
        kVK_ANSI_V, // 'v'
        kVK_ANSI_W, // 'w'
        kVK_ANSI_X, // 'x'
        kVK_ANSI_Y, // 'y'
        kVK_ANSI_Z, // 'z'
        0, // 123 '{'
        0, // 124 '|'
        0, // 125 '}'
        0, // 126 '~'
        0, // 127 DEL (Delete)
    };

    std::map<std::string_view, uint16_t> KeyBinding::special_to_keycode_ = {
        { "esc", kVK_Escape },
        { "enter", kVK_Return },
        { "tab", kVK_Tab },
        { "bs", kVK_Delete },
        { "del", kVK_ForwardDelete },
        { "right", kVK_RightArrow },
        { "left", kVK_LeftArrow },
        { "down", kVK_DownArrow },
        { "up", kVK_UpArrow },
        { "f1", kVK_F1 },
        { "f2", kVK_F2 },
        { "f3", kVK_F3 },
        { "f4", kVK_F4 },
        { "f5", kVK_F5 },
        { "f6", kVK_F6 },
        { "f7", kVK_F7 },
        { "f8", kVK_F8 },
        { "f9", kVK_F9 },
        { "f10", kVK_F10 },
        { "f11", kVK_F11 },
        { "f12", kVK_F12 },
        { "f13", kVK_F13 },
        { "f14", kVK_F14 },
        { "f15", kVK_F15 },
        { "f16", kVK_F16 },
        { "f17", kVK_F17 },
        { "f18", kVK_F18 },
        { "f19", kVK_F19 },
        { "f20", kVK_F20 },
    };






    std::optional<char> KeyBinding::KeyCodeToAscii(uint16_t keycode)
    {
        for (const char c : std::string_view("abcdefghijklmnopqrstuvwxyz0123456789")) {
            if (ascii_to_keycode_[(uint8_t)c] == keycode) return c;
        }
        return std::nullopt;
    }

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
    std::expected<bool, KeyBinding::ErrorReason> KeyBinding::Register(const char* key_string, int func_ref, std::string_view desc)
    {
        auto r = ParseKey(key_string);
        if (!r) return std::unexpected(r.error());
        return Register(r.value(), func_ref, desc);
    }
    std::expected<bool, KeyBinding::ErrorReason> KeyBinding::Register(const KeyStroke& ks, int func_ref, std::string_view desc)
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
        // 説明は、登録し直すと置き換わる(説明なしで登録し直したら、前の説明は残さない)
        if (desc.empty()) descriptions_.erase(ks);
        else descriptions_[ks] = std::string(desc);
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
        descriptions_.erase(ks);
        return r.value();
    }

    std::vector<KeyBinding::Continuation> KeyBinding::Continuations(const KeyStroke& prefix) const
    {
        const int n = prefix.Size();
        std::vector<Continuation> result;

        for (const auto& [ks, ref] : bindings_) {
            if (ks.Size() <= n) continue;
            bool under_prefix = true;
            for (int i = 0; i < n; ++i) {
                if (!(ks.Keys()[(size_t)i] == prefix.Keys()[(size_t)i])) {
                    under_prefix = false;
                    break;
                }
            }
            if (!under_prefix) continue;

            const Key next = ks.Keys()[(size_t)n];
            auto it = std::find_if(result.begin(), result.end(), [&](const Continuation& c) { return c.key == next; });
            if (it == result.end()) {
                Continuation c;
                c.key = next;
                result.push_back(std::move(c));
                it = std::prev(result.end());
            }
            // このキーだけで確定する割り当て(prefix + next)があれば leaf。それより長い割り当ては、完全一致が先なので到達できない
            if (ks.Size() == n + 1) {
                it->leaf = true;
                auto d = descriptions_.find(ks);
                it->desc = d == descriptions_.end() ? std::string() : d->second;
            }
        }

        // 修飾キー無しを先に、表示名の昇順
        std::sort(result.begin(), result.end(), [](const Continuation& a, const Continuation& b) {
            const bool a_mod = a.key.Mods() != 0;
            const bool b_mod = b.key.Mods() != 0;
            if (a_mod != b_mod) return b_mod;
            return KeyName(a.key) < KeyName(b.key);
        });
        return result;
    }

    std::string KeyBinding::KeyName(const Key& key)
    {
        const uint16_t code = key.KeyCode();
        const uint16_t mods = key.Mods();

        // 括弧と修飾を除いた、キーの名前。bare は、修飾が無ければ、括弧なしで書ける(スペースと、1文字の印字文字)こと
        std::string name;
        bool bare = false;
        for (const auto& [special, special_code] : special_to_keycode_) {
            if (special_code == code) {
                name = std::string(special);
                break;
            }
        }
        if (name.empty()) {
            if (code == kVK_Space) {
                // README に書いたとおり、"<space>" は bind では使えない(表示だけ)
                name = "Space";
                bare = true;
            }
            else if (const auto c = KeyCodeToAscii(code)) {
                // 英数字(表の値 0 は「割り当て無し」と a の両方。その曖昧さは、KeyCodeToAscii が吸収している)
                name = std::string(1, *c);
                bare = true;
            }
            else {
                // 記号。大文字は小文字と同じキー(Shift ではない)で、上で引けているので、ここに来ない。
                // code は 0 ではない(0 は a で、上で引けている)ので、表の「割り当て無し」(0)には当たらない
                for (int c = 33; c < 127; ++c) {
                    if (ascii_to_keycode_[c] == code) {
                        name = std::string(1, (char)c);
                        bare = true;
                        break;
                    }
                }
            }
            if (name.empty()) name = std::format("key:{}", code);
        }

        if (mods == 0 && bare) return name;
        std::string s = "<";
        if (mods & pl_modifier::Ctrl) s += "C-";
        if (mods & pl_modifier::Shift) s += "S-";
        if (mods & pl_modifier::Alt) s += "A-";
        if (mods & pl_modifier::Super) s += "M-";
        s += name;
        s += '>';
        return s;
    }

    std::string KeyBinding::StrokeName(const KeyStroke& ks)
    {
        std::string s;
        for (int i = 0; i < ks.Size(); ++i) {
            s += KeyName(ks.Keys()[(size_t)i]);
        }
        return s;
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
                mods |= pl_modifier::Shift;
            }
            else if (t == "C" || t == "c") {
                mods |= pl_modifier::Ctrl;
            }
            else if (t == "A" || t == "a") {
                mods |= pl_modifier::Alt;
            }
            else if (t == "M" || t == "m") {
                mods |= pl_modifier::Super;
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
