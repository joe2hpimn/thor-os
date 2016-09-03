//=======================================================================
// Copyright Baptiste Wicht 2013-2016.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)
//=======================================================================

#ifndef TLIB_CONFIG_HPP
#define TLIB_CONFIG_HPP

namespace tlib {

constexpr bool is_thor_program(){
#ifdef THOR_PROGRAM
    return true;
#else
    return false;
#endif
}

constexpr bool is_thor_lib(){
#ifdef THOR_TLIB
    return true;
#else
    return false;
#endif
}

} // end of namespace tlib

#define ASSERT_ONLY_THOR_PROGRAM static_assert(tlib::is_thor_program() || tlib::is_thor_lib(), __FILE__ " can only be used in Thor programs");

// Conditional namespace

#ifdef THOR_TLIB
#define THOR_NAMESPACE_NAME(LIB_NS,THOR_NS) LIB_NS
#elif defined(THOR_PROGRAM)
#define THOR_NAMESPACE_NAME(LIB_NS,THOR_NS) LIB_NS
#else
#define THOR_NAMESPACE_NAME(LIB_NS,THOR_NS) THOR_NS
#endif

// Namespaces in kernel

#ifdef THOR_TLIB
#define KERNEL_NAMESPACE_BEGIN(THOR_NS)
#define KERNEL_NAMESPACE_END
#elif defined(THOR_PROGRAM)
#define KERNEL_NAMESPACE_BEGIN(THOR_NS)
#define KERNEL_NAMESPACE_END
#else
#define KERNEL_NAMESPACE_BEGIN(THOR_NS) namespace THOR_NS {
#define KERNEL_NAMESPACE_END }
#endif

// Conditional prefixing

#ifdef THOR_TLIB
#define THOR_PREFIX(prefix) prefix
#elif defined(THOR_PROGRAM)
#define THOR_PREFIX(prefix) prefix
#else
#define THOR_PREFIX(prefix)
#endif

#endif
