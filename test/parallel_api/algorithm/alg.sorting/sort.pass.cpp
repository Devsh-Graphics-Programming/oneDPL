// -*- C++ -*-
//===-- sort.pass.cpp -----------------------------------------------------===//
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
#endif

// Testing with and without predicate may be useful due to different implementations, e.g. merge-sort and radix-sort
#if !defined(_PSTL_TEST_WITH_PREDICATE) && !defined(_PSTL_TEST_WITHOUT_PREDICATE)
#define _PSTL_TEST_WITH_PREDICATE
#define _PSTL_TEST_WITHOUT_PREDICATE
#endif

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
#endif

using namespace TestUtils;

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
    ParanoidKey(std::int32_t index, std::int32_t value, OddTag) : value(value), index(index) {}
    ParanoidKey(ParanoidKey&& k) : value(k.value), index(k.index)
    {
        EXPECT_TRUE(k.isConstructed(), "source for move-construction is dead");
// ::std::stable_sort() fails in move semantics on paranoid test before VS2015
#if !defined(_MSC_VER) || _MSC_VER >= 1900
        k.index = Empty;
#endif
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
#endif
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
    KeyCompare(OddTag) : status(Live) {}
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
#endif

static bool
Equal(float32_t x, float32_t y)
{
    return x == y;
}

static bool
Equal(std::int32_t x, std::int32_t y)
{
    return x == y;
}

template <typename T>
struct test_sort_with_compare
{
#if TEST_DPCPP_BACKEND_PRESENT
#if _PSTL_SYCL_TEST_USM
    template <sycl::usm::alloc alloc_type, typename Policy, typename InputIterator, typename OutputIterator,
              typename OutputIterator2, typename Size, typename Compare>
    typename ::std::enable_if<is_base_of_iterator_category<::std::random_access_iterator_tag, InputIterator>::value,
                              void>::type
    test_usm(Policy&& exec, OutputIterator tmp_first, OutputIterator tmp_last, OutputIterator2 expected_first,
             OutputIterator2 expected_last, InputIterator first, InputIterator /* last */, Size n, Compare compare)
    {
        using namespace std;
        copy_n(first, n, expected_first);
        copy_n(first, n, tmp_first);
        if (Stable)
            ::std::stable_sort(expected_first + 1, expected_last - 1, compare);
        else
            ::std::sort(expected_first + 1, expected_last - 1, compare);

        const auto itSortFrom = tmp_first + 1;
        const auto itSortTo = tmp_last - 1;
        const auto size = itSortTo - itSortFrom;

        using _src_type = decltype(*itSortFrom);
        using _src_type_decay = std::decay_t<_src_type>;

        auto queue = exec.queue();

        // allocate USM memory and copying data to USM shared/device memory
        TestUtils::usm_data_transfer<alloc_type, _src_type_decay> dt_helper(queue, itSortFrom, itSortTo);
        auto sortingData = dt_helper.get_data();

        int32_t count0 = KeyCount;
        if (Stable)
            stable_sort(exec, sortingData, sortingData + size, compare);
        else
            sort(exec, sortingData, sortingData + size, compare);

        // check result
        dt_helper.retrieve_data(::std::addressof(*itSortFrom));

        for (size_t i = 0; i < n; ++i, ++expected_first, ++tmp_first)
        {
            // Check that expected[i] is equal to tmp[i]
            EXPECT_TRUE(Equal(*expected_first, *tmp_first), "wrong result from sort without predicate #1");
        }

        int32_t count1 = KeyCount;
        EXPECT_EQ(count0, count1, "key cleanup error");
    }
#endif // _PSTL_SYCL_TEST_USM
#endif // TEST_DPCPP_BACKEND_PRESENT

    template <typename Policy, typename InputIterator, typename OutputIterator, typename OutputIterator2, typename Size,
              typename Compare>
    oneapi::dpl::__internal::__enable_if_host_execution_policy<Policy, void>
    test(Policy&& exec, OutputIterator tmp_first, OutputIterator tmp_last, OutputIterator2 expected_first,
         OutputIterator2 expected_last, InputIterator first, InputIterator /*last*/, Size n, Compare compare)
    {
        using namespace std;
        copy_n(first, n, expected_first);
        copy_n(first, n, tmp_first);
        if (Stable)
            ::std::stable_sort(expected_first + 1, expected_last - 1, compare);
        else
            ::std::sort(expected_first + 1, expected_last - 1, compare);
        std::int32_t count0 = KeyCount;
        if (Stable)
            stable_sort(exec, tmp_first + 1, tmp_last - 1, compare);
        else
            sort(exec, tmp_first + 1, tmp_last - 1, compare);

        for (size_t i = 0; i < n; ++i, ++expected_first, ++tmp_first)
        {
            // Check that expected[i] is equal to tmp[i]
            EXPECT_TRUE(Equal(*expected_first, *tmp_first), "wrong result from sort without predicate #2");
        }
        std::int32_t count1 = KeyCount;
        EXPECT_EQ(count0, count1, "key cleanup error");
    }

    template <typename Policy, typename InputIterator, typename OutputIterator, typename OutputIterator2, typename Size,
              typename Compare>
    oneapi::dpl::__internal::__enable_if_non_host_execution_policy<Policy, void>
    test(Policy&& exec, OutputIterator tmp_first, OutputIterator tmp_last, OutputIterator2 expected_first,
         OutputIterator2 expected_last, InputIterator first, InputIterator last, Size n, Compare compare)
    {
#if TEST_DPCPP_BACKEND_PRESENT
#if _PSTL_SYCL_TEST_USM
        // Run tests for USM shared memory
        test_usm<sycl::usm::alloc::shared>(exec, tmp_first, tmp_last, expected_first, expected_last, first, last, n,
                                           compare);
        // Run tests for USM device memory
        test_usm<sycl::usm::alloc::device>(exec, tmp_first, tmp_last, expected_first, expected_last, first, last, n,
                                           compare);
#endif // _PSTL_SYCL_TEST_USM
#endif // TEST_DPCPP_BACKEND_PRESENT
    }

    template <typename Policy, typename InputIterator, typename OutputIterator, typename OutputIterator2, typename Size,
              typename Compare>
    typename ::std::enable_if<is_base_of_iterator_category<::std::random_access_iterator_tag, InputIterator>::value,
                              void>::type
    operator()(Policy&& exec, OutputIterator tmp_first, OutputIterator tmp_last, OutputIterator2 expected_first,
               OutputIterator2 expected_last, InputIterator first, InputIterator last, Size n, Compare compare)
    {
        test(exec, tmp_first, tmp_last, expected_first, expected_last, first, last, n, compare);
    }

    template <typename Policy, typename InputIterator, typename OutputIterator, typename OutputIterator2, typename Size,
              typename Compare>
    typename ::std::enable_if<!is_base_of_iterator_category<::std::random_access_iterator_tag, InputIterator>::value,
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
#if TEST_DPCPP_BACKEND_PRESENT
#if _PSTL_SYCL_TEST_USM
    template <sycl::usm::alloc alloc_type, typename Policy, typename InputIterator, typename OutputIterator,
              typename OutputIterator2, typename Size>
    typename ::std::enable_if<is_base_of_iterator_category<::std::random_access_iterator_tag, InputIterator>::value &&
                                  can_use_default_less_operator<T>::value,
                              void>::type
    test_usm(Policy&& exec, OutputIterator tmp_first, OutputIterator tmp_last, OutputIterator2 expected_first,
             OutputIterator2 expected_last, InputIterator first, InputIterator /* last */, Size n)
    {
        using namespace std;

        copy_n(first, n, expected_first);
        copy_n(first, n, tmp_first);

        if (Stable)
            ::std::stable_sort(expected_first + 1, expected_last - 1);
        else
            ::std::sort(expected_first + 1, expected_last - 1);

        const auto itSortFrom = tmp_first + 1;
        const auto itSortTo = tmp_last - 1;
        const auto size = itSortTo - itSortFrom;

        using _src_type = decltype(*itSortFrom);
        using _src_type_decay = std::decay_t<_src_type>;

        auto queue = exec.queue();

        // allocate USM memory and copying data to USM shared/device memory
        TestUtils::usm_data_transfer<alloc_type, _src_type_decay> dt_helper(queue, itSortFrom, itSortTo);
        auto sortingData = dt_helper.get_data();

        int32_t count0 = KeyCount;
        if (Stable)
            stable_sort(exec, sortingData, sortingData + size);
        else
            sort(exec, sortingData, sortingData + size);

        // check result
        dt_helper.retrieve_data(::std::addressof(*itSortFrom));

        for (size_t i = 0; i < n; ++i, ++expected_first, ++tmp_first)
        {
            // Check that expected[i] is equal to tmp[i]
            EXPECT_TRUE(Equal(*expected_first, *tmp_first), "wrong result from sort with predicate #3");
        }

        int32_t count1 = KeyCount;
        EXPECT_EQ(count0, count1, "key cleanup error");
    }
#endif // _PSTL_SYCL_TEST_USM
#endif // TEST_DPCPP_BACKEND_PRESENT

    template <typename Policy, typename InputIterator, typename OutputIterator, typename OutputIterator2, typename Size>
    oneapi::dpl::__internal::__enable_if_host_execution_policy<Policy, void>
    test(Policy&& exec, OutputIterator tmp_first, OutputIterator tmp_last, OutputIterator2 expected_first,
         OutputIterator2 expected_last, InputIterator first, InputIterator /*last*/, Size n)
    {
        using namespace std;
        copy_n(first, n, expected_first);
        copy_n(first, n, tmp_first);
        if (Stable)
            ::std::stable_sort(expected_first + 1, expected_last - 1);
        else
            ::std::sort(expected_first + 1, expected_last - 1);
        std::int32_t count0 = KeyCount;
        if (Stable)
            stable_sort(exec, tmp_first + 1, tmp_last - 1);
        else
            sort(exec, tmp_first + 1, tmp_last - 1);

        for (size_t i = 0; i < n; ++i, ++expected_first, ++tmp_first)
        {
            // Check that expected[i] is equal to tmp[i]
            EXPECT_TRUE(Equal(*expected_first, *tmp_first), "wrong result from sort with predicate #4");
        }
        std::int32_t count1 = KeyCount;
        EXPECT_EQ(count0, count1, "key cleanup error");
    }

    template <typename Policy, typename InputIterator, typename OutputIterator, typename OutputIterator2, typename Size>
    oneapi::dpl::__internal::__enable_if_non_host_execution_policy<Policy, void>
    test(Policy&& exec, OutputIterator tmp_first, OutputIterator tmp_last, OutputIterator2 expected_first,
         OutputIterator2 expected_last, InputIterator first, InputIterator last, Size n)
    {
#if TEST_DPCPP_BACKEND_PRESENT
#if _PSTL_SYCL_TEST_USM
        // Run tests for USM shared memory
        test_usm<sycl::usm::alloc::shared>(exec, tmp_first, tmp_last, expected_first, expected_last, first, last, n);
        // Run tests for USM device memory
        test_usm<sycl::usm::alloc::device>(exec, tmp_first, tmp_last, expected_first, expected_last, first, last, n);
#endif // _PSTL_SYCL_TEST_USM
#endif // TEST_DPCPP_BACKEND_PRESENT
    }

    template <typename Policy, typename InputIterator, typename OutputIterator, typename OutputIterator2, typename Size>
    typename ::std::enable_if<is_base_of_iterator_category<::std::random_access_iterator_tag, InputIterator>::value &&
                                  can_use_default_less_operator<T>::value,
                              void>::type
    operator()(Policy&& exec, OutputIterator tmp_first, OutputIterator tmp_last, OutputIterator2 expected_first,
               OutputIterator2 expected_last, InputIterator first, InputIterator last, Size n)
    {
        test(exec, tmp_first, tmp_last, expected_first, expected_last, first, last, n);
    }

    template <typename Policy, typename InputIterator, typename OutputIterator, typename OutputIterator2, typename Size>
    typename ::std::enable_if<!is_base_of_iterator_category<::std::random_access_iterator_tag, InputIterator>::value ||
                                  !can_use_default_less_operator<T>::value,
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
#endif
#ifdef _PSTL_TEST_WITH_PREDICATE
        TestUtils::invoke_on_all_policies<1>()(test_sort_with_compare<T>(), tmp.begin(), tmp.end(), expected.begin(),
                                               expected.end(), in.begin(), in.end(), in.size(), compare);
#endif
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
        sort(exec, iter, iter, non_const(::std::less<T>()));
#endif
#ifdef _PSTL_TEST_STABLE_SORT
        stable_sort(exec, iter, iter, non_const(::std::less<T>()));
#endif
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
#endif
#ifndef _PSTL_TEST_STABLE_SORT
    end = 1;
#endif
    for (std::int32_t kind = start; kind < end; ++kind)
    {
        Stable = kind != 0;

#if !TEST_DPCPP_BACKEND_PRESENT
        // ParanoidKey has atomic increment in ctors. It's not allowed in kernel
        test_sort<ParanoidKey>(KeyCompare(OddTag()),
                               [](size_t k, size_t val) { return ParanoidKey(k, val, OddTag()); });
#endif

#if !ONEDPL_FPGA_DEVICE
        test_sort<float32_t>([](float32_t x, float32_t y) { return x < y; },
                             [](size_t, size_t val) { return float32_t(val); });
#endif
        test_sort<std::int32_t>(
            [](std::int32_t x, std::int32_t y) { return x > y; }, // Reversed so accidental use of < will be detected.
            [](size_t, size_t val) { return std::int32_t(val); });
    }

#if !ONEDPL_FPGA_DEVICE
    test_algo_basic_single<std::int32_t>(run_for_rnd<test_non_const<std::int32_t>>());
#endif

    return TestUtils::done();
}
