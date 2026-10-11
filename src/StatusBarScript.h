#ifndef STATUS_BAR_SCRIPT_H__
#define STATUS_BAR_SCRIPT_H__

#include <expected>
#include <string>
extern "C" {
#include <lua.h>
}
#include "platform.h"
#include "views/StatusBarState.h"

namespace miata {
    // ステータスバーの Lua の関数(Miata.config.set_statusbar)との受け渡し。Lua の C API だけを使う(AppKit に依存しない)。
    // ctx の組み立てと、戻り値の読み取りを、ここに集める(書式の実装は、C++ の DefaultStatusText が唯一。Lua には、材料と、
    // その既定の文字列を渡して、置き換えを受け取るだけ)。

    // fn_ref(luaL_ref の参照)の関数を、fn(ctx) で呼ぶ。ctx は表(README の「ステータスバーの内容」)。戻り値は
    // nil(既定のまま)/ 文字列(左だけ置き換える)/ {left=, right=}(書いた側だけ置き換える。値は文字列か数値)。
    // メインの state で、保護して(Script::CallProtected)、命令数の上限(kStatusBarInstructionLimit)つきで呼ぶ。
    // 失敗(実行時のエラー・戻り値の型違い・未知のキー・命令数の上限・yield)は、エラーの文面で返す
    std::expected<views::StatusOverride, std::string> CallStatusBarFunction(
        int fn_ref,
        const views::StatusSnapshot& snapshot,
        const views::StatusText& defaults
    );

    // 関数 1 回の、命令数の上限(VM の命令。ふつうの関数は、数百〜数千で済む)
    inline constexpr int kStatusBarInstructionLimit = 5'000'000;

    // info の属性を、いまスタックの先頭にある表に足す: perm / mode / owner / group / uid / gid / mtime / mtime_text / dataless、
    // 通常のファイルなら size / size_text、シンボリックリンクなら link。ctx.entry と、Miata.command.file_info が共有する
    void SetFileInfoFields(lua_State* L, const pl_file_info& info);

    // Miata.command.file_info の戻り値(表)を積む: is_dir / is_link(リンクそのものの種類)と、SetFileInfoFields の項目
    void PushFileInfoTable(lua_State* L, const pl_file_info& info);
}

#endif // STATUS_BAR_SCRIPT_H__
