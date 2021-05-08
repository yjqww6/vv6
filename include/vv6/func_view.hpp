#pragma once
#include <cstdlib>
#include <utility>
#include <type_traits>

namespace vv6
{

template <typename Sig>
class func_view;

namespace details
{

union functor
{
    const void *obj = nullptr;
    void (*fun)();
};

template <typename T, bool Const>
decltype(auto) functor_cast(functor fun)
{
    if constexpr(std::is_function_v<T>)
    {
        return reinterpret_cast<T*>(fun.fun);
    }
    else if constexpr(Const)
    {
        return *static_cast<const T*>(fun.obj);
    }
    else
    {
        return *const_cast<T*>(static_cast<const T*>(fun.obj));
    }
}

template <typename T>
struct pass_by_value
{
    static constexpr bool value = std::is_object_v<T> &&
            std::is_trivially_copy_constructible_v<T> &&
            std::is_trivially_move_constructible_v<T> &&
            std::is_trivially_destructible_v<T> &&
            (sizeof(T) <= sizeof(void*));
};

template <typename T>
using argument_t = std::conditional_t<pass_by_value<T>::value, T, T&&>;

template <typename Sig, typename T, bool Const>
struct invoker;

template <typename Ret, typename... Args, typename T, bool Const>
struct invoker<Ret(Args...), T, Const>
{
    static Ret s_invoke(functor fun, argument_t<Args>... args)
    {
        return static_cast<Ret>(functor_cast<T, Const>(fun)(std::forward<Args>(args)...));
    }
};
}

struct use_non_const_type {};
inline use_non_const_type use_non_const;

template <typename Ret, typename ...Args>
class func_view<Ret(Args...)>
{
    details::functor m_functor;
    Ret( *m_invoker)(details::functor, details::argument_t<Args>...);

    template <typename T>
    static constexpr bool proper_class = std::is_class_v<T> &&
            !std::is_convertible_v<T*, func_view*> &&
            std::is_invocable_r_v<Ret, const T&, Args&&...>;

    template <typename T>
    static constexpr bool proper_non_const_class = std::is_class_v<T> &&
            std::is_invocable_r_v<Ret, T&, Args&&...>;

public:
    constexpr func_view() noexcept :
        m_functor(), m_invoker()
    {

    }

    template <typename T, std::enable_if_t<proper_class<std::decay_t<T>>, int> = 0>
    constexpr func_view(const T& obj) noexcept :m_invoker(details::invoker<Ret(Args...), T, true>::s_invoke)
    {
        m_functor.obj = &obj;
    }

    template <typename T, std::enable_if_t<proper_class<std::decay_t<T>>, int> = 0>
    constexpr func_view(const T&& obj) noexcept = delete;

    template <typename T, std::enable_if_t<proper_non_const_class<std::decay_t<T>> && !std::is_const_v<T>, int> = 0>
    constexpr func_view(use_non_const_type, T& obj) noexcept :
        m_invoker(details::invoker<Ret(Args...), T, false>::s_invoke)
    {
        m_functor.obj = &obj;
    }

    template <typename T,
                  std::enable_if_t<std::is_function_v<std::remove_pointer_t<T>> && std::is_invocable_r_v<Ret, T, Args&&...>,
                                   int> = 0>
    constexpr func_view(T t) noexcept :
        m_invoker(details::invoker<Ret(Args...), std::remove_pointer_t<T>, true>::s_invoke)
    {
        m_functor.fun = reinterpret_cast<void(*)()>(t);
    }

    Ret operator()(Args&&... args) const
    {
        return m_invoker(m_functor, std::forward<Args>(args)...);
    }

    explicit constexpr operator bool() const noexcept
    {
        return m_invoker != nullptr;
    }
};

}
