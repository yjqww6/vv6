#include <vv6/func_view.hpp>
#include <vv6/shared_func.hpp>
#include <vv6/unique_func.hpp>

#define BOOST_TEST_MODULE vv6 Test
#include <boost/test/included/unit_test.hpp>

struct F
{
    int a;
    constexpr F(int x = 42) noexcept : a(x) {}

    constexpr int operator()(int x) const
    {
        return a + x;
    }

    constexpr int operator()(int x)
    {
        return a - x;
    }

    static constexpr int f(int x)
    {
        return 42 + x;
    }
};



constexpr int (*f)(int) = &F::f;

static_assert (f(0) == 42);

template <typename T>
T& ref(T&& t)
{
    return t;
}

BOOST_AUTO_TEST_SUITE(test_func_view)

static constexpr F a;

static_assert (std::is_copy_constructible_v<vv6::func_view<int(int)>>);
static_assert (std::is_move_constructible_v<vv6::func_view<int(int)>>);
static_assert (std::is_copy_assignable_v<vv6::func_view<int(int)>>);
static_assert (std::is_move_assignable_v<vv6::func_view<int(int)>>);

// not constructible from class prvalue;
// avoid some dangling references, but be more verbose in parameter cases
static_assert (!std::is_constructible_v<vv6::func_view<int(int)>, F>);
static_assert (!std::is_invocable_v<void(*)(vv6::func_view<int(int)>), F>);
static_assert (std::is_invocable_v<void(*)(vv6::func_view<int(int)>), decltype(ref(F()))>);

static_assert (std::is_trivially_copyable_v<vv6::func_view<int(int)>>);
static_assert (std::is_trivially_copy_assignable_v<vv6::func_view<int(int)>>);
static_assert (std::is_trivially_destructible_v<vv6::func_view<int(int)>>);

BOOST_AUTO_TEST_CASE(constexpr_test)
{
    constexpr vv6::func_view<int(int)> const_f;
    static_assert(!const_f);
    constexpr vv6::func_view<int(int)> const_f2(const_f);

    struct G : vv6::func_view<int(int)>
    {
        using vv6::func_view<int(int)>::func_view;
    };

    constexpr G g;
    constexpr vv6::func_view<int(int)> const_f3(g);

    static_assert(!const_f3);

    constexpr vv6::func_view<int(int)> const_f4(a);
    static_assert(bool(const_f4));
}

BOOST_AUTO_TEST_CASE(test1)
{
    constexpr vv6::func_view<int(int)> f(a);

    static_assert(bool(f));

    BOOST_TEST(f(0) == 42);

    vv6::func_view<int(int)> g(F::f);
    BOOST_TEST(g(0) == 42);
}

BOOST_AUTO_TEST_CASE(test2)
{
    constexpr vv6::func_view<short(short)> f(a);
    BOOST_TEST(f(1) == 43);

    vv6::func_view<long(long)> g(F::f);
    BOOST_TEST(g(1) == 43);
}

BOOST_AUTO_TEST_CASE(test_void)
{
    constexpr vv6::func_view<void(int)> f(a);
    static_assert (bool(f));
}

BOOST_AUTO_TEST_CASE(use_non_const)
{
    struct C
    {
        constexpr int operator()(int x)
        {
            return 42 + x;
        }
    };

    static_assert (!std::is_constructible_v<vv6::func_view<int(int)>, C&>);
    static_assert (std::is_constructible_v<vv6::func_view<int(int)>, vv6::use_non_const_type, F&>);
    static_assert (!std::is_constructible_v<vv6::func_view<int(int)>, vv6::use_non_const_type, F>);
    static_assert (std::is_constructible_v<vv6::func_view<int(int)>, vv6::use_non_const_type, C&>);
    static_assert (!std::is_constructible_v<vv6::func_view<int(int)>, vv6::use_non_const_type, const C&>);

    C c;
    vv6::func_view<int(int)> f1(vv6::use_non_const, c);
    BOOST_TEST(f1(10) == 52);

    F d;
    vv6::func_view<int(int)> f2(d), f3(vv6::use_non_const, d);
    BOOST_TEST(f2(10) == 52);
    BOOST_TEST(f3(10) == 32);
}

BOOST_AUTO_TEST_CASE(variance)
{
    struct A {};
    struct B : A {};
    struct C : B {};

    struct F1
    {
        B operator()(B x) const
        {
            return x;
        }
    } a;


    static_assert (std::is_constructible_v<vv6::func_view<A(B)>, F1&>);
    static_assert (std::is_constructible_v<vv6::func_view<B(C)>, F1&>);

    static_assert (!std::is_constructible_v<vv6::func_view<C(B)>, F1&>);
    static_assert (!std::is_constructible_v<vv6::func_view<B(A)>, F1&>);
}


BOOST_AUTO_TEST_SUITE_END()


BOOST_AUTO_TEST_SUITE(test_shared_func)

static_assert (std::is_nothrow_copy_constructible_v<vv6::shared_func<int(int)>>);
static_assert (std::is_nothrow_copy_assignable_v<vv6::shared_func<int(int)>>);
static_assert (std::is_nothrow_move_constructible_v<vv6::shared_func<int(int)>>);
static_assert (std::is_nothrow_move_assignable_v<vv6::shared_func<int(int)>>);

static_assert (!std::is_constructible_v<vv6::shared_func<int(int)>, F&>);
static_assert (std::is_constructible_v<vv6::shared_func<int(int)>, vv6::func_view<int(int)>>);


BOOST_AUTO_TEST_CASE(test1)
{
    vv6::shared_func<int(int)> f1(std::make_shared<F>());
    BOOST_TEST(f1(10) == 52);

    vv6::shared_func<int(int)> f2(F::f);
    BOOST_TEST(f2(0) == 42);

    vv6::shared_func<int(int)> f3(vv6::use_non_const, std::make_shared<F>());
    BOOST_TEST(f3(10) == 32);

    vv6::shared_func<int(int)> f4(f1), f5(f3);
    BOOST_TEST(f4(10) == 52);
    BOOST_TEST(f5(10) == 32);

    BOOST_TEST(f5.view()(0) == 42);
}

BOOST_AUTO_TEST_CASE(test2)
{
    auto f1 = vv6::make_shared_func<int(int)>(F());
    BOOST_TEST(f1(10) == 52);

    auto f2 = vv6::make_shared_func([](int x) {return 42 - x;});
    BOOST_TEST(f2(10) == 32);

    auto f3 = vv6::make_shared_func<int(int)>([](int x) {return 42 - x;});
    BOOST_TEST(f3(10) == 32);

    struct C
    {
        constexpr int operator()(int x)
        {
            return 42 + x;
        }
    };

    auto f4 = vv6::make_shared_func(C());
    BOOST_TEST(f4(10) == 52);

    auto f5 = vv6::make_shared_func<int(int)>(vv6::use_non_const, C());
    BOOST_TEST(f5(10) == 52);
}

BOOST_AUTO_TEST_CASE(bool_conv)
{
    vv6::shared_func<int(int)> f1, f2(f1);;
    BOOST_TEST(!f1);
    BOOST_TEST(!f2);
    vv6::shared_func<int(int)> f3(std::move(f2));
    BOOST_TEST(!f3);

    vv6::shared_func<int(int)> f4(F::f);
    BOOST_TEST(bool(f4));

    vv6::shared_func<int(int)> f5(std::move(f4));
    BOOST_TEST(!f4);
    BOOST_TEST(bool(f5));

    vv6::shared_func<int(int)> f6;
    f6 = std::move(f5);
    BOOST_TEST(!f5);
    BOOST_TEST(bool(f6));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(test_unique_func)

static_assert (std::is_nothrow_move_constructible_v<vv6::unique_func<int(int)>>);
static_assert (std::is_nothrow_move_assignable_v<vv6::unique_func<int(int)>>);
static_assert (!std::is_copy_constructible_v<vv6::unique_func<int(int)>>);
static_assert (!std::is_copy_assignable_v<vv6::unique_func<int(int)>>);


static constexpr F a;

template <typename T>
const T& as_const(T& a)
{
    return a;
}

template <typename T>
void test_a(T& a)
{
    vv6::unique_func<int(int) const> f(a);

    BOOST_TEST(f(0) == 42);

    vv6::unique_func<int(int) const> f2(a);
    vv6::unique_func<int(int)> f3(a);
    BOOST_TEST(f2(10) == 52);
    BOOST_TEST(f3(10) == 32);

    vv6::unique_func<int(int) const> f4(std::move(f));
    BOOST_TEST(f4(0) == 42);
    BOOST_TEST(!f);
    f = std::move(f4);
    BOOST_TEST(!f4);
    BOOST_TEST(bool(f));
    BOOST_TEST(f(0) == 42);
}

BOOST_AUTO_TEST_CASE(fp)
{
    vv6::unique_func<int(int)> g(F::f);
    BOOST_TEST(g(0) == 42);
}

BOOST_AUTO_TEST_CASE(test1)
{
    static_assert(vv6::uf_details::must_be_implicit_lifetime_type<F>);
    test_a(a);
}

BOOST_AUTO_TEST_CASE(test2)
{
    struct A : F
    {
        virtual ~A() = default;
    } a;

    static_assert(vv6::uf_details::is_inplace<A>);
    test_a(a);
}

BOOST_AUTO_TEST_CASE(test3)
{
    struct A : F
    {
        A() = default;
        A(const A&) = default;
        A(A&&) = delete;
    } a;

    static_assert(!vv6::uf_details::is_inplace<A>);
    test_a(a);
}

BOOST_AUTO_TEST_CASE(test4)
{
    vv6::unique_func<int(int) const> f1(std::in_place_type<F>, 20);
    BOOST_TEST(f1(10) == 30);

    vv6::unique_func<int(int)> f2(std::in_place_type<F>, 20);
    BOOST_TEST(f2(10) == 10);
}

BOOST_AUTO_TEST_CASE(test_void)
{
    vv6::unique_func<void(int)> f(a);
    BOOST_TEST(bool(f));
}

BOOST_AUTO_TEST_CASE(variance)
{
    struct A {};
    struct B : A {};
    struct C : B {};

    struct F1
    {
        B operator()(B x) const
        {
            return x;
        }
    } a;


    static_assert (std::is_constructible_v<vv6::unique_func<A(B)>, F1&>);
    static_assert (std::is_constructible_v<vv6::unique_func<B(C)>, F1&>);

    static_assert (!std::is_constructible_v<vv6::unique_func<C(B)>, F1&>);
    static_assert (!std::is_constructible_v<vv6::unique_func<B(A)>, F1&>);
}

BOOST_AUTO_TEST_SUITE_END()
