#ifndef _V2UTIME_H

/*
 * linux/fs/ods5/v2utime.h
 *
 * This file is part of the OpenVMS ODS5 file system for Linux.
 * Copyright (C) 2010 Hartmut Becker.
 *
 * The OpenVMS ODS5 file system for Linux is free software; you can
 * redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * The OpenVMS ODS5 file system for Linux is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/types.h>
#include <asm/div64.h>

#include "./vms_types.h"

#define V_TICKS 10000000		/* VMS ticks per second */
#define VU_DELTA 0x007c95674beb4000LL	/* VMS ticks from 17.11.1858 to 1.1.1970 */

static inline struct timespec64 v2utime (vms_quad bintime)
{
	vms_quad sec;
	struct timespec64 ts;
	bintime -= VU_DELTA;
# ifdef __alpha
	sec = bintime / V_TICKS;
# else
	sec = bintime;
	do_div(sec, V_TICKS);
# endif
	ts.tv_sec = (time64_t)sec;
	ts.tv_nsec = (long)((bintime - sec * V_TICKS) * 100LL);
	return ts;
}

#define _V2UTIME_H loaded
#endif
