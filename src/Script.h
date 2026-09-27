#ifndef SCRIPT_H__
#define SCRIPT_H__

#include <filesystem>
#include <any>
#include <functional>
#include <string>
#include <map>
#include <expected>
#include <print>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}



namespace miata {
    class Script {
        struct ThreadParam {
            lua_State* L_;
            int status_;
            int nresult_;

            ThreadParam(lua_State* L) : L_(L), status_(LUA_OK), nresult_(0) {}
        };
    public:
        static inline Script& Instance()
        {
            static Script instance;
            return instance;
        }

        template<typename R, int EXPECTED>
        static R GetTableField(lua_State* L, int table_idx, std::string key, R default_value)
        {
            lua_getfield(L, table_idx, key.c_str());
            if (lua_type(L, -1) == LUA_TNIL) {
                return default_value;
            }
            if (lua_type(L, -1) != EXPECTED) {
                luaL_error(L, "expected %d, got %d", EXPECTED, lua_type(L, -1));
            }
            R r;
            switch (EXPECTED) {
            case LUA_TNUMBER:
                r = lua_tonumber(L, -1);
                break;
            case LUA_TBOOLEAN:
                r = lua_toboolean(L, -1);
                break;
            case LUA_TSTRING:
                r = lua_tostring(L, -1);
                break;
            default:
                luaL_error(L, "unsupported type");
            }
            lua_pop(L, 1);
            return r;
        }

        template<typename T>
        static int FreeSharedUserdata(lua_State* L)
        {
            auto p = static_cast<std::shared_ptr<T>*>(lua_touserdata(L, 1));
            p->~shared_ptr();
            std::print("free shared userdata\n");
            return 0;
        }

        template<typename T>
        static std::shared_ptr<T> PushSharedUserdata(lua_State* L, std::shared_ptr<T>& ptr)
        {
            void* ud = lua_newuserdata(L, sizeof(std::shared_ptr<T>));
            new(ud) std::shared_ptr<T>(ptr);
            if (luaL_newmetatable(L, typeid(std::shared_ptr<T>).name())) {
                lua_pushstring(L, "__gc");
                lua_pushcfunction(L, FreeSharedUserdata<T>);
                lua_settable(L, -3);
            }
            lua_setmetatable(L, -2);
            return *static_cast<std::shared_ptr<T>*>(ud);
        }


        void Initialize();
        void PostInitialize();
        bool Update();
        std::expected<bool, std::string> DoFile(std::filesystem::path path);
        std::expected<bool, std::string> DoResourceFile(std::filesystem::path path);
        std::expected<bool, std::string> DoString(const char* str);

        void StackUsing(std::function<void()> func);
        void GetGlobal(const std::string_view& name);
        void RegisterFunctions(const std::string_view& table, const std::vector<luaL_Reg>& funcs);
        void RegisterFunctionsToMetaTable(const std::vector<luaL_Reg>& funcs);

        std::expected<bool, std::string> InvokeRefFunction(int ref);
        std::expected<bool, std::string> InvokeRefFunctionOnThread(std::string thread_key, int ref);




        static void CheckArgType(lua_State* L, int n, int expected)
        {
            if (lua_type(L, n) != expected) {
                luaL_error(L, "expected %d arguments, got %d", expected, n);
            }
        }

    private:
        Script();
        virtual ~Script();
        static void* lua_Alloc(void* ud, void* ptr, size_t osize, size_t nsize);
        void SetupCommands();



        static int Command_Quit(lua_State* L);










        lua_State* L_;
        std::map<std::string, std::unique_ptr<ThreadParam>> threads_;



    };
}







#endif // SCRIPT_H__
