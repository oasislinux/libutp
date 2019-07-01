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

#ifndef __UTP_PACKEDSOCKADDR_H__
#define __UTP_PACKEDSOCKADDR_H__

#include "utp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PackedSockAddr PackedSockAddr;
struct PACKED_ATTRIBUTE PackedSockAddr {
	// The values are always stored here in network byte order
	union {
		byte _in6[16];			// IPv6
		uint16 _in6w[8];		// IPv6, word based (for convenience)
		uint32 _in6d[4];		// Dword access
		struct in6_addr _in6addr;	// For convenience
	} _in;

	// Host byte order
	uint16 _port;

	#define _sin4 _in._in6d[3]	// IPv4 is stored where it goes if mapped

	#define _sin6 _in._in6
	#define _sin6w _in._in6w
	#define _sin6d _in._in6d

} ALIGNED_ATTRIBUTE(4);

byte packed_sockaddr_get_family(const PackedSockAddr *addr);

bool packed_sockaddr_equal(const PackedSockAddr *lhs, const PackedSockAddr *rhs);
void packed_sockaddr_set(PackedSockAddr *addr, const SOCKADDR_STORAGE *sa, socklen_t len);

SOCKADDR_STORAGE packed_sockaddr_get_sockaddr_storage(const PackedSockAddr *addr, socklen_t *len);

cstr packed_sockaddr_fmt(const PackedSockAddr *addr, str s, size_t len);

#ifdef __cplusplus
}
#endif

#endif //__UTP_PACKEDSOCKADDR_H__
