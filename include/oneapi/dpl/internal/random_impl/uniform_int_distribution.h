// -*- C++ -*-
//===-- uniform_int_distribution.h ----------------------------------------===//
//
// Copyright (C) 2020 Intel Corporation
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
//
// Abstract:
//
// Public header file provides implementation for Uniform Int Distribution

#ifndef DPSTD_UNIFORM_INT_DISTRIBUTION
#define DPSTD_UNIFORM_INT_DISTRIBUTION

namespace oneapi
{
namespace dpl
{

template <class _IntType = int>
class uniform_int_distribution
{
  public:
    // Distribution types
    using result_type = _IntType;
    using scalar_type = internal::element_type_t<result_type>;
    using param_type = typename ::std::pair<scalar_type, scalar_type>;

    // Constructors
    uniform_int_distribution() : uniform_int_distribution(static_cast<scalar_type>(0)) {}
    explicit uniform_int_distribution(scalar_type __a, scalar_type __b = ::std::numeric_limits<scalar_type>::max()) :
        a_(__a), b_(__b) {}
    explicit uniform_int_distribution(const param_type& __params) : a_(__params.first), b_(__params.second) {}

    // Reset function
    void
    reset() {}

    // Property functions
    scalar_type
    a() const
    {
        return a_;
    }

    scalar_type
    b() const
    {
        return b_;
    }

    param_type
    param() const
    {
        return param_type(a_, b_);
    }

    void
    param(const param_type& __parm)
    {
        a_ = __parm.first;
        b_ = __parm.second;
    }

    scalar_type
    min() const
    {
        return a();
    }

    scalar_type
    max() const
    {
        return b();
    }


    // Generate functions
    template <class _Engine>
    result_type
    operator()(_Engine& __engine)
    {
        return operator()<_Engine>(__engine, param_type(a_, b_));
    }

    template <class _Engine>
    result_type
    operator()(_Engine& __engine, const param_type& __params)
    {
        return generate<size_of_type_, _Engine>(__engine, __params);
    }

    // Generation by portion
    template <class _Engine>
    result_type
    operator()(_Engine& __engine, unsigned int __randoms_num)
    {
        return operator()<_Engine>(__engine, param_type(a_, b_), __randoms_num);
    }

    template <class _Engine>
    result_type
    operator()(_Engine& __engine, const param_type& __params, unsigned int __randoms_num)
    {
        result_type __part_vec;
        if (__randoms_num < 1)
            return __part_vec;

        unsigned int __portion = (__randoms_num > size_of_type_) ? size_of_type_ : __randoms_num;

        __part_vec = result_portion_internal<size_of_type_, _Engine>(__engine, __params, __portion);
        return __part_vec;
    }

  private:
    // Size of type
    static constexpr int size_of_type_ = internal::type_traits_t<result_type>::num_elems;

    // Type of real distribution
    using RealType = typename ::std::conditional<size_of_type_ == 0, double, sycl::vec<double, size_of_type_>>::type;

    // Static asserts
    static_assert(::std::is_integral<scalar_type>::value, "Unsupported IntType of uniform_int_distribution");

    // Distribution parameters
    scalar_type a_;
    scalar_type b_;

    // Real distribution for the conversion
    uniform_real_distribution<RealType> uniform_real_distribution_;

    // Implementation for generate function
    template <int _Ndistr, class _Engine>
    typename ::std::enable_if<(_Ndistr != 0), result_type>::type
    generate(_Engine& __engine, const param_type& __params)
    {
        RealType __res =
            uniform_real_distribution_(__engine, ::std::pair<double, double>(static_cast<double>(__params.first),
                                                                         static_cast<double>(__params.second) + 1.0));

        return __res.template convert<scalar_type, cl::sycl::rounding_mode::rte>();
    }

    template <int _Ndistr, class _Engine>
    typename ::std::enable_if<(_Ndistr == 0), result_type>::type
    generate(_Engine& __engine, const param_type& __params)
    {
        RealType __res =
            uniform_real_distribution_(__engine, ::std::pair<double, double>(static_cast<double>(__params.first),
                                                                         static_cast<double>(__params.second) + 1.0));

        return static_cast<scalar_type>(__res);
    }

    // Implementation for result_portion function
    template <int _Ndistr, class _Engine>
    typename ::std::enable_if<(_Ndistr != 0), result_type>::type
    result_portion_internal(_Engine& __engine, const param_type& __params, unsigned int __N)
    {
        RealType __res = uniform_real_distribution_(__engine,
            ::std::pair<double, double>(static_cast<double>(__params.first),
            static_cast<double>(__params.second) + 1.0), __N);

        return __res.template convert<scalar_type, cl::sycl::rounding_mode::rte>();
    }
};

} // namespace dpl
} // namespace oneapi

#endif // #ifndf DPSTD_UNIFORM_INT_DISTRIBUTION