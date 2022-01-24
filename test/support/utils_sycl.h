// -*- C++ -*-
//===-- utils_sycl.h ------------------------------------------------------===//
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
#ifndef UTILS_SYCL
#define UTILS_SYCL

// File contains common utilities for SYCL that tests rely on

// Do not #include <algorithm>, because if we do we will not detect accidental dependencies.

#include <iterator>
#include "oneapi/dpl/pstl/hetero/dpcpp/sycl_defs.h"
#if _ONEDPL_FPGA_DEVICE
#    if __LIBSYCL_VERSION >= 50400
#        include <sycl/ext/intel/fpga_extensions.hpp>
#    else
#        include <CL/sycl/INTEL/fpga_extensions.hpp>
#    endif
#endif

#include "test_config.h"

#include _PSTL_TEST_HEADER(iterator)
#include "oneapi/dpl/pstl/hetero/dpcpp/parallel_backend_sycl.h"
#include "iterator_utils.h"
#include "utils_invoke.h"
#include "utils_test_base.h"

#include _PSTL_TEST_HEADER(execution)

namespace TestUtils
{

constexpr int kDefaultMultValue = 1;

#define PRINT_DEBUG(message) ::TestUtils::print_debug(message)

inline void
print_debug(const char*
#if _ONEDPL_DEBUG_SYCL
                message
#endif
)
{
#if _ONEDPL_DEBUG_SYCL
    ::std::cout << message << ::std::endl;
#endif
}

// Check values in sequence
template <typename Iterator, typename T>
bool
check_values(Iterator first, Iterator last, const T& val)
{
    return ::std::all_of(first, last, [&val](const T& x) { return x == val; });
}

auto async_handler = [](sycl::exception_list ex_list) {
    for (auto& ex : ex_list)
    {
        try
        {
            ::std::rethrow_exception(ex);
        }
        catch (sycl::exception& ex)
        {
            ::std::cerr << ex.what() << ::std::endl;
            ::std::exit(EXIT_FAILURE);
        }
    }
};

//function is needed to wrap kernel name into another class
template <typename _NewKernelName, typename _Policy,
          oneapi::dpl::__internal::__enable_if_device_execution_policy<_Policy, int> = 0>
auto
make_new_policy(_Policy&& __policy)
    -> decltype(oneapi::dpl::execution::make_device_policy<_NewKernelName>(::std::forward<_Policy>(__policy)))
{
    return oneapi::dpl::execution::make_device_policy<_NewKernelName>(::std::forward<_Policy>(__policy));
}

#if ONEDPL_FPGA_DEVICE
template <typename _NewKernelName, typename _Policy,
          oneapi::dpl::__internal::__enable_if_fpga_execution_policy<_Policy, int> = 0>
auto
make_new_policy(_Policy&& __policy)
    -> decltype(oneapi::dpl::execution::make_fpga_policy<::std::decay<_Policy>::type::unroll_factor, _NewKernelName>(
        ::std::forward<_Policy>(__policy)))
{
    return oneapi::dpl::execution::make_fpga_policy<::std::decay<_Policy>::type::unroll_factor, _NewKernelName>(
        ::std::forward<_Policy>(__policy));
}
#endif

#if ONEDPL_FPGA_DEVICE
    auto default_selector =
#    if ONEDPL_FPGA_EMULATOR
        __dpl_sycl::__fpga_emulator_selector{};
#    else
        __dpl_sycl::__fpga_selector{};
#    endif // ONEDPL_FPGA_EMULATOR

    auto&& default_dpcpp_policy =
#    if ONEDPL_USE_PREDEFINED_POLICIES
        oneapi::dpl::execution::dpcpp_fpga;
#    else
        oneapi::dpl::execution::make_fpga_policy(sycl::queue{default_selector});
#    endif // ONEDPL_USE_PREDEFINED_POLICIES
#else
    auto default_selector = sycl::default_selector{};
    auto&& default_dpcpp_policy =
#    if ONEDPL_USE_PREDEFINED_POLICIES
        oneapi::dpl::execution::dpcpp_default;
#    else
        oneapi::dpl::execution::make_device_policy(sycl::queue{default_selector});
#    endif // ONEDPL_USE_PREDEFINED_POLICIES
#endif     // ONEDPL_FPGA_DEVICE

// create the queue with custom asynchronous exceptions handler
static auto my_queue = sycl::queue(default_selector, async_handler);

inline
sycl::queue get_test_queue()
{
    return my_queue;
}

template <sycl::usm::alloc alloc_type, typename TestValueType, typename TestName>
void
test1buffer()
{
    sycl::queue queue = get_test_queue(); // usm and allocator requires queue

#if _PSTL_SYCL_TEST_USM
    { // USM
        // 1. allocate usm memory
        using TestBaseData = test_base_data_usm<TestValueType>;
        TestBaseData test_base_data(alloc_type, queue, max_n, inout1_offset);

        // 2. create a pointer at first+offset
        auto inout1_offset_first = test_base_data.get_start_from(0);

        // 3. run algorithms
        for (size_t n = 1; n <= max_n; n = n <= 16 ? n + 1 : size_t(3.1415 * n))
        {
#    if _ONEDPL_DEBUG_SYCL
            ::std::cout << "n = " << n << ::std::endl;
#    endif
            if constexpr (::std::is_base_of<test_base<TestValueType>, TestName>::value)
            {
                TestName testObj(test_base_data);
                invoke_on_all_hetero_policies<0>()(testObj,
                                                   inout1_offset_first, inout1_offset_first + n,
                                                   n);
            }
            else
            {
-                invoke_on_all_hetero_policies<0>()(TestName(),
                                                    inout1_offset_first, inout1_offset_first + n,
                                                    n);
            }
        }
    }
#endif
    { // sycl::buffer
        // 1. create buffers
        using TestBaseData = test_base_data_buffer<TestValueType>;
        TestBaseData test_base_data({ { max_n, inout1_offset } });

        // 2. create iterators over buffers
        auto inout1_offset_first = test_base_data.get_start_from(0);

        // 3. run algorithms
        for (size_t n = 1; n <= max_n; n = n <= 16 ? n + 1 : size_t(3.1415 * n))
        {
#if _ONEDPL_DEBUG_SYCL
            ::std::cout << "n = " << n << ::std::endl;
#endif
            if constexpr (::std::is_base_of<test_base<TestValueType>, TestName>::value)
            {
                TestName testObj(test_base_data);
                invoke_on_all_hetero_policies<1>()(testObj,
                                                   inout1_offset_first, inout1_offset_first + n,
                                                   n);
            }
            else
            {
                invoke_on_all_hetero_policies<1>()(TestName(),
                                                   inout1_offset_first, inout1_offset_first + n,
                                                   n);
            }
        }
    }
}

template <sycl::usm::alloc alloc_type, typename TestValueType, typename TestName>
void
test2buffers()
{
    sycl::queue queue = get_test_queue(); // usm and allocator requires queue

#if _PSTL_SYCL_TEST_USM
    { // USM
        // 1. allocate usm memory
        using TestBaseData = test_base_data_usm<TestValueType>;
        TestBaseData test_base_data(alloc_type, queue, max_n, inout1_offset, inout2_offset);

        // 2. create pointers at first+offset
        auto inout1_offset_first = test_base_data.get_start_from(0);
        auto inout2_offset_first = test_base_data.get_start_from(1);

        // 3. run algorithms
        for (size_t n = 1; n <= max_n; n = n <= 16 ? n + 1 : size_t(3.1415 * n))
        {
#    if _ONEDPL_DEBUG_SYCL
            ::std::cout << "n = " << n << ::std::endl;
#    endif
            if constexpr (::std::is_base_of<test_base<TestValueType>, TestName>::value)
            {
                TestName testObj(test_base_data);
                invoke_on_all_hetero_policies<0>()(testObj,
                                                   inout1_offset_first, inout1_offset_first + n,
                                                   inout2_offset_first, inout2_offset_first + n,
                                                   n);
            }
            else
            {
                invoke_on_all_hetero_policies<0>()(TestName(),
                                                   inout1_offset_first, inout1_offset_first + n,
                                                   inout2_offset_first, inout2_offset_first + n,
                                                   n);
            }
        }
    }
#endif
    { // sycl::buffer
        // 1. create buffers
        using TestBaseData = test_base_data_buffer<TestValueType>;
        TestBaseData test_base_data({ { max_n, inout1_offset },
                                      { max_n, inout2_offset } });

        // 2. create iterators over buffers
        auto inout1_offset_first = test_base_data.get_start_from(0);
        auto inout2_offset_first = test_base_data.get_start_from(1);

        // 3. run algorithms
        for (size_t n = 1; n <= max_n; n = n <= 16 ? n + 1 : size_t(3.1415 * n))
        {
#if _ONEDPL_DEBUG_SYCL
            ::std::cout << "n = " << n << ::std::endl;
#endif
            if constexpr (::std::is_base_of<test_base<TestValueType>, TestName>::value)
            {
                TestName testObj(test_base_data);
                invoke_on_all_hetero_policies<1>()(testObj,
                                                   inout1_offset_first, inout1_offset_first + n,
                                                   inout2_offset_first, inout2_offset_first + n,
                                                   n);
            }
            else
            {
                invoke_on_all_hetero_policies<1>()(TestName(),
                                                   inout1_offset_first, inout1_offset_first + n,
                                                   inout2_offset_first, inout2_offset_first + n,
                                                   n);
            }
        }
    }
}

template <sycl::usm::alloc alloc_type, typename TestValueType, typename TestName>
void
test3buffers(int mult = kDefaultMultValue)
{
    sycl::queue queue = get_test_queue(); // usm requires queue

#if _PSTL_SYCL_TEST_USM
    { // USM

        // 1. allocate usm memory
        using TestBaseData = test_base_data_usm<TestValueType>;
        TestBaseData test_base_data(alloc_type, queue, max_n, inout1_offset, inout2_offset, inout3_offset);

        // 2. create pointers at first+offset
        auto inout1_offset_first = test_base_data.get_start_from(0);
        auto inout2_offset_first = test_base_data.get_start_from(1);
        auto inout3_offset_first = test_base_data.get_start_from(2);

        // 3. run algorithms
        for (size_t n = 1; n <= max_n; n = (n <= 16 ? n + 1 : size_t(3.1415 * n)))
        {
#    if _ONEDPL_DEBUG_SYCL
            ::std::cout << "n = " << n << ::std::endl;
#    endif
            if constexpr (::std::is_base_of<test_base<TestValueType>, TestName>::value)
            {
                TestName testObj(test_base_data);
                invoke_on_all_hetero_policies<0>()(testObj,
                                                   inout1_offset_first, inout1_offset_first + n,
                                                   inout2_offset_first, inout2_offset_first + n,
                                                   inout3_offset_first, inout3_offset_first + n,
                                                   n);
            }
            else
            {
                invoke_on_all_hetero_policies<0>()(TestName(),
                                                   inout1_offset_first, inout1_offset_first + n,
                                                   inout2_offset_first, inout2_offset_first + n,
                                                   inout3_offset_first, inout3_offset_first + n,
                                                   n);
            }
        }
    }
#endif
    { // sycl::buffer
        // 1. create buffers
        using TestBaseData = test_base_data_buffer<TestValueType>;
        TestBaseData test_base_data({ { max_n,        inout1_offset },
                                      { max_n,        inout2_offset },
                                      { mult * max_n, inout3_offset } });

        // 2. create iterators over buffers
        auto inout1_offset_first = test_base_data.get_start_from(0);
        auto inout2_offset_first = test_base_data.get_start_from(1);
        auto inout3_offset_first = test_base_data.get_start_from(2);

        // 3. run algorithms
        for (size_t n = 1; n <= max_n; n = (n <= 16 ? n + 1 : size_t(3.1415 * n)))
        {
#if _ONEDPL_DEBUG_SYCL
            ::std::cout << "n = " << n << ::std::endl;
#endif
            if constexpr (::std::is_base_of<test_base<TestValueType>, TestName>::value)
            {
                TestName testObj(test_base_data);
                invoke_on_all_hetero_policies<1>()(testObj,
                                                   inout1_offset_first, inout1_offset_first + n,
                                                   inout2_offset_first, inout2_offset_first + n,
                                                   inout3_offset_first, inout3_offset_first + n,
                                                   n);
            }
            else
            {
                invoke_on_all_hetero_policies<1>()(TestName(),
                                                   inout1_offset_first, inout1_offset_first + n,
                                                   inout2_offset_first, inout2_offset_first + n,
                                                   inout3_offset_first, inout3_offset_first + n,
                                                   n);
            }
        }
    }
}

template <sycl::usm::alloc alloc_type, typename TestName>
typename ::std::enable_if<
    ::std::is_base_of<test_base<typename TestName::UsedValueType>, TestName>::value,
    void>::type
test1buffer()
{
    test1buffer<alloc_type, typename TestName::UsedValueType, TestName>();
}

template <sycl::usm::alloc alloc_type, typename TestName>
typename ::std::enable_if<
    ::std::is_base_of<test_base<typename TestName::UsedValueType>, TestName>::value,
    void>::type
test2buffers()
{
    test2buffers<alloc_type, typename TestName::UsedValueType, TestName>();
}

template <sycl::usm::alloc alloc_type, typename TestName>
typename ::std::enable_if<
    ::std::is_base_of<test_base<typename TestName::UsedValueType>, TestName>::value,
    void>::type
test3buffers(int mult = kDefaultMultValue)
{
    test3buffers<alloc_type, typename TestName::UsedValueType, TestName>(mult);
}

template <typename Iter, sycl::access::mode mode = sycl::access::mode::read_write>
auto
get_host_access(Iter it)
    -> decltype(it.get_buffer().template get_access<mode>(__dpl_sycl::__get_buffer_size(it.get_buffer()) -
                                                              (it - oneapi::dpl::begin(it.get_buffer())),
                                                          it - oneapi::dpl::begin(it.get_buffer())))
{
    auto temp_buf = it.get_buffer();
    auto temp_idx = it - oneapi::dpl::begin(temp_buf);
    return temp_buf.template get_access<mode>(__dpl_sycl::__get_buffer_size(temp_buf) - temp_idx, temp_idx);
}

template <typename T>
T*
get_host_access(T* data)
{
    return data;
}
} /* namespace TestUtils */

#endif
