#ifndef VIEW_H__
#define VIEW_H__

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <queue>
#include <vector>
#include "Dialog.h"
#include "BrowserView.h"
#include "ProgressOverlay.h"
#include "ProgressState.h"
#include "../FileError.h"
#include "../FileOperationProgress.h"
#include "../models/Model.h"
#include "../models/PaneState.h"

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
        // カーソルのペインの、カーソル下のエントリ。一覧が空(ファイルもフォルダも1つも無い、または絞り込みで0行)ならnullptr
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
        // listを表示しているペインの、pathsのファイルのマークを外して、再描画する(FileListView::UnmarkPaths参照)。
        void UnmarkPaths(const models::FileListModel& list, const std::vector<std::filesystem::path>& paths);

        // paneのペインをpathのフォルダへ移す(Enterでフォルダに入るのと同じ移動。履歴にも記録される)。移れなければ、
        // 移さずにダイアログで知らせる(ReportFileError。権限の失敗には許可の案内が付く)。移れず、かつそのフォルダが
        // もう無い(消えた・フォルダでなくなった)と確かめられたときは、履歴からも外す(権限が無い、
        // 一時的なI/Oエラーなどで有無が分からないときは外さない)。移れたらtrue。
        // ダイアログの表示中でも動く: 履歴を選ぶダイアログを閉じた直後のティックでは、閉じたはずのダイアログが
        // まだ「開いている」扱い(IsAnyDialogOpened()がtrue)なので、それで弾いてはいけない。
        bool JumpToPath(constants::Pane pane, const std::filesystem::path& path);

        // 前回の終了時のペインの状態(それぞれのいるフォルダとソート、フォーカスしているペイン)を戻す。起動時に一度だけ、
        // Viewを作った後(最初のティックより前)に呼ぶ。保存してあったフォルダに入れなければ(無い・フォルダでなくなった・
        // 権限が無い等)、何も言わずに、ホームのままにする(保存してあった値は、そのペインのフォルダかソートが変わるまで残る)。
        // 戻すのは履歴に記録しない移動(FileListModel::TryRestoreTo)なので、履歴の並びは動かない。
        void RestorePanes();
        // タイマーから定期的に呼ぶ。ペインの状態が、保存した(復元した)ものから変わっていれば、保存する(NSUserDefaults)。
        // 毎ティック呼んでよい(変わっていなければ何もしない)。終了時にまとめて保存する処理は無い(強制終了でも残るように)ので、
        // 変わるたびに、ティックごとにまとめて保存する。変わったかは、値の比較で見る(ReactiveProperty::Value()は
        // 同じ値でも毎回通知するので、パスの変更の購読では判断できない)。
        void SavePanesIfChanged();

        // Quick Lookのプレビューを、areaの範囲の一覧に被せて表示する/閉じる(BrowserView::ToggleQuickLook
        // 参照)。呼んだ後に表示中ならtrue。Navigate::Cancel(Escなど)でも閉じる。
        bool ToggleQuickLook(constants::QuickLookArea area);
        // タイマーから定期的に呼ぶ。プレビューを、カーソル下のファイルに追従させる。
        void UpdateQuickLook();
        // Quick Lookのプレビューの倍率(1.0 = 100%)を読む/指定する(BrowserView::QuickLookZoom参照)。
        // 表示していない・読み込み中・ズームできない種類のときは、nulloptを返して何もしない。
        std::optional<double> QuickLookZoom() const;
        std::optional<double> SetQuickLookZoom(double factor);

        // カーソルのペインで、ファイル名の検索を始める/次・前のマッチへ動く/終える(BrowserViewの同名の
        // メソッドを参照)。ダイアログの表示中は、始めない・動かない(falseを返す)。
        // Navigate::Cancel(Escなど)でも、検索を終える。
        bool BeginSearch();
        bool StepSearch(int dir);
        bool ClearSearch();
        // カーソルのペインで、絞り込みを始める(BrowserViewの同名のメソッドを参照)。ダイアログの表示中は、始めない(false)。
        // kindは、語の一致のしかた(部分一致か、あいまい一致か)。
        bool BeginFilter(MatchKind kind = MatchKind::Substring);
        // paneの絞り込みを解除する/入力欄を使わずに確定済みにする(空なら解除。戻り値は、見えている行数と全行数)。
        // どちらも、ダイアログの表示中でも動く(閉じた直後のティックでは、閉じたはずのダイアログがまだ「開いている」扱いなので、
        // それで弾いてはいけない。jump_toと同じ)。通常時のEsc(Navigate::Cancel)は、絞り込みを解除しない。
        bool ClearFilter(constants::Pane pane);
        FilterStatus SetFilter(constants::Pane pane, const std::string& query, MatchKind kind = MatchKind::Substring);
        // タイマーから定期的に呼ぶ。入力バー(検索・絞り込み)の入力欄と、ペインの状態を整える(BrowserView::UpdateQueryBars参照)。
        void UpdateQueryBars();

        // 進捗パネル(ウィンドウの右上。ファイル操作の進み具合)。タイマーから毎ティック呼ぶ。running は、実行中の操作の写し
        // (FileOperationManager::Running())。0.3 秒たった操作のパネルを出し、進み具合を更新し、終わったものを消す。
        // ダイアログや Quick Look が出ていても、パネルは出し続ける(Quick Look の覆いより上、ダイアログより下)
        void UpdateProgress(ProgressClock::time_point now, const std::vector<FileOperationStatus>& running);
        // 操作が終わった(FileOperationManager のコールバックから)。成功なら、パネルに「完了」を見せてから消す。失敗は、
        // ダイアログで知らせるので、パネルは、その場で消す。画面への反映は、同じティックの UpdateProgress で行う
        void FinishProgress(FileOperationId id, bool succeeded, ProgressClock::time_point now);

    private:
        void OpenNextDialogIfNeeded();
        // 親ディレクトリ/pathへ移動する。読めない場合(権限が無い等)は、移動せずに、ダイアログで知らせる
        // (例外で落とさない)。
        void MoveToParentOrReport(models::FileListModel& list);
        // 移れたらtrue。移れなければ、ダイアログで知らせてfalse
        bool JumpToOrReport(models::FileListModel& list, const std::filesystem::path& path);
        // listを表示しているペイン(どちらでもなければnullptr)
        FileListView* FindFileListView(const models::FileListModel& list);
        // paneの、いまのフォルダとソート(保存・復元する状態)
        models::PaneState GetPaneState(constants::Pane pane);

        std::unique_ptr<BrowserView> browser_;
        // 進捗パネルの、表示の状態と、画面(contentView の、ブラウザの上に足す)
        ProgressState progress_state_;
        std::unique_ptr<ProgressOverlay> progress_overlay_;
        std::shared_ptr<IDialog> current_dialog_;
        std::queue<std::shared_ptr<IDialog>> dialog_requests_;
        // 保存した(または復元した)ペインの状態(添字はconstants::Pane)と、フォーカスしているペイン。これと違えば保存する。
        // 復元の後の「いまの値」で始まるので、入れなかったフォルダは、そのペインが変わるまで、保存してある値が残る
        std::array<models::PaneState, 2> saved_panes_;
        constants::Pane saved_focus_ = constants::Pane::Left;
    };
}


#endif // VIEW_H__
