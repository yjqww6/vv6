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

template <typename Sig>
struct void_invoker;


template <typename Ret, typename ...Args>
struct void_invoker<Ret(Args...)>
{
    static Ret s_invoke(const storage_type&, Args&&...)
    {
        std::abort();
    }
};

template <typename T, bool Const>
auto& internal_cast(const storage_type& obj)
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

template <typename T, bool Const>
auto& external_cast(const storage_type& obj)
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

template <typename Sig, bool Const, typename T>
struct internal_invoker;

template <typename Ret, typename... Args, typename T, bool Const>
struct internal_invoker<Ret(Args...), Const, T>
{
    static Ret s_invoke(const storage_type& obj, Args&&... args)
    {
        return internal_cast<T, Const>(obj)(std::forward<Args>(args)...);
    }
};

template <typename... Args, typename T, bool Const>
struct internal_invoker<void(Args...), Const, T>
{
    static void s_invoke(const storage_type& obj, Args&&... args)
    {
        internal_cast<T, Const>(obj)(std::forward<Args>(args)...);
    }
};

template <typename Sig, bool Const, typename T>
struct external_invoker;

template <typename Ret, typename... Args, typename T, bool Const>
struct external_invoker<Ret(Args...), Const, T>
{
    static Ret s_invoke(const storage_type& obj, Args&&... args)
    {
        return external_cast<T, Const>(obj)(std::forward<Args>(args)...);
    }
};

template <typename... Args, typename T, bool Const>
struct external_invoker<void(Args...), Const, T>
{
    static void s_invoke(const storage_type& obj, Args&&... args)
    {
        external_cast<T, Const>(obj)(std::forward<Args>(args)...);
    }
};

}

template <typename Sig>
class unique_func;

template <typename Ret, typename... Args>
class unique_func<Ret(Args...)>
{
    Ret (*m_invoker)(const uf_details::storage_type& obj, Args&&... args);
    uf_details::manager_type m_manager;
    uf_details::storage_type m_storage;

    template <typename T>
    static constexpr bool proper_type = !std::is_convertible_v<T*, unique_func*> &&
            std::is_invocable_r_v<Ret, const T&, Args&&...>;

    template <typename DT, typename C, typename... DTArgs>
    unique_func(int, std::in_place_type_t<DT>, C, DTArgs&& ...args)
    {
        constexpr bool Const = C::value;

        if constexpr(uf_details::must_be_implicit_lifetime_type<DT>)
        {
            m_invoker = uf_details::internal_invoker<Ret(Args...), Const, DT>::s_invoke;
            m_manager = nullptr;
            new(&m_storage) DT(std::forward<DTArgs>(args)...);
        }
        else if constexpr(uf_details::is_inplace<DT>)
        {
            m_invoker = uf_details::internal_invoker<Ret(Args...), Const, DT>::s_invoke;
            m_manager = uf_details::internal_manager<DT>::s_manage;
            new (&m_storage) DT(std::forward<DTArgs>(args)...);
        }
        else
        {
            m_invoker = uf_details::external_invoker<Ret(Args...), Const, DT>::s_invoke;
            m_manager = uf_details::external_manager<DT>::s_manage;
            new (&m_storage) DT*(new DT(std::forward<DTArgs>(args)...));
        }
    }
public:
    constexpr unique_func() noexcept:
        m_invoker(uf_details::void_invoker<Ret(Args...)>::s_invoke),
        m_manager(nullptr),
        m_storage()
    {

    }

    unique_func(const unique_func&) = delete;

    unique_func(unique_func&& other) noexcept : m_invoker(other.m_invoker), m_manager(other.m_manager)
    {
        if(m_manager)
        {
            m_manager(&other.m_storage, &m_storage, uf_details::MoveAndDestroy);
        }
        else
        {
            //redundant in empty case
            std::memcpy(&m_storage, &other.m_storage, sizeof(m_storage));
        }
        other.m_invoker = uf_details::void_invoker<Ret(Args...)>::s_invoke;
        other.m_manager = nullptr;
    }

    unique_func& operator=(unique_func&& other) noexcept
    {
        m_invoker = other.m_invoker;
        m_manager = other.m_manager;
        if(m_manager)
        {
            m_manager(&other.m_storage, &m_storage, uf_details::MoveAndDestroy);
        }
        else
        {
            std::memcpy(&m_storage, &other.m_storage, sizeof(m_storage));
        }
        other.m_invoker = uf_details::void_invoker<Ret(Args...)>::s_invoke;
        other.m_manager = nullptr;
        return *this;
    }

    unique_func& operator=(const unique_func& other) = delete;

    ~unique_func()
    {
        if(m_manager)
        {
            m_manager(&m_storage, nullptr, uf_details::Destroy);
        }
    }

    Ret operator()(Args... args) const
    {
        return m_invoker(m_storage, static_cast<Args&&>(args)...);
    }

    explicit operator bool() const noexcept
    {
        return m_invoker != uf_details::void_invoker<Ret(Args...)>::s_invoke;
    }

    template <typename T, std::enable_if_t<proper_type<std::decay_t<T>>, int> = 0>
    unique_func(T&& t) : unique_func(0, std::in_place_type<std::decay_t<T>>, std::true_type(), std::forward<T>(t))
    {
    }

    template <typename T, std::enable_if_t<proper_type<std::decay_t<T>>, int> = 0>
    unique_func(use_non_const_type, T&& t) : unique_func(0, std::in_place_type<std::decay_t<T>>, std::false_type(), std::forward<T>(t))
    {
    }

    template <typename T, typename ...DTArgs, std::enable_if_t<proper_type<T>, int> = 0>
    unique_func(std::in_place_type_t<T>, DTArgs&& ...args)
        : unique_func(0, std::in_place_type<T>, std::true_type(), std::forward<DTArgs>(args)...)
    {
    }

    template <typename T, typename ...DTArgs, std::enable_if_t<proper_type<T>, int> = 0>
    unique_func(std::in_place_type_t<T>, use_non_const_type, DTArgs&& ...args)
        : unique_func(0, std::in_place_type<T>, std::false_type(), std::forward<DTArgs>(args)...)
    {
    }
};

}
