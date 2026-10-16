#ifndef SCRIPT_H__
#define SCRIPT_H__

#include <filesystem>
#include <any>
#include <functional>
#include <optional>
#include <string>
#include <map>
#include <expected>
#include <print>
#include <vector>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}



namespace miata {
    class Script {
        struct ThreadParam {
            lua_State* L_;
            int ref_; // レジストリでの固定(GC に回収されないように)。プロセスの終わりまで解放しない
            int status_;
            int nresult_;

            ThreadParam(lua_State* L, int ref) : L_(L), ref_(ref), status_(LUA_OK), nresult_(0) {}
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


        // 設定の読み込みで起きたエラー
        struct ConfigError {
            std::string file;    // 読み込もうとしたファイル(組み込みの既定の設定は "test.lua"、ユーザーの設定はそのパス)
            std::string message; // Luaのエラーメッセージ(ふつう "ファイル名:行番号: ..." の形)

            // 1 つの文面("ファイル名: メッセージ")。画面のダイアログと、コンソールのログで、同じ文面にするため。Luaのメッセージは、
            // ふつうファイル名を含む("ファイル名:行番号: ...")ので、含まなければ(error({}) など)、どのファイルか分かるように足す
            std::string Describe() const
            {
                return (message.find(file) == std::string::npos ? file + ": " : std::string()) + message;
            }
        };

        // キー関数(コルーチン)の失敗。ログに出す文面を作るのは、呼び出し側(Application)の仕事: Script はログを知らない
        struct ThreadError {
            std::string label;   // どのキーの関数か(キー列の表示名。KeyBinding::StrokeName の "dd" "<C-d>" など)
            std::string message; // Luaのエラーメッセージ
        };

        // base.lua を読んで、コマンドの準備をする。失敗したら、その文面を返す(準備は、そこで止まる)
        std::optional<std::string> Initialize();
        // 設定を読み込む。組み込みの既定の設定(resources/test.lua。.appのContents/Resourcesに埋め込まれる)の
        // 後に、ユーザーの設定(UserConfigFile())があれば読み込み、同じ設定は後に読むユーザーの設定が上書きする。
        // 失敗した設定は返す(エラーが起きた行より前の設定は有効)。ユーザーの設定が無いのはエラーではない。
        // この時点ではまだViewが無いので、エラーを画面に出すのは呼び出し側(Application)に任せる。
        std::vector<ConfigError> PostInitialize();
        // ユーザーの設定ファイル。$XDG_CONFIG_HOME/miata/init.lua(未設定なら ~/.config/miata/init.lua)
        static std::filesystem::path UserConfigFile();
        // ダイアログなどで待っている(yield した)キー関数を、1 つ先へ進める。毎ティック呼ぶ。進めたものが失敗したら、その失敗を返す
        // (失敗したキー関数は、後始末されて、同じキーで、また使える。InvokeRefFunctionOnThread と同じ)
        std::vector<ThreadError> Update();
        std::expected<bool, std::string> DoFile(std::filesystem::path path);
        std::expected<bool, std::string> DoResourceFile(std::filesystem::path path);
        std::expected<bool, std::string> DoString(const char* str);

        void StackUsing(std::function<void()> func);
        void GetGlobal(const std::string_view& name);
        // wrapperを渡すと、各関数をそのまま登録する代わりに、wrapper(C関数)をクロージャとして登録し、
        // 本来の関数をそのupvalue(1)に持たせる。wrapperの中から lua_tocfunction(L, lua_upvalueindex(1)) で
        // 呼び出す(呼び出しの前に共通の確認を挟むための仕組み)。
        void RegisterFunctions(const std::string_view& table, const std::vector<luaL_Reg>& funcs, lua_CFunction wrapper = nullptr);
        void RegisterFunctionsToMetaTable(const std::vector<luaL_Reg>& funcs);

        std::expected<bool, std::string> InvokeRefFunction(int ref);
        // ref の関数を、name というコルーチンで始める(キー関数。name は、キー列の表示名: 同じキーは、同じコルーチンを使い回す)。
        // 待ち(dialog_* の yield)に入ったら、成功として nullopt を返す(続きは Update が進める)。失敗したら、その失敗を返す。
        // 失敗したコルーチンは、その場で後始末する(Settle)ので、同じキーを押し直すと、関数が、ちゃんと実行される
        std::optional<ThreadError> InvokeRefFunctionOnThread(const std::string& name, int ref);

        // Lua の警告(warn() と、VM が出す __gc のエラーの警告)の受け取り先。断片(warn("a", "b"))は、1 件にまとめて渡す。
        // Lua は warn_ 関数が無いと警告を捨てる(lua_newstate を直接呼ぶので、標準の関数が入らない)。
        // コアの中(GC の最中を含む)から呼ばれるので、handler の中で Lua の API を呼ばないこと。例外は握りつぶす
        void SetWarningHandler(std::function<void(const std::string&)> handler);

        // func を、メインの state で、保護して(lua_pcall)呼ぶ。func は、lightuserdata(ud)を引数に取り、戻り値を 1 つ返す
        // C 関数。Luaのエラー(funcの中で呼んだ Lua の関数のエラー・メモリ不足を含む)は、戻り値のエラーの文面になる。
        // 成功したときだけ、funcの戻り値(スタックの先頭)を read(L) に渡す(読んだ後に、スタックは元に戻る)。
        // max_instructions が 1 以上なら、その数の VM 命令を超えたところでエラーにする(暴走する関数で、UI が固まらない
        // ようにするため。os.execute のように、C の側で待つものは止められない)。コルーチンの外なので、yield はエラーになる。
        // 引数 func の中は、C++ のデストラクタを持つオブジェクトを作らないこと(Lua のエラーは longjmp)
        std::expected<void, std::string> CallProtected(
            lua_CFunction func,
            void* ud,
            int max_instructions,
            const std::function<void(lua_State*)>& read
        );




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
        // lua_setwarnf に渡す関数(lua_WarnFunction)。ud は Script*
        static void WarnFunction(void* ud, const char* message, int to_continue);
        void OnWarning(const char* message, bool to_continue);
        // lua_resume が status を返した直後に呼ぶ(最初の区間も、Update が進めた後続の区間も、同じ)。
        //  - 成功か待ち(LUA_OK / LUA_YIELD): 積まれた戻り値を捨てて(積んだままにすると、同じコルーチンを使い回すたびに積み上がる)、
        //    nullopt を返す
        //  - 失敗: スタックが生きているうちに、エラーを読み、コルーチンを閉じて(lua_closethread。__close の変数があれば走る)、
        //    再利用できる状態(LUA_OK・空のスタック)に戻して、失敗を返す。閉じないと、コルーチンは「エラーで死んだ」まま残り、
        //    次に同じキーを押したとき、lua_closethread が古いエラーを返して関数が実行されず、(戻り値を捨てた後だと)
        //    意味のないエラーが出る
        std::optional<ThreadError> Settle(const std::string& name, ThreadParam& param, int status);
        void SetupCommands();
        // chunk_nameは、エラーメッセージの "ファイル名:行番号:" に出る名前("@"で始まるとファイル名として扱われる)
        std::expected<bool, std::string> DoBuffer(const std::string& code, const std::string& chunk_name);



        static int Command_Quit(lua_State* L);










        lua_State* L_;
        std::map<std::string, std::unique_ptr<ThreadParam>> threads_;
        // 警告の受け取り(SetWarningHandler)と、warn() の状態。lauxlib の標準の warn 関数と同じ規則: 続きでない "@on" / "@off" は
        // 制御メッセージ(出力を入り切りする。既定は出す)、断片(to_continue)は、最後の断片まで貯めて 1 件にする
        std::function<void(const std::string&)> warning_handler_;
        std::string warning_text_;
        bool warning_enabled_ = true;
        bool warning_continuing_ = false;



    };
}







#endif // SCRIPT_H__
