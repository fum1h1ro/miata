#ifndef MISC_HPP_
#define MISC_HPP_

#include <iterator>
#include <rxcpp/rx.hpp>
#include <type_traits>
#include <imgui.h>
#include <typeinfo>

namespace misc {
    template<typename T>
    class Flags {
        using UT = std::underlying_type_t<T>;
    public:
        inline Flags() : flags_(T())
        {
        }
        inline void clear()
        {
            flags_ = T();
        }
        inline bool is(T flag) const
        {
            return ((UT)flags_ & (UT)flag) != 0;
        }
        inline void on(T flag)
        {
            auto mask = (UT)flag;
            flags_ = (T)((UT)flags_ | mask);
        }
        inline void off(T flag)
        {
            auto mask = (UT)flag;
            flags_ = (T)((UT)flags_ & ~mask);
        }
        inline void set(T flag, bool set)
        {
            if (set) {
                on(flag);
            }
            else {
                off(flag);
            }
        }
        inline T operator=(T flags)
        {
            return flags_ = flags;
        }
    private:
        T flags_;
    };

    template<>
    class Flags<uint8_t> {
    public:
        inline Flags() : flags_(0)
        {
        }
        inline void clear()
        {
            flags_ = 0;
        }
        inline bool is(uint8_t flag) const
        {
            return (flags_ & flag) != 0;
        }
        inline void on(uint8_t flag)
        {
            flags_ |= flag;
        }
        inline void off(uint8_t flag)
        {
            flags_ &= ~flag;
        }
        inline void set(uint8_t flag, bool set)
        {
            if (set) {
                on(flag);
            }
            else {
                off(flag);
            }
        }
        inline int value() const
        {
            return flags_;
        }
        inline uint8_t operator=(uint8_t flags)
        {
            return flags_ = flags;
        }
    private:
        uint8_t flags_;
    };

    template<>
    class Flags<uint16_t> {
    public:
        inline Flags() : flags_(0)
        {
        }
        inline void clear()
        {
            flags_ = 0;
        }
        inline bool is(uint16_t flag) const
        {
            return (flags_ & flag) != 0;
        }
        inline void on(uint16_t flag)
        {
            flags_ |= flag;
        }
        inline void off(uint16_t flag)
        {
            flags_ &= ~flag;
        }
        inline void set(uint16_t flag, bool set)
        {
            if (set) {
                on(flag);
            }
            else {
                off(flag);
            }
        }
        inline int value() const
        {
            return flags_;
        }
        inline uint16_t operator=(uint16_t flags)
        {
            return flags_ = flags;
        }
    private:
        uint16_t flags_;
    };

    template<>
    class Flags<int> {
    public:
        inline Flags() : flags_(0)
        {
        }
        inline void clear()
        {
            flags_ = 0;
        }
        inline bool is(int flag) const
        {
            return (flags_ & flag) != 0;
        }
        inline void on(int flag)
        {
            flags_ |= flag;
        }
        inline void off(int flag)
        {
            flags_ &= ~flag;
        }
        inline void set(int flag, bool set)
        {
            if (set) {
                on(flag);
            }
            else {
                off(flag);
            }
        }
        inline int value() const
        {
            return flags_;
        }
        inline int operator=(int flags)
        {
            return flags_ = flags;
        }
    private:
        int flags_;
    };

    class ImGuiIdHolder {
    public:
        ImGuiIdHolder() : id_(0)
        {
        }
        ImGuiIdHolder(const std::string& str)
        {
            id_ = ImGui::GetID(str.c_str());
        }
        ImGuiIdHolder(const ImGuiIdHolder& r)
        {
            id_ = r.id_;
        }
        ~ImGuiIdHolder()
        {
        }
        inline bool IsValid() const
        {
            return id_ != 0;
        }
        inline operator ImGuiID() const
        {
            return id_;
        }
    private:
        ImGuiID id_;
    };

    class ScopedImGuiStyle {
    public:
        ScopedImGuiStyle(ImGuiCol idx, ImVec4 col)
        {
            ImGui::PushStyleColor(idx, col);
            color_count_ = 1;
        }

        ScopedImGuiStyle(ImGuiStyleVar idx, float val)
        {
            ImGui::PushStyleVar(idx, val);
            var_count_ = 1;
        }

        ScopedImGuiStyle(ImGuiStyleVar idx, const ImVec2& val)
        {
            ImGui::PushStyleVar(idx, val);
            var_count_ = 1;
        }

        void AddStyleColor(ImGuiCol idx, ImVec4 col)
        {
            ImGui::PushStyleColor(idx, col);
            color_count_++;
        }

        void AddStyleVar(ImGuiStyleVar idx, float val)
        {
            ImGui::PushStyleVar(idx, val);
            var_count_++;
        }

        void AddStyleVar(ImGuiStyleVar idx, const ImVec2& val)
        {
            ImGui::PushStyleVar(idx, val);
            var_count_++;
        }

        ~ScopedImGuiStyle()
        {
            if (color_count_ > 0) {
                ImGui::PopStyleColor(color_count_);
            }
            if (var_count_ > 0) {
                ImGui::PopStyleVar(var_count_);
            }
        }
    private:
        int color_count_ = 0;
        int var_count_ = 0;
    };

    template<typename T>
    class ReactiveProperty {
    public:
        ReactiveProperty() : value_(T()), subject_(value_)
        {
        }
        ReactiveProperty(T value) : value_(value), subject_(value)
        {
        }
        T Value() const
        {
            return value_;
        }
        void Value(T v)
        {
            //if (value_ == v) return;
            value_ = v;
            subject_.get_subscriber().on_next(v);
        }
        rxcpp::observable<T> Observe()
        {
            return subject_.get_observable();
        }
    private:
        T value_;
        rxcpp::subjects::behavior<T> subject_;
    };

    class SubscriptionGuard {
    public:
        SubscriptionGuard() : s_()
        {
        }
        SubscriptionGuard(rxcpp::subscription s) : s_(s)
        {
        }
        SubscriptionGuard(SubscriptionGuard&& r)
        {
            s_ = r.s_;
            r.s_ = rxcpp::subscription();
        }
        ~SubscriptionGuard()
        {
            s_.unsubscribe();
        }
    private:
        rxcpp::subscription s_;
    };


    class MessageBroker {
    public:
        class Handle {
            friend class MessageBroker;
        public:
            Handle() : type_hash_(0), func_id_(0)
            {
            }
            Handle(size_t type_hash, uint32_t func_id) : type_hash_(type_hash), func_id_(func_id)
            {
            }
            Handle(const Handle& r) = delete;
            Handle(Handle&& r)
            {
                type_hash_ = r.type_hash_;
                func_id_ = r.func_id_;
                r.type_hash_ = 0;
                r.func_id_ = 0;
            }
            ~Handle()
            {
                if (IsValid()) {
                    Unsubscribe(*this);
                }
            }
            inline bool IsValid() const
            {
                return type_hash_ != 0 && func_id_ != 0;
            }
        private:
            inline void Reset()
            {
                type_hash_ = 0;
                func_id_ = 0;
            }
            size_t type_hash_;
            uint32_t func_id_;
        };

        template<typename T>
        static void Publish(T message)
        {
            auto& broker = Instance();
            auto hash = typeid(T).hash_code();
            auto it = broker.observers_.find(hash);
            if (it != broker.observers_.end()) {
                for (auto& f : (*it).second) {
                    std::get<1>(f)(static_cast<void*>(&message));
                }
            }
        }

        template<typename T>
        static Handle Subscribe(std::function<void(T&)> f)
        {
            auto& broker = Instance();
            auto hash = typeid(T).hash_code();
            auto it = broker.observers_.find(hash);
            if (it == broker.observers_.end()) {
                broker.observers_[hash] = std::vector<std::tuple<uint32_t, std::function<void(void*)>>>();
            }
            auto wrap_func = [f](void* p) {
                f(*static_cast<T*>(p));
            };
            auto id = ++broker.id_;
            broker.observers_[hash].emplace_back(std::make_tuple(id, wrap_func));
            return Handle(hash, id);
        }

        static void Unsubscribe(Handle& handle)
        {
            auto& broker = Instance();
            auto type_hash = handle.type_hash_;
            auto func_id = handle.func_id_;
            auto it = broker.observers_.find(type_hash);
            if (it != broker.observers_.end()) {
                auto& v = (*it).second;
                v.erase(std::remove_if(v.begin(), v.end(), [func_id](const std::tuple<uint32_t, std::function<void(void*)>>& f) {
                    return std::get<0>(f) == func_id;
                }), v.end());
            }
            handle.Reset();
        }
    private:
        MessageBroker() : id_(0)
        {
        }
        static inline MessageBroker& Instance()
        {
            if (instance_ == nullptr) instance_ = new MessageBroker();
            return *instance_;
        }

        std::map<size_t, std::vector<std::tuple<uint32_t, std::function<void(void*)>>>> observers_;
        uint32_t id_;
        static MessageBroker* instance_;
    };


}

#endif // MISC_HPP_
