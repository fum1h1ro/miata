#ifndef VIEW_H__
#define VIEW_H__

#include <filesystem>
#include <memory>
#include <optional>
#include <queue>
#include "Dialog.h"
#include "BrowserView.h"
#include "../FileError.h"
#include "../models/Model.h"

namespace miata::views {
    class View {
    public:
        View();
        ~View();

        void Navigate(constants::Navigate dir);
        // ダイアログの表示中に、項目のショートカット(ラベルの `&x`。DialogPanel::AddButton参照)のキーが押されたとき。
        // keyをショートカットにした項目があれば、選んでtrue(Enterを押したのと同じ)。無ければ、何もせずfalse。
        // キーバインドの無かったキーだけを渡すこと(Application::KeyDown。キーバインドが優先される)。
        bool ActivateDialogMnemonic(char key);

        void RequestDialog(std::shared_ptr<IDialog> dialog);
        // ファイル操作の失敗を、ダイアログで知らせる。whatは何に失敗したか("リネームできませんでした"など)で、
        // 画面には「what: OSの説明」と出る。権限が無い失敗(OSの保護など)のときは、許可のしかた(システム設定の
        // 「フルディスクアクセス」)も案内し、システム設定を開くボタンを付ける。
        void ReportFileError(const std::string& what, const FileError& error);
        bool IsAnyDialogOpened() const
        {
            return current_dialog_ != nullptr;
        }
        // タイマーから定期的に呼ぶ。閉じたダイアログを検出してキューの次を開く。
        void CheckDialogState();
        // タイマーから定期的に呼ぶ(CheckDialogStateの後)。ディレクトリ監視で古くなった一覧を
        // 自動リロードする。ダイアログ表示中は保留する(FileListView::UpdateAutoReload参照)。
        void UpdateAutoReload();

        void NavigateForBrowser(constants::Navigate dir);
        void ToggleFocus();
        void Mark();
        void Unmark();
        void ToggleMark();
        models::FileListModel& CurrentList();
        models::FileListModel& OtherList();
        // カーソルのペインの、カーソル下のエントリ。一覧が空(ファイルもフォルダも1つも無い)ならnullptr
        models::FileEntryModel* CurrentEntry();
        FileListView& CurrentFileListView();
        // カーソル(フォーカス)のあるペインと、ペインを指定して取得するアクセサ
        constants::Pane CurrentPane() const;
        FileListView& GetFileListView(constants::Pane pane);
        models::FileListModel& GetList(constants::Pane pane);
        // listを表示しているペインを再スキャンして最新にする。カーソルとマークは維持される
        // (FileListView::Reload参照。cursor_toの意味も同じ)。ファイル操作の後始末用で、
        // 失敗(ディレクトリが読めない等)しても一覧が変わらないだけなので呼び出し側には返さない。
        void ReloadList(const models::FileListModel& list, std::optional<std::filesystem::path> cursor_to = std::nullopt);
        // listを表示しているペインのマークをすべて解除して、再描画する(FileListView::ClearMarks参照)。
        void ClearListMarks(const models::FileListModel& list);

        // paneのペインをpathのフォルダへ移す(Enterでフォルダに入るのと同じ移動。履歴にも記録される)。移れなければ、
        // 移さずにダイアログで知らせる(ReportFileError。権限の失敗には許可の案内が付く)。移れず、かつそのフォルダが
        // もう無い(消えた・フォルダでなくなった)と確かめられたときは、履歴からも外す(権限が無い、
        // 一時的なI/Oエラーなどで有無が分からないときは外さない)。移れたらtrue。
        // ダイアログの表示中でも動く: 履歴を選ぶダイアログを閉じた直後のティックでは、閉じたはずのダイアログが
        // まだ「開いている」扱い(IsAnyDialogOpened()がtrue)なので、それで弾いてはいけない。
        bool JumpToPath(constants::Pane pane, const std::filesystem::path& path);

        // Quick Lookのプレビューを、areaの範囲の一覧に被せて表示する/閉じる(BrowserView::ToggleQuickLook
        // 参照)。呼んだ後に表示中ならtrue。Navigate::Cancel(Escなど)でも閉じる。
        bool ToggleQuickLook(constants::QuickLookArea area);
        // タイマーから定期的に呼ぶ。プレビューを、カーソル下のファイルに追従させる。
        void UpdateQuickLook();

        // カーソルのペインで、ファイル名の検索を始める/次・前のマッチへ動く/終える(BrowserViewの同名の
        // メソッドを参照)。ダイアログの表示中は、始めない・動かない(falseを返す)。
        // Navigate::Cancel(Escなど)でも、検索を終える。
        bool BeginSearch();
        bool StepSearch(int dir);
        bool ClearSearch();
        // タイマーから定期的に呼ぶ。検索バーの入力欄と、ペインの検索の状態を整える(BrowserView::UpdateSearchBar参照)。
        void UpdateSearchBar();

    private:
        void OpenNextDialogIfNeeded();
        // 親ディレクトリ/pathへ移動する。読めない場合(権限が無い等)は、移動せずに、ダイアログで知らせる
        // (例外で落とさない)。
        void MoveToParentOrReport(models::FileListModel& list);
        // 移れたらtrue。移れなければ、ダイアログで知らせてfalse
        bool JumpToOrReport(models::FileListModel& list, const std::filesystem::path& path);
        // listを表示しているペイン(どちらでもなければnullptr)
        FileListView* FindFileListView(const models::FileListModel& list);

        std::unique_ptr<BrowserView> browser_;
        std::shared_ptr<IDialog> current_dialog_;
        std::queue<std::shared_ptr<IDialog>> dialog_requests_;
    };
}


#endif // VIEW_H__
