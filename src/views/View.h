#ifndef VIEW_H__
#define VIEW_H__

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include "Dialog.h"
#include "FileListView.h"
#include "BrowserView.h"
#include "../misc.h"
#include "../models/Model.h"
#include "../widgets/Widget.h"

namespace miata::views {
    class View {
        struct dialog_request {
            std::string id_;
            std::function<void(views::IDialog&)> on_close_;
            std::any args_;
        };
    public:
        enum class Mode {
            Browser,
            Viewer,
        };
        enum class Flags : uint8_t {
            None = 0,
            AnyDialogOpened = 1<<0,
        };

        View(int w, int h);
        virtual ~View();
        void OnGui(int width, int height);
        void Navigate(constants::Navigate dir);

        void RequestDialog(std::shared_ptr<IDialog> dialog);
        bool IsAnyDialogOpened() const
        {
            return current_dialog_ != nullptr;
        }
        void NavigateForBrowser(constants::Navigate dir);
        void ToggleFocus();
        void Mark();
        void Unmark();
        void ToggleMark();
        models::FileListModel& CurrentList();
        models::FileListModel& OtherList();
        models::FileEntryModel& CurrentEntry();
        FileListView& CurrentFileListView();

        //void KeyDown(int key, constants::osx_modifier_flags flags);


    private:
        void OnGuiDialogs(int width, int height);
        std::shared_ptr<views::IDialog>& CurrentDialog();






        //void KeyProcessForBrowser(int key, constants::osx_modifier_flags flags);
        //
        Mode mode_;
        misc::Flags<Flags> flags_;
        bool resized_;
        int window_width_;
        int window_height_;

        std::shared_ptr<IDialog> current_dialog_;
        std::queue<std::shared_ptr<IDialog>> dialog_requests_;



        struct {
            //std::unique_ptr<widgets::HorizontalLayouter> test_;
            std::unique_ptr<widgets::VerticalLayouter> test_;
            std::unique_ptr<widgets::VerticalLayouter> main_;
            std::shared_ptr<widgets::Pane> top_;
            std::shared_ptr<widgets::Pane> bottom_;
            std::shared_ptr<BrowserView> browser_;
            std::unique_ptr<widgets::Pane> log_;
            std::unique_ptr<widgets::Pane> viewer_;
        } panes_;
        struct {
            ImVec2 window_;
            ImVec2 top_;
            ImVec2 bottom_;
        } pane_sizes_;
    };
}


#endif // VIEW_H__
