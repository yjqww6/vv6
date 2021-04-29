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

template <typename Sig, typename T>
struct class_invoker;

template <typename Ret, typename... Args, typename Klass>
struct class_invoker<Ret(Args...), Klass>
{
    static Ret s_invoke(functor fun, Args&&... args)
    {
        return (*static_cast<const Klass*>(fun.obj))(std::forward<Args>(args)...);
    }
};

template <typename... Args, typename Klass>
struct class_invoker<void(Args...), Klass>
{
    static void s_invoke(functor fun, Args&&... args)
    {
        (*static_cast<const Klass*>(fun.obj))(std::forward<Args>(args)...);
    }
};

template <typename Sig, typename T>
struct non_const_class_invoker;

template <typename Ret, typename... Args, typename Klass>
struct non_const_class_invoker<Ret(Args...), Klass>
{
    static Ret s_invoke(functor fun, Args&&... args)
    {
        return (*const_cast<Klass*>(static_cast<const Klass*>(fun.obj)))(std::forward<Args>(args)...);
    }
};

template <typename... Args, typename Klass>
struct non_const_class_invoker<void(Args...), Klass>
{
    static void s_invoke(functor fun, Args&&... args)
    {
        (*const_cast<Klass*>(static_cast<const Klass*>(fun.obj)))(std::forward<Args>(args)...);
    }
};

template <typename Sig, typename T>
struct func_invoker;

template <typename Ret, typename... Args, typename F>
struct func_invoker<Ret(Args...), F*>
{
    static Ret s_invoke(functor fun, Args&&... args)
    {
        return reinterpret_cast<F*>(fun.fun)(std::forward<Args>(args)...);
    }
};

template <typename... Args, typename F>
struct func_invoker<void(Args...), F*>
{
    static void s_invoke(functor fun, Args&&... args)
    {
        reinterpret_cast<F*>(fun.fun)(std::forward<Args>(args)...);
    }
};

}

struct use_non_const_type {};
inline use_non_const_type use_non_const;

template <typename Ret, typename ...Args>
class func_view<Ret(Args...)>
{
    details::functor m_functor;
    Ret (*m_invoker)(details::functor, Args&& ...);

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
    constexpr func_view(const T& obj) noexcept :m_invoker(details::class_invoker<Ret(Args...), T>::s_invoke)
    {
        m_functor.obj = &obj;
    }

    template <typename T, std::enable_if_t<proper_class<std::decay_t<T>>, int> = 0>
    constexpr func_view(const T&& obj) noexcept = delete;

    template <typename T, std::enable_if_t<proper_non_const_class<std::decay_t<T>> && !std::is_const_v<T>, int> = 0>
    constexpr func_view(use_non_const_type, T& obj) noexcept :
        m_invoker(details::non_const_class_invoker<Ret(Args...), T>::s_invoke)
    {
        m_functor.obj = &obj;
    }

    template <typename T,
                  std::enable_if_t<std::is_function_v<std::remove_pointer_t<T>> && std::is_invocable_r_v<Ret, T, Args&&...>,
                                   int> = 0>
    constexpr func_view(T t) noexcept :
        m_invoker(details::func_invoker<Ret(Args...), T>::s_invoke)
    {
        m_functor.fun = reinterpret_cast<void(*)()>(t);
    }

    Ret operator()(Args... args) const
    {
        return m_invoker(m_functor, std::forward<Args>(args)...);
    }

    explicit constexpr operator bool() const noexcept
    {
        return m_invoker != nullptr;
    }
};

}
