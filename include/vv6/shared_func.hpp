#pragma once
#include <memory>
#include "func_view.hpp"

namespace vv6
{

namespace details
{

template <typename T>
struct empty_on_move : T
{
    using T::T;

    empty_on_move(const empty_on_move&) = default;

    empty_on_move(empty_on_move&& other) noexcept(std::is_nothrow_move_constructible_v<T> && noexcept (static_cast<T&>(other) = T())) :
        T(std::move(static_cast<T&>(other)))
    {
        static_cast<T&>(other) = T();
    }

    empty_on_move& operator=(const empty_on_move&) = default;

    empty_on_move& operator=(empty_on_move&& other) noexcept(noexcept (static_cast<T&>(other) = T()))
    {
        static_cast<T&>(*this) = std::move(static_cast<T&>(other));
        static_cast<T&>(other) = T();
        return *this;
    }
};

template <typename T, typename = void>
struct has_unique_interface : std::false_type {};

template <typename T>
struct has_unique_interface<T, std::void_t<decltype(&T::operator())>> : std::true_type {};

template <typename T>
struct signature_from_memfn;

template <typename C, typename Ret, typename... Args>
struct signature_from_memfn<Ret (C::*)(Args...) const>
{
    using type = Ret(Args...);
    static constexpr bool is_const = true;
};

template <typename C, typename Ret, typename... Args>
struct signature_from_memfn<Ret (C::*)(Args...)>
{
    using type = Ret(Args...);
    static constexpr bool is_const = false;
};

}

template <typename Sig>
class shared_func;

template <typename Ret, typename... Args>
class shared_func<Ret(Args...)> : private details::empty_on_move<func_view<Ret(Args...)>>
{
    using view_type = details::empty_on_move<func_view<Ret(Args...)>>;
    std::shared_ptr<const volatile void> m_obj;

public:
    constexpr shared_func() noexcept = default;

    //from func_view, but avoid implicit conversion
    template <typename T, std::enable_if_t<std::is_same_v<T, func_view<Ret(Args...)>>, int> = 0>
    constexpr shared_func(T f) noexcept :
        view_type(f)
    {

    }

    template <typename Func,
              std::enable_if_t<std::is_function_v<std::remove_pointer_t<Func>> &&
                               std::is_constructible_v<view_type, Func>,
                               int> = 0>
    shared_func(Func fp) noexcept : view_type(fp)
    {

    }

    template <typename T, std::enable_if_t<std::is_constructible_v<view_type, T&>, int> = 0>
    shared_func(std::shared_ptr<T> sh) noexcept : view_type(*sh), m_obj(std::move(sh))
    {

    }

    template <typename T, std::enable_if_t<std::is_constructible_v<view_type, use_non_const_type, T&>, int> = 0>
    shared_func(use_non_const_type, std::shared_ptr<T> sh) noexcept : view_type(use_non_const, *sh), m_obj(std::move(sh))
    {

    }

    using view_type::operator();
    using view_type::operator bool;

    view_type view() const noexcept
    {
        return *this;
    }

};

template <typename Sig, typename T>
shared_func<Sig> make_shared_func(T&& t)
{
    return shared_func<Sig>(std::make_shared<std::decay_t<T>>(std::forward<T>(t)));
}

template <typename Sig, typename T>
shared_func<Sig> make_shared_func(use_non_const_type, T&& t)
{
    return shared_func<Sig>(use_non_const, std::make_shared<std::decay_t<T>>(std::forward<T>(t)));
}

template <typename T, std::enable_if_t<details::has_unique_interface<std::decay_t<T>>::value, int> = 0>
auto make_shared_func(T&& t)
{
    using DT = std::decay_t<T>;

    using M = decltype(&DT::operator());

    using Sig = typename details::signature_from_memfn<M>::type;

    if constexpr(details::signature_from_memfn<M>::is_const)
    {
        return shared_func<Sig>(std::make_shared<std::decay_t<T>>(std::forward<T>(t)));
    }
    else
    {
        return shared_func<Sig>(use_non_const, std::make_shared<std::decay_t<T>>(std::forward<T>(t)));
    }
}

}
