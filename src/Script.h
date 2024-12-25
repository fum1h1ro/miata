#ifndef SCRIPT_H__
#define SCRIPT_H__

#include <filesystem>
#include <any>
#include <functional>
#include <string>
#include <map>
#include <expected>

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
            //SOKOL_ASSERT(instance_ == nullptr);
            if (instance_ == nullptr) {
                instance_ = new Script();
            }
            return *instance_;
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










        static Script* instance_;
        lua_State* L_;
        std::map<std::string, std::unique_ptr<ThreadParam>> threads_;



    };
}







#endif // SCRIPT_H__
