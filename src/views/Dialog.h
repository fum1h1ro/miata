#ifndef DIALOG_H__
#define DIALOG_H__

#include <any>
#include <functional>
#include <imgui.h>
#include "../misc.h"
#include "../const.h"
#include "Constants.h"


namespace miata::views {
    class IDialog {
    public:
        IDialog(const char* id, std::function<void(IDialog&)> on_close);
        virtual ~IDialog() = default;
        void Open();
        void OnGui(int window_width, int window_height);
        void DrawFocus();
        virtual void OnGuiImpl() = 0;
        virtual void Navigate(constants::Navigate dir) {}
        virtual ImVec2 GetIdealSize() const { return ImVec2(0, 0); }
        inline const std::string& Id()
        {
            return id_;
        }
        inline bool IsOpened() const
        {
            return is_opened_;
        }
    private:
        std::string id_;
        std::function<void(IDialog&)> on_close_;
        bool is_opened_;
    };


    class YesNoDialog : public IDialog {
    public:
        struct arguments {
            std::string message_ = "";
            bool reverse_ = false;
        };
        YesNoDialog(std::function<void(IDialog&)> on_close, arguments args);
        void OnGuiImpl() override;
        ImVec2 GetIdealSize() const override
        {
            return ImVec2(400, 0);
        }
        bool Result() const
        {
            return result_;
        }
    private:
        arguments args_;
        bool result_;
    };

    class ConfirmDialog : public IDialog {
    public:
        struct arguments {
            std::string message_;
            std::vector<std::string> button_texts_;
        };
        ConfirmDialog(const char* id, std::function<void(IDialog&)> on_close);
        void OnGuiImpl() override;
        int Result() const
        {
            return result_;
        }
    private:
        std::string message_;
        std::vector<std::string> button_texts_;
        int result_;
    };








}


#endif // DIALOG_H__
