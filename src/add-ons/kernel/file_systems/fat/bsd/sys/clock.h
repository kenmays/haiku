/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Gordon W. Ross
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 *	$NetBSD: clock_subr.h,v 1.7 2000/10/03 13:41:07 tsutsui Exp $
 *
 *
 * This file is the central clearing-house for calendrical issues.
 *
 * In general the kernel does not know about minutes, hours, days, timezones,
 * daylight savings time, leap-years and such. All that is theoretically a
 * matter for userland only.
 *
 * Parts of kernel code does however care: badly designed filesystems store
 * timestamps in local time and RTC chips sometimes track time in a local
 * timezone instead of UTC and so on.
 *
 * All that code should go here for service.
 */
#ifndef FAT_CLOCK_H_
#define FAT_CLOCK_H_


// Modified to support the Haiku FAT driver.

int utc_offset(void);

void timespec2fattime(const struct timespec* tsp, int utc, u_int16_t* ddp, u_int16_t* dtp,
	u_int8_t* dhp);
void fattime2timespec(unsigned dd, unsigned dt, unsigned dh, int utc, struct timespec* tsp);


#endif // FAT_CLOCK_H_
