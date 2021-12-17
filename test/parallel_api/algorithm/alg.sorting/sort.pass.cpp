// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Copyright (C) Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// This file incorporates work covered by the following copyright and permission
// notice:
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#include "support/test_config.h"

#include _PSTL_TEST_HEADER(execution)
#include _PSTL_TEST_HEADER(algorithm)

#if !defined(_PSTL_TEST_SORT) && !defined(_PSTL_TEST_STABLE_SORT)
#define _PSTL_TEST_SORT
#define _PSTL_TEST_STABLE_SORT
#endif // !defined(_PSTL_TEST_SORT) && !defined(_PSTL_TEST_STABLE_SORT)

// Testing with and without predicate may be useful due to different implementations, e.g. merge-sort and radix-sort
#if !defined(_PSTL_TEST_WITH_PREDICATE) && !defined(_PSTL_TEST_WITHOUT_PREDICATE)
#define _PSTL_TEST_WITH_PREDICATE
#define _PSTL_TEST_WITHOUT_PREDICATE
#endif // !defined(_PSTL_TEST_WITH_PREDICATE) && !defined(_PSTL_TEST_WITHOUT_PREDICATE)

#define _CRT_SECURE_NO_WARNINGS

#include <atomic>

#include "support/utils.h"
#include "support/sycl_alloc_utils.h"

static bool Stable;

//! Number of extant keys
static ::std::atomic<std::int32_t> KeyCount;

//! One more than highest index in array to be sorted.
static std::uint32_t LastIndex;

//! Keeping Equal() static and a friend of ParanoidKey class (C++, paragraphs 3.5/7.1.1)
class ParanoidKey;
#if !TEST_DPCPP_BACKEND_PRESENT
static bool
Equal(const ParanoidKey& x, const ParanoidKey& y);
#endif // !TEST_DPCPP_BACKEND_PRESENT

//! A key to be sorted, with lots of checking.
class ParanoidKey
{
    //! Value used by comparator
    std::int32_t value;
    //! Original position or special value (Empty or Dead)
    std::int32_t index;
    //! Special value used to mark object without a comparable value, e.g. after being moved from.
    static const std::int32_t Empty = -1;
    //! Special value used to mark destroyed objects.
    static const std::int32_t Dead = -2;
    // True if key object has comparable value
    bool
    isLive() const
    {
        return (std::uint32_t)(index) < LastIndex;
    }
    // True if key object has been constructed.
    bool
    isConstructed() const
    {
        return isLive() || index == Empty;
    }

  public:
    ParanoidKey()
    {
        ++KeyCount;
        index = Empty;
        value = Empty;
    }
    ParanoidKey(const ParanoidKey& k) : value(k.value), index(k.index)
    {
        EXPECT_TRUE(k.isLive(), "source for copy-constructor is dead");
        ++KeyCount;
    }
    ~ParanoidKey()
    {
        EXPECT_TRUE(isConstructed(), "double destruction");
        index = Dead;
        --KeyCount;
    }
    ParanoidKey&
    operator=(const ParanoidKey& k)
    {
        EXPECT_TRUE(k.isLive(), "source for copy-assignment is dead");
        EXPECT_TRUE(isConstructed(), "destination for copy-assignment is dead");
        value = k.value;
        index = k.index;
        return *this;
    }
    ParanoidKey(std::int32_t index, std::int32_t value, TestUtils::OddTag) : value(value), index(index) {}
    ParanoidKey(ParanoidKey&& k) : value(k.value), index(k.index)
    {
        EXPECT_TRUE(k.isConstructed(), "source for move-construction is dead");
// ::std::stable_sort() fails in move semantics on paranoid test before VS2015
#if !defined(_MSC_VER) || _MSC_VER >= 1900
        k.index = Empty;
#endif // !defined(_MSC_VER) || _MSC_VER >= 1900
        ++KeyCount;
    }
    ParanoidKey&
    operator=(ParanoidKey&& k)
    {
        EXPECT_TRUE(k.isConstructed(), "source for move-assignment is dead");
        EXPECT_TRUE(isConstructed(), "destination for move-assignment is dead");
        value = k.value;
        index = k.index;
// ::std::stable_sort() fails in move semantics on paranoid test before VS2015
#if !defined(_MSC_VER) || _MSC_VER >= 1900
        k.index = Empty;
#endif // !defined(_MSC_VER) || _MSC_VER >= 1900
        return *this;
    }
    friend class KeyCompare;
    friend bool
    Equal(const ParanoidKey& x, const ParanoidKey& y);
};

class KeyCompare
{
    enum statusType
    {
        //! Special value used to mark defined object.
        Live = 0xabcd,
        //! Special value used to mark destroyed objects.
        Dead = -1
    } status;

  public:
    KeyCompare(TestUtils::OddTag) : status(Live) {}
    ~KeyCompare() { status = Dead; }
    bool
    operator()(const ParanoidKey& j, const ParanoidKey& k) const
    {
        EXPECT_TRUE(status == Live, "key comparison object not defined");
        EXPECT_TRUE(j.isLive(), "first key to operator() is not live");
        EXPECT_TRUE(k.isLive(), "second key to operator() is not live");
        return j.value < k.value;
    }
};

// Equal is equality comparison used for checking result of sort against expected result.
#if !TEST_DPCPP_BACKEND_PRESENT
static bool
Equal(const ParanoidKey& x, const ParanoidKey& y)
{
    return (x.value == y.value && !Stable) || (x.index == y.index);
}
#endif // !TEST_DPCPP_BACKEND_PRESENT

static bool
Equal(TestUtils::float32_t x, TestUtils::float32_t y)
{
    return x == y;
}

static bool
Equal(std::int32_t x, std::int32_t y)
{
    return x == y;
}

template <typename T>
struct test_sort_base
{
    template <typename InputIterator, typename OutputIterator1, typename OutputIterator2, typename Size>
    void
    copy_data(InputIterator first, OutputIterator1 expected_first, OutputIterator1 expected_last,
              OutputIterator2 tmp_first, Size n)
    {
        ::std::copy_n(first, n, expected_first);
        ::std::copy_n(first, n, tmp_first);
    }

    template <typename Iterator, typename Compare>
    void
    sort_data(Iterator __from, Iterator __to, Compare compare)
    {
        if (Stable)
            ::std::stable_sort(__from, __to, compare);
        else
            ::std::sort(__from, __to, compare);
    }

    template <typename Policy, typename Iterator, typename Compare>
    void
    sort_data(Policy&& exec, Iterator __from, Iterator __to, Compare compare)
    {
        if (Stable)
            ::std::stable_sort(exec, __from, __to, compare);
        else
            ::std::sort(exec, __from, __to, compare);
    }

    template <typename OutputIterator1, typename OutputIterator2, typename Size>
    void
    check_results(OutputIterator1 expected_first, OutputIterator2 tmp_first, Size n, const char* error_msg)
    {
        for (size_t i = 0; i < n; ++i, ++expected_first, ++tmp_first)
        {
            // Check that expected[i] is equal to tmp[i]
            EXPECT_TRUE(Equal(*expected_first, *tmp_first), error_msg);
        }
    }

    template <typename Policy, typename InputIterator, typename OutputIterator, typename OutputIterator2, typename Size,
              typename Compare>
    oneapi::dpl::__internal::__enable_if_host_execution_policy<Policy, void>
    run_test(Policy&& exec, OutputIterator tmp_first, OutputIterator tmp_last, OutputIterator2 expected_first,
             OutputIterator2 expected_last, InputIterator first, InputIterator /*last*/, Size n, Compare compare)
    {
        copy_data(first, expected_first, expected_last, tmp_first, n);
        sort_data(expected_first + 1, expected_last - 1, compare);

        const std::int32_t count0 = KeyCount;
        sort_data(exec, tmp_first + 1, tmp_last - 1, compare);

        check_results(expected_first, tmp_first, n, "wrong result from sort without predicate #1");

        const std::int32_t count1 = KeyCount;
        EXPECT_EQ(count0, count1, "key cleanup error");
    }

#if TEST_DPCPP_BACKEND_PRESENT
#if _PSTL_SYCL_TEST_USM
    template <sycl::usm::alloc alloc_type, typename Policy, typename InputIterator, typename OutputIterator,
              typename OutputIterator2, typename Size, typename Compare>
    typename ::std::enable_if<
        TestUtils::is_base_of_iterator_category<::std::random_access_iterator_tag, InputIterator>::value, void>::type
    test_usm(Policy&& exec, OutputIterator tmp_first, OutputIterator tmp_last, OutputIterator2 expected_first,
             OutputIterator2 expected_last, InputIterator first, InputIterator /* last */, Size n, Compare compare)
    {
        copy_data(first, expected_first, expected_last, tmp_first, n);
        sort_data(expected_first + 1, expected_last - 1, compare);

        const auto _it_from = tmp_first + 1;
        const auto _it_to = tmp_last - 1;
        const auto _size = _it_to - _it_from;

        using _Data_Type = typename std::iterator_traits<OutputIterator>::value_type;

        auto queue = exec.queue();

        // allocate USM memory and copying data to USM shared/device memory
        TestUtils::usm_data_transfer<alloc_type, _Data_Type> dt_helper(queue, _it_from, _it_to);
        auto sortingData = dt_helper.get_data();

        const std::int32_t count0 = KeyCount;
        sort_data(exec, sortingData, sortingData + _size, compare);

        // check result
        dt_helper.retrieve_data(_it_from);

        check_results(expected_first, tmp_first, n, "wrong result from sort without predicate #2");

        const std::int32_t count1 = KeyCount;
        EXPECT_EQ(count0, count1, "key cleanup error");
    }

    template <typename Policy, typename InputIterator, typename OutputIterator, typename OutputIterator2, typename Size,
              typename Compare>
    oneapi::dpl::__internal::__enable_if_hetero_execution_policy<Policy, void>
    run_test(Policy&& exec, OutputIterator tmp_first, OutputIterator tmp_last, OutputIterator2 expected_first,
             OutputIterator2 expected_last, InputIterator first, InputIterator last, Size n, Compare compare)
    {
        // Run tests for USM shared memory
        test_usm<sycl::usm::alloc::shared>(exec, tmp_first, tmp_last, expected_first, expected_last, first, last, n,
                                           compare);
        // Run tests for USM device memory
        test_usm<sycl::usm::alloc::device>(exec, tmp_first, tmp_last, expected_first, expected_last, first, last, n,
                                           compare);
    }
#endif // _PSTL_SYCL_TEST_USM
#endif // TEST_DPCPP_BACKEND_PRESENT
};

template <typename T>
struct test_sort_with_compare
{
    template <typename Policy, typename InputIterator, typename OutputIterator, typename OutputIterator2, typename Size,
              typename Compare>
    typename ::std::enable_if<TestUtils::is_base_of_iterator_category<::std::random_access_iterator_tag, InputIterator>::value,
                              void>::type
    operator()(Policy&& exec, OutputIterator tmp_first, OutputIterator tmp_last, OutputIterator2 expected_first,
               OutputIterator2 expected_last, InputIterator first, InputIterator last, Size n, Compare compare)
    {
        test_sort_base<T>().run_test(exec, tmp_first, tmp_last, expected_first, expected_last, first, last, n, compare);
    }

    template <typename Policy, typename InputIterator, typename OutputIterator, typename OutputIterator2, typename Size,
              typename Compare>
    typename ::std::enable_if<!TestUtils::is_base_of_iterator_category<::std::random_access_iterator_tag, InputIterator>::value,
                              void>::type
    operator()(Policy&& /* exec */, OutputIterator /* tmp_first */, OutputIterator /* tmp_last */,
               OutputIterator2 /* expected_first */, OutputIterator2 /* expected_last */, InputIterator /* first */,
               InputIterator /* last */, Size /* n */, Compare /* compare */)
    {
    }
};

template <typename T>
struct test_sort_without_compare
{
    template <typename Policy, typename InputIterator, typename OutputIterator, typename OutputIterator2, typename Size>
    typename ::std::enable_if<TestUtils::is_base_of_iterator_category<::std::random_access_iterator_tag, InputIterator>::value &&
                                  TestUtils::can_use_default_less_operator<T>::value,
                              void>::type
    operator()(Policy&& exec, OutputIterator tmp_first, OutputIterator tmp_last, OutputIterator2 expected_first,
               OutputIterator2 expected_last, InputIterator first, InputIterator last, Size n)
    {
        test_sort_base<T>().run_test(exec, tmp_first, tmp_last, expected_first, expected_last, first, last, n,
                                     ::std::less<typename std::iterator_traits<OutputIterator2>::value_type>());
    }

    template <typename Policy, typename InputIterator, typename OutputIterator, typename OutputIterator2, typename Size>
    typename ::std::enable_if<!TestUtils::is_base_of_iterator_category<::std::random_access_iterator_tag, InputIterator>::value ||
                                  !TestUtils::can_use_default_less_operator<T>::value,
                              void>::type
    operator()(Policy&& /* exec */, OutputIterator /* tmp_first */, OutputIterator /* tmp_last */,
               OutputIterator2 /* expected_first */, OutputIterator2 /* expected_last */, InputIterator /* first */,
               InputIterator /* last */, Size /* n */)
    {
    }
};

template <typename T, typename Compare, typename Convert>
void
test_sort(Compare compare, Convert convert)
{
    for (size_t n = 0; n < 100000; n = n <= 16 ? n + 1 : size_t(3.1415 * n))
    {
        LastIndex = n + 2;
        // The rand()%(2*n+1) encourages generation of some duplicates.
        // Sequence is padded with an extra element at front and back, to detect overwrite bugs.
        TestUtils::Sequence<T> in(n + 2, [=](size_t k) { return convert(k, rand() % (2 * n + 1)); });
        TestUtils::Sequence<T> expected(in);
        TestUtils::Sequence<T> tmp(in);
#ifdef _PSTL_TEST_WITHOUT_PREDICATE
        TestUtils::invoke_on_all_policies<0>()(test_sort_without_compare<T>(), tmp.begin(), tmp.end(), expected.begin(),
                                               expected.end(), in.begin(), in.end(), in.size());
#endif // _PSTL_TEST_WITHOUT_PREDICATE
#ifdef _PSTL_TEST_WITH_PREDICATE
        TestUtils::invoke_on_all_policies<1>()(test_sort_with_compare<T>(), tmp.begin(), tmp.end(), expected.begin(),
                                               expected.end(), in.begin(), in.end(), in.size(), compare);
#endif // _PSTL_TEST_WITH_PREDICATE
    }
}

template <typename T>
struct test_non_const
{
    template <typename Policy, typename Iterator>
    void
    operator()(Policy&& exec, Iterator iter)
    {
#ifdef _PSTL_TEST_SORT
        ::std::sort(exec, iter, iter, TestUtils::non_const(::std::less<T>()));
#endif // _PSTL_TEST_SORT
#ifdef _PSTL_TEST_STABLE_SORT
        ::std::stable_sort(exec, iter, iter, TestUtils::non_const(::std::less<T>()));
#endif // _PSTL_TEST_STABLE_SORT
    }
};

int
main()
{
    ::std::srand(42);
    std::int32_t start = 0;
    std::int32_t end = 2;
#ifndef _PSTL_TEST_SORT
    start = 1;
#endif // #ifndef _PSTL_TEST_SORT
#ifndef _PSTL_TEST_STABLE_SORT
    end = 1;
#endif // _PSTL_TEST_STABLE_SORT
    for (std::int32_t kind = start; kind < end; ++kind)
    {
        Stable = kind != 0;

#if !TEST_DPCPP_BACKEND_PRESENT
        // ParanoidKey has atomic increment in ctors. It's not allowed in kernel
        test_sort<ParanoidKey>(KeyCompare(TestUtils::OddTag()),
                               [](size_t k, size_t val) { return ParanoidKey(k, val, TestUtils::OddTag()); });
#endif // !TEST_DPCPP_BACKEND_PRESENT

#if !ONEDPL_FPGA_DEVICE
        test_sort<TestUtils::float32_t>([](TestUtils::float32_t x, TestUtils::float32_t y) { return x < y; },
                                        [](size_t, size_t val) { return TestUtils::float32_t(val); });
#endif // !ONEDPL_FPGA_DEVICE
        test_sort<std::int32_t>(
            [](std::int32_t x, std::int32_t y) { return x > y; }, // Reversed so accidental use of < will be detected.
            [](size_t, size_t val) { return std::int32_t(val); });
    }

#if !ONEDPL_FPGA_DEVICE
    TestUtils::test_algo_basic_single<int32_t>(TestUtils::run_for_rnd<test_non_const<int32_t>>());
#endif // !ONEDPL_FPGA_DEVICE

    return TestUtils::done();
}
