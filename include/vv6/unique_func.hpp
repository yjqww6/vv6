#pragma once

#include <cstring>
#include <cstddef>
#include <exception>
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

class not_const_invocable : public std::exception
{
public:
    const char* what() const noexcept override
    {
        return "this unique_func is not const invocable";
    }
};

template <typename Sig, typename T, bool External>
struct invoker;

template <typename Ret, typename... Args, typename T, bool External>
struct invoker<Ret(Args...), T, External>
{
    static Ret s_invoke(const storage_type& obj, details::argument_t<Args>... args, bool is_const)
    {
        if(is_const)
        {
            if constexpr(std::is_invocable_r_v<Ret, const T&, Args&&...>)
            {

                return static_cast<Ret>(storage_cast<T, true, External>(obj)(std::forward<Args>(args)...));
            }
            else
            {
                throw not_const_invocable();
            }
        }
        else
        {

            return static_cast<Ret>(storage_cast<T, false, External>(obj)(std::forward<Args>(args)...));
        }
    }
};

}

template <typename Sig>
class unique_func;

template <typename Ret, typename... Args>
class unique_func<Ret(Args...)>
{
    Ret (*m_invoker)(const uf_details::storage_type& obj, details::argument_t<Args>... args, bool);
    uf_details::manager_type m_manager;
    uf_details::storage_type m_storage;

    template <typename T>
    static constexpr bool proper_type = !std::is_convertible_v<T*, unique_func*> &&
            std::is_invocable_r_v<Ret, T&, Args&&...>;

    template <typename DT, typename... DTArgs>
    unique_func(int, std::in_place_type_t<DT>, DTArgs&& ...args)
    {
        if constexpr(uf_details::must_be_implicit_lifetime_type<DT>)
        {
            m_invoker = uf_details::invoker<Ret(Args...), DT, false>::s_invoke;
            m_manager = nullptr;
            new(&m_storage) DT(std::forward<DTArgs>(args)...);
        }
        else if constexpr(uf_details::is_inplace<DT>)
        {
            m_invoker = uf_details::invoker<Ret(Args...), DT, false>::s_invoke;
            m_manager = uf_details::internal_manager<DT>::s_manage;
            new (&m_storage) DT(std::forward<DTArgs>(args)...);
        }
        else
        {
            m_invoker = uf_details::invoker<Ret(Args...), DT, true>::s_invoke;
            m_manager = uf_details::external_manager<DT>::s_manage;
            new (&m_storage) DT*(new DT(std::forward<DTArgs>(args)...));
        }
    }
public:
    constexpr unique_func() noexcept:
        m_invoker(nullptr),
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
        other.m_invoker = nullptr;
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
        other.m_invoker = nullptr;
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

    Ret operator()(Args&&... args) const
    {
        return m_invoker(m_storage, std::forward<Args>(args)..., true);
    }

    Ret operator()(Args&&... args)
    {
        return m_invoker(m_storage, std::forward<Args>(args)..., false);
    }


    explicit operator bool() const noexcept
    {
        return m_invoker != nullptr;
    }

    template <typename T, std::enable_if_t<proper_type<std::decay_t<T>>, int> = 0>
    unique_func(T&& t) : unique_func(0, std::in_place_type<std::decay_t<T>>, std::forward<T>(t))
    {
    }

    template <typename T, typename ...DTArgs, std::enable_if_t<proper_type<T>, int> = 0>
    unique_func(std::in_place_type_t<T>, DTArgs&& ...args)
        : unique_func(0, std::in_place_type<T>, std::forward<DTArgs>(args)...)
    {
    }
};

}
