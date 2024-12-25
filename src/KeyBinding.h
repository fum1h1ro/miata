#ifndef KEYBINDING_H__
#define KEYBINDING_H__

#include <string>
#include <vector>
#include <format>
#include <map>
#include <expected>
#include <sokol_app.h>
#include "misc.h"
extern "C" {
#include <lua.h>
#include "lauxlib.h"
}

namespace miata {
    class KeyBinding {
    public:
        enum class ErrorReason {
            InvalidKey,
            NotFound,
            Duplicated,
            MaxDepthOver, // KeyStroke is too long
            MaybeTooShort, // KeyStroke is too short(Maybe add some keys to find the right key stroke)
        };

        struct Key {
            Key() : mods_(), keycode_(0) {}
            bool IsValid() const
            {
                return keycode_ != 0;
            }
            static Key MakeKey(uint16_t mods, uint16_t code)
            {
                Key key;
                key.mods_ = mods;
                key.keycode_ = code;
                return key;
            }
            std::string ToString() const
            {
                std::string s;
                if (mods_.is(SAPP_MODIFIER_SHIFT)) {
                    s += "Shift+";
                }
                if (mods_.is(SAPP_MODIFIER_CTRL)) {
                    s += "Ctrl+";
                }
                if (mods_.is(SAPP_MODIFIER_ALT)) {
                    s += "Alt+";
                }
                if (mods_.is(SAPP_MODIFIER_SUPER)) {
                    s += "Super+";
                }
                s += std::format("{:d}", keycode_);
                return s;
            }
            bool operator<(const Key& rhs) const
            {
                if (mods_.value() < rhs.mods_.value()) return true;
                if (mods_.value() > rhs.mods_.value()) return false;
                return keycode_ < rhs.keycode_;
            }
            bool operator==(const Key& rhs) const
            {
                return mods_.value() == rhs.mods_.value() && keycode_ == rhs.keycode_;
            }
        private:
            misc::Flags<uint16_t> mods_;
            uint16_t keycode_;
        };

        class KeyStroke {
        public:
            KeyStroke() : index_(0)
            {
            }
            void Clear()
            {
                index_ = 0;
            }
            std::expected<bool, ErrorReason> Add(const Key& key)
            {
                if (index_ >= 4) return std::unexpected(ErrorReason::MaxDepthOver);
                keys_.at((size_t)index_++) = key;
                return true;
            }
            int Size() const
            {
                return index_;
            }
            const std::array<Key, 4>& Keys() const
            {
                return keys_;
            }
            std::string ToString() const
            {
                std::string s;
                for (auto i = 0; i < index_; ++i) {
                    s += keys_[(size_t)i].ToString();
                    //if (i < index_ - 1) s += "-";
                }
                return s;
            }
            bool operator==(const KeyStroke& rhs) const
            {
                if (Size() != rhs.Size()) return false;
                for (auto i = 0; i < Size(); ++i) {
                    if (keys_[(size_t)i] != rhs.keys_[(size_t)i]) return false;
                }
                return true;
            }
        private:
            int index_;
            std::array<Key, 4> keys_;
        };

        struct KeyStrokeHash {
            size_t operator()(const KeyStroke& ks) const
            {
                size_t h = 0;
                for (auto i = 0; i < ks.Size(); ++i) {
                    auto& k = ks.Keys()[(size_t)i];
                    // @todo fix this
                    h ^= std::hash<uint32_t>()(*(uint32_t*)&k) + 1;
                }
                return h;
            }
        };


        struct Setting {
            int func_ref_;
            std::map<Key, std::unique_ptr<Setting>> next_;
            const Setting* parent_;

            Setting(Setting* parent) : func_ref_(LUA_NOREF), parent_(parent)
            {
            }
        };










        KeyBinding();
        virtual ~KeyBinding();
        std::expected<int, ErrorReason> Has(const char* key_string);
        std::expected<int, ErrorReason> Has(const KeyStroke& ks);
        std::expected<bool, ErrorReason> Register(const char* key_string, int func_ref);
        std::expected<bool, ErrorReason> Register(const KeyStroke& ks, int func_ref);
        std::expected<int, ErrorReason> Unregister(const char* key_string);
        std::expected<int, ErrorReason> Unregister(const KeyStroke& ks);
        void DumpAll();




        void Clear(lua_State* L, const char* key);
        void ClearAll(lua_State* L);
        void Bind(lua_State* L, const char* key, int func_ref);


        static inline bool IsModifierKey(const sapp_keycode code)
        {
            return code == SAPP_KEYCODE_LEFT_SHIFT || code == SAPP_KEYCODE_RIGHT_SHIFT ||
                code == SAPP_KEYCODE_LEFT_CONTROL || code == SAPP_KEYCODE_RIGHT_CONTROL ||
                code == SAPP_KEYCODE_LEFT_ALT || code == SAPP_KEYCODE_RIGHT_ALT ||
                code == SAPP_KEYCODE_LEFT_SUPER || code == SAPP_KEYCODE_RIGHT_SUPER;
        }
    private:
        std::expected<KeyStroke, ErrorReason> ParseKey(const std::string_view& key_stroke);
        KeyBinding::Key ParseTag(const std::string_view& tag);
        uint16_t ParseSpecialKey(const std::string_view& tok);
        void Dump(int& indent, std::map<Key, std::unique_ptr<Setting>>& bindings);







        //Setting& Find(const char* key);
        void ClearImpl(lua_State* L, Setting& setting);



        std::unordered_map<KeyStroke, int, KeyStrokeHash> routes_;
        std::unordered_map<KeyStroke, int, KeyStrokeHash> bindings_;




        //std::map<Key, std::unique_ptr<Setting>> bindings_;
        static uint16_t ascii_to_keycode_[128];
        static std::map<std::string_view, sapp_keycode> special_to_keycode_;
    };








}



#endif // KEYBINDING_H__
