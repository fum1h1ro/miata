#ifndef VIEWS_QUERY_BAR_H__
#define VIEWS_QUERY_BAR_H__

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace miata::views {
    // ファイル名の検索、または絞り込みをしているペインの下端に出す入力バー(vimのコマンドラインのようなもの)。
    // プロンプト(検索なら「/」)、語の入力欄、右端の件数を並べる。ペインごと・入力の種類ごとに1つ持つ。
    // 実体は、背景のNSViewと、その中のNSTextField。AppKit型はQueryBar.mmに閉じ込める。表示する位置(frame)は
    // 親(BrowserView)が決める(QuickLookViewと同じ作法)。
    //
    // 語の文字入力は、本物のNSTextFieldでしか受けられない(MiataRootViewはkeyCodeしか受け取らず、日本語の
    // 入力(IME)ができないため)。入力欄がfirst responderの間は、MiataRootView::keyDown:が呼ばれず、Normalの
    // キーバインドも効かない。Enter / Esc / ↑↓ / Tabは、入力欄のdelegateが横取りして、コールバックで知らせる
    // (FilterListDialog.mmと同じ作法)。IMEの変換中は、Enter / 矢印はIMEに渡り(delegateは受けない)、
    // 変換中の未確定の文字は、語にしない(SettledText()が返さない)。
    //
    // 入力の始まりと終わり(BeginInput / EndInput)は、呼び出し側(BrowserView)が、各ペインの状態に
    // 合わせて行う。入力欄は、こちらの知らないところでfirst responderを失うことがある(ダイアログが閉じるとき
    // など。BrowserView::SettleQueryInput参照)ので、IsEditing()で確かめられる。
    class QueryBar {
    public:
        struct Callbacks {
            // 入力欄の文字が変わった(打鍵ごと。IMEの変換中にも呼ばれうる)。文字は、SettledText()で読む
            std::function<void()> on_query_changed;
            // ↓(+1) / ↑(-1) / Ctrl-N / Ctrl-P: 検索なら次・前のマッチへ、絞り込みなら一覧のカーソルを1行
            std::function<void(int)> on_step;
            // Enter / Esc。first responderを手放す処理(確定・取り消し)を、delegateの呼び出しの中で行わないよう、
            // 次のランループで呼ぶ。呼ばれたときには、状態が変わっていることがある
            std::function<void()> on_commit;
            std::function<void()> on_cancel;
        };

        // バーに出す内容。語と件数の文言は、呼び出し側(BrowserView)が、ペインの状態から作る
        struct Display {
            // 入力中、または確定済み。falseのときは、プロンプトも件数も出さない(バー自体も、親が隠す)
            bool active = false;
            std::string query; // 語
            std::string count; // 右端の件数の文言。出さないなら空
            // 語の入力欄の左に出す文字。空なら、変えない(作るときのプロンプトのまま)。絞り込みは、一致のしかたで変わる
            std::string prompt;
        };

        // promptは、語の入力欄の左に出す文字(検索なら「/」)
        explicit QueryBar(const std::string& prompt);
        ~QueryBar();

        // 親にaddSubviewするNSView*を(__bridge void*)で返す。高さは、親が決める(Height()参照)
        void* NativeView() const;
        // 表示するときの高さ。ヘッダー(パス表示)と同じ
        static double Height();

        void SetCallbacks(Callbacks callbacks);

        // 表示内容を更新する(同じ内容なら何もしない)。入力中(IsEditing)は、入力欄の文字には触らない
        // (打っている途中の文字、IMEの変換中の文字を壊さないため)。
        void Update(const Display& display);

        // 入力欄を空にして、編集できるようにし、first responderにする。できなければfalse。
        bool BeginInput();
        // 入力欄を編集できなくし(確定後の表示用)、first responderを手放して、MiataRootViewに戻す。
        // 入力欄がfirst responderでなければ、他の誰かが持っているfirst responderを奪わない。何度呼んでもよい。
        void EndInput();
        // 入力欄がfirst responderか(=いま文字を打てるか)
        bool IsEditing() const;
        // 入力欄の今の文字。入力中で、IMEの変換中(未確定の文字がある)でないときだけ返す
        std::optional<std::string> SettledText() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

#endif // VIEWS_QUERY_BAR_H__
