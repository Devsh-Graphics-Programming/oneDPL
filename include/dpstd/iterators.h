// -*- C++ -*-
//===-- iterators.h -------------------------------------------------------===//
//
// Copyright (C) 2017-2020 Intel Corporation
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

#ifndef _PSTL_iterators_H
#define _PSTL_iterators_H

#pragma message("WARNING: 'dpstd/iterators.h' will be removed after Beta. Please use 'dpstd/iterator' instead.")

#if _PSTL_BACKEND_SYCL
#    include "dpstd/pstl/hetero/dpcpp/sycl_iterator.h"
#endif

#include "dpstd/pstl/iterator_impl.h"

#include "dpstd/internal/iterator_impl.h"

#endif /* __PSTL_iterators_H */