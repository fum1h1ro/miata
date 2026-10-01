#ifndef VIEWS_SEARCH_BAR_H__
#define VIEWS_SEARCH_BAR_H__

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include "SearchState.h"

namespace miata::views {
    // 検索しているペインの下端に出す検索バー(vimのコマンドラインのようなもの)。「/」、検索語の入力欄、右端の件数を
    // 並べる。ペインごとに1つ持つ。実体は、背景のNSViewと、その中のNSTextField。AppKit型はSearchBar.mmに
    // 閉じ込める。表示する位置(frame)は親(BrowserView)が決める(QuickLookViewと同じ作法)。
    //
    // 検索語の文字入力は、本物のNSTextFieldでしか受けられない(MiataRootViewはkeyCodeしか受け取らず、日本語の
    // 入力(IME)ができないため)。入力欄がfirst responderの間は、MiataRootView::keyDown:が呼ばれず、Normalの
    // キーバインドも効かない。Enter / Esc / ↑↓ / Tabは、入力欄のdelegateが横取りして、コールバックで知らせる
    // (FilterListDialog.mmと同じ作法)。IMEの変換中は、Enter / 矢印はIMEに渡り(delegateは受けない)、
    // 変換中の未確定の文字は、検索語にしない(SettledText()が返さない)。
    //
    // 入力の始まりと終わり(BeginInput / EndInput)は、呼び出し側(BrowserView)が、各ペインの検索の状態に
    // 合わせて行う。入力欄は、こちらの知らないところでfirst responderを失うことがある(ダイアログが閉じるとき
    // など。BrowserView::CommitSearchInput参照)ので、IsEditing()で確かめられる。
    class SearchBar {
    public:
        struct Callbacks {
            // 入力欄の文字が変わった(打鍵ごと。IMEの変換中にも呼ばれうる)。文字は、SettledText()で読む
            std::function<void()> on_query_changed;
            // ↓(+1) / ↑(-1) / Ctrl-N / Ctrl-P: 次・前のマッチへ
            std::function<void(int)> on_step;
            // Enter / Esc。first responderを手放す処理(確定・取り消し)を、delegateの呼び出しの中で行わないよう、
            // 次のランループで呼ぶ。呼ばれたときには、状態が変わっていることがある
            std::function<void()> on_commit;
            std::function<void()> on_cancel;
        };

        SearchBar();
        ~SearchBar();

        // 親にaddSubviewするNSView*を(__bridge void*)で返す。高さは、親が決める(Height()参照)
        void* NativeView() const;
        // 表示するときの高さ。ヘッダー(パス表示)と同じ
        static double Height();

        void SetCallbacks(Callbacks callbacks);

        // 表示内容を更新する(同じ内容なら何もしない)。statusは、このバーのペインの検索の状態。
        // 入力中(IsEditing)は、入力欄の文字には触らない(打っている途中の文字、IMEの変換中の文字を壊さないため)。
        // 検索していない(mode == Idle)ときは、「/」も件数も出さない(バー自体も、親が隠す)。
        void Update(const SearchStatus& status);

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

#endif // VIEWS_SEARCH_BAR_H__
