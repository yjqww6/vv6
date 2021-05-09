#pragma once

#include <cstring>
#include <cstddef>
#include "func_view.hpp"

namespace vv6
{

namespace uf_details
{

template <typename T>
static constexpr bool must_be_implicit_lifetime_type =
        std::is_trivially_destructible_v<T> &&
        std::is_trivially_move_constructible_v<T> &&
        std::is_trivially_copy_constructible_v<T>;

enum uf_op
{
    MoveAndDestroy, Destroy
};


using storage_type = std::aligned_storage_t<2 * sizeof(std::max_align_t), alignof(std::max_align_t)>;

using manager_type = void(*)(storage_type*, storage_type*, uf_op) noexcept;

template <typename T>
static constexpr bool is_inplace =
        (sizeof(T) <= sizeof(storage_type)) &&
        (alignof (storage_type) >= alignof (T)) &&
        (alignof (storage_type) % alignof (T) == 0) &&
        std::is_nothrow_move_constructible<T>::value;

template <typename T>
struct external_manager
{
    static void s_manage(storage_type* src, storage_type* dst, uf_op op) noexcept
    {
        if(op == uf_op::MoveAndDestroy)
        {
            *reinterpret_cast<T**>(dst) = *reinterpret_cast<T**>(src);
            *reinterpret_cast<T**>(src) = nullptr;
        }
        else if(op == uf_op::Destroy)
        {
            delete *reinterpret_cast<T**>(src);
        }
    }
};

template <typename T>
struct internal_manager
{
    static void s_manage(storage_type* src, storage_type* dst, uf_op op) noexcept
    {
        if(op == uf_op::MoveAndDestroy)
        {
            new(dst) T(std::move(*reinterpret_cast<T*>(src)));
            reinterpret_cast<T*>(src)->~T();
        }
        else if(op == uf_op::Destroy)
        {
            reinterpret_cast<T*>(src)->~T();
        }
    }
};

template <typename T, bool Const, bool External>
decltype(auto) storage_cast(const storage_type& obj)
{
    if constexpr(External)
    {
        if constexpr(Const)
        {
            return **reinterpret_cast<const T* const *>(&obj);
        }
        else
        {
            return *const_cast<T*>(*reinterpret_cast<const T* const *>(&obj));
        }
    }
    else
    {
        if constexpr(Const)
        {
            return *reinterpret_cast<const T*>(&obj);
        }
        else
        {
            return *const_cast<T*>(reinterpret_cast<const T*>(&obj));
        }
    }
}

template <typename Sig, typename T, bool External>
struct invoker;

template <typename Ret, typename... Args, typename T, bool External>
struct invoker<Ret(Args...) const, T, External>
{
    static Ret s_invoke(const storage_type& obj, details::argument_t<Args>... args)
    {

        return static_cast<Ret>(storage_cast<T, true, External>(obj)(std::forward<Args>(args)...));
    }
};

template <typename Ret, typename... Args, typename T, bool External>
struct invoker<Ret(Args...), T, External>
{
    static Ret s_invoke(const storage_type& obj, details::argument_t<Args>... args)
    {

        return static_cast<Ret>(storage_cast<T, false, External>(obj)(std::forward<Args>(args)...));
    }
};

template <typename Sig>
class unique_func_base;

template <typename Ret, typename... Args>
class unique_func_base<Ret(Args...)>
{
protected:
    Ret (*m_invoker)(const storage_type& obj, details::argument_t<Args>... args);
    manager_type m_manager;
    storage_type m_storage;

    template <typename Sig, typename DT, typename... DTArgs>
    static constexpr void construct(unique_func_base* self, DTArgs&& ...args)
    {
        if constexpr(must_be_implicit_lifetime_type<DT>)
        {
            self->m_invoker = invoker<Sig, DT, false>::s_invoke;
            self->m_manager = nullptr;
            new(&self->m_storage) DT(std::forward<DTArgs>(args)...);
        }
        else if constexpr(is_inplace<DT>)
        {
            self->m_invoker = invoker<Sig, DT, false>::s_invoke;
            self->m_manager = internal_manager<DT>::s_manage;
            new (&self->m_storage) DT(std::forward<DTArgs>(args)...);
        }
        else
        {
            self->m_invoker = invoker<Sig, DT, true>::s_invoke;
            self->m_manager = external_manager<DT>::s_manage;
            new (&self->m_storage) DT*(new DT(std::forward<DTArgs>(args)...));
        }
    }
public:
    constexpr unique_func_base() noexcept:
        m_invoker(nullptr),
        m_manager(nullptr),
        m_storage()
    {

    }

    unique_func_base(const unique_func_base&) = delete;

    unique_func_base(unique_func_base&& other) noexcept : m_invoker(other.m_invoker), m_manager(other.m_manager)
    {
        if(m_manager)
        {
            m_manager(&other.m_storage, &m_storage, MoveAndDestroy);
        }
        else
        {
            //redundant in empty case
            std::memcpy(&m_storage, &other.m_storage, sizeof(m_storage));
        }
        other.m_invoker = nullptr;
        other.m_manager = nullptr;
    }

    unique_func_base& operator=(unique_func_base&& other) noexcept
    {
        m_invoker = other.m_invoker;
        m_manager = other.m_manager;
        if(m_manager)
        {
            m_manager(&other.m_storage, &m_storage, MoveAndDestroy);
        }
        else
        {
            std::memcpy(&m_storage, &other.m_storage, sizeof(m_storage));
        }
        other.m_invoker = nullptr;
        other.m_manager = nullptr;
        return *this;
    }

    unique_func_base& operator=(const unique_func_base& other) = delete;

    ~unique_func_base()
    {
        if(m_manager)
        {
            m_manager(&m_storage, nullptr, Destroy);
        }
    }

    explicit operator bool() const noexcept
    {
        return m_invoker != nullptr;
    }
};

}

template <typename Sig>
class unique_func;

template <typename Ret, typename... Args>
class unique_func<Ret(Args...)> : public uf_details::unique_func_base<Ret(Args...)>
{
    using signature_type = Ret(Args ...);
    using base_type = uf_details::unique_func_base<Ret(Args...)>;
public:
    template <typename T>
    static constexpr bool proper = !std::is_convertible_v<T*, unique_func*> &&
            std::is_invocable_r_v<Ret, T&, Args&&...>;

    using base_type::base_type;

    template <typename T, std::enable_if_t<proper<std::decay_t<T>>, int> = 0>
    unique_func(T&& t)
    {
        base_type::template construct<signature_type, std::decay_t<T>>(this, std::forward<T>(t));
    }

    template <typename T, typename... DTArgs, std::enable_if_t<proper<std::decay_t<T>>, int> = 0>
    unique_func(std::in_place_type_t<T>, DTArgs&&... args)
    {
        base_type::template construct<signature_type, T>(this, std::forward<DTArgs>(args)...);
    }

    unique_func(unique_func<Ret(Args...) const>&& other) : base_type(std::move(other))
    {
    }

    Ret operator()(Args&& ...args)
    {
        return base_type::m_invoker(base_type::m_storage, std::forward<Args>(args)...);
    }
};

template <typename Ret, typename... Args>
class unique_func<Ret(Args...) const> : public uf_details::unique_func_base<Ret(Args...)>
{
    using signature_type = Ret(Args ...) const;
    using base_type = uf_details::unique_func_base<Ret(Args...)>;
    template <typename T>
    static constexpr bool proper = !std::is_convertible_v<T*, unique_func*> &&
            std::is_invocable_r_v<Ret, const T&, Args&&...>;
public:
    using base_type::base_type;

    template <typename T, std::enable_if_t<proper<std::decay_t<T>>, int> = 0>
    unique_func(T&& t)
    {
        base_type::template construct<signature_type, std::decay_t<T>>(this, std::forward<T>(t));
    }

    template <typename T, typename... DTArgs, std::enable_if_t<proper<std::decay_t<T>>, int> = 0>
    unique_func(std::in_place_type_t<T>, DTArgs&&... args)
    {
        base_type::template construct<signature_type, T>(this, std::forward<DTArgs>(args)...);
    }

    Ret operator()(Args&& ...args) const
    {
        return base_type::m_invoker(base_type::m_storage, std::forward<Args>(args)...);
    }
};

}
