#ifndef VIEWS_QUICK_LOOK_VIEW_H__
#define VIEWS_QUICK_LOOK_VIEW_H__

#include <filesystem>
#include <memory>
#include <optional>

namespace miata::views {
    // Quick Look(QLPreviewView)のプレビューを、ファイル一覧の上に被せて表示するビュー。
    // 実体は、マウスを受け止める覆いのNSViewと、その中のQLPreviewView。AppKit型はQuickLookView.mmに
    // 閉じ込める。表示する位置(frame)は親(BrowserView)が決める。
    //
    // 覆いは、クリックがQLPreviewViewに届かないようにするためのもの。QLPreviewViewはfirst responderに
    // なれ、クリックで奪われるとキー入力の窓口(MiataRootView)から外れてしまう。その代わり、プレビューの中の
    // マウス操作(PDFのスクロール、動画の再生ボタン等)はできない。ただしトラックパッドのピンチだけは、
    // 覆いが受けて、倍率を指定できる中のビュー(下のZoom参照)へ渡す(ピンチはfirst responderを動かさない)。
    class QuickLookView {
    public:
        // 倍率(1.0 = 100%)の範囲。SetZoom()は、この範囲に丸める
        static constexpr double kMinZoom = 0.25;
        static constexpr double kMaxZoom = 4.0;

        QuickLookView();
        ~QuickLookView();

        // 親にaddSubviewするNSView*(覆い)を(__bridge void*)で返す。表示していない間は隠れている
        void* NativeView() const;

        // 表示を始める(QLPreviewViewを作る)。表示中なら何もしない。中身はSetFile()で設定する
        void Show();
        // 表示をやめる(QLPreviewViewをcloseして手放す)。表示していなければ何もしない
        void Hide();
        bool IsShown() const;

        // プレビューするファイルを切り替える。nulloptなら空にする。表示していなければ何もしない
        void SetFile(const std::optional<std::filesystem::path>& path);

        // プレビューの倍率(1.0 = 100%)。ズームできる種類のプレビューが読み込まれて表示されているときだけ値がある
        // (表示していない・読み込み中・ズームできない種類のときはnullopt)。QLPreviewViewには、倍率を指定する公開APIが
        // 無い(非公開のzoomFactor等は、試した画像・PDF・txt・xlsxのどれでも効かない)ので、中身のビューを直接触る。中身の作りは種類で違う:
        //   ・WebKitで描かれる種類(xlsx・docx・csv・html・svgなど): 中のWKWebView(magnification)
        //   ・テキスト系(txt・md・rtfなど): 中のNSScrollView(テキストを載せたもの。magnification)
        //   ・画像・PDF・jsonなど: 別プロセスで描かれ(NSRemoteView)、触る手段が無い(ズームできない)
        // ファイルを切り替えると、倍率は100%に戻る。
        std::optional<double> Zoom() const;
        // 倍率を指定する(factorは有限の正の数)。[kMinZoom, kMaxZoom]に丸めて、実際になった倍率を返す。
        // ズームできなければ、何もせずnullopt。
        std::optional<double> SetZoom(double factor);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

#endif // VIEWS_QUICK_LOOK_VIEW_H__
