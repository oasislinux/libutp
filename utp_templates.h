/*
 * Copyright (c) 2010-2013 BitTorrent, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef __TEMPLATES_H__
#define __TEMPLATES_H__

#include "utp_types.h"
#include <assert.h>

#if defined(POSIX)
/* Allow over-writing FORCEINLINE from makefile because gcc 3.4.4 for buffalo
   doesn't seem to support __attribute__((always_inline)) in -O0 build
   (strangely, it works in -Os build) */
#ifndef FORCEINLINE
// The always_inline attribute asks gcc to inline the function even if no optimization is being requested.
// This macro should be used exclusive-or with the inline directive (use one or the other but not both)
// since Microsoft uses __forceinline to also mean inline,
// and this code is following a Microsoft compatibility model.
// Just setting the attribute without also specifying the inline directive apparently won't inline the function,
// as evidenced by multiply-defined symbols found at link time.
#define FORCEINLINE inline __attribute__((always_inline))
#endif
#endif

// Utility macros
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#if (defined(__SVR4) && defined(__sun))
	#pragma pack(1)
#else
	#pragma pack(push,1)
#endif

#if (defined(__SVR4) && defined(__sun))
	#pragma pack(0)
#else
	#pragma pack(pop)
#endif


#endif //__TEMPLATES_H__
