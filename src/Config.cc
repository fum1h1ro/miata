#include "Config.h"
#include "Script.h"
#include <charconv>



namespace miata {
    Config* Config::_instance = nullptr;

    const char* Config::Color::names_[] = {
#define X(name, var) #var,
        CONFIG_COLOR_LIST
#undef X
    };

    Config::Color::Color()
    {
        constexpr int sz = sizeof(names_) / sizeof(names_[0]);
        values_.resize(sz);

        for (std::underlying_type_t<Type> i = 0; i < sz; ++i) {
            table_[names_[i]] = (Type)i;
        }
        for (auto t : table_) {
            std::printf("%s: %d\n", t.first.c_str(), (int)t.second);
        }
    }
    std::string Config::Color::ToString(const Color4f& v)
    {
        auto r = (int)(v.r * 255);
        auto g = (int)(v.g * 255);
        auto b = (int)(v.b * 255);
        auto a = (int)(v.a * 255);
        return std::format("#{:02x}{:02x}{:02x}{:02x}", r, g, b, a);
    }
    std::expected<Color4f, std::string> Config::Color::FromString(const std::string_view& v)
    {
        Color4f result;
        const size_t sz = v.size();
        if (v[0] != '#') {
            return std::unexpected(std::format("unknown color format: {}", v));
        }
        auto p = v.data();
        if (sz == 7) {
            int r, g, b;
            auto result_r = std::from_chars(&p[1], &p[3], r, 16);
            auto result_g = std::from_chars(&p[3], &p[5], g, 16);
            auto result_b = std::from_chars(&p[5], &p[7], b, 16);
            if (result_r.ec != std::errc() || result_g.ec != std::errc() || result_b.ec != std::errc()) {
                return std::unexpected(std::format("unknown color format: {}", v));
            }
            result.r = (float)r / 255.0f;
            result.g = (float)g / 255.0f;
            result.b = (float)b / 255.0f;
            result.a = 1.0f;
        }
        else if (sz == 9) {
            int r, g, b, a;
            auto result_r = std::from_chars(&p[1], &p[3], r, 16);
            auto result_g = std::from_chars(&p[3], &p[5], g, 16);
            auto result_b = std::from_chars(&p[5], &p[7], b, 16);
            auto result_a = std::from_chars(&p[7], &p[9], a, 16);
            if (result_r.ec != std::errc() || result_g.ec != std::errc() || result_b.ec != std::errc() || result_a.ec != std::errc()) {
                return std::unexpected(std::format("unknown color format: {}", v));
            }
            result.r = (float)r / 255.0f;
            result.g = (float)g / 255.0f;
            result.b = (float)b / 255.0f;
            result.a = (float)a / 255.0f;
        }
        else {
            return std::unexpected(std::format("unknown color format: {}", v));
        }
        return result;
    }








    Config::Config()
    {
    }

    Config::~Config()
    {
    }

    void Config::ScriptInitialize()
    {
        //auto& config = Instance();
        auto& script = Script::Instance();
        script.StackUsing([&]() {
            script.GetGlobal("Miata.config.color");
            static luaL_Reg metas[] = {
                { "__index", lua_color_index },
                { "__newindex", lua_color_newindex },
            };
            script.RegisterFunctionsToMetaTable(std::vector<luaL_Reg>(std::begin(metas), std::end(metas)));
        });
    }

    int Config::lua_color_index(lua_State* L)
    {
        auto nargs = lua_gettop(L);
        if (nargs != 2) {
            luaL_error(L, "invalid number of arguments");
        }
        Script::CheckArgType(L, 1, LUA_TTABLE);
        Script::CheckArgType(L, 2, LUA_TSTRING);
        auto name = lua_tostring(L, 2);

        auto result = Config::Color().Get(name);
        if (!result) {
            luaL_error(L, "unknown color type");
        }
        auto str = Config::Color::ToString(result.value());
        lua_pushstring(L, str.c_str());
        return 1;
    }

    int Config::lua_color_newindex(lua_State* L)
    {
        auto nargs = lua_gettop(L);
        if (nargs != 3) {
            luaL_error(L, "invalid number of arguments");
        }
        Script::CheckArgType(L, 1, LUA_TTABLE);
        Script::CheckArgType(L, 2, LUA_TSTRING);
        Script::CheckArgType(L, 3, LUA_TSTRING);
        auto name = lua_tostring(L, 2);
        auto value = lua_tostring(L, 3);

        auto color = Config::Color::FromString(value);
        if (!color) {
            luaL_error(L, color.error().c_str());
        }
        auto r = Config::Color().Set(name, color.value());
        if (!r) {
            luaL_error(L, r.error().c_str());
        }
        return 0;
    }

}
