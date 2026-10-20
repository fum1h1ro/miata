#ifndef STDIO_CAPTURE_H__
#define STDIO_CAPTURE_H__

#include "models/ConsoleLog.h"

namespace miata {
    // C の stdout / stderr を、書かれたものをコンソールの受け口(ConsoleLog)にも流す FILE に差し替える(funopen)。
    // printf・std::print・Lua の print / io.write / io.stderr:write など、標準出力・標準エラーに書くものを全部、同じ口で拾う。
    // stdout に書かれたものは Normal の行、stderr に書かれたもの(LogError もここ)は Error の行(赤)になる。
    // 元の FILE にも書き戻す(ターミナルから起動したとき=rake run の表示は、これまでどおり)。
    //
    // 守ること:
    //  - **Lua を開く(Script の構築)より前に呼ぶ**: Lua の io.stdout / io.stderr は、開いた時点の FILE* を握り続けるので、
    //    後だと io.write が拾えない(print は、呼ぶたびに stdout を見るので、後でも拾える)。Application::InitializeImpl の先頭で呼ぶ
    //  - FILE の書き込みコールバックは、書いたスレッドで、FILE のロックを持ったまま呼ばれる。コールバックの中で、差し替えた
    //    stdout / stderr を使う関数(printf など)を呼ばない(同じ FILE のロックで再入する)。触ってよいのは、受け口と、元の FILE だけ
    //  - 差し替えた FILE と、その cookie は、解放しない(プロセスの終わりまで)。exit() が flush するのは、静的破棄の後でもありうる
    //  - 拾えないもの: 子プロセスの出力(fd を継承したもの)、NSLog、write(1, ...) の直叩き
    //  - 行バッファ(stdout も stderr も)なので、改行の無い出力は、改行か fflush まで出ない(届いた分は、受け口が、
    //    未完の行として持ち越す)。stderr は、通常は無バッファだが、差し替えた FILE では行バッファにしている(StdioCapture.cc)
    class StdioCapture {
    public:
        // 差し替える。2 回目以降は何もしない(true)。funopen に失敗したら、何も変えずに false(コンソールは空のまま。
        // 端末への出力は、これまでどおり)
        static bool Install(models::ConsoleLog& log);
        static bool IsInstalled();
    };
}

#endif // STDIO_CAPTURE_H__
