#ifndef WIDGET_HPP__
#define WIDGET_HPP__


#include <any>
#include <functional>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>
#include "../misc.h"

namespace miata::widgets {
    class Pane {
    public:
        enum class flags : uint8_t {
            none = 0,
            force_resize = 1<<0,
            horizontal = 1<<1,
        };

        Pane(const char* name, int w, int h);
        virtual ~Pane() = default;
        void ForceResize()
        {
            flags_.on(flags::force_resize);
        }
        bool OnGui(int parent_width, int parent_height, std::function<void()> on_gui = nullptr);
        virtual void OnGuiImpl(bool parent_resized);
        virtual void AddChild(std::shared_ptr<Pane> child);
        virtual void RemoveChild(std::shared_ptr<Pane> child);

        const std::string& Name() const
        {
            return name_;
        }
        misc::Flags<flags>& Flags()
        {
            return flags_;
        }
        misc::Flags<ImGuiWindowFlags>& WindowFlags()
        {
            return window_flags_;
        }
        misc::Flags<ImGuiChildFlags>& ChildFlags()
        {
            return child_flags_;
        }
        ImVec2 Size() const
        {
            return size_;
        }
        int Width() const
        {
            return (int)size_.x;
        }
        void Width(int w)
        {
            size_.x = (float)w;
        }
        int Height() const
        {
            return (int)size_.y;
        }
        void Height(int h)
        {
            size_.y = (float)h;
        }
        int ParentWidth() const
        {
            return (int)parent_size_.x;
        }
        int ParentHeight() const
        {
            return (int)parent_size_.y;
        }
    protected:
        std::vector<std::shared_ptr<Pane>>& Children()
        {
            return children_;
        }
        struct {
            ImVec2 window_padding_;
            float child_border_size_;
        } style_;
    private:
        std::string name_;
        ImVec2 parent_size_;
        ImVec2 size_;
        misc::Flags<enum flags> flags_;
        misc::Flags<ImGuiWindowFlags> window_flags_;
        misc::Flags<ImGuiChildFlags> child_flags_;
        std::vector<std::shared_ptr<Pane>> children_;
    };

    class Layouter : public Pane {
    public:
        Layouter(const char* name, int w, int h);
        virtual ~Layouter();
        //void OnGuiImpl(bool parent_resized) override;
        void AddChild(std::shared_ptr<Pane> child) override;
        void RemoveChild(std::shared_ptr<Pane> child) override;
        inline int SplitterSize() const
        {
            return splitter_size_;
        }
        inline void SplitterSize(int size)
        {
            splitter_size_ = size;
        }
    protected:
        struct Size {
            std::shared_ptr<Pane> pane_;
            std::string name_;
            std::string splitter_name_;
            std::string button_name_;
            float ratio_;
        };
        std::vector<Size> sizes_;
        int splitter_size_;
    };

    class HorizontalLayouter : public Layouter {
    public:
        HorizontalLayouter(const char* name, int w, int h) : Layouter(name, w, h)
        {
        }
        void OnGuiImpl(bool parent_resized) override;
    };

    class VerticalLayouter : public Layouter {
    public:
        VerticalLayouter(const char* name, int w, int h) : Layouter(name, w, h)
        {
        }
        void OnGuiImpl(bool parent_resized) override;
    };

    class ItemFocus {
        enum class Flags : uint8_t {
            Ok = 1<<0,
            Cancel = 1<<1,
        };
        enum class Dir : uint8_t {
            None = 0,
            Up,
            Down,
            Left,
            Right,
        };
    public:
        ItemFocus(int32_t index);
        void Begin();
        void End();
        bool Focus();
        void NextItemIsDefault();
        void NavigateUp();
        void NavigateDown();
        void NavigateLeft();
        void NavigateRight();
        void NavigateOk();
        void NavigateCancel();
        bool Button(const char* label, const ImVec2& size = ImVec2(0, 0));
        bool Checkbox(const std::string& label, bool value);
        bool Selectable(const std::string& label, bool selected);

        bool IsOk()
        {
            auto r = flags_.is(Flags::Ok);
            flags_.off(Flags::Ok);
            return r;
        }
        bool IsCancel()
        {
            auto r = flags_.is(Flags::Cancel);
            flags_.off(Flags::Cancel);
            return r;
        }
    private:
        std::vector<ImRect> items_;
        int32_t index_;
        Dir dir_;
        misc::Flags<Flags> flags_;
    };




}

#endif // WIDGET_HPP__
