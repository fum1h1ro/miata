#ifndef DIALOG_H__
#define DIALOG_H__

#include <any>
#include <functional>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include "../misc.h"
#include "../const.h"
#include "Constants.h"
#include "../widgets/Widget.h"

namespace miata::views {
    class IDialog {
    public:
        IDialog(const char* id, std::function<void(IDialog&)> on_close);
        virtual ~IDialog() = default;
        void Open();
        void OnGui(int window_width, int window_height);
        virtual void OnOpen() {}
        virtual void OnGuiImpl() = 0;
        virtual void OnClose() {}
        virtual void Navigate(constants::Navigate dir);
        virtual ImVec2 GetIdealSize() const { return ImVec2(0, 0); }
        inline const std::string& Id()
        {
            return id_;
        }
        inline bool IsOpened() const
        {
            return is_opened_;
        }
    protected:
        widgets::ItemFocus focus_;
    private:
        std::string id_;
        std::function<void(IDialog&)> on_close_;
        bool is_opened_;
    };

    class ConfirmDialog : public IDialog {
    public:
        struct arguments {
            std::string message_;
            std::string button_text_;
        };
        ConfirmDialog(std::function<void(IDialog&)> on_close, const arguments& args);
        void OnGuiImpl() override;
    private:
        std::string message_;
        std::string button_text_;
    };

    class YesNoDialog : public IDialog {
    public:
        struct arguments {
            std::string message_ = "";
            bool default_select_ = false;
            std::string yes_text_ = "YES";
            std::string no_text_ = "NO";
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

    class InputTextDialog : public IDialog {
    public:
        struct arguments {
            std::string message_;
            std::string initial_text_;
        };
        InputTextDialog(std::function<void(IDialog&)> on_close, const arguments& args);
        void OnGuiImpl() override;
        const std::string& Result() const
        {
            return text_;
        }
    private:
        std::string message_;
        std::string text_;
    };

    class CustomDialog : public IDialog {
        struct Item {
            std::any value_;
            std::any result_;
        };

    public:
        struct arguments {
            std::string message_;
            std::string button_text_;
            std::function<void()> on_button;
        };
        CustomDialog(std::function<void(IDialog&)> on_close, const arguments& args);
        void OnGuiImpl() override;


        void SetTitle(const std::string& title);
        void AddCheckbox(const std::string& label, bool initial_value = false);
        void AddSelectables(const std::vector<std::string>& items, int initial_value = -1);

    private:
        std::string title_;
        std::vector<Item> items_;
    };




}


#endif // DIALOG_H__
