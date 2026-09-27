#ifndef CONFIG_H__
#define CONFIG_H__

#include "platform.h"
#include <format>
#include <map>
#include <vector>
#include <expected>
extern "C" {
#include "lua.h"
}

#define CONFIG_COLOR_LIST \
    X(Background, background) \
    X(NormalText, normal_text) \
    X(NormalFile, normal_file) \
    X(Directory, directory) \



namespace miata {
    class Config {
    public:
        class Color {
            friend class Config;
        public:
            enum class Type : int32_t {
#define X(name, var) name,
                CONFIG_COLOR_LIST
#undef X
            };

            const Color4f& Get(Type type) const
            {
                return values_[(size_t)type];
            }
            std::expected<Color4f, std::string> Get(std::string type) const
            {
                auto idx = Find(type);
                if (!idx) return std::unexpected(idx.error());
                return values_[(size_t)idx.value()];
            }
            void Set(Type type, const Color4f& value)
            {
                values_[(size_t)type] = value;
            }
            std::expected<bool, std::string> Set(std::string type, const Color4f& value)
            {
                auto idx = Find(type);
                if (!idx) return std::unexpected(idx.error());
                values_[(size_t)idx.value()] = value;
                return true;
            }
            static std::string ToString(const Color4f& v);
            static std::expected<Color4f, std::string> FromString(const std::string_view& v);

        private:
            Color();
            std::expected<Type, std::string> Find(std::string type) const
            {
                auto t = table_.find(type);
                if (t == table_.end()) return std::unexpected(std::format("type not found: {}", type));
                return (*t).second;
            }

            static const char* names_[];
            std::vector<Color4f> values_;
            std::map<std::string, Type> table_;
        };



        static void ScriptInitialize();
        static inline Color& Color()
        {
            return Instance().color_;
        }

        // フォントファミリー名。未指定(空文字列)の場合はシステムデフォルトフォントを使う。
        static inline const std::string& FontFamily()
        {
            return Instance().font_family_;
        }

        // ファイル一覧本体の基準フォントサイズ(pt)。ヘッダーはこれより少し大きいサイズを使う。
        static inline float FontSize()
        {
            return Instance().font_size_;
        }

    private:
        static inline Config& Instance()
        {
            if (_instance == nullptr) {
                _instance = new Config();
            }
            return *_instance;
        }

        Config();
        ~Config();
        static int lua_color_index(lua_State* L);
        static int lua_color_newindex(lua_State* L);
        static int lua_set_font(lua_State* L);
        static int lua_set_font_size(lua_State* L);

        static Config* _instance;
        class Color color_;
        std::string font_family_;
        float font_size_ = 12.0f;
    };
}





#endif // CONFIG_H__
