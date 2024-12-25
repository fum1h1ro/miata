#ifndef WIDGET_HPP__
#define WIDGET_HPP__

#include <any>
#include <functional>
#include <imgui.h>
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





}

#endif // WIDGET_HPP__
