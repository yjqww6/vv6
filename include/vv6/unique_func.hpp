#pragma once

#include <cstring>
#include <cstddef>
#include <memory>
#include "func_view.hpp"

namespace vv6
{

namespace uf_details
{

template <typename T, typename U>
T launder_cast(U* ptr)
{
    return std::launder(reinterpret_cast<T>(ptr));
}

template <typename T>
static constexpr bool must_be_implicit_lifetime_type =
        std::is_trivially_destructible_v<T> &&
        std::is_trivially_move_constructible_v<T> &&
        std::is_trivially_copy_constructible_v<T>;

using storage_type = std::aligned_storage_t<2 * sizeof(std::max_align_t), alignof(std::max_align_t)>;

using manager_type = void(*)(storage_type*, storage_type*) noexcept;

template <typename T>
static constexpr bool is_inplace =
        (sizeof(T) <= sizeof(storage_type)) &&
        (alignof (storage_type) >= alignof (T)) &&
        (alignof (storage_type) % alignof (T) == 0) &&
        std::is_nothrow_move_constructible<T>::value;

template <typename T>
struct external_manager
{
    static void s_manage(storage_type* src, storage_type* dst) noexcept
    {
        auto s = launder_cast<T**>(src);
        if(dst)
        {
            *launder_cast<T**>(dst) = *s;
            *s = nullptr;
        }
        else
        {
            delete *s;
        }
    }
};

template <typename Alloc, typename = void>
struct with_allocator_base;

template <typename Alloc>
struct with_allocator_base<Alloc, std::enable_if_t<!std::is_final_v<Alloc>>> : Alloc
{
    with_allocator_base(const Alloc& alloc) : Alloc(alloc) {}

    using allocator_type = Alloc;
    const Alloc& get_allocator() const
    {
        return static_cast<const Alloc&>(*this);
    }
};

template <typename Alloc>
struct with_allocator_base<Alloc, std::enable_if_t<std::is_final_v<Alloc>>>
{
    Alloc alloc_;

    using allocator_type = Alloc;
    const Alloc& get_allocator() const
    {
        return alloc_;
    }
};

template <typename T, typename Alloc>
struct with_allocator : with_allocator_base<Alloc>
{
    T t_;

    template <typename... Args>
    with_allocator(const Alloc& alloc, Args&& ...args) :
        with_allocator_base<Alloc>{alloc},  t_(std::forward<Args>(args)...)
    {

    }

    template <typename... Args>
    decltype(auto) operator()(Args&& ...args)
    {
        return t_(std::forward<Args>(args)...);
    }

    template <typename... Args>
    decltype(auto) operator()(Args&& ...args) const
    {
        return t_(std::forward<Args>(args)...);
    }
};

template <typename T, typename Alloc>
struct external_manager<with_allocator<T, Alloc>>
{
    static void s_manage(storage_type* src, storage_type* dst) noexcept
    {
        auto s = launder_cast<with_allocator<T, Alloc>**>(src);
        if(dst)
        {
            *launder_cast<with_allocator<T, Alloc>**>(dst) = *s;
            *s = nullptr;
        }
        else
        {
            auto p = *s;
            using A = typename std::allocator_traits<Alloc>
            ::template rebind_alloc<with_allocator<T, Alloc>>;
            A alloc(p->get_allocator());
            std::allocator_traits<A>::destroy(alloc, p);
            std::allocator_traits<A>::deallocate(alloc, p, 1);
        }
    }
};

template <typename T>
struct internal_manager
{
    static void s_manage(storage_type* src, storage_type* dst) noexcept
    {
        auto s = launder_cast<T*>(src);
        if(dst)
        {
            new(dst) T(std::move(*s));
            s->~T();
        }
        else
        {
            s->~T();
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
            return **launder_cast<const T* const *>(&obj);
        }
        else
        {
            return *const_cast<T*>(*launder_cast<const T* const *>(&obj));
        }
    }
    else
    {
        if constexpr(Const)
        {
            return *launder_cast<const T*>(&obj);
        }
        else
        {
            return *const_cast<T*>(launder_cast<const T*>(&obj));
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
    Ret (*m_invoker)(const storage_type& obj, details::argument_t<Args>... args);
    manager_type m_manager;
    storage_type m_storage;
protected:
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

    template <typename Sig, typename DT, typename Alloc, typename... DTArgs>
    static void construct(unique_func_base* self, std::allocator_arg_t, Alloc&& alloc, DTArgs&& ...args)
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
            using Allocator = std::decay_t<Alloc>;
            using type = with_allocator<DT, Allocator>;
            self->m_invoker = invoker<Sig, type, true>::s_invoke;
            self->m_manager = external_manager<type>::s_manage;

            using A = typename std::allocator_traits<Allocator>
            ::template rebind_alloc<type>;
            A a(alloc);
            type *p = std::allocator_traits<A>::allocate(a, 1);
            if constexpr (std::is_nothrow_constructible_v<DT, DTArgs&&...>)
            {
                std::allocator_traits<A>::construct(a, p, alloc, std::forward<DTArgs>(args)...);
            }
            else
            {
                try
                {
                    std::allocator_traits<A>::construct(a, p, alloc, std::forward<DTArgs>(args)...);
                }
                catch (...)
                {
                    std::allocator_traits<A>::deallocate(a, p, 1);
                    throw;
                }
            }
            new (&self->m_storage) type*(p);
        }
    }

    Ret call(Args&& ...args) const
    {
        return m_invoker(m_storage, std::forward<Args>(args)...);
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
            m_manager(&other.m_storage, &m_storage);
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
        if(m_manager)
        {
            m_manager(&m_storage, nullptr);
        }
        m_invoker = other.m_invoker;
        m_manager = other.m_manager;
        if(m_manager)
        {

            m_manager(&other.m_storage, &m_storage);
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
            m_manager(&m_storage, nullptr);
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
    template <typename T>
    static constexpr bool proper = !std::is_convertible_v<T*, unique_func*> &&
            std::is_invocable_r_v<Ret, T&, Args&&...>;
public:
    using base_type::base_type;

    template <typename T, std::enable_if_t<proper<std::decay_t<T>>, int> = 0>
    unique_func(T&& t)
    {
        base_type::template construct<signature_type, std::decay_t<T>>(this, std::forward<T>(t));
    }

    template <typename Allocator, typename T, std::enable_if_t<proper<std::decay_t<T>>, int> = 0>
    unique_func(std::allocator_arg_t, const Allocator& a, T&& t)
    {
        base_type::template construct<signature_type, std::decay_t<T>>(this, std::allocator_arg, a, std::forward<T>(t));
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
        return base_type::call(std::forward<Args>(args)...);
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

    template <typename Allocator, typename T, std::enable_if_t<proper<std::decay_t<T>>, int> = 0>
    unique_func(std::allocator_arg_t, const Allocator& a, T&& t)
    {
        base_type::template construct<signature_type, std::decay_t<T>>(this, std::allocator_arg, a, std::forward<T>(t));
    }

    template <typename T, typename... DTArgs, std::enable_if_t<proper<std::decay_t<T>>, int> = 0>
    unique_func(std::in_place_type_t<T>, DTArgs&&... args)
    {
        base_type::template construct<signature_type, T>(this, std::forward<DTArgs>(args)...);
    }

    Ret operator()(Args&& ...args) const
    {
        return base_type::call(std::forward<Args>(args)...);
    }
};

}
